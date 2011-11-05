/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2011 Free Software Foundation, Inc.

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
#include <stdio.h>
#include <stdlib.h>

#include "data/dataset.h"
#include "data/format.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

enum
  {
    FORMATS_PRINT = 001,
    FORMATS_WRITE = 002
  };

static int internal_cmd_formats (struct lexer *, struct dataset *ds, int);

int
cmd_print_formats (struct lexer *lexer, struct dataset *ds)
{
  return internal_cmd_formats (lexer, ds, FORMATS_PRINT);
}

int
cmd_write_formats (struct lexer *lexer, struct dataset *ds)
{
  return internal_cmd_formats (lexer, ds, FORMATS_WRITE);
}

int
cmd_formats (struct lexer *lexer, struct dataset *ds)
{
  return internal_cmd_formats (lexer, ds, FORMATS_PRINT | FORMATS_WRITE);
}

static int
internal_cmd_formats (struct lexer *lexer, struct dataset *ds, int which)
{
  /* Variables. */
  struct variable **v;
  size_t cv;

  for (;;)
    {
      struct fmt_spec f;
      int width;
      size_t i;

      lex_match (lexer, T_SLASH);

      if (lex_token (lexer) == T_ENDCMD)
	break;

      if (!parse_variables (lexer, dataset_dict (ds), &v, &cv, PV_SAME_WIDTH))
	return CMD_FAILURE;
      width = var_get_width (v[0]);

      if (!lex_match (lexer, T_LPAREN))
	{
          lex_error_expecting (lexer, "`('", NULL_SENTINEL);
	  goto fail;
	}
      if (!parse_format_specifier (lexer, &f)
          || !fmt_check_output (&f)
          || !fmt_check_width_compat (&f, width))
	goto fail;

      if (!lex_match (lexer, T_RPAREN))
	{
          lex_error_expecting (lexer, "`)'", NULL_SENTINEL);
	  goto fail;
	}

      for (i = 0; i < cv; i++)
	{
	  if (which & FORMATS_PRINT)
            var_set_print_format (v[i], &f);
	  if (which & FORMATS_WRITE)
            var_set_write_format (v[i], &f);
	}
      free (v);
      v = NULL;
    }
  return CMD_SUCCESS;

fail:
  free (v);
  return CMD_FAILURE;
}
