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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include "error.h"
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#include "alloc.h"
#include "case.h"
#include "command.h"
#include "dictionary.h"
#include "error.h"
#include "lexer.h"
#include "misc.h"
#include "settings.h"
#include "str.h"
#include "var.h"
#include "vfm.h"

/* List of variable names. */
struct varname
  {
    struct varname *next;
    char name[9];
  };

/* Represents a FLIP input program. */
struct flip_pgm 
  {
    struct variable **var;      /* Variables to transpose. */
    int *idx_to_fv;             /* var[]->index to compacted sink case fv. */
    int var_cnt;                /* Number of elements in `var'. */
    int case_cnt;               /* Pre-flip case count. */
    size_t case_size;           /* Post-flip bytes per case. */

    struct variable *new_names; /* Variable containing new variable names. */
    struct varname *new_names_head; /* First new variable. */
    struct varname *new_names_tail; /* Last new variable. */

    FILE *file;                 /* Temporary file containing data. */
  };

static void destroy_flip_pgm (struct flip_pgm *);
static struct case_sink *flip_sink_create (struct flip_pgm *);
static struct case_source *flip_source_create (struct flip_pgm *);
static void flip_file (struct flip_pgm *);
static int build_dictionary (struct flip_pgm *);

static const struct case_source_class flip_source_class;
static const struct case_sink_class flip_sink_class;

/* Parses and executes FLIP. */
int
cmd_flip (void)
{
  struct flip_pgm *flip;

  if (temporary != 0)
    {
      msg (SM, _("FLIP ignores TEMPORARY.  "
                 "Temporary transformations will be made permanent."));
      cancel_temporary (); 
    }

  flip = xmalloc (sizeof *flip);
  flip->var = NULL;
  flip->idx_to_fv = dict_get_compacted_idx_to_fv (default_dict);
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
      if (!parse_variables (default_dict, &flip->var, &flip->var_cnt, PV_NO_DUPLICATE))
	return CMD_FAILURE;
      lex_match ('/');
    }
  else
    dict_get_vars (default_dict, &flip->var, &flip->var_cnt, 1u << DC_SYSTEM);

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
      int i;
      
      for (i = 0; i < flip->var_cnt; i++)
	if (flip->var[i] == flip->new_names)
	  {
	    memmove (&flip->var[i], &flip->var[i + 1], sizeof *flip->var * (flip->var_cnt - i - 1));
	    flip->var_cnt--;
	    break;
	  }
    }

  /* Read the active file into a flip_sink. */
  flip->case_cnt = 0;
  temp_trns = temporary = 0;
  vfm_sink = flip_sink_create (flip);
  flip->new_names_tail = NULL;
  procedure (NULL, NULL);

  /* Flip the data we read. */
  flip_file (flip);

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

  return lex_end_of_command ();

 error:
  destroy_flip_pgm (flip);
  return CMD_FAILURE;
}

/* Destroys FLIP. */
static void
destroy_flip_pgm (struct flip_pgm *flip) 
{
  struct varname *iter, *next;
  
  free (flip->var);
  free (flip->idx_to_fv);
  for (iter = flip->new_names_head; iter != NULL; iter = next) 
    {
      next = iter->next;
      free (iter);
    }
  if (flip->file != NULL)
    fclose (flip->file);
  free (flip);
}

/* Make a new variable with base name NAME, which is bowdlerized and
   mangled until acceptable, and returns success. */
