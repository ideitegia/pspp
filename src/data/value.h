/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007 Free Software Foundation, Inc.

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

#ifndef DATA_VALUE_H
#define DATA_VALUE_H 1

#include <libpspp/misc.h>
#include <stdbool.h>
#include <stddef.h>
#include "minmax.h"

/* "Short" strings, which are generally those no more than 8
   characters wide, can participate in more operations than
   longer strings. */
#define MAX_SHORT_STRING (MAX (ROUND_UP (SIZEOF_DOUBLE, 2), 8))
#define MIN_LONG_STRING (MAX_SHORT_STRING + 1)

/* A numeric or short string value.
   Multiple consecutive values represent a long string. */
union value
  {
    double f;
    char s[MAX_SHORT_STRING];
  };

union value *value_dup (const union value *, int width);
union value *value_create (int width);

int compare_values (const void *, const void *, const void *var);
unsigned hash_value (const void *, const void *var);

int compare_values_short (const void *, const void *, const void *var);
unsigned hash_value_short (const void *, const void *var);

static inline size_t value_cnt_from_width (int width);
void value_copy (union value *, const union value *, int width);
void value_set_missing (union value *, int width);
bool value_is_resizable (const union value *, int old_width, int new_width);
void value_resize (union value *, int old_width, int new_width);
int value_compare_3way (const union value *, const union value *, int width);

/* Number of "union value"s required for a variable of the given
   WIDTH. */
static inline size_t
value_cnt_from_width (int width)
{
  return width == 0 ? 1 : DIV_RND_UP (width, MAX_SHORT_STRING);
}

#endif /* data/value.h */
