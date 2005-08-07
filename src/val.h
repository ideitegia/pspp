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

#if !val_h
#define val_h 1

#include <float.h>
#include "magic.h"

/* Values. */

/* Max length of a short string value, generally 8 chars. */
#define MAX_SHORT_STRING ((SIZEOF_DOUBLE)>=8 ? ((SIZEOF_DOUBLE)+1)/2*2 : 8)
#define MIN_LONG_STRING (MAX_SHORT_STRING+1)

/* Max string length. */
#define MAX_STRING 255

/* FYI: It is a bad situation if sizeof(flt64) < MAX_SHORT_STRING:
   then short string missing values can be truncated in system files
   because there's only room for as many characters as can fit in a
   flt64. */
#if MAX_SHORT_STRING > SHORT_NAME_LEN
#error MAX_SHORT_STRING must be less than or equal to SHORT_NAME_LEN.
#endif

/* Special values. */
#define SYSMIS (-DBL_MAX)
#define LOWEST second_lowest_value
#define HIGHEST DBL_MAX

/* Describes one value, which is either a floating-point number or a
   short string. */
union value
  {
    /* A numeric value. */
    double f;

    /* A short-string value. */
    unsigned char s[MAX_SHORT_STRING];

    /* Used by evaluate_expression() to return a string result.
       As currently implemented, it's a pointer to a dynamic
       buffer in the appropriate expression.

       Also used by the AGGREGATE procedure in handling string
       values. */
    unsigned char *c;
  };

/* Maximum number of `union value's in a single number or string
   value. */
#define MAX_ELEMS_PER_VALUE (MAX_STRING / sizeof (union value) + 1)

int compare_values (const union value *a, const union value *b, int width);

unsigned  hash_value(const union value  *v, int width);



#endif /* !val_h */
