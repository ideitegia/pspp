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
#include <float.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "dfm.h"
#include "error.h"
#include "expr.h"
#include "file-handle.h"
#include "inpt-pgm.h"
#include "lexer.h"
#include "misc.h"
#include "str.h"
#include "var.h"
#include "vfm.h"

#undef DEBUGGING
/*#define DEBUGGING 1*/
#include "debug-print.h"

/* A bit-vector of two-bit entries.  The array tells INPUT PROGRAM how
   to initialize each `value'.  Modified by envector(), devector(),
   which are called by create_variable(), also by LEAVE, COMPUTE(!).  */
unsigned char *inp_init;

/* Number of bytes allocated for inp_init. */
size_t inp_init_size;

/* Number of `values' created inside INPUT PROGRAM. */
static int inp_nval;

static int end_case_trns_proc (struct trns_header *, struct ccase *);
static int end_file_trns_proc (struct trns_header * t, struct ccase * c);
static int reread_trns_proc (struct trns_header *, struct ccase *);
static void reread_trns_free (struct trns_header *);

int
cmd_input_program (void)
{
  lex_match_id ("INPUT");
  lex_match_id ("PROGRAM");
  discard_variables ();

  vfm_source = &input_program_source;

  inp_init = NULL;
  inp_init_size = 0;

  return lex_end_of_command ();
}

int
cmd_end_input_program (void)
{
  lex_match_id ("END");
  lex_match_id ("INPUT");
  lex_match_id ("PROGRAM");

  if (vfm_source != &input_program_source)
    {
      msg (SE, _("No matching INPUT PROGRAM command."));
      return CMD_FAILURE;
    }
  
  if (default_dict.nval == 0)
    msg (SW, _("No data-input or transformation commands specified "
	 "between INPUT PROGRAM and END INPUT PROGRAM."));

  /* Mark the boundary between INPUT PROGRAM and more-mundane
     transformations. */
  f_trns = n_trns;

  /* Mark the boundary between input program `values' and
     later-created `values'. */
  inp_nval = default_dict.nval;

  return lex_end_of_command ();
}

/* Initializes temp_case.  Called before the first case is read. */
static void
init_case (void)
{
  union value *val = temp_case->data;
  unsigned char *cp = inp_init;
  unsigned char c;
  int i, j;

  /* This code is 2-3X the complexity it might be, but I felt like
     it.  It initializes temp_case union values to 0, or SYSMIS, or
     blanks, as appropriate. */
  for (i = 0; i < inp_nval / 4; i++)
    {
      c = *cp++;
      for (j = 0; j < 4; j++)
	{
	  switch (c & INP_MASK)
	    {
	    case INP_NUMERIC | INP_RIGHT:
	      val++->f = SYSMIS;
	      break;
	    case INP_NUMERIC | INP_LEFT:
	      val++->f = 0.0;
	      break;
	    case INP_STRING | INP_RIGHT:
	    case INP_STRING | INP_LEFT:
	      memset (val++->s, ' ', MAX_SHORT_STRING);
	      break;
	    }
	  c >>= 2;
	}
    }
  if (inp_nval % 4)
    {
      c = *cp;
      for (j = 0; j < inp_nval % 4; j++)
	{
	  switch (c & INP_MASK)
	    {
	    case INP_NUMERIC | INP_RIGHT:
	      val++->f = SYSMIS;
	      break;
	    case INP_NUMERIC | INP_LEFT:
	      val++->f = 0.0;
	      break;
	    case INP_STRING | INP_RIGHT:
	    case INP_STRING | INP_LEFT:
	      memset (val++->s, ' ', MAX_SHORT_STRING);
	      break;
	    }
	  c >>= 2;
	}
    }
}

/* Clears temp_case.  Called between reading successive records. */
static void
clear_case (void)
{
  union value *val = temp_case->data;
  unsigned char *cp = inp_init;
  unsigned char c;
  int i, j;

  /* This code is 2-3X the complexity it might be, but I felt like
     it.  It initializes temp_case values to SYSMIS, or
     blanks, or does nothing, as appropriate. */
  for (i = 0; i < inp_nval / 4; i++)
    {
      c = *cp++;
      for (j = 0; j < 4; j++)
	{
	  if (!(c & INP_LEFT))
	    {
	      if (c & INP_STRING)
		memset (val->s, ' ', MAX_SHORT_STRING);
	      else
		val->f = SYSMIS;
	    }
	  val++;
	  c >>= 2;
	}
    }
  
  if (inp_nval % 4)
    {
      c = *cp;
      for (j = 0; j < inp_nval % 4; j++)
	{
	  if (!(c & INP_LEFT))
	    {
	      if (c & INP_STRING)
		memset (val->s, ' ', MAX_SHORT_STRING);
	      else
		val->f = SYSMIS;
	    }
	  val++;
	  c >>= 2;
	}
    }
}

/* Executes each transformation in turn on a `blank' case.  When a
   transformation fails, returning -2, then that's the end of the
   file.  -1 means go on to the next transformation.  Otherwise the
   return value is the index of the transformation to go to next. */
