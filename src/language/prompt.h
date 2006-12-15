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

#ifndef PROMPT_H
#define PROMPT_H 1

#include <stdbool.h>

enum prompt_style
  {
    PROMPT_FIRST,		/* First line of command. */
    PROMPT_LATER,          /* Second or later line of command. */
    PROMPT_DATA,		/* Between BEGIN DATA and END DATA. */
    PROMPT_CNT
  };


void prompt_init (void);
void prompt_done (void);

enum prompt_style prompt_get_style (void);

const char *prompt_get (enum prompt_style);
void prompt_set (enum prompt_style, const char *);
void prompt_set_style (enum prompt_style);


#endif /* PROMPT_H */
