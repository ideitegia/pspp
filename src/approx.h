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

#if !approx_h
#define approx_h 1

#include <float.h>
#include <math.h>

/* Minimum difference to consider values to be distinct. */
#define EPSILON (DBL_EPSILON*10)

/* The boundary at EPSILON is considered to be equal. */
/* Possible modification: insert frexp() into all these expressions. */

#define approx_eq(A, B)				\
	(fabs((A)-(B))<=EPSILON)

#define approx_ne(A, B)				\
	(fabs((A)-(B))>EPSILON)

#define approx_ge(A, B)				\
	((A) >= (B)-EPSILON)

#define approx_gt(A, B)				\
	((A) > (B)+EPSILON)

#define approx_le(A, B)				\
	((A) <= (B)+EPSILON)

#define approx_lt(A, B)				\
	((A) < (B)-EPSILON)

#define approx_floor(x)				\
	(floor((x)+EPSILON))

#define approx_in_range(V, L, H)			\
	(((V) >= (L)-EPSILON) && ((V) <= (H)+EPSILON))

#define approx_compare(A, B) 					\
	(approx_gt(A,B) ? 1 : (approx_lt(A,B) ? -1 : 0))

#endif /* !approx_h */
