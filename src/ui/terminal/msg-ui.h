/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

#ifndef MSG_UI_H
#define MSG_UI_H 1

#include <stdbool.h>

struct source_stream ;

void msg_ui_set_error_file (const char *filename);
void msg_ui_init (struct source_stream *);
void msg_ui_done (void);
void check_msg_count (struct source_stream *);
void reset_msg_count (void);
bool any_errors (void);

#endif /* msg-ui.h */
