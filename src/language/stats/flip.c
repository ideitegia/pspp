/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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
#include <libpspp/message.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <libpspp/array.h>
#include <libpspp/alloc.h>
#include <data/case.h>
#include <language/command.h>
#include <data/dictionary.h>
#include <libpspp/message.h>
#include "intprops.h"
#include <language/lexer/lexer.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <data/settings.h>
#include <libpspp/str.h>
#include <data/value.h>
#include <data/variable.h>
#include <procedure.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

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
    struct variable **var;      /* Variables to transpose. */
    int *idx_to_fv;             /* var[]->index to compacted sink case fv. */
    size_t var_cnt;             /* Number of elements in `var'. */
    int case_cnt;               /* Pre-flip case count. */
    size_t case_size;           /* Post-flip bytes per case. */

    union value *output_buf;            /* Case output buffer. */

    struct variable *new_names; /* Variable containing new variable names. */
    struct varname *new_names_head; /* First new variable. */
    struct varname *new_names_tail; /* Last new variable. */

    FILE *file;                 /* Temporary file containing data. */
  };

static void destroy_flip_pgm (struct flip_pgm *);
static struct case_sink *flip_sink_create (struct flip_pgm *);
static struct case_source *flip_source_create (struct flip_pgm *);
static bool flip_file (struct flip_pgm *);
static int build_dictionary (struct flip_pgm *);

static const struct case_source_class flip_source_class;
static const struct case_sink_class flip_sink_class;

/* Parses and executes FLIP. */
int
cmd_flip (void)
{
  struct flip_pgm *flip;
  bool ok;

  if (temporary != 0)
    {
      msg (SM, _("FLIP ignores TEMPORARY.  "
                 "Temporary transformations will be made permanent."));
      cancel_temporary (); 
    }

  flip = pool_create_container (struct flip_pgm, pool);
  flip->var = NULL;
  flip->idx_to_fv = dict_get_compacted_idx_to_fv (default_dict);
  pool_register (flip->pool, free, flip->idx_to_fv);
  flip->var_cnt = 0;
  flip->case_cnt = 0;
  flip->new_names = NULL;
  flip->new_names_head = NULL;
  flip->new_names_tail = NULL;
  flip->file = NULL;

  lex_match ('/');
  if (lex_match_id ("VARIABLES"))
    {
      lex_match ('=');
      if (!parse_variables (default_dict, &flip->var, &flip->var_cnt,
                            PV_NO_DUPLICATE))
	goto error;
      lex_match ('/');
    }
  else
    dict_get_vars (default_dict, &flip->var, &flip->var_cnt, 1u << DC_SYSTEM);
  pool_register (flip->pool, free, flip->var);

  lex_match ('/');
  if (lex_match_id ("NEWNAMES"))
    {
      lex_match ('=');
      flip->new_names = parse_variable ();
      if (!flip->new_names)
        goto error;
    }
  else
    flip->new_names = dict_lookup_var (default_dict, "CASE_LBL");

  if (flip->new_names)
    {
      size_t i;
      
      for (i = 0; i < flip->var_cnt; i++)
	if (flip->var[i] == flip->new_names)
	  {
            remove_element (flip->var, flip->var_cnt, sizeof *flip->var, i);
	    flip->var_cnt--;
	    break;
	  }
    }

  /* Read the active file into a flip_sink. */
  flip->case_cnt = 0;
  temp_trns = temporary = 0;
  vfm_sink = flip_sink_create (flip);
  if (vfm_sink == NULL)
    goto error;
  flip->new_names_tail = NULL;
  ok = procedure (NULL, NULL);

  /* Flip the data we read. */
  if (!flip_file (flip)) 
    {
      discard_variables ();
      goto error;
    }

  /* Flip the dictionary. */
  dict_clear (default_dict);
  if (!build_dictionary (flip))
    {
      discard_variables ();
      goto error;
    }
  flip->case_size = dict_get_case_size (default_dict);

  /* Set up flipped data for reading. */
  vfm_source = flip_source_create (flip);

  return ok ? lex_end_of_command () : CMD_CASCADING_FAILURE;

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
make_new_var (char name[])
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
  
  if (dict_create_var (default_dict, name, 0))
    return 1;

  /* Add numeric extensions until acceptable. */
  {
    const int len = (int) strlen (name);
    char n[SHORT_NAME_LEN + 1];
    int i;

    for (i = 1; i < 10000000; i++)
      {
	int ofs = min (7 - intlog10 (i), len);
	memcpy (n, name, ofs);
	sprintf (&n[ofs], "%d", i);

	if (dict_create_var (default_dict, n, 0))
	  return 1;
      }
  }

  msg (SE, _("Could not create acceptable variant for variable %s."), name);
  return 0;
}

/* Make a new dictionary for all the new variable names. */
static int
build_dictionary (struct flip_pgm *flip)
{
  dict_create_var_assert (default_dict, "CASE_LBL", 8);

  if (flip->new_names_head == NULL)
    {
      int i;
      
      if (flip->case_cnt > 99999)
	{
	  msg (SE, _("Cannot create more than 99999 variable names."));
	  return 0;
	}
      
      for (i = 0; i < flip->case_cnt; i++)
	{
          struct variable *v;
	  char s[SHORT_NAME_LEN + 1];

	  sprintf (s, "VAR%03d", i);
	  v = dict_create_var_assert (default_dict, s, 0);
	}
    }
  else
    {
      struct varname *v;

      for (v = flip->new_names_head; v; v = v->next)
        if (!make_new_var (v->name))
          return 0;
    }
  
  return 1;
}
     
