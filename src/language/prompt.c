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

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include "prompt.h"

#include <data/file-name.h>
#include <data/settings.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <libpspp/alloc.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/str.h>
#include <libpspp/verbose-msg.h>
#include <libpspp/version.h>
#include <output/table.h>

/* Current prompts in each style. */
static char *prompts[PROMPT_CNT];

/* Current prompting style. */
static enum prompt_style current_style;

/* Initializes prompts. */
void
prompt_init (void) 
{
  prompts[PROMPT_FIRST] = xstrdup ("PSPP> ");
  prompts[PROMPT_LATER] = xstrdup ("    > ");
  prompts[PROMPT_DATA] = xstrdup ("data> ");
  current_style = PROMPT_FIRST;
}

/* Frees prompts. */
void
prompt_done (void) 
{
  int i;

  for (i = 0; i < PROMPT_CNT; i++) 
    {
      free (prompts[i]);
      prompts[i] = NULL;
    }
}

/* Gets the command prompt for the given STYLE. */
const char * 
prompt_get (enum prompt_style style)
{
  assert (style < PROMPT_CNT);
  return prompts[style];
}

/* Sets the given STYLE's prompt to STRING. */
void
prompt_set (enum prompt_style style, const char *string)
{
  assert (style < PROMPT_CNT);
  free (prompts[style]);
  prompts[style] = xstrdup (string);
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
