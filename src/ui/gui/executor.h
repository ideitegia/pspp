/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2009, 2010, 2011  Free Software Foundation

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


#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <glib.h>
#include "ui/gui/psppire-data-window.h"

struct lex_reader;

gboolean execute_syntax (PsppireDataWindow *, struct lex_reader *);
gchar *execute_syntax_string (PsppireDataWindow *, gchar *syntax);
void execute_const_syntax_string (PsppireDataWindow *, const gchar *syntax);

#endif
