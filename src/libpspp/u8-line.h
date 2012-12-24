/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_U8_LINE_H
#define LIBPSPP_U8_LINE_H 1

#include "libpspp/str.h"

struct pool;

/* A line of text, encoded in UTF-8, with support functions that properly
   handle double-width characters and backspaces.

   Designed to make appending text fast, and access and modification of other
   column positions possible. */
struct u8_line
  {
    struct string s;            /* Content, in UTF-8. */
    size_t width;               /* Display width, in character positions. */
  };

void u8_line_init (struct u8_line *);
void u8_line_destroy (struct u8_line *);
void u8_line_clear (struct u8_line *);
char *u8_line_reserve (struct u8_line *, int x0, int x1, int n);
void u8_line_put (struct u8_line *, int x0, int x1, const char *s, int n);
void u8_line_set_length (struct u8_line *, int x);

#endif /* libpspp/u8-line.h */
