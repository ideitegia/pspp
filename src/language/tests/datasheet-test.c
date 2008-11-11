/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

#include "datasheet-check.h"

#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/tests/check-model.h>
#include <libpspp/array.h>
#include <libpspp/assertion.h>

#include "error.h"
#include "xalloc.h"

static bool parse_coordinates (struct lexer *, int *rows, int *cols);

/* Parses and executes the DEBUG DATASHEET command, which runs
   the model checker on the datasheet data structure.  The
   command may include a specification of the form
   MAX=(ROWS,COLS) to specify the maximum size of the data sheet
   during the model checker run (default: 4x4) or
   BACKING=(ROWS,COLS) to specify the size of the casereader
   backing the datasheet (default: no backing).  These may be
   optionally followed by any of the common model checker option
   specifications (see check-model.q). */
int
cmd_debug_datasheet (struct lexer *lexer, struct dataset *dataset UNUSED)
{
  struct datasheet_test_params params;
  bool ok;

  params.max_rows = 4;
  params.max_cols = 4;
  params.backing_rows = 0;
  params.backing_cols = 0;


  for (;;)
    {
      if (lex_match_id (lexer, "MAX"))
        {
          if (!parse_coordinates (lexer, &params.max_rows, &params.max_cols))
            return CMD_FAILURE;
        }
      else if (lex_match_id (lexer, "BACKING"))
        {
          if (!parse_coordinates (lexer,
                                  &params.backing_rows, &params.backing_cols))
            return CMD_FAILURE;
        }
      else
        break;
      lex_match (lexer, '/');
    }

  ok = check_model (lexer, datasheet_test, &params);
  printf ("Datasheet test max(%d,%d) backing(%d,%d) %s.\n",
          params.max_rows, params.max_cols,
          params.backing_rows, params.backing_cols,
          ok ? "successful" : "failed");
  return ok ? lex_end_of_command (lexer) : CMD_FAILURE;
}

/* Parses a pair of coordinates with the syntax =(ROWS,COLS),
   where all of the delimiters are optional, into *ROWS and
   *COLS.  Returns true if successful, false on parse failure. */
static bool
parse_coordinates (struct lexer *lexer, int *rows, int *cols)
{
  lex_match (lexer, '=');
  lex_match (lexer, '(');

  if (!lex_force_int (lexer))
    return false;
  *rows = lex_integer (lexer);
  lex_get (lexer);

  lex_match (lexer, ',');

  if (!lex_force_int (lexer))
    return false;
  *cols = lex_integer (lexer);
  lex_get (lexer);

  lex_match (lexer, ')');
  return true;
}

