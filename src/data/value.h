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

#if !value_h
#define value_h 1

#include <float.h>
#include <libpspp/misc.h>
#include "minmax.h"

/* Values. */

/* "Short" strings, which are generally those no more than 8
   characters wide, can participate in more operations than
   longer strings. */
#define MAX_SHORT_STRING (MAX (ROUND_UP (SIZEOF_DOUBLE, 2), 8))
#define MIN_LONG_STRING (MAX_SHORT_STRING + 1)
#define MAX_STRING 32767

/* Special values. */
#define SYSMIS (-DBL_MAX)
#define LOWEST second_lowest_value
#define HIGHEST DBL_MAX

/* A numeric or short string value.
   Multiple consecutive values represent a long string. */
union value
  {
    double f;
    char s[MAX_SHORT_STRING];
  };

union value *value_dup (const union value *, int width);
int compare_values (const union value *, const union value *, int width);
unsigned hash_value (const union value  *, int width);

#endif /* !value.h */
