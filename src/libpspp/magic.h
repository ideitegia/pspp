/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#if !magic_h
#define magic_h 1

/* Magic numbers. */

#include <float.h>
#include <limits.h>

/* Allows us to specify individual bytes of a double. */
union cvt_dbl {
  unsigned char cvt_dbl_i[8];
  double cvt_dbl_d;
};


/* "Second-lowest value" bytes for an IEEE-754 double. */
#if WORDS_BIGENDIAN
#define SECOND_LOWEST_BYTES {0xff,0xef,0xff,0xff, 0xff,0xff,0xff,0xfe}
#else
#define SECOND_LOWEST_BYTES {0xfe,0xff,0xff,0xff, 0xff,0xff,0xef,0xff}
#endif

/* "Second-lowest value" for a double. */
#if __GNUC__
#define second_lowest_value                                               \
        (__extension__ ((union cvt_dbl) {SECOND_LOWEST_BYTES}).cvt_dbl_d)
#else /* not GNU C */
extern union cvt_dbl second_lowest_value_union;
#define second_lowest_value (second_lowest_value_union.cvt_dbl_d)
#endif

/* Used when we want a "missing value". */
#define NOT_DOUBLE (-DBL_MAX)
#define NOT_LONG LONG_MIN
#define NOT_INT INT_MIN

#endif /* magic.h */
