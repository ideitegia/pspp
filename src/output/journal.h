/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2012 Free Software Foundation, Inc.

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

/* Journal for commands and errors.

   The journal file records the commands entered interactively
   during a PSPP session.  It also records, prefixed by "> ",
   commands from files included with interactive commands and
   errors. */

#ifndef OUTPUT_JOURNAL_H
#define OUTPUT_JOURNAL_H 1

#include <stdbool.h>

void journal_init (void);
void journal_enable (void);
void journal_disable (void);
bool journal_is_enabled (void);
void journal_set_file_name (const char *);
const char *journal_get_file_name (void);

#endif /* output/journal.h */
