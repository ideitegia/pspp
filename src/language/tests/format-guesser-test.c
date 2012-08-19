/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2010, 2011 Free Software Foundation, Inc.

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

#include <stdio.h>

#include "data/format.h"
#include "data/format-guesser.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "libpspp/message.h"

/* Executes the DEBUG FORMAT GUESSER command. */
int
cmd_debug_format_guesser (struct lexer *lexer, struct dataset *ds UNUSED)
{
  struct fmt_guesser *g;
  struct fmt_spec format;
  char format_string[FMT_STRING_LEN_MAX + 1];

  g = fmt_guesser_create ();
  while (lex_is_string (lexer))
    {
      fprintf (stderr, "\"%s\" ", lex_tokcstr (lexer));
      fmt_guesser_add (g, lex_tokss (lexer));
      lex_get (lexer);
    }

  fmt_guesser_guess (g, &format);
  fmt_to_string (&format, format_string);
  fprintf (stderr, "=> %s", format_string);
  msg_disable ();
  if (!fmt_check_input (&format))
    {
      fmt_fix_input (&format);
      fmt_to_string (&format, format_string);
      fprintf (stderr, " (%s)", format_string);
    }
  msg_enable ();
  putc ('\n', stderr);
  fmt_guesser_destroy (g);

  return CMD_SUCCESS;
}
