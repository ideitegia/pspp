/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011 Free Software Foundation, Inc.

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
#include <errno.h>
#include <stdlib.h>

#include "language/prompt.h"

#include "data/file-name.h"
#include "data/settings.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/version.h"
#include "output/tab.h"

#include "gl/xalloc.h"

/* Current prompting style. */
static enum prompt_style current_style;

/* Gets the command prompt for the given STYLE. */
const char *
prompt_get (enum prompt_style style)
{
  switch (style)
    {
    case PROMPT_FIRST:
      return "PSPP> ";

    case PROMPT_LATER:
      return "    > ";

    case PROMPT_DATA:
      return "data> ";

    case PROMPT_CNT:
      NOT_REACHED ();
    }
  NOT_REACHED ();
}

/* Sets STYLE as the current prompt style. */
void
prompt_set_style (enum prompt_style style)
{
  assert (style < PROMPT_CNT);
  current_style = style;
}

/* Returns the current prompt. */
enum prompt_style
prompt_get_style (void)
{
  return current_style;
}
