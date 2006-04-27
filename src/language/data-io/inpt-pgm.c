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

#include <config.h>

#include <language/data-io/inpt-pgm.h>

#include <float.h>
#include <stdlib.h>

#include <data/case.h>
#include <data/dictionary.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/data-io/data-list.h>
#include <language/data-io/data-reader.h>
#include <language/data-io/file-handle.h>
#include <language/expressions/public.h>
#include <language/lexer/lexer.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>
#include <procedure.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Indicates how a `union value' should be initialized. */
enum value_init_type
  {
    INP_NUMERIC = 01,		/* Numeric. */
    INP_STRING = 0,		/* String. */
    
    INP_INIT_ONCE = 02,		/* Initialize only once. */
    INP_REINIT = 0,		/* Reinitialize for each iteration. */
  };

struct input_program_pgm 
  {
    enum value_init_type *init; /* How to initialize each `union value'. */
    size_t init_cnt;            /* Number of elements in inp_init. */
    size_t case_size;           /* Size of case in bytes. */
  };

static trns_proc_func end_case_trns_proc, reread_trns_proc, end_file_trns_proc;
static trns_free_func reread_trns_free;
static const struct case_source_class input_program_source_class;
static bool inside_input_program;

/* Returns true if we're parsing the inside of a INPUT
   PROGRAM...END INPUT PROGRAM construct, false otherwise. */
bool
in_input_program (void) 
{
  return inside_input_program;
}

int
cmd_input_program (void)
{
  struct input_program_pgm *inp;
  size_t i;

  discard_variables ();
  if (token != '.')
    return lex_end_of_command ();

  inside_input_program = true;
  for (;;) 
    {
      enum cmd_result result;
      lex_get ();
      result = cmd_parse (CMD_STATE_INPUT_PROGRAM);
      if (result == CMD_END_SUBLOOP)
        break;
      if (result == CMD_EOF || result == CMD_QUIT || result == CMD_CASCADING_FAILURE)
        {
          if (result == CMD_EOF)
            msg (SE, _("Unexpected end-of-file within INPUT PROGRAM."));
          discard_variables ();
          inside_input_program = false;
          return result;
        }
    }
  inside_input_program = false;

  if (dict_get_next_value_idx (default_dict) == 0)
    msg (SW, _("No data-input or transformation commands specified "
               "between INPUT PROGRAM and END INPUT PROGRAM."));

  /* Mark the boundary between INPUT PROGRAM transformations and
     ordinary transformations. */
  f_trns = n_trns;

  /* Figure out how to initialize each input case. */
  inp = xmalloc (sizeof *inp);
  inp->init_cnt = dict_get_next_value_idx (default_dict);
  inp->init = xnmalloc (inp->init_cnt, sizeof *inp->init);
  for (i = 0; i < inp->init_cnt; i++)
    inp->init[i] = -1;
  for (i = 0; i < dict_get_var_cnt (default_dict); i++)
    {
      struct variable *var = dict_get_var (default_dict, i);
      enum value_init_type value_init;
      size_t j;
      
      value_init = var->type == NUMERIC ? INP_NUMERIC : INP_STRING;
      value_init |= var->leave ? INP_INIT_ONCE : INP_REINIT;

      for (j = 0; j < var->nv; j++)
        inp->init[j + var->fv] = value_init;
    }
  for (i = 0; i < inp->init_cnt; i++)
    assert (inp->init[i] != -1);
  inp->case_size = dict_get_case_size (default_dict);

  /* Create vfm_source. */
  vfm_source = create_case_source (&input_program_source_class, inp);

  return CMD_SUCCESS;
}

int
cmd_end_input_program (void)
{
  assert (in_input_program ());
  return CMD_END_SUBLOOP; 
}

/* Initializes case C.  Called before the first case is read. */
static void
init_case (const struct input_program_pgm *inp, struct ccase *c)
{
  size_t i;

  for (i = 0; i < inp->init_cnt; i++)
    switch (inp->init[i]) 
      {
      case INP_NUMERIC | INP_INIT_ONCE:
        case_data_rw (c, i)->f = 0.0;
        break;
      case INP_NUMERIC | INP_REINIT:
        case_data_rw (c, i)->f = SYSMIS;
        break;
      case INP_STRING | INP_INIT_ONCE:
      case INP_STRING | INP_REINIT:
        memset (case_data_rw (c, i)->s, ' ', sizeof case_data_rw (c, i)->s);
        break;
      default:
        assert (0);
      }
}

/* Clears case C.  Called between reading successive records. */
static void
clear_case (const struct input_program_pgm *inp, struct ccase *c)
{
  size_t i;

  for (i = 0; i < inp->init_cnt; i++)
    switch (inp->init[i]) 
      {
      case INP_NUMERIC | INP_INIT_ONCE:
        break;
      case INP_NUMERIC | INP_REINIT:
        case_data_rw (c, i)->f = SYSMIS;
        break;
      case INP_STRING | INP_INIT_ONCE:
        break;
      case INP_STRING | INP_REINIT:
        memset (case_data_rw (c, i)->s, ' ', sizeof case_data_rw (c, i)->s);
        break;
      default:
        assert (0);
      }
}

/* Executes each transformation in turn on a `blank' case.
   Returns true if successful, false if an I/O error occurred. */