static int
make_new_var (char name[])
{
  /* Fix invalid characters. */
  {
    char *cp;
  
    for (cp = name; *cp && !isspace (*cp); cp++)
      {
	*cp = toupper ((unsigned char) *cp);
	if (!isalpha (*cp) && *cp != '@' && *cp != '#'
	    && (cp == name || (*cp != '.' && *cp != '$' && *cp != '_'
                               && !isdigit (*cp))))
	  {
	    if (cp == name)
	      *cp = 'V';	/* _ not valid in first position. */
	    else
	      *cp = '_';
	  }
      }
    *cp = 0;
  }
  
  if (dict_create_var (default_dict, name, 0))
    return 1;

  /* Add numeric extensions until acceptable. */
  {
    int len = (int) strlen (name);
    char n[9];
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
	  char s[9];

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
     
/* Cases during transposition. */
struct flip_sink_info 
  {
    struct flip_pgm *flip;              /* FLIP program. */
    union value *output_buf;            /* Case output buffer. */
  };

/* Creates a flip sink based on FLIP. */
static struct case_sink *
flip_sink_create (struct flip_pgm *flip) 
{
  struct flip_sink_info *info = xmalloc (sizeof *info);
  int i;

  info->flip = flip;
  info->output_buf = xmalloc (sizeof *info->output_buf * flip->var_cnt);

  flip->file = tmpfile ();
  if (!flip->file)
    msg (FE, _("Could not create temporary file for FLIP."));

  /* Write variable names as first case. */
  for (i = 0; i < flip->var_cnt; i++) 
    st_bare_pad_copy (info->output_buf[i].s, flip->var[i]->name, 8);
  if (fwrite (info->output_buf, sizeof *info->output_buf,
              flip->var_cnt, flip->file) != (size_t) flip->var_cnt)
    msg (FE, _("Error writing FLIP file: %s."), strerror (errno));

  flip->case_cnt = 1;

  return create_case_sink (&flip_sink_class, default_dict, info);
}

/* Writes case C to the FLIP sink. */
static void
flip_sink_write (struct case_sink *sink, const struct ccase *c)
{
  struct flip_sink_info *info = sink->aux;
  struct flip_pgm *flip = info->flip;
  int i;
  
  flip->case_cnt++;

  if (flip->new_names != NULL)
    {
      struct varname *v = xmalloc (sizeof (struct varname));
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
              char name[INT_DIGITS + 2];
              sprintf (name, "V%d", (int) f);
              strncpy (v->name, name, 8);
              name[8] = 0; 
            }
        }
      else
	{
	  int width = min (flip->new_names->width, 8);
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
      info->output_buf[i].f = out;
    }
	  
  if (fwrite (info->output_buf, sizeof *info->output_buf,
              flip->var_cnt, flip->file) != (size_t) flip->var_cnt)
    msg (FE, _("Error writing FLIP file: %s."), strerror (errno));
}

/* Transposes the external file into a new file. */
static void
flip_file (struct flip_pgm *flip)
{
  size_t case_bytes;
  size_t case_capacity;
  size_t case_idx;
  union value *input_buf, *output_buf;
  FILE *input_file, *output_file;

  /* Allocate memory for many cases. */
  case_bytes = flip->var_cnt * sizeof *input_buf;
  case_capacity = get_max_workspace() / case_bytes;
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

  /* Use half the allocated memory for input_buf, half for
     output_buf. */
  case_capacity /= 2;
  output_buf = input_buf + flip->var_cnt * case_capacity;

  input_file = flip->file;
  if (fseek (input_file, 0, SEEK_SET) != 0)
    msg (FE, _("Error rewinding FLIP file: %s."), strerror (errno));

  output_file = tmpfile ();
  if (output_file == NULL)
    msg (FE, _("Error creating FLIP source file."));
  
  for (case_idx = 0; case_idx < flip->case_cnt; )
    {
      unsigned long read_cases = min (flip->case_cnt - case_idx,
                                      case_capacity);
      int i;

      if (read_cases != fread (input_buf, case_bytes, read_cases, input_file))
	msg (FE, _("Error reading FLIP file: %s."), strerror (errno));

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
	    msg (FE, _("Error seeking FLIP source file: %s."),
		       strerror (errno));

	  if (fwrite (output_buf, sizeof *output_buf, read_cases, output_file)
	      != read_cases)
	    msg (FE, _("Error writing FLIP source file: %s."),
		 strerror (errno));
	}

      case_idx += read_cases;
    }

  fclose (input_file);
  free (input_buf);
  
  if (fseek (output_file, 0, SEEK_SET) != 0)
    msg (FE, _("Error rewind FLIP source file: %s."), strerror (errno));
  flip->file = output_file;
}

/* Destroy sink's internal data. */
static void
flip_sink_destroy (struct case_sink *sink)
{
  struct flip_sink_info *info = sink->aux;

  free (info->output_buf);
  free (info);
}

/* FLIP sink class. */
static const struct case_sink_class flip_sink_class = 
  {
    "FLIP",
    NULL,
    flip_sink_write,
    flip_sink_destroy,
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
   WRITE_CASE passing WC_DATA. */
static void
flip_source_read (struct case_source *source,
                  struct ccase *c,
                  write_case_func *write_case, write_case_data wc_data)
{
  struct flip_pgm *flip = source->aux;
  union value *input_buf;
  int i;

  input_buf = xmalloc (sizeof *input_buf * flip->case_cnt);
  for (i = 0; i < flip->var_cnt; i++)
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
            assert (0);
          break;
        }

      for (j = 0; j < flip->case_cnt; j++)
        case_data_rw (c, j)->f = input_buf[j].f;
      if (!write_case (wc_data))
        break;
    }
  free (input_buf);
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
