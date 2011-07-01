/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/command.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/string-map.h"
#include "output/measure.h"

/* Executes the DEBUG PAPER SIZE command. */
int
cmd_debug_paper_size (struct lexer *lexer, struct dataset *ds UNUSED)
{
  const char *paper_size;
  int h, v;

  if (!lex_force_string (lexer))
    return CMD_FAILURE;
  paper_size = lex_tokcstr (lexer);

  printf ("\"%s\" => ", paper_size);
  if (measure_paper (paper_size, &h, &v))
    printf ("%.1f x %.1f in, %.0f x %.0f mm\n",
            h / 72000., v / 72000.,
            h / (72000 / 25.4), v / (72000 / 25.4));
  else
    printf ("error\n");
  lex_get (lexer);

  return CMD_SUCCESS;
}
