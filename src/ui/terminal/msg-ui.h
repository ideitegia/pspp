/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

#ifndef MSG_UI_H
#define MSG_UI_H 1

#include <stdbool.h>
#include <stdio.h>

struct source_stream;

void msg_ui_set_error_file (FILE *);
void msg_ui_init (struct source_stream *);
void msg_ui_done (void);
bool msg_ui_too_many_errors (void);
void msg_ui_reset_counts (void);
bool msg_ui_any_errors (void);

#endif /* msg-ui.h */
