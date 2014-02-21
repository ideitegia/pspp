/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include <limits.h>
#include <stdlib.h>

#include "data/dataset.h"
#include "data/value.h"
#include "language/command.h"
#include "language/data-io/data-writer.h"
#include "language/data-io/file-handle.h"
#include "language/expressions/public.h"
#include "language/lexer/lexer.h"
#include "libpspp/message.h"
#include "output/text-item.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* PRINT SPACE transformation. */
struct print_space_trns
  {
    struct dfm_writer *writer;  /* Output data file. */
    struct expression *expr;	/* Number of lines; NULL means 1. */
  };

static trns_proc_func print_space_trns_proc;
static trns_free_func print_space_trns_free;

int
cmd_print_space (struct lexer *lexer, struct dataset *ds)
{
  struct print_space_trns *trns;
  struct file_handle *handle = NULL;
  struct expression *expr = NULL;
  struct dfm_writer *writer;
  char *encoding = NULL;

  if (lex_match_id (lexer, "OUTFILE"))
    {
      lex_match (lexer, T_EQUALS);

      handle = fh_parse (lexer, FH_REF_FILE, NULL);
      if (handle == NULL)
	return CMD_FAILURE;

      if (lex_match_id (lexer, "ENCODING"))
	{
	  lex_match (lexer, T_EQUALS);
	  if (!lex_force_string (lexer))
	    goto error;

          encoding = ss_xstrdup (lex_tokss (lexer));

	  lex_get (lexer);
	}
    }
  else
    handle = NULL;

  if (lex_token (lexer) != T_ENDCMD)
    {
      expr = expr_parse (lexer, ds, EXPR_NUMBER);
      if (lex_token (lexer) != T_ENDCMD)
	{
          lex_error (lexer, _("expecting end of command"));
          goto error;
	}
    }
  else
    expr = NULL;

  if (handle != NULL)
    {
      writer = dfm_open_writer (handle, encoding);
      if (writer == NULL)
        goto error;
    }
  else
    writer = NULL;

  trns = xmalloc (sizeof *trns);
  trns->writer = writer;
  trns->expr = expr;

  add_transformation (ds,
		      print_space_trns_proc, print_space_trns_free, trns);
  fh_unref (handle);
  return CMD_SUCCESS;

error:
  fh_unref (handle);
  expr_free (expr);
  return CMD_FAILURE;
}

/* Executes a PRINT SPACE transformation. */
static int
print_space_trns_proc (void *t_, struct ccase **c,
                       casenumber case_num UNUSED)
{
  struct print_space_trns *trns = t_;
  int n;

  n = 1;
  if (trns->expr)
    {
      double f = expr_evaluate_num (trns->expr, *c, case_num);
      if (f == SYSMIS)
        msg (SW, _("The expression on %s evaluated to the "
                   "system-missing value."), "PRINT SPACE");
      else if (f < 0 || f > INT_MAX)
        msg (SW, _("The expression on %s evaluated to %g."), "PRINT SPACE", f);
      else
        n = f;
    }

  while (n--)
    if (trns->writer == NULL)
      text_item_submit (text_item_create (TEXT_ITEM_BLANK_LINE, ""));
    else
      dfm_put_record (trns->writer, " ", 1); /* XXX */

  if (trns->writer != NULL && dfm_write_error (trns->writer))
    return TRNS_ERROR;
  return TRNS_CONTINUE;
}

/* Frees a PRINT SPACE transformation.
   Returns true if successful, false if an I/O error occurred. */
static bool
print_space_trns_free (void *trns_)
{
  struct print_space_trns *trns = trns_;
  bool ok = dfm_close_writer (trns->writer);
  expr_free (trns->expr);
  free (trns);
  return ok;
}
