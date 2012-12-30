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

#if !libpspp_misc_h
#define libpspp_misc_h 1

#include <stddef.h>
#include <float.h>
#include <math.h>

#define EPSILON (10 * DBL_EPSILON)

/* Divides nonnegative X by positive Y, rounding up. */
#define DIV_RND_UP(X, Y) (((X) + ((Y) - 1)) / (Y))

/* Returns nonnegative difference between {nonnegative X} and {the
   least multiple of positive Y greater than or equal to X}. */
#define REM_RND_UP(X, Y) ((X) % (Y) ? (Y) - (X) % (Y) : 0)

/* Rounds X up to the next multiple of Y. */
#define ROUND_UP(X, Y) (((X) + ((Y) - 1)) / (Y) * (Y))

/* Rounds X down to the previous multiple of Y. */
#define ROUND_DOWN(X, Y) ((X) / (Y) * (Y))

int intlog10 (unsigned);

/* Returns the square of X. */
static inline double
pow2 (double x)
{
  return x * x;
}

/* Returns the cube of X. */
static inline double
pow3 (double x)
{
  return x * x * x;
}

/* Returns the fourth power of X. */
static inline double
pow4 (double x)
{
  double y = x * x;
  y *= y;
  return y;
}

/* Set *DEST to the lower of *DEST and SRC */
static inline void
minimize (double *dest, double src)
{
  if (src < *dest)
    *dest = src;
}


/* Set *DEST to the greater of *DEST and SRC */
static inline void
maximize (double *dest, double src)
{
  if (src > *dest)
    *dest = src;
}


/* Set *DEST to the lower of *DEST and SRC */
static inline void
minimize_int (int *dest, int src)
{
  if (src < *dest)
    *dest = src;
}


/* Set *DEST to the greater of *DEST and SRC */
static inline void
maximize_int (int *dest, int src)
{
  if (src > *dest)
    *dest = src;
}

int c_dtoastr (char *buf, size_t bufsize, int flags, int width, double x);


#endif /* libpspp/misc.h */
