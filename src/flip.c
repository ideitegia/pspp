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
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "lexer.h"
#include "misc.h"
#include "str.h"
#include "var.h"
#include "vfm.h"

/* Variables to transpose. */
static struct variable **var;
static int nvar;

/* Variable containing new variable names. */
static struct variable *newnames;

/* List of variable names. */
struct varname
  {
    struct varname *next;
    char name[1];
  };

/* New variable names. */
static struct varname *new_names_head, *new_names_tail;
static int case_count;

static int build_dictionary (void);

/* Parses and executes FLIP. */
int
cmd_flip (void)
{
  lex_match_id ("FLIP");
  lex_match ('/');
  if (lex_match_id ("VARIABLES"))
    {
      lex_match ('=');
      if (!parse_variables (&default_dict, &var, &nvar, PV_NO_DUPLICATE))
	return CMD_FAILURE;
      lex_match ('/');
    }
  else
    fill_all_vars (&var, &nvar, FV_NO_SYSTEM);

  lex_match ('/');
  if (lex_match_id ("NEWNAMES"))
    {
      lex_match ('=');
      newnames = parse_variable ();
      if (!newnames)
	{
	  free (var);
	  return CMD_FAILURE;
	}
    }
  else
    newnames = find_variable ("CASE_LBL");

  if (newnames)
    {
      int i;
      
      for (i = 0; i < nvar; i++)
	if (var[i] == newnames)
	  {
	    memmove (&var[i], &var[i + 1], sizeof *var * (nvar - i - 1));
	    nvar--;
	    break;
	  }
    }

  case_count = 0;
  temp_trns = temporary = 0;
  vfm_sink = &flip_stream;
  new_names_tail = NULL;
  procedure (NULL, NULL, NULL);

  clear_default_dict ();
  if (!build_dictionary ())
    {
      discard_variables ();
      free (var);
      return CMD_FAILURE;
    }

  free (var);
  return lex_end_of_command ();
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
	    && (cp == name || (*cp != '.' && *cp != '$' && *cp != '_')))
	  {
	    if (cp == name)
	      *cp = 'V';	/* _ not valid in first position. */
	    else
	      *cp = '_';
	  }
      }
    *cp = 0;
  }
  
  if (create_variable (&default_dict, name, NUMERIC, 0))
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

	if (create_variable (&default_dict, n, NUMERIC, 0))
	  return 1;
      }
  }

  msg (SE, _("Could not create acceptable variant for variable %s."), name);
  return 0;
}

/* Make a new dictionary for all the new variable names. */
static int
build_dictionary (void)
{
  force_create_variable (&default_dict, "CASE_LBL", ALPHA, 8);

  if (!new_names_tail)
    {
      int i;
      
      if (case_count > 99999)
	{
	  msg (SE, _("Cannot create more than 99999 variable names."));
	  return 0;
	}
      
      for (i = 0; i < case_count; i++)
	{
	  char s[9];

	  sprintf (s, "VAR%03d", i);
	  force_create_variable (&default_dict, s, NUMERIC, 0);
	}
    }
  else
    {
      struct varname *v, *n;

      new_names_tail->next = NULL;
      for (v = new_names_head; v; v = n)
	{
	  n = v->next;
	  if (!make_new_var (v->name))
	    {
	      for (; v; v = n)
		{
		  n = v->next;
		  free (v);
		}
	      return 0;
	    }
	  free (v);
	}
    }
  
  return 1;
}
     

/* Each case to be transposed. */
struct flip_case
  {
    struct flip_case *next;
    double v[1];
  };

/* Sink: Cases during transposition. */
static int internal;			/* Internal vs. external flipping. */
static char *sink_old_names;		/* Old variable names. */
static unsigned long sink_cases;	/* Number of cases. */
static struct flip_case *head, *tail;	/* internal == 1: Cases. */
static FILE *sink_file;			/* internal == 0: Temporary file. */

