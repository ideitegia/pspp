/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011, 2013 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/casereader-provider.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/settings.h"
#include "data/short-names.h"
#include "data/value.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "data/data-in.h"
#include "data/data-out.h"

#include "gl/intprops.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* List of variable names. */
struct var_names
  {
    const char **names;
    size_t n_names, allocated_names;
  };

static void var_names_init (struct var_names *);
static void var_names_add (struct pool *, struct var_names *, const char *);

/* Represents a FLIP input program. */
struct flip_pgm
  {
    struct pool *pool;          /* Pool containing FLIP data. */
    size_t n_vars;              /* Pre-flip number of variables. */
    int n_cases;                /* Pre-flip number of cases. */

    struct variable *new_names_var; /* Variable with new variable names. */
    const char *encoding;           /* Variable names' encoding. */
    struct var_names old_names; /* Variable names before FLIP. */
    struct var_names new_names; /* Variable names after FLIP. */

    FILE *file;                 /* Temporary file containing data. */
    size_t cases_read;          /* Number of cases already read. */
    bool error;                 /* Error reading temporary file? */
  };

static const struct casereader_class flip_casereader_class;

static void destroy_flip_pgm (struct flip_pgm *);
static bool flip_file (struct flip_pgm *);
static void make_new_var (struct dictionary *, const char *name);

