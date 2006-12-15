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

#include <config.h>

#include <language/data-io/inpt-pgm.h>

#include <float.h>
#include <stdlib.h>

#include <data/case-source.h>
#include <data/case.h>
#include <data/case-source.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/transformations.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/data-io/data-reader.h>
#include <language/data-io/file-handle.h>
#include <language/expressions/public.h>
#include <language/lexer/lexer.h>
#include <libpspp/alloc.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Private result codes for use within INPUT PROGRAM. */
enum cmd_result_extensions 
  {
    CMD_END_INPUT_PROGRAM = CMD_PRIVATE_FIRST,
    CMD_END_CASE
  };

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
    struct trns_chain *trns_chain;

    size_t case_nr;             /* Incremented by END CASE transformation. */
    write_case_func *write_case;/* Called by END CASE. */
    write_case_data wc_data;    /* Aux data used by END CASE. */

    enum value_init_type *init; /* How to initialize each `union value'. */
    size_t init_cnt;            /* Number of elements in inp_init. */
    size_t case_size;           /* Size of case in bytes. */
  };

static void destroy_input_program (struct input_program_pgm *);
static trns_proc_func end_case_trns_proc;
static trns_proc_func reread_trns_proc;
static trns_proc_func end_file_trns_proc;
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

/* Emits an END CASE transformation for INP. */
static void
emit_END_CASE (struct dataset *ds, struct input_program_pgm *inp) 
{
  add_transformation (ds, end_case_trns_proc, NULL, inp);
}

int
cmd_input_program (struct lexer *lexer, struct dataset *ds)
{
  struct input_program_pgm *inp;
  size_t i;
  bool saw_END_CASE = false;

  discard_variables (ds);
  if (lex_token (lexer) != '.')
    return lex_end_of_command (lexer);

  inp = xmalloc (sizeof *inp);
  inp->trns_chain = NULL;
  inp->init = NULL;
  
  inside_input_program = true;
  for (;;) 
    {
      enum cmd_result result = cmd_parse (lexer, ds, CMD_STATE_INPUT_PROGRAM);
      if (result == CMD_END_INPUT_PROGRAM)
        break;
      else if (result == CMD_END_CASE) 
        {
          emit_END_CASE (ds, inp);
          saw_END_CASE = true; 
        }
      else if (cmd_result_is_failure (result) && result != CMD_FAILURE)
        {
          if (result == CMD_EOF)
            msg (SE, _("Unexpected end-of-file within INPUT PROGRAM."));
          inside_input_program = false;
          discard_variables (ds);
          destroy_input_program (inp);
          return result;
        }
    }
  if (!saw_END_CASE)
    emit_END_CASE (ds, inp);
  inside_input_program = false;

  if (dict_get_next_value_idx (dataset_dict (ds)) == 0) 
    {
      msg (SE, _("Input program did not create any variables."));
      discard_variables (ds);
      destroy_input_program (inp);
      return CMD_FAILURE;
    }
  
  inp->trns_chain = proc_capture_transformations (ds);
  trns_chain_finalize (inp->trns_chain);

  /* Figure out how to initialize each input case. */
  inp->init_cnt = dict_get_next_value_idx (dataset_dict (ds));
  inp->init = xnmalloc (inp->init_cnt, sizeof *inp->init);
  for (i = 0; i < inp->init_cnt; i++)
    inp->init[i] = -1;
  for (i = 0; i < dict_get_var_cnt (dataset_dict (ds)); i++)
    {
      struct variable *var = dict_get_var (dataset_dict (ds), i);
      size_t value_cnt = var_get_value_cnt (var);
      enum value_init_type value_init;
      size_t j;
      
      value_init = var_is_numeric (var) ? INP_NUMERIC : INP_STRING;
      value_init |= var_get_leave (var) ? INP_INIT_ONCE : INP_REINIT;

      for (j = 0; j < value_cnt; j++)
        inp->init[j + var_get_case_index (var)] = value_init;
    }
  for (i = 0; i < inp->init_cnt; i++)
    assert (inp->init[i] != -1);
  inp->case_size = dict_get_case_size (dataset_dict (ds));

  proc_set_source (ds, 
		  create_case_source (&input_program_source_class, inp));

  return CMD_SUCCESS;
}

