/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include <data/case.h>
#include <data/casereader.h>
#include <data/casereader-provider.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/settings.h>
#include <data/value.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/alloc.h>
#include <libpspp/array.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>

#include "intprops.h"
#include "minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* List of variable names. */
struct varname
  {
    struct varname *next;
    char name[SHORT_NAME_LEN + 1];
  };

/* Represents a FLIP input program. */
struct flip_pgm
  {
    struct pool *pool;          /* Pool containing FLIP data. */
    const struct variable **var;      /* Variables to transpose. */
    int *idx_to_fv;             /* var[]->index to compacted sink case fv. */
    size_t var_cnt;             /* Number of elements in `var'. */
    int case_cnt;               /* Pre-flip case count. */
    size_t case_size;           /* Post-flip bytes per case. */

    struct variable *new_names; /* Variable containing new variable names. */
    struct varname *new_names_head; /* First new variable. */
    struct varname *new_names_tail; /* Last new variable. */

    FILE *file;                 /* Temporary file containing data. */
    union value *input_buf;     /* Input buffer for temporary file. */
    size_t cases_read;          /* Number of cases already read. */
    bool error;                 /* Error reading temporary file? */
  };

static const struct casereader_class flip_casereader_class;

static void destroy_flip_pgm (struct flip_pgm *);
static bool flip_file (struct flip_pgm *);
static bool build_dictionary (struct dictionary *, struct flip_pgm *);
static bool write_flip_case (struct flip_pgm *, const struct ccase *);

/* Parses and executes FLIP. */
int
cmd_flip (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct flip_pgm *flip;
  struct casereader *input, *reader;
  union value *output_buf;
  struct ccase c;
  size_t i;
  bool ok;

  if (proc_make_temporary_transformations_permanent (ds))
    msg (SW, _("FLIP ignores TEMPORARY.  "
               "Temporary transformations will be made permanent."));

  flip = pool_create_container (struct flip_pgm, pool);
  flip->var = NULL;
  flip->idx_to_fv = dict_get_compacted_dict_index_to_case_index (dict);
  pool_register (flip->pool, free, flip->idx_to_fv);
  flip->var_cnt = 0;
  flip->case_cnt = 0;
  flip->new_names = NULL;
  flip->new_names_head = NULL;
  flip->new_names_tail = NULL;
  flip->file = NULL;
  flip->input_buf = NULL;
  flip->cases_read = 0;
  flip->error = false;

  lex_match (lexer, '/');
  if (lex_match_id (lexer, "VARIABLES"))
    {
      lex_match (lexer, '=');
      if (!parse_variables_const (lexer, dict, &flip->var, &flip->var_cnt,
                            PV_NO_DUPLICATE))
	goto error;
      lex_match (lexer, '/');
    }
  else
    dict_get_vars (dict, &flip->var, &flip->var_cnt, 1u << DC_SYSTEM);
  pool_register (flip->pool, free, flip->var);

  lex_match (lexer, '/');
  if (lex_match_id (lexer, "NEWNAMES"))
    {
      lex_match (lexer, '=');
      flip->new_names = parse_variable (lexer, dict);
      if (!flip->new_names)
        goto error;
    }
  else
    flip->new_names = dict_lookup_var (dict, "CASE_LBL");

  if (flip->new_names)
    {
      for (i = 0; i < flip->var_cnt; i++)
	if (flip->var[i] == flip->new_names)
	  {
            remove_element (flip->var, flip->var_cnt, sizeof *flip->var, i);
	    flip->var_cnt--;
	    break;
	  }
    }

  output_buf = pool_nalloc (flip->pool,
                                  flip->var_cnt, sizeof *output_buf);

  flip->file = pool_tmpfile (flip->pool);
  if (flip->file == NULL)
    {
      msg (SE, _("Could not create temporary file for FLIP."));
      goto error;
    }

  /* Write variable names as first case. */
  for (i = 0; i < flip->var_cnt; i++)
    buf_copy_str_rpad (output_buf[i].s, MAX_SHORT_STRING,
                       var_get_name (flip->var[i]));
  if (fwrite (output_buf, sizeof *output_buf,
              flip->var_cnt, flip->file) != (size_t) flip->var_cnt)
    {
      msg (SE, _("Error writing FLIP file: %s."), strerror (errno));
      goto error;
    }

  flip->case_cnt = 1;

  /* Read the active file into a flip_sink. */
  proc_make_temporary_transformations_permanent (ds);
  proc_discard_output (ds);

  input = proc_open (ds);
  while (casereader_read (input, &c))
    {
      write_flip_case (flip, &c);
      case_destroy (&c);
    }
  ok = casereader_destroy (input);
  ok = proc_commit (ds) && ok;

  /* Flip the data we read. */
  if (!ok || !flip_file (flip))
    {
      proc_discard_active_file (ds);
      goto error;
    }

  /* Flip the dictionary. */
  dict_clear (dict);
  if (!build_dictionary (dict, flip))
    {
      proc_discard_active_file (ds);
      goto error;
    }
  flip->case_size = dict_get_case_size (dict);

  /* Set up flipped data for reading. */
  reader = casereader_create_sequential (NULL, dict_get_next_value_idx (dict),
                                         flip->case_cnt,
                                         &flip_casereader_class, flip);
  proc_set_active_file_data (ds, reader);
  return lex_end_of_command (lexer);

 error:
  destroy_flip_pgm (flip);
  return CMD_CASCADING_FAILURE;
}

