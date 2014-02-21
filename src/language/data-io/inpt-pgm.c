/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include <float.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/caseinit.h"
#include "data/casereader-provider.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/session.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/data-io/data-reader.h"
#include "language/data-io/file-handle.h"
#include "language/data-io/inpt-pgm.h"
#include "language/expressions/public.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Indicates how a `union value' should be initialized. */
struct input_program_pgm
  {
    struct session *session;
    struct dataset *ds;
    struct trns_chain *trns_chain;
    enum trns_result restart;

    casenumber case_nr;             /* Incremented by END CASE transformation. */

    struct caseinit *init;
    struct caseproto *proto;
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
  bool saw_END_FILE = false;
  bool saw_DATA_LIST = false;

  if (!lex_match (lexer, T_ENDCMD))
    return lex_end_of_command (lexer);

  inp = xmalloc (sizeof *inp);
  inp->session = session_create (dataset_session (ds));
  inp->ds = dataset_create (inp->session, "INPUT PROGRAM");
  inp->trns_chain = NULL;
  inp->init = NULL;
  inp->proto = NULL;

  inside_input_program = true;
  while (!lex_match_phrase (lexer, "END INPUT PROGRAM"))
    {
      enum cmd_result result;

      result = cmd_parse_in_state (lexer, inp->ds, CMD_STATE_INPUT_PROGRAM);
      switch (result)
        {
        case CMD_DATA_LIST:
          saw_DATA_LIST = true;
          break;

        case CMD_END_CASE:
          emit_END_CASE (inp->ds, inp);
          saw_END_CASE = true;
          break;

        case CMD_END_FILE:
          saw_END_FILE = true;
          break;

        case CMD_FAILURE:
          break;

        default:
          if (cmd_result_is_failure (result)
              && lex_get_error_mode (lexer) != LEX_ERROR_TERMINAL)
            {
              if (result == CMD_EOF)
                msg (SE, _("Unexpected end-of-file within %s."), "INPUT PROGRAM");
              inside_input_program = false;
              destroy_input_program (inp);
              return result;
            }
        }
    }
  if (!saw_END_CASE)
    emit_END_CASE (inp->ds, inp);
  inside_input_program = false;

  if (!saw_DATA_LIST && !saw_END_FILE)
    {
      msg (SE, _("Input program must contain %s or %s."), "DATA LIST", "END FILE");
      destroy_input_program (inp);
      return CMD_FAILURE;
    }
  if (dict_get_next_value_idx (dataset_dict (inp->ds)) == 0)
    {
      msg (SE, _("Input program did not create any variables."));
      destroy_input_program (inp);
      return CMD_FAILURE;
    }

  inp->trns_chain = proc_capture_transformations (inp->ds);
  trns_chain_finalize (inp->trns_chain);

  inp->restart = TRNS_CONTINUE;

  /* Figure out how to initialize each input case. */
  inp->init = caseinit_create ();
  caseinit_mark_for_init (inp->init, dataset_dict (inp->ds));
  inp->proto = caseproto_ref (dict_get_proto (dataset_dict (inp->ds)));

  dataset_set_dict (ds, dict_clone (dataset_dict (inp->ds)));
  dataset_set_source (
    ds, casereader_create_sequential (NULL, inp->proto, CASENUMBER_MAX,
                                      &input_program_casereader_class, inp));

  return CMD_SUCCESS;
}

int
cmd_end_input_program (struct lexer *lexer UNUSED, struct dataset *ds UNUSED)
{
  /* Inside INPUT PROGRAM, this should get caught at the top of the loop in
     cmd_input_program().

     Outside of INPUT PROGRAM, the command parser should reject this
     command. */
  NOT_REACHED ();
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

/* Reads and returns one case.
   Returns the case if successful, null at end of file or if an
   I/O error occurred. */
static struct ccase *
input_program_casereader_read (struct casereader *reader UNUSED, void *inp_)
{
  struct input_program_pgm *inp = inp_;
  struct ccase *c = case_create (inp->proto);

  do
    {
      assert (is_valid_state (inp->restart));
      if (inp->restart == TRNS_ERROR || inp->restart == TRNS_END_FILE)
        {
          case_unref (c);
          return NULL;
        }

      c = case_unshare (c);
      caseinit_init_vars (inp->init, c);
      inp->restart = trns_chain_execute (inp->trns_chain, inp->restart,
                                         &c, inp->case_nr);
      assert (is_valid_state (inp->restart));
      caseinit_update_left_vars (inp->init, c);
    }
  while (inp->restart < 0);

  return c;
}

static void
destroy_input_program (struct input_program_pgm *pgm)
{
  if (pgm != NULL)
    {
      session_destroy (pgm->session);
      trns_chain_destroy (pgm->trns_chain);
      caseinit_destroy (pgm->init);
      caseproto_unref (pgm->proto);
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
  if (lex_token (lexer) == T_ENDCMD)
    return CMD_END_CASE;
  return CMD_SUCCESS;
}

/* Outputs the current case */
int
end_case_trns_proc (void *inp_, struct ccase **c UNUSED,
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
  char *encoding = NULL;

  fh = fh_get_default_handle ();
  e = NULL;
  while (lex_token (lexer) != T_ENDCMD)
    {
      if (lex_match_id (lexer, "COLUMN"))
	{
	  lex_match (lexer, T_EQUALS);

	  if (e)
	    {
              lex_sbc_only_once ("COLUMN");
              goto error;
	    }

	  e = expr_parse (lexer, ds, EXPR_NUMBER);
	  if (!e)
            goto error;
	}
      else if (lex_match_id (lexer, "FILE"))
	{
	  lex_match (lexer, T_EQUALS);
          fh_unref (fh);
          fh = fh_parse (lexer, FH_REF_FILE | FH_REF_INLINE, NULL);
	  if (fh == NULL)
            goto error;
	}
      else if (lex_match_id (lexer, "ENCODING"))
	{
	  lex_match (lexer, T_EQUALS);
	  if (!lex_force_string (lexer))
	    goto error;

          free (encoding);
          encoding = ss_xstrdup (lex_tokss (lexer));

	  lex_get (lexer);
	}
      else
	{
	  lex_error (lexer, NULL);
          goto error;
	}
    }

  t = xmalloc (sizeof *t);
  t->reader = dfm_open_reader (fh, lexer, encoding);
  t->column = e;
  add_transformation (ds, reread_trns_proc, reread_trns_free, t);

  fh_unref (fh);
  free (encoding);
  return CMD_SUCCESS;

error:
  expr_free (e);
  free (encoding);
  return CMD_CASCADING_FAILURE;
}

/* Executes a REREAD transformation. */
static int
reread_trns_proc (void *t_, struct ccase **c, casenumber case_num)
{
  struct reread_trns *t = t_;

  if (t->column == NULL)
    dfm_reread_record (t->reader, 1);
  else
    {
      double column = expr_evaluate_num (t->column, *c, case_num);
      if (!isfinite (column) || column < 1)
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
cmd_end_file (struct lexer *lexer UNUSED, struct dataset *ds)
{
  assert (in_input_program ());

  add_transformation (ds, end_file_trns_proc, NULL, NULL);

  return CMD_END_FILE;
}

/* Executes an END FILE transformation. */
static int
end_file_trns_proc (void *trns_ UNUSED, struct ccase **c UNUSED,
                    casenumber case_num UNUSED)
{
  return TRNS_END_FILE;
}
