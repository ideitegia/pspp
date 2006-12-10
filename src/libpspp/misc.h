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

#if !math_misc_h
#define math_misc_h 1

#include <float.h>
#include <math.h>

#define EPSILON (10 * DBL_EPSILON)

/* HUGE_VAL is traditionally defined as positive infinity, or
   alternatively, DBL_MAX. */
#if !HAVE_ISINF
#define isinf(X) (fabs (X) == HUGE_VAL)
#endif

/* A Not a Number is not equal to itself. */
#if !HAVE_ISNAN
#define isnan(X) ((X) != (X))
#endif

/* Finite numbers are not infinities or NaNs. */
#if !HAVE_FINITE
#define finite(X) (!isinf (X) && !isnan (X))
#elif HAVE_IEEEFP_H
#include <ieeefp.h>		/* Declares finite() under Solaris. */
#endif

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

#endif /* math/misc.h */