/* Source: Cases after transposition. */
static struct flip_case *src;		/* Internal transposition records. */
static char *src_old_names;		/* Old variable names. */
static unsigned long src_cases;		/* Number of cases. */
static FILE *src_file;			/* src == NULL: Temporary file. */

/* Initialize the FLIP stream. */
static void 
flip_stream_init (void)
{
  internal = 1;
  sink_cases = 0;
  tail = NULL;
  
  {
    size_t n = nvar;
    char *p;
    int i;
    
    for (i = 0; i < nvar; i++)
      n += strlen (var[i]->name);
    p = sink_old_names = xmalloc (n);
    for (i = 0; i < nvar; i++)
      p = stpcpy (p, var[i]->name) + 1;
  }
}

/* Reads the FLIP stream and passes it to write_case(). */
static void
flip_stream_read (void)
{
  if (src || (src == NULL && src_file == NULL))
    {
      /* Internal transposition, or empty file. */
      int i, j;
      char *p = src_old_names;
      
      for (i = 0; i < nvar; i++)
	{
	  struct flip_case *iter;
	  
	  st_bare_pad_copy (temp_case->data[0].s, p, 8);
	  p = strchr (p, 0) + 1;

	  for (iter = src, j = 1; iter; iter = iter->next, j++)
	    temp_case->data[j].f = iter->v[i];

	  if (!write_case ())
	    return;
	}
    }
  else
    {
      int i;
      char *p = src_old_names;
      
      for (i = 0; i < nvar; i++)
	{
	  st_bare_pad_copy (temp_case->data[0].s, p, 8);
	  p = strchr (p, 0) + 1;

	  if (fread (&temp_case->data[1], sizeof (double), src_cases,
		     src_file) != src_cases)
	    msg (FE, _("Error reading FLIP source file: %s."),
		 strerror (errno));

	  if (!write_case ())
	    return;
	}
    }
}

/* Writes temp_case to the FLIP stream. */
static void
flip_stream_write (void)
{
  sink_cases++;

  if (newnames)
    {
      struct varname *v;
      char name[INT_DIGITS + 2];

      if (newnames->type == NUMERIC)
	sprintf (name, "V%d", (int) temp_case->data[newnames->fv].f);
      else
	{
	  int width = min (newnames->width, 8);
	  memcpy (name, temp_case->data[newnames->fv].s, width);
	  name[width] = 0;
	}

      v = xmalloc (sizeof (struct varname) + strlen (name) - 1);
      strcpy (v->name, name);
      
      if (new_names_tail == NULL)
	new_names_head = v;
      else
	new_names_tail->next = v;
      new_names_tail = v;
    }
  else
    case_count++;

  if (internal)
    {
#if 0
      flip_case *c = malloc (sizeof (flip_case)
			     + sizeof (double) * (nvar - 1));
      
      if (c != NULL)
	{
	  /* Write to internal file. */
	  int i;

	  for (i = 0; i < nvar; i++)
	    if (var[i]->type == NUMERIC)
	      c->v[i] = temp_case->data[var[i]->fv].f;
	    else
	      c->v[i] = SYSMIS;

	  if (tail == NULL)
	    head = c;
	  else
	    tail->next = c;
	  tail = c;
	  
	  return;
	}
      else
#endif
	{
	  /* Initialize external file. */
	  struct flip_case *iter, *next;

	  internal = 0;

	  sink_file = tmpfile ();
	  if (!sink_file)
	    msg (FE, _("Could not create temporary file for FLIP."));

	  if (tail)
	    tail->next = NULL;
	  for (iter = head; iter; iter = next)
	    {
	      next = iter->next;

	      if (fwrite (iter->v, sizeof (double), nvar, sink_file)
		  != (size_t) nvar)
		msg (FE, _("Error writing FLIP file: %s."),
		     strerror (errno));
	      free (iter);
	    }
	}
    }

  /* Write to external file. */
  {
    double *d = local_alloc (sizeof *d * nvar);
    int i;

    for (i = 0; i < nvar; i++)
      if (var[i]->type == NUMERIC)
	d[i] = temp_case->data[var[i]->fv].f;
      else
	d[i] = SYSMIS;
	  
    if (fwrite (d, sizeof *d, nvar, sink_file) != (size_t) nvar)
      msg (FE, _("Error writing FLIP file: %s."),
	   strerror (errno));

    local_free (d);
  }
}
      
