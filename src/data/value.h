/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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

#include <config.h>

/* Values. */

/* Max length of a short string value, generally 8 chars. */
#define MAX_SHORT_STRING ( (SIZEOF_DOUBLE)>=8 ? (SIZEOF_DOUBLE + 1)/2 * 2 : 8 )

#define MIN_LONG_STRING (MAX_SHORT_STRING + 1)

/* Max string length. */
#define MAX_LONG_STRING 255

/* This nonsense is required for SPSS compatibility */
#define EFFECTIVE_LONG_STRING_LENGTH (MAX_LONG_STRING - 3)

#define MAX_VERY_LONG_STRING 32767

#define MAX_STRING MAX_VERY_LONG_STRING


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

/* Maximum number of `union value's in a single number or string
   value. */
#define MAX_ELEMS_PER_VALUE (MAX_STRING / sizeof (union value) + 1)

int compare_values (const union value *a, const union value *b, int width);

unsigned  hash_value(const union value  *v, int width);



#endif /* !value.h */