int
cmd_end_input_program (struct lexer *lexer UNUSED, struct dataset *ds UNUSED)
{
  assert (in_input_program ());
  return CMD_END_INPUT_PROGRAM; 
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
        case_data_rw_idx (c, i)->f = 0.0;
        break;
      case INP_NUMERIC | INP_REINIT:
        case_data_rw_idx (c, i)->f = SYSMIS;
        break;
      case INP_STRING | INP_INIT_ONCE:
      case INP_STRING | INP_REINIT:
        memset (case_data_rw_idx (c, i)->s, ' ',
                sizeof case_data_rw_idx (c, i)->s);
        break;
      default:
        NOT_REACHED ();
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
        case_data_rw_idx (c, i)->f = SYSMIS;
        break;
      case INP_STRING | INP_INIT_ONCE:
        break;
      case INP_STRING | INP_REINIT:
        memset (case_data_rw_idx (c, i)->s, ' ',
                sizeof case_data_rw_idx (c, i)->s);
        break;
      default:
        NOT_REACHED ();
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

  inp->case_nr = 1;
  inp->write_case = write_case;
  inp->wc_data = wc_data;
  for (init_case (inp, c); ; clear_case (inp, c))
    {
      enum trns_result result = trns_chain_execute (inp->trns_chain, c,
                                                    &inp->case_nr);
      if (result == TRNS_ERROR)
        return false;
      else if (result == TRNS_END_FILE)
        return true;
    }
}

static void
destroy_input_program (struct input_program_pgm *pgm) 
{
  if (pgm != NULL) 
    {
      trns_chain_destroy (pgm->trns_chain);
      free (pgm->init);
      free (pgm);
    }
}

/* Destroys an INPUT PROGRAM source. */
static void
input_program_source_destroy (struct case_source *source)
{
  struct input_program_pgm *inp = source->aux;

  destroy_input_program (inp);
}

static const struct case_source_class input_program_source_class =
  {
    "INPUT PROGRAM",
    NULL,
    input_program_source_read,
    input_program_source_destroy,
  };

int
cmd_end_case (struct lexer *lexer, struct dataset *ds UNUSED)
{
  assert (in_input_program ());
  if (lex_token (lexer) == '.')
    return CMD_END_CASE;
  return lex_end_of_command (lexer);
}

/* Sends the current case as the source's output. */
int
end_case_trns_proc (void *inp_, struct ccase *c, casenumber case_nr UNUSED)
{
  struct input_program_pgm *inp = inp_;

  if (!inp->write_case (inp->wc_data))
    return TRNS_ERROR;

  inp->case_nr++;
  clear_case (inp, c);
  return TRNS_CONTINUE;
}

/* REREAD transformation. */
struct reread_trns
  {
    struct dfm_reader *reader;	/* File to move file pointer back on. */
    struct expression *column;	/* Column to reset file pointer to. */
  };

/* Parses REREAD command. */
int
cmd_reread (struct lexer *lexer, struct dataset *ds)
{
  struct file_handle *fh;       /* File to be re-read. */
  struct expression *e;         /* Expression for column to set. */
  struct reread_trns *t;        /* Created transformation. */

  fh = fh_get_default_handle ();
  e = NULL;
  while (lex_token (lexer) != '.')
    {
      if (lex_match_id (lexer, "COLUMN"))
	{
	  lex_match (lexer, '=');
	  
	  if (e)
	    {
	      msg (SE, _("COLUMN subcommand multiply specified."));
	      expr_free (e);
	      return CMD_CASCADING_FAILURE;
	    }
	  
	  e = expr_parse (lexer, ds, EXPR_NUMBER);
	  if (!e)
	    return CMD_CASCADING_FAILURE;
	}
      else if (lex_match_id (lexer, "FILE"))
	{
	  lex_match (lexer, '=');
          fh = fh_parse (lexer, FH_REF_FILE | FH_REF_INLINE);
	  if (fh == NULL)
	    {
	      expr_free (e);
	      return CMD_CASCADING_FAILURE;
	    }
	}
      else
	{
	  lex_error (lexer, NULL);
	  expr_free (e);
          return CMD_CASCADING_FAILURE;
	}
    }

  t = xmalloc (sizeof *t);
  t->reader = dfm_open_reader (fh, lexer);
  t->column = e;
  add_transformation (ds, reread_trns_proc, reread_trns_free, t);

  return CMD_SUCCESS;
}

/* Executes a REREAD transformation. */
static int
reread_trns_proc (void *t_, struct ccase *c, casenumber case_num)
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
cmd_end_file (struct lexer *lexer, struct dataset *ds)
{
  assert (in_input_program ());

  add_transformation (ds, end_file_trns_proc, NULL, NULL);

  return lex_end_of_command (lexer);
}

/* Executes an END FILE transformation. */
static int
end_file_trns_proc (void *trns_ UNUSED, struct ccase *c UNUSED,
                    casenumber case_num UNUSED)
{
  return TRNS_END_FILE;
}