/* Transpose the external file. */
static void
transpose_external_file (void)
{
  unsigned long n_cases;
  unsigned long cur_case;
  double *case_buf, *temp_buf;

  n_cases = 4 * 1024 * 1024 / ((nvar + 1) * sizeof *case_buf);
  if (n_cases < 2)
    n_cases = 2;
  for (;;)
    {
      assert (n_cases >= 2 /* 1 */);
      case_buf = ((n_cases <= 2 ? xmalloc : (void *(*)(size_t)) malloc)
		  ((nvar + 1) * sizeof *case_buf * n_cases));
      if (case_buf)
	break;

      n_cases /= 2;
      if (n_cases < 2)
	n_cases = 2;
    }

  /* A temporary buffer that holds n_cases elements. */
  temp_buf = &case_buf[nvar * n_cases];

  src_file = tmpfile ();
  if (!src_file)
    msg (FE, _("Error creating FLIP source file."));
  
  if (fseek (sink_file, 0, SEEK_SET) != 0)
    msg (FE, _("Error rewinding FLIP file: %s."), strerror (errno));

  for (cur_case = 0; cur_case < sink_cases; )
    {
      unsigned long read_cases = min (sink_cases - cur_case, n_cases);
      int i;

      if (read_cases != fread (case_buf, sizeof *case_buf * nvar,
			       read_cases, sink_file))
	msg (FE, _("Error reading FLIP file: %s."), strerror (errno));

      for (i = 0; i < nvar; i++)
	{
	  unsigned long j;
	  
	  for (j = 0; j < read_cases; j++)
	    temp_buf[j] = case_buf[i + j * nvar];

	  if (fseek (src_file,
		     sizeof *case_buf * (cur_case + i * sink_cases),
		     SEEK_SET) != 0)
	    msg (FE, _("Error seeking FLIP source file: %s."),
		       strerror (errno));

	  if (fwrite (temp_buf, sizeof *case_buf, read_cases, src_file)
	      != read_cases)
	    msg (FE, _("Error writing FLIP source file: %s."),
		 strerror (errno));
	}

      cur_case += read_cases;
    }

  if (fseek (src_file, 0, SEEK_SET) != 0)
    msg (FE, _("Error rewind FLIP source file: %s."), strerror (errno));

  fclose (sink_file);

  free (case_buf);
}

/* Change the FLIP stream from sink to source mode. */
static void
flip_stream_mode (void)
{
  src_cases = sink_cases;
  src_old_names = sink_old_names;
  sink_old_names = NULL;
  
  if (internal)
    {
      if (tail)
	{
	  tail->next = NULL;
	  src = head;
	}
      else
	{
	  src = NULL;
	  src_file = NULL;
	}
    }
  else
    {
      src = NULL;
      transpose_external_file ();
    }
}

/* Destroy source's internal data. */
static void
flip_stream_destroy_source (void)
{
  free (src_old_names);
  if (internal)
    {
      struct flip_case *iter, *next;

      for (iter = src; iter; iter = next)
	{
	  next = iter->next;
	  free (iter);
	}
    }
  else
    fclose (src_file);
}

/* Destroy sink's internal data. */
static void
flip_stream_destroy_sink (void)
{
  struct flip_case *iter, *next;
  
  free (sink_old_names);
  if (tail == NULL)
    return;

  tail->next = NULL;
  for (iter = head; iter; iter = next)
    {
      next = iter->next;
      free (iter);
    }
}

struct case_stream flip_stream = 
  {
    flip_stream_init,
    flip_stream_read,
    flip_stream_write,
    flip_stream_mode,
    flip_stream_destroy_source,
    flip_stream_destroy_sink,
    "FLIP",
  };
