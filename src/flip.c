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

/* FIXME: does this work with long string variables? */

#include <config.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
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
    int var_cnt;                /* Number of variables. */
    struct variable *newnames;  /* Variable containing new variable names. */
    struct varname *new_names_head, *new_names_tail;
                                /* New variable names. */
    int case_count;             /* Number of cases. */

  };

static void destroy_flip_pgm (struct flip_pgm *flip);
static struct case_sink *flip_sink_create (struct flip_pgm *);
static const struct case_source_class flip_source_class;
static int build_dictionary (struct flip_pgm *flip);

/* Parses and executes FLIP. */
int
cmd_flip (void)
{
  struct flip_pgm *flip;

  flip = xmalloc (sizeof *flip);
  flip->var = NULL;
  flip->var_cnt = 0;
  flip->newnames = NULL;
  flip->new_names_head = flip->new_names_tail = NULL;
  flip->case_count = 0;

  lex_match_id ("FLIP");
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
      flip->newnames = parse_variable ();
      if (!flip->newnames)
        goto error;
    }
  else
    flip->newnames = dict_lookup_var (default_dict, "CASE_LBL");

  if (flip->newnames)
    {
      int i;
      
      for (i = 0; i < flip->var_cnt; i++)
	if (flip->var[i] == flip->newnames)
	  {
	    memmove (&flip->var[i], &flip->var[i + 1], sizeof *flip->var * (flip->var_cnt - i - 1));
	    flip->var_cnt--;
	    break;
	  }
    }

  flip->case_count = 0;
  temp_trns = temporary = 0;
  vfm_sink = flip_sink_create (flip);
  flip->new_names_tail = NULL;
  procedure (NULL, NULL, NULL, NULL);

  dict_clear (default_dict);
  if (!build_dictionary (flip))
    {
      discard_variables ();
      goto error;
    }

  return lex_end_of_command ();

 error:
  destroy_flip_pgm (flip);
  return CMD_FAILURE;
}

static void
destroy_flip_pgm (struct flip_pgm *flip) 
{
  struct varname *iter, *next;
  
  free (flip->var);
  for (iter = flip->new_names_head; iter != NULL; iter = next) 
    {
      next = iter->next;
      free (iter);
    }
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
      
      if (flip->case_count > 99999)
	{
	  msg (SE, _("Cannot create more than 99999 variable names."));
	  return 0;
	}
      
      for (i = 0; i < flip->case_count; i++)
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
    int internal;			/* Internal or external flip. */
    char *old_names;                    /* Old variable names. */
    unsigned long case_cnt;             /* Number of cases. */
    FILE *file;                         /* Temporary file. */
  };

/* Source: Cases after transposition. */
struct flip_source_info 
  {
    struct flip_pgm *flip;              /* FLIP program. */
    char *old_names;    		/* Old variable names. */
    unsigned long case_cnt;		/* Number of cases. */
    FILE *file;                         /* Temporary file. */
  };

static const struct case_sink_class flip_sink_class;

static FILE *flip_file (struct flip_sink_info *info);

/* Creates a flip sink based on FLIP, of which it takes
   ownership. */
static struct case_sink *
flip_sink_create (struct flip_pgm *flip) 
{
  struct flip_sink_info *info = xmalloc (sizeof *info);

  info->flip = flip;
  info->case_cnt = 0;
  
  {
    size_t n = flip->var_cnt;
    char *p;
    int i;
    
    for (i = 0; i < flip->var_cnt; i++)
      n += strlen (flip->var[i]->name);
    p = info->old_names = xmalloc (n);
    for (i = 0; i < flip->var_cnt; i++)
      p = stpcpy (p, flip->var[i]->name) + 1;
  }

  return create_case_sink (&flip_sink_class, info);
}

/* Open the FLIP sink. */
static void
flip_sink_open (struct case_sink *sink) 
{
  struct flip_sink_info *info = sink->aux;

  info->file = tmpfile ();
  if (!info->file)
    msg (FE, _("Could not create temporary file for FLIP."));
}