/* Parses and executes FLIP. */
int
cmd_flip (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *old_dict = dataset_dict (ds);
  struct dictionary *new_dict = NULL;
  const struct variable **vars;
  struct flip_pgm *flip;
  struct casereader *input, *reader;
  struct ccase *c;
  size_t i;
  bool ok;

  if (proc_make_temporary_transformations_permanent (ds))
    msg (SW, _("FLIP ignores TEMPORARY.  "
               "Temporary transformations will be made permanent."));

  flip = pool_create_container (struct flip_pgm, pool);
  flip->n_vars = 0;
  flip->n_cases = 0;
  flip->new_names_var = NULL;
  var_names_init (&flip->old_names);
  var_names_init (&flip->new_names);
  flip->file = NULL;
  flip->cases_read = 0;
  flip->error = false;

  lex_match (lexer, T_SLASH);
  if (lex_match_id (lexer, "VARIABLES"))
    {
      lex_match (lexer, T_EQUALS);
      if (!parse_variables_const (lexer, old_dict, &vars, &flip->n_vars,
                                  PV_NO_DUPLICATE))
	goto error;
      lex_match (lexer, T_SLASH);
    }
  else
    dict_get_vars (old_dict, &vars, &flip->n_vars, DC_SYSTEM);
  pool_register (flip->pool, free, vars);

  lex_match (lexer, T_SLASH);
  if (lex_match_id (lexer, "NEWNAMES"))
    {
      lex_match (lexer, T_EQUALS);
      flip->new_names_var = parse_variable (lexer, old_dict);
      if (!flip->new_names_var)
        goto error;
    }
  else
    flip->new_names_var = dict_lookup_var (old_dict, "CASE_LBL");

  if (flip->new_names_var)
    {
      for (i = 0; i < flip->n_vars; i++)
	if (vars[i] == flip->new_names_var)
	  {
            remove_element (vars, flip->n_vars, sizeof *vars, i);
	    flip->n_vars--;
	    break;
	  }
    }

  flip->file = pool_create_temp_file (flip->pool);
  if (flip->file == NULL)
    {
      msg (SE, _("Could not create temporary file for FLIP."));
      goto error;
    }

  /* Save old variable names for use as values of CASE_LBL
     variable in flipped file. */
  for (i = 0; i < flip->n_vars; i++)
    var_names_add (flip->pool, &flip->old_names,
                   pool_strdup (flip->pool, var_get_name (vars[i])));

  /* Read the active dataset into a flip_sink. */
  proc_discard_output (ds);

  /* Save old dictionary. */
  new_dict = dict_clone (old_dict);
  flip->encoding = dict_get_encoding (new_dict);
  dict_clear (new_dict);

  input = proc_open_filtering (ds, false);
  while ((c = casereader_read (input)) != NULL)
    {
      flip->n_cases++;
      for (i = 0; i < flip->n_vars; i++)
        {
          const struct variable *v = vars[i];
          double out = var_is_numeric (v) ? case_num (c, v) : SYSMIS;
          fwrite (&out, sizeof out, 1, flip->file);
        }
      if (flip->new_names_var != NULL)
        {
          const union value *value = case_data (c, flip->new_names_var);
          const char *name;
          if (var_is_numeric (flip->new_names_var))
            {
              double f = value->f;
              name = (f == SYSMIS ? "VSYSMIS"
                      : f < INT_MIN ? "VNEGINF"
                      : f > INT_MAX ? "VPOSINF"
                      : pool_asprintf (flip->pool, "V%d", (int) f));
            }
          else
            {
              name = data_out_pool (value, dict_get_encoding (old_dict),
                                    var_get_write_format (flip->new_names_var),
                                    flip->pool);
            }
          var_names_add (flip->pool, &flip->new_names, name);
        }
      case_unref (c);
    }
  ok = casereader_destroy (input);
  ok = proc_commit (ds) && ok;

  /* Flip the data we read. */
  if (!ok || !flip_file (flip))
    {
      dataset_clear (ds);
      goto error;
    }

  /* Flip the dictionary. */
  dict_create_var_assert (new_dict, "CASE_LBL", 8);
  for (i = 0; i < flip->n_cases; i++)
    if (flip->new_names.n_names)
      make_new_var (new_dict, flip->new_names.names[i]);
    else
      {
        char s[3 + INT_STRLEN_BOUND (i) + 1];
        sprintf (s, "VAR%03zu", i);
        dict_create_var_assert (new_dict, s, 0);
      }

  /* Set up flipped data for reading. */
  reader = casereader_create_sequential (NULL, dict_get_proto (new_dict),
                                         flip->n_vars,
                                         &flip_casereader_class, flip);
  dataset_set_dict (ds, new_dict);
  dataset_set_source (ds, reader);
  return CMD_SUCCESS;

 error:
  dict_destroy (new_dict);
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
   mangled until acceptable. */
static void
make_new_var (struct dictionary *dict, const char *name_)
{
  char *name = xstrdup (name_);
  char *cp;

  /* Trim trailing spaces. */
  cp = strchr (name, '\0');
  while (cp > name && isspace ((unsigned char) cp[-1]))
    *--cp = '\0';

  /* Fix invalid characters. */
  for (cp = name; *cp && cp < name + ID_MAX_LEN; cp++)
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

  /* Use the mangled name, if it is available, or add numeric
     extensions until we find one that is. */
  if (!dict_create_var (dict, name, 0))
    {
      int len = strlen (name);
      int i;
      for (i = 1; ; i++)
        {
          char n[ID_MAX_LEN + 1];
          int ofs = MIN (ID_MAX_LEN - 1 - intlog10 (i), len);
          strncpy (n, name, ofs);
          sprintf (&n[ofs], "%d", i);

          if (dict_create_var (dict, n, 0))
            break;
        }
    }
  free (name);
}

/* Transposes the external file into a new file. */
static bool
flip_file (struct flip_pgm *flip)
{
  size_t case_bytes;
  size_t case_capacity;
  size_t case_idx;
  double *input_buf, *output_buf;
  FILE *input_file, *output_file;

  /* Allocate memory for many cases. */
  case_bytes = flip->n_vars * sizeof *input_buf;
  case_capacity = settings_get_workspace () / case_bytes;
  if (case_capacity > flip->n_cases * 2)
    case_capacity = flip->n_cases * 2;
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
  output_buf = input_buf + flip->n_vars * case_capacity;

  input_file = flip->file;
  if (fseeko (input_file, 0, SEEK_SET) != 0)
    {
      msg (SE, _("Error rewinding FLIP file: %s."), strerror (errno));
      return false;
    }

  output_file = pool_create_temp_file (flip->pool);
  if (output_file == NULL)
    {
      msg (SE, _("Error creating FLIP source file."));
      return false;
    }

  for (case_idx = 0; case_idx < flip->n_cases; )
    {
      unsigned long read_cases = MIN (flip->n_cases - case_idx,
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

      for (i = 0; i < flip->n_vars; i++)
	{
	  unsigned long j;

	  for (j = 0; j < read_cases; j++)
	    output_buf[j] = input_buf[i + j * flip->n_vars];

	  if (fseeko (output_file,
                      sizeof *input_buf * (case_idx
                                           + (off_t) i * flip->n_cases),
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

  pool_fclose_temp_file (flip->pool, input_file);
  pool_unregister (flip->pool, input_buf);
  free (input_buf);

  if (fseeko (output_file, 0, SEEK_SET) != 0)
    {
      msg (SE, _("Error rewinding FLIP source file: %s."), strerror (errno));
      return false;
    }
  flip->file = output_file;

  return true;
}

/* Reads and returns one case.
   Returns a null pointer at end of file or if an I/O error occurred. */
static struct ccase *
flip_casereader_read (struct casereader *reader, void *flip_)
{
  struct flip_pgm *flip = flip_;
  struct ccase *c;
  size_t i;

  if (flip->error || flip->cases_read >= flip->n_vars)
    return false;

  c = case_create (casereader_get_proto (reader));
  data_in (ss_cstr (flip->old_names.names[flip->cases_read]), flip->encoding,
           FMT_A, case_data_rw_idx (c, 0), 8, flip->encoding);

  for (i = 0; i < flip->n_cases; i++)
    {
      double in;
      if (fread (&in, sizeof in, 1, flip->file) != 1)
        {
          case_unref (c);
          if (ferror (flip->file))
            msg (SE, _("Error reading FLIP temporary file: %s."),
                 strerror (errno));
          else if (feof (flip->file))
            msg (SE, _("Unexpected end of file reading FLIP temporary file."));
          else
            NOT_REACHED ();
          flip->error = true;
          return NULL;
        }
      case_data_rw_idx (c, i + 1)->f = in;
    }

  flip->cases_read++;

  return c;
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

static void
var_names_init (struct var_names *vn)
{
  vn->names = NULL;
  vn->n_names = 0;
  vn->allocated_names = 0;
}

static void
var_names_add (struct pool *pool, struct var_names *vn, const char *name)
{
  if (vn->n_names >= vn->allocated_names)
    vn->names = pool_2nrealloc (pool, vn->names, &vn->allocated_names,
                                sizeof *vn->names);
  vn->names[vn->n_names++] = name;
}