void
input_program_source_read (void)
{
  int i;

  /* Nonzero if there were any END CASE commands in the set of
     transformations. */
  int end_case = 0;

  /* We don't automatically write out cases if the user took over
     that prerogative.  */
  for (i = 0; i < f_trns; i++)
    if (t_trns[i]->proc == end_case_trns_proc)
      end_case = 1;

  init_case ();
  for (;;)
    {
      /* Index of current transformation. */
      int i;

      /* Return value of last-called transformation. */
      int code;

      debug_printf (("input-program: "));

      /* Perform transformations on `blank' case. */
      for (i = 0; i < f_trns;)
	{
#if DEBUGGING
	  printf ("/%d", i);
	  if (t_trns[i]->proc == end_case_trns_proc)
	    printf ("\n");
#endif
	  code = t_trns[i]->proc (t_trns[i], temp_case);
	  switch (code)
	    {
	    case -1:
	      i++;
	      break;
	    case -2:
	      return;
	    case -3:
	      goto next_case;
	    default:
	      i = code;
	      break;
	    }
	}

#if DEBUGGING
      if (!end_case)
	printf ("\n");
#endif

      /* Write the case if appropriate. */
      if (!end_case)
	if (!write_case ())
	  return;

      /* Blank out the case for the next iteration. */
    next_case:
      clear_case ();
    }
}

static void
input_program_source_destroy_source (void)
{
  cancel_transformations ();
  free (inp_init);
  inp_init = NULL;
}

struct case_stream input_program_source =
  {
    NULL,
    input_program_source_read,
    NULL,
    NULL,
    input_program_source_destroy_source,
    NULL,
    "INPUT PROGRAM",
  };

int
cmd_end_case (void)
{
  struct trns_header *t;

  lex_match_id ("END");
  lex_match_id ("CASE");

  if (vfm_source != &input_program_source)
    {
      msg (SE, _("This command may only be executed between INPUT PROGRAM "
		 "and END INPUT PROGRAM."));
      return CMD_FAILURE;
    }

  t = xmalloc (sizeof *t);
  t->proc = end_case_trns_proc;
  t->free = NULL;
  add_transformation ((struct trns_header *) t);

  return lex_end_of_command ();
}

int
end_case_trns_proc (struct trns_header *t unused, struct ccase * c unused)
{
#if DEBUGGING
  printf ("END CASE\n");
#endif
  if (!write_case ())
    return -2;
  clear_case ();
  return -1;
}

/* REREAD transformation. */
struct reread_trns
  {
    struct trns_header h;

    struct file_handle *handle;	/* File to move file pointer back on. */
    struct expression *column;	/* Column to reset file pointer to. */
  };

/* Parses REREAD command. */
int
cmd_reread (void)
{
  /* File to be re-read. */
  struct file_handle *h;
  
  /* Expression for column to set file pointer to. */
  struct expression *e;

  /* Created transformation. */
  struct reread_trns *t;

  lex_match_id ("REREAD");

  h = default_handle;
  e = NULL;
  while (token != '.')
    {
      if (lex_match_id ("COLUMN"))
	{
	  lex_match ('=');
	  
	  if (e)
	    {
	      msg (SE, _("COLUMN subcommand multiply specified."));
	      expr_free (e);
	      return CMD_FAILURE;
	    }
	  
	  e = expr_parse (PXP_NUMERIC);
	  if (!e)
	    return CMD_FAILURE;
	}
      else if (lex_match_id ("FILE"))
	{
	  lex_match ('=');
	  if (token != T_ID)
	    {
	      lex_error (_("expecting file handle name"));
	      expr_free (e);
	      return CMD_FAILURE;
	    }
	  h = fh_get_handle_by_name (tokid);
	  if (!h)
	    {
	      expr_free (e);
	      return CMD_FAILURE;
	    }
	  lex_get ();
	}
      else
	{
	  lex_error (NULL);
	  expr_free (e);
	}
    }

  t = xmalloc (sizeof *t);
  t->h.proc = reread_trns_proc;
  t->h.free = reread_trns_free;
  t->handle = h;
  t->column = e;
  add_transformation ((struct trns_header *) t);

  return CMD_SUCCESS;
}

static int
reread_trns_proc (struct trns_header * pt, struct ccase * c)
{
  struct reread_trns *t = (struct reread_trns *) pt;

  if (t->column == NULL)
    dfm_bkwd_record (t->handle, 1);
  else
    {
      union value column;

      expr_evaluate (t->column, c, &column);
      if (!finite (column.f) || column.f < 1)
	{
	  msg (SE, _("REREAD: Column numbers must be positive finite "
	       "numbers.  Column set to 1."));
	  dfm_bkwd_record (t->handle, 1);
	}
      else
	dfm_bkwd_record (t->handle, column.f);
    }
  return -1;
}

static void
reread_trns_free (struct trns_header * t)
{
  expr_free (((struct reread_trns *) t)->column);
}

/* Parses END FILE command. */
int
cmd_end_file (void)
{
  struct trns_header *t;

  lex_match_id ("END");
  lex_match_id ("FILE");

  if (vfm_source != &input_program_source)
    {
      msg (SE, _("This command may only be executed between INPUT PROGRAM "
		 "and END INPUT PROGRAM."));
      return CMD_FAILURE;
    }

  t = xmalloc (sizeof *t);
  t->proc = end_file_trns_proc;
  t->free = NULL;
  add_transformation ((struct trns_header *) t);

  return lex_end_of_command ();
}

static int
end_file_trns_proc (struct trns_header * t unused, struct ccase * c unused)
{
#if DEBUGGING
  printf ("END FILE\n");
#endif
  return -2;
}
