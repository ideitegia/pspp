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

#include <data/case.h>
#include <data/caseinit.h>
#include <data/casereader-provider.h>
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
    enum trns_result restart;

    casenumber case_nr;             /* Incremented by END CASE transformation. */

    struct caseinit *init;
    size_t value_cnt;
  };

static void destroy_input_program (struct input_program_pgm *);
static trns_proc_func end_case_trns_proc;
static trns_proc_func reread_trns_proc;
static trns_proc_func end_file_trns_proc;
static trns_free_func reread_trns_free;

static const struct casereader_class input_program_casereader_class;

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
  bool saw_END_CASE = false;

  proc_discard_active_file (ds);
  if (lex_token (lexer) != '.')
    return lex_end_of_command (lexer);

  inp = xmalloc (sizeof *inp);
  inp->trns_chain = NULL;
  inp->init = NULL;

  inside_input_program = true;
  for (;;)
    {
      enum cmd_result result = cmd_parse_in_state (lexer, ds, CMD_STATE_INPUT_PROGRAM);
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
          proc_discard_active_file (ds);
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
      proc_discard_active_file (ds);
      destroy_input_program (inp);
      return CMD_FAILURE;
    }

  inp->trns_chain = proc_capture_transformations (ds);
  trns_chain_finalize (inp->trns_chain);

  inp->restart = TRNS_CONTINUE;

  /* Figure out how to initialize each input case. */
  inp->init = caseinit_create ();
  caseinit_mark_for_init (inp->init, dataset_dict (ds));
  inp->value_cnt = dict_get_next_value_idx (dataset_dict (ds));

  proc_set_active_file_data (
    ds, casereader_create_sequential (NULL, inp->value_cnt, CASENUMBER_MAX,
                                      &input_program_casereader_class, inp));

  return CMD_SUCCESS;
}

int
cmd_end_input_program (struct lexer *lexer UNUSED, struct dataset *ds UNUSED)
{
  assert (in_input_program ());
  return CMD_END_INPUT_PROGRAM;
}

/* Returns true if STATE is valid given the transformations that
   are allowed within INPUT PROGRAM. */
static bool
is_valid_state (enum trns_result state)
{
  return (state == TRNS_CONTINUE
          || state == TRNS_ERROR
          || state == TRNS_END_FILE
          || state >= 0);
}

/* Reads one case into C.
   Returns true if successful, false at end of file or if an
   I/O error occurred. */
static bool
input_program_casereader_read (struct casereader *reader UNUSED, void *inp_,
                               struct ccase *c)
{
  struct input_program_pgm *inp = inp_;

  case_create (c, inp->value_cnt);

  do
    {
      assert (is_valid_state (inp->restart));
      if (inp->restart == TRNS_ERROR || inp->restart == TRNS_END_FILE)
        {
          case_destroy (c);
          return false;
        }

      caseinit_init_vars (inp->init, c);
      inp->restart = trns_chain_execute (inp->trns_chain, inp->restart,
                                         c, inp->case_nr);
      assert (is_valid_state (inp->restart));
      caseinit_update_left_vars (inp->init, c);
    }
  while (inp->restart < 0);

  return true;
}

static void
destroy_input_program (struct input_program_pgm *pgm)
{
  if (pgm != NULL)
    {
      trns_chain_destroy (pgm->trns_chain);
      caseinit_destroy (pgm->init);
      free (pgm);
    }
}

/* Destroys the casereader. */
static void
input_program_casereader_destroy (struct casereader *reader UNUSED, void *inp_)
{
  struct input_program_pgm *inp = inp_;
  if (inp->restart == TRNS_ERROR)
    casereader_force_error (reader);
  destroy_input_program (inp);
}

static const struct casereader_class input_program_casereader_class =
  {
    input_program_casereader_read,
    input_program_casereader_destroy,
    NULL,
    NULL,
  };

int
cmd_end_case (struct lexer *lexer, struct dataset *ds UNUSED)
{
  assert (in_input_program ());
  if (lex_token (lexer) == '.')
    return CMD_END_CASE;
  return lex_end_of_command (lexer);
}

/* Outputs the current case */
int
end_case_trns_proc (void *inp_, struct ccase *c UNUSED,
                    casenumber case_nr UNUSED)
{
  struct input_program_pgm *inp = inp_;
  inp->case_nr++;
  return TRNS_END_CASE;
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