/* Writes case C to the FLIP sink. */
static void
flip_sink_write (struct case_sink *sink, struct ccase *c)
{
  struct flip_sink_info *info = sink->aux;
  struct flip_pgm *flip = info->flip;
  
  info->case_cnt++;

  if (flip->newnames)
    {
      struct varname *v = xmalloc (sizeof (struct varname));
      v->next = NULL;
      if (flip->newnames->type == NUMERIC) 
        {
          double f = c->data[flip->newnames->fv].f;

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
	  int width = min (flip->newnames->width, 8);
	  memcpy (v->name, c->data[flip->newnames->fv].s, width);
	  v->name[width] = 0;
	}
      
      if (flip->new_names_head == NULL)
	flip->new_names_head = v;
      else
	flip->new_names_tail->next = v;
      flip->new_names_tail = v;
    }
  else
    flip->case_count++;


  /* Write to external file. */
  {
    double *d = local_alloc (sizeof *d * flip->var_cnt);
    int i;

    for (i = 0; i < flip->var_cnt; i++)
      if (flip->var[i]->type == NUMERIC)
	d[i] = c->data[flip->var[i]->fv].f;
      else
	d[i] = SYSMIS;
	  
    if (fwrite (d, sizeof *d, flip->var_cnt, info->file) != (size_t) flip->var_cnt)
      msg (FE, _("Error writing FLIP file: %s."),
	   strerror (errno));

    local_free (d);
  }
}

/* Destroy sink's internal data. */
static void
flip_sink_destroy (struct case_sink *sink)
{
  struct flip_sink_info *info = sink->aux;
  
  free (info->old_names);
  destroy_flip_pgm (info->flip);
  free (info);
}

/* Convert the FLIP sink into a source. */
static struct case_source *
flip_sink_make_source (struct case_sink *sink)
{
  struct flip_sink_info *sink_info = sink->aux;
  struct flip_source_info *source_info;

  source_info = xmalloc (sizeof *source_info);
  source_info->flip = sink_info->flip;
  source_info->old_names = sink_info->old_names;
  source_info->case_cnt = sink_info->case_cnt;
  source_info->file = flip_file (sink_info);
  fclose (sink_info->file);

  free (sink_info);

  return create_case_source (&flip_source_class, source_info);
}

/* Transposes the external file into a new file and returns a
   pointer to the transposed file. */
static FILE *
flip_file (struct flip_sink_info *info)
{
  struct flip_pgm *flip = info->flip;
  size_t case_bytes;
  size_t case_capacity;
  size_t case_idx;
  union value *input_buf, *output_buf;
  FILE *input_file, *output_file;

  /* Allocate memory for many cases. */
  case_bytes = flip->var_cnt * sizeof *input_buf;
  case_capacity = set_max_workspace / case_bytes;
  if (case_capacity > info->case_cnt)
    case_capacity = info->case_cnt;
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

  input_file = info->file;
  if (fseek (input_file, 0, SEEK_SET) != 0)
    msg (FE, _("Error rewinding FLIP file: %s."), strerror (errno));

  output_file = tmpfile ();
  if (output_file == NULL)
    msg (FE, _("Error creating FLIP source file."));
  
  for (case_idx = 0; case_idx < info->case_cnt; )
    {
      unsigned long read_cases = min (info->case_cnt - case_idx,
                                      case_capacity);
      int i;

      if (read_cases != fread (input_buf, case_bytes, read_cases, input_file))
	msg (FE, _("Error reading FLIP file: %s."), strerror (errno));

      for (i = 0; i < flip->var_cnt; i++)
	{
	  unsigned long j;
	  
	  for (j = 0; j < read_cases; j++)
	    output_buf[j] = input_buf[i + j * flip->var_cnt];

	  if (fseek (output_file,
                     sizeof *input_buf * (case_idx + i * info->case_cnt),
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

  if (fseek (output_file, 0, SEEK_SET) != 0)
    msg (FE, _("Error rewind FLIP source file: %s."), strerror (errno));

  free (input_buf);
  return output_file;
}

/* FLIP sink class. */
static const struct case_sink_class flip_sink_class = 
  {
    "FLIP",
    flip_sink_open,
    flip_sink_write,
    flip_sink_destroy,
    flip_sink_make_source,
  };

/* Reads the FLIP stream and passes it to WRITE_CASE(). */
static void
flip_source_read (struct case_source *source,
                  write_case_func *write_case, write_case_data wc_data)
{
  struct flip_source_info *info = source->aux;
  struct flip_pgm *flip = info->flip;
  int i;
  char *p = info->old_names;
      
  for (i = 0; i < flip->var_cnt; i++)
    {
      st_bare_pad_copy (temp_case->data[0].s, p, 8);
      p = strchr (p, 0) + 1;

      if (fread (&temp_case->data[1], sizeof (double), info->case_cnt,
                 info->file) != info->case_cnt)
        msg (FE, _("Error reading FLIP source file: %s."),
             strerror (errno));

      if (!write_case (wc_data))
        return;
    }
}

/* Destroy source's internal data. */
static void
flip_source_destroy (struct case_source *source)
{
  struct flip_source_info *info = source->aux;

  destroy_flip_pgm (info->flip);
  free (info->old_names);
  fclose (info->file);
  free (info);
}

static const struct case_source_class flip_source_class = 
  {
    "FLIP",
    NULL,
    flip_source_read,
    flip_source_destroy
  };