/* Destroys FLIP. */
static void
destroy_flip_pgm (struct flip_pgm *flip)
{
  if (flip != NULL)
    pool_destroy (flip->pool);
}

/* Make a new variable with base name NAME, which is bowdlerized and
   mangled until acceptable, and returns success. */
static int
make_new_var (struct dictionary *dict, char name[])
{
  char *cp;

  /* Trim trailing spaces. */
  cp = strchr (name, '\0');
  while (cp > name && isspace ((unsigned char) cp[-1]))
    *--cp = '\0';

  /* Fix invalid characters. */
  for (cp = name; *cp && cp < name + SHORT_NAME_LEN; cp++)
    if (cp == name)
      {
        if (!lex_is_id1 (*cp) || *cp == '$')
          *cp = 'V';
      }
    else
      {
        if (!lex_is_idn (*cp))
          *cp = '_';
      }
  *cp = '\0';
  str_uppercase (name);

  if (dict_create_var (dict, name, 0))
    return 1;

  /* Add numeric extensions until acceptable. */
  {
    const int len = (int) strlen (name);
    char n[SHORT_NAME_LEN + 1];
    int i;

    for (i = 1; i < 10000000; i++)
      {
	int ofs = MIN (7 - intlog10 (i), len);
	memcpy (n, name, ofs);
	sprintf (&n[ofs], "%d", i);

	if (dict_create_var (dict, n, 0))
	  return 1;
      }
  }

  msg (SE, _("Could not create acceptable variant for variable %s."), name);
  return 0;
}

/* Make a new dictionary for all the new variable names. */
static bool
build_dictionary (struct dictionary *dict, struct flip_pgm *flip)
{
  dict_create_var_assert (dict, "CASE_LBL", 8);

  if (flip->new_names_head == NULL)
    {
      int i;

      if (flip->case_cnt > 99999)
	{
	  msg (SE, _("Cannot create more than 99999 variable names."));
	  return false;
	}

      for (i = 0; i < flip->case_cnt; i++)
	{
          struct variable *v;
	  char s[SHORT_NAME_LEN + 1];

	  sprintf (s, "VAR%03d", i);
	  v = dict_create_var_assert (dict, s, 0);
	}
    }
  else
    {
      struct varname *v;

      for (v = flip->new_names_head; v; v = v->next)
        if (!make_new_var (dict, v->name))
          return false;
    }

  return true;
}

/* Writes case C to the FLIP sink.
   Returns true if successful, false if an I/O error occurred. */
static bool
write_flip_case (struct flip_pgm *flip, const struct ccase *c)
{
  size_t i;

  flip->case_cnt++;

  if (flip->new_names != NULL)
    {
      struct varname *v = pool_alloc (flip->pool, sizeof *v);
      int fv = flip->idx_to_fv[var_get_dict_index (flip->new_names)];
      v->next = NULL;
      if (var_is_numeric (flip->new_names))
        {
          double f = case_num_idx (c, fv);

          if (f == SYSMIS)
            strcpy (v->name, "VSYSMIS");
          else if (f < INT_MIN)
            strcpy (v->name, "VNEGINF");
          else if (f > INT_MAX)
            strcpy (v->name, "VPOSINF");
          else
            snprintf (v->name, sizeof v->name, "V%d", (int) f);
        }
      else
	{
	  int width = MIN (var_get_width (flip->new_names), MAX_SHORT_STRING);
	  memcpy (v->name, case_str_idx (c, fv), width);
	  v->name[width] = 0;
	}

      if (flip->new_names_head == NULL)
	flip->new_names_head = v;
      else
	flip->new_names_tail->next = v;
      flip->new_names_tail = v;
    }

  /* Write to external file. */
  for (i = 0; i < flip->var_cnt; i++)
    {
      double out;

      if (var_is_numeric (flip->var[i]))
        {
          int fv = flip->idx_to_fv[var_get_dict_index (flip->var[i])];
          out = case_num_idx (c, fv);
        }
      else
        out = SYSMIS;
      fwrite (&out, sizeof out, 1, flip->file);
    }
  return true;
}