static bool
input_program_source_read (struct case_source *source,
                           struct ccase *c,
                           write_case_func *write_case,
                           write_case_data wc_data)
{
  struct input_program_pgm *inp = source->aux;
  size_t i;

  /* Nonzero if there were any END CASE commands in the set of
     transformations.  If so, we don't automatically write out
     cases. */
  int end_case = 0;

  /* FIXME?  This is the number of cases sent out of the input
     program, not the number of cases written to the procedure.
     The difference should only show up in $CASENUM in COMPUTE.
     We should check behavior against SPSS. */
  int cases_written = 0;

  assert (inp != NULL);

  /* Figure end_case. */
  for (i = 0; i < f_trns; i++)
    if (t_trns[i].proc == end_case_trns_proc)
      end_case = 1;

  /* FIXME: This is an ugly kluge. */
  for (i = 0; i < f_trns; i++)
    if (t_trns[i].proc == repeating_data_trns_proc)
      repeating_data_set_write_case (t_trns[i].private, write_case, wc_data);

  init_case (inp, c);
  for (;;)
    {
      /* Perform transformations on `blank' case. */
      for (i = 0; i < f_trns; )
	{
          int code;

          if (t_trns[i].proc == end_case_trns_proc) 
            {
              cases_written++;
              if (!write_case (wc_data))
                return false;
              clear_case (inp, c);
              i++;
              continue;
            }

	  code = t_trns[i].proc (t_trns[i].private, c, cases_written + 1);
	  switch (code)
	    {
	    case TRNS_CONTINUE:
	      i++;
	      break;

            case TRNS_DROP_CASE:
              abort ();

            case TRNS_ERROR:
              return false;

	    case TRNS_NEXT_CASE:
	      goto next_case;

	    case TRNS_END_FILE:
              return true;

	    default:
	      i = code;
	      break;
	    }
	}

      /* Write the case if appropriate. */
      if (!end_case) 
        {
          cases_written++;
          if (!write_case (wc_data))
            return false;
        }

      /* Blank out the case for the next iteration. */
    next_case:
      clear_case (inp, c);
    }
}

/* Destroys an INPUT PROGRAM source. */
static void
input_program_source_destroy (struct case_source *source)
{
  struct input_program_pgm *inp = source->aux;

  cancel_transformations ();

  if (inp != NULL) 
    {
      free (inp->init);
      free (inp);
    }
}

static const struct case_source_class input_program_source_class =
  {
    "INPUT PROGRAM",
    NULL,
    input_program_source_read,
    input_program_source_destroy,
  };

int
cmd_end_case (void)
{
  assert (in_input_program ());
  add_transformation (end_case_trns_proc, NULL, NULL);

  return lex_end_of_command ();
}

/* Should never be called, because this is handled in
   input_program_source_read(). */
int
end_case_trns_proc (void *trns_ UNUSED, struct ccase *c UNUSED,
                    int case_num UNUSED)
{
  abort ();
}

/* REREAD transformation. */
struct reread_trns
  {
    struct dfm_reader *reader;	/* File to move file pointer back on. */
    struct expression *column;	/* Column to reset file pointer to. */
  };

/* Parses REREAD command. */
int
cmd_reread (void)
{
  struct file_handle *fh;       /* File to be re-read. */
  struct expression *e;         /* Expression for column to set. */
  struct reread_trns *t;        /* Created transformation. */

  fh = fh_get_default_handle ();
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
	      return CMD_CASCADING_FAILURE;
	    }
	  
	  e = expr_parse (default_dict, EXPR_NUMBER);
	  if (!e)
	    return CMD_CASCADING_FAILURE;
	}
      else if (lex_match_id ("FILE"))
	{
	  lex_match ('=');
          fh = fh_parse (FH_REF_FILE | FH_REF_INLINE);
	  if (fh == NULL)
	    {
	      expr_free (e);
	      return CMD_CASCADING_FAILURE;
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
  t->reader = dfm_open_reader (fh);
  t->column = e;
  add_transformation (reread_trns_proc, reread_trns_free, t);

  return CMD_SUCCESS;
}

/* Executes a REREAD transformation. */
static int
reread_trns_proc (void *t_, struct ccase *c, int case_num)
{
  struct reread_trns *t = t_;

  if (t->column == NULL)
    dfm_reread_record (t->reader, 1);
  else
    {
      double column = expr_evaluate_num (t->column, c, case_num);
      if (!finite (column) || column < 1)
	{
	  msg (SE, _("REREAD: Column numbers must be positive finite "
	       "numbers.  Column set to 1."));
	  dfm_reread_record (t->reader, 1);
	}
      else
	dfm_reread_record (t->reader, column);
    }
  return TRNS_CONTINUE;
}

/* Frees a REREAD transformation.
   Returns true if successful, false if an I/O error occurred. */
static bool
reread_trns_free (void *t_)
{
  struct reread_trns *t = t_;
  expr_free (t->column);
  dfm_close_reader (t->reader);
  return true;
}

/* Parses END FILE command. */
int
cmd_end_file (void)
{
  assert (in_input_program ());

  add_transformation (end_file_trns_proc, NULL, NULL);

  return lex_end_of_command ();
}

/* Executes an END FILE transformation. */
static int
end_file_trns_proc (void *trns_ UNUSED, struct ccase *c UNUSED,
                    int case_num UNUSED)
{
  return TRNS_END_FILE;
}
