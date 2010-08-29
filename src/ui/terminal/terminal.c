/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2010 Free Software Foundation, Inc.

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

#include "ui/terminal/terminal.h"

#include <stdbool.h>
#include <stdlib.h>

#include "data/settings.h"
#include "libpspp/compiler.h"

#include "gl/error.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Code that interfaces to ncurses.  This must be at the very end
   of this file because curses.h redefines "bool" on some systems
   (e.g. OpenBSD), causing declaration mismatches with functions
   that have parameters or return values of type "bool". */
#if LIBNCURSES_USABLE
#include <curses.h>
#include <term.h>
#endif

/* Determines the size of the terminal, if possible, or at least
   takes an educated guess. */
void
terminal_check_size (void)
{
  int view_width = 0;
  int view_length = 0;

#if LIBNCURSES_USABLE
  if (getenv ("TERM") != NULL)
    {
      char term_buffer [16384];

      if (tgetent (term_buffer, getenv ("TERM")) > 0)
        {
          if (tgetnum ("li") > 0)
            view_length = tgetnum ("li");
          if (tgetnum ("co") > 1)
            view_width = tgetnum ("co") - 1;
        }
      else
        error (0, 0, _("could not access definition for terminal `%s'"),
               getenv ("TERM"));
    }
#endif

  if (view_width <= 0 && getenv ("COLUMNS") != NULL)
    view_width = atoi (getenv ("COLUMNS"));
  if (view_width > 0)
    settings_set_viewwidth (view_width);

  if (view_length <= 0 && getenv ("LINES") != NULL)
    view_length = atoi (getenv ("LINES"));
  if (view_length > 0)
    settings_set_viewlength (view_length);
}