/* Transposes the external file into a new file. */
static bool
flip_file (struct flip_pgm *flip)
{
  size_t case_bytes;
  size_t case_capacity;
  size_t case_idx;
  union value *input_buf, *output_buf;
  FILE *input_file, *output_file;

  /* Allocate memory for many cases. */
  case_bytes = flip->var_cnt * sizeof *input_buf;
  case_capacity = get_workspace () / case_bytes;
  if (case_capacity > flip->case_cnt * 2)
    case_capacity = flip->case_cnt * 2;
  if (case_capacity < 2)
    case_capacity = 2;
  for (;;)
    {
      size_t bytes = case_bytes * case_capacity;
      if (case_capacity > 2)
        input_buf = malloc (bytes);
      else
        input_buf = xmalloc (bytes);
      if (input_buf != NULL)
	break;

      case_capacity /= 2;
      if (case_capacity < 2)
	case_capacity = 2;
    }
  pool_register (flip->pool, free, input_buf);

  /* Use half the allocated memory for input_buf, half for
     output_buf. */
  case_capacity /= 2;
  output_buf = input_buf + flip->var_cnt * case_capacity;

  input_file = flip->file;
  if (fseek (input_file, 0, SEEK_SET) != 0)
    {
      msg (SE, _("Error rewinding FLIP file: %s."), strerror (errno));
      return false;
    }

  output_file = pool_tmpfile (flip->pool);
  if (output_file == NULL)
    {
      msg (SE, _("Error creating FLIP source file."));
      return false;
    }

  for (case_idx = 0; case_idx < flip->case_cnt; )
    {
      unsigned long read_cases = MIN (flip->case_cnt - case_idx,
                                      case_capacity);
      size_t i;

      if (read_cases != fread (input_buf, case_bytes, read_cases, input_file))
        {
          if (ferror (input_file))
            msg (SE, _("Error reading FLIP file: %s."), strerror (errno));
          else
            msg (SE, _("Unexpected end of file reading FLIP file."));
          return false;
        }

      for (i = 0; i < flip->var_cnt; i++)
	{
	  unsigned long j;

	  for (j = 0; j < read_cases; j++)
	    output_buf[j] = input_buf[i + j * flip->var_cnt];

#ifndef HAVE_FSEEKO
#define fseeko fseek
#endif

#ifndef HAVE_OFF_T
#define off_t long int
#endif

	  if (fseeko (output_file,
                      sizeof *input_buf * (case_idx
                                           + (off_t) i * flip->case_cnt),
                      SEEK_SET) != 0)
            {
              msg (SE, _("Error seeking FLIP source file: %s."),
                   strerror (errno));
              return false;
            }

	  if (fwrite (output_buf, sizeof *output_buf, read_cases, output_file)
	      != read_cases)
            {
              msg (SE, _("Error writing FLIP source file: %s."),
                   strerror (errno));
              return false;
            }
	}

      case_idx += read_cases;
    }

  if (pool_fclose (flip->pool, input_file) == EOF)
    {
      msg (SE, _("Error closing FLIP source file: %s."), strerror (errno));
      return false;
    }
  pool_unregister (flip->pool, input_buf);
  free (input_buf);

  if (fseek (output_file, 0, SEEK_SET) != 0)
    {
      msg (SE, _("Error rewinding FLIP source file: %s."), strerror (errno));
      return false;
    }
  flip->file = output_file;

  return true;
}

/* Reads one case into C.
   Returns true if successful, false at end of file or if an
   I/O error occurred. */
static bool
flip_casereader_read (struct casereader *reader UNUSED, void *flip_,
                      struct ccase *c)
{
  struct flip_pgm *flip = flip_;
  size_t i;

  if (flip->error || flip->cases_read >= flip->var_cnt)
    return false;

  case_create (c, flip->case_cnt);
  for (i = 0; i < flip->case_cnt; i++)
    {
      double in;
      if (fread (&in, sizeof in, 1, flip->file) != 1)
        {
          case_destroy (c);
          if (ferror (flip->file))
            msg (SE, _("Error reading FLIP temporary file: %s."),
                 strerror (errno));
          else if (feof (flip->file))
            msg (SE, _("Unexpected end of file reading FLIP temporary file."));
          else
            NOT_REACHED ();
          flip->error = true;
          return false;
        }
      case_data_rw_idx (c, i)->f = in;
    }

  flip->cases_read++;

  return true;
}

/* Destroys the source.
   Returns true if successful read, false if an I/O occurred
   during destruction or previously. */
static void
flip_casereader_destroy (struct casereader *reader UNUSED, void *flip_)
{
  struct flip_pgm *flip = flip_;
  if (flip->error)
    casereader_force_error (reader);
  destroy_flip_pgm (flip);
}

static const struct casereader_class flip_casereader_class =
  {
    flip_casereader_read,
    flip_casereader_destroy,
    NULL,
    NULL,
  };
