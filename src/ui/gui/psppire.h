/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2005, 2006  Free Software Foundation

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

#ifndef PSPPIRE_H
#define PSPPIRE_H

#include <argp.h>

struct command_line_processor ;
extern const struct argp non_option_argp ;

extern GtkRecentManager *the_recent_mgr;

void initialize (struct command_line_processor *, int argc, char **argv);
void de_initialize (void);
void psppire_quit (void);


#endif /* PSPPIRE_H */