/* Creates a flip sink based on FLIP. */
static struct case_sink *
flip_sink_create (struct flip_pgm *flip) 
{
  size_t i;

  flip->output_buf = pool_nalloc (flip->pool,
                                  flip->var_cnt, sizeof *flip->output_buf);

  flip->file = pool_tmpfile (flip->pool);
  if (flip->file == NULL)
    {
      msg (SE, _("Could not create temporary file for FLIP."));
      return NULL;
    }

  /* Write variable names as first case. */
  for (i = 0; i < flip->var_cnt; i++) 
    buf_copy_str_rpad (flip->output_buf[i].s, MAX_SHORT_STRING,
                       flip->var[i]->name);
  if (fwrite (flip->output_buf, sizeof *flip->output_buf,
              flip->var_cnt, flip->file) != (size_t) flip->var_cnt) 
    {
      msg (SE, _("Error writing FLIP file: %s."), strerror (errno));
      return NULL;
    }

  flip->case_cnt = 1;

  return create_case_sink (&flip_sink_class, default_dict, flip);
}

/* Writes case C to the FLIP sink.
   Returns true if successful, false if an I/O error occurred. */
static bool
flip_sink_write (struct case_sink *sink, const struct ccase *c)
{
  struct flip_pgm *flip = sink->aux;
  size_t i;
  
  flip->case_cnt++;

  if (flip->new_names != NULL)
    {
      struct varname *v = pool_alloc (flip->pool, sizeof *v);
      v->next = NULL;
      if (flip->new_names->type == NUMERIC) 
        {
          double f = case_num (c, flip->idx_to_fv[flip->new_names->index]);

          if (f == SYSMIS)
            strcpy (v->name, "VSYSMIS");
          else if (f < INT_MIN)
            strcpy (v->name, "VNEGINF");
          else if (f > INT_MAX)
            strcpy (v->name, "VPOSINF");
          else 
            {
              char name[INT_STRLEN_BOUND (int) + 2];
              sprintf (name, "V%d", (int) f);
              str_copy_trunc (v->name, sizeof v->name, name);
            }
        }
      else
	{
	  int width = min (flip->new_names->width, MAX_SHORT_STRING);
	  memcpy (v->name, case_str (c, flip->idx_to_fv[flip->new_names->index]),
                  width);
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
      
      if (flip->var[i]->type == NUMERIC)
        out = case_num (c, flip->idx_to_fv[flip->var[i]->index]);
      else
        out = SYSMIS;
      flip->output_buf[i].f = out;
    }
	  
  if (fwrite (flip->output_buf, sizeof *flip->output_buf,
              flip->var_cnt, flip->file) != (size_t) flip->var_cnt) 
    {
      msg (SE, _("Error writing FLIP file: %s."), strerror (errno));
      return false; 
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
      unsigned long read_cases = min (flip->case_cnt - case_idx,
                                      case_capacity);
      size_t i;

      if (read_cases != fread (input_buf, case_bytes, read_cases, input_file)) 
        {
          msg (SE, _("Error reading FLIP file: %s."), strerror (errno));
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

  pool_fclose (flip->pool, input_file);
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

/* FLIP sink class. */
static const struct case_sink_class flip_sink_class = 
  {
    "FLIP",
    NULL,
    flip_sink_write,
    NULL,
    NULL,
  };

/* Creates and returns a FLIP source based on PGM,
   which should have already been used as a sink. */
static struct case_source *
flip_source_create (struct flip_pgm *pgm)
{
  return create_case_source (&flip_source_class, pgm);
}

/* Reads the FLIP stream.  Copies each case into C and calls
   WRITE_CASE passing WC_DATA.
   Returns true if successful, false if an I/O error occurred. */
static bool
flip_source_read (struct case_source *source,
                  struct ccase *c,
                  write_case_func *write_case, write_case_data wc_data)
{
  struct flip_pgm *flip = source->aux;
  union value *input_buf;
  size_t i;
  bool ok = true;

  input_buf = xnmalloc (flip->case_cnt, sizeof *input_buf);
  for (i = 0; ok && i < flip->var_cnt; i++)
    {
      size_t j;
      
      if (fread (input_buf, sizeof *input_buf, flip->case_cnt,
                 flip->file) != flip->case_cnt) 
        {
          if (ferror (flip->file))
            msg (SE, _("Error reading FLIP temporary file: %s."),
                 strerror (errno));
          else if (feof (flip->file))
            msg (SE, _("Unexpected end of file reading FLIP temporary file."));
          else
            abort ();
          ok = false;
          break;
        }

      for (j = 0; j < flip->case_cnt; j++)
        case_data_rw (c, j)->f = input_buf[j].f;
      ok = write_case (wc_data);
    }
  free (input_buf);

  return ok;
}

/* Destroy internal data in SOURCE. */
static void
flip_source_destroy (struct case_source *source)
{
  struct flip_pgm *flip = source->aux;

  destroy_flip_pgm (flip);
}

static const struct case_source_class flip_source_class = 
  {
    "FLIP",
    NULL,
    flip_source_read,
    flip_source_destroy
  };
