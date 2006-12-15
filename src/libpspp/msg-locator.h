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

struct msg_locator ;

void msg_locator_done (void);

/* File locator stack functions. */

/* Pushes F onto the stack of file locations. */
void msg_push_msg_locator (const struct msg_locator *loc);

/* Pops F off the stack of file locations.
   Argument F is only used for verification that that is actually the
   item on top of the stack. */
void msg_pop_msg_locator (const struct msg_locator *loc);

struct source_stream ;
/* Puts the current file and line number into LOC, or NULL and -1 if
   none. */
void get_msg_location (const struct source_stream *ss, struct msg_locator *loc);
