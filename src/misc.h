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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#if !math_misc_h
#define math_misc_h 1

#include <float.h>
#include <math.h>

#define EPSILON (10 * DBL_EPSILON)

/* HUGE_VAL is traditionally defined as positive infinity, or
   alternatively, DBL_MAX. */
#if !HAVE_ISINF
#define isinf(X) 				\
	(fabs (X) == HUGE_VAL)
#endif

/* A Not a Number is not equal to itself. */
#if !HAVE_ISNAN
#define isnan(X) 				\
	((X) != (X))
#endif

/* Finite numbers are not infinities or NaNs. */
#if !HAVE_FINITE
#define finite(X) 				\
	(!isinf (X) && !isnan (X))
#elif HAVE_IEEEFP_H
#include <ieeefp.h>		/* Declares finite() under Solaris. */
#endif

#if __TURBOC__
#include <stdlib.h>		/* screwed-up Borland headers define min(), max(),
				   so we might as well let 'em */
#endif

#ifndef min
#if __GNUC__ && !__STRICT_ANSI__
#define min(A, B)				\
	({					\
	  int _a = (A), _b = (B);		\
	  _a < _b ? _a : _b;			\
	})
#else /* !__GNUC__ */
#define min(A, B) 				\
	((A) < (B) ? (A) : (B))
#endif /* !__GNUC__ */
#endif /* !min */

#ifndef max
#if __GNUC__ && !__STRICT_ANSI__
#define max(A, B)				\
	({					\
	  int _a = (A), _b = (B);		\
	  _a > _b ? _a : _b;			\
	})
#else /* !__GNUC__ */
#define max(A, B) 				\
	((A) > (B) ? (A) : (B))
#endif /* !__GNUC__ */
#endif /* !max */

/* Clamps A to be between B and C. */
#define range(A, B, C)				\
	((A) < (B) ? (B) : ((A) > (C) ? (C) : (A)))

/* Divides nonnegative X by positive Y, rounding up. */
#define DIV_RND_UP(X, Y) 			\
	(((X) + ((Y) - 1)) / (Y))

/* Returns nonnegative difference between {nonnegative X} and {the
   least multiple of positive Y greater than or equal to X}. */
#if __GNUC__ && !__STRICT_ANSI__
#define REM_RND_UP(X, Y)			\
	({					\
	  int rem = (X) % (Y);			\
	  rem ? (Y) - rem : 0;			\
	})
#else
#define REM_RND_UP(X, Y) 			\
	((X) % (Y) ? (Y) - (X) % (Y) : 0)
#endif

/* Rounds X up to the next multiple of Y. */
#define ROUND_UP(X, Y) 				\
	(((X) + ((Y) - 1)) / (Y) * (Y))

/* Rounds X down to the previous multiple of Y. */
#define ROUND_DOWN(X, Y) 			\
	((X) / (Y) * (Y))

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
