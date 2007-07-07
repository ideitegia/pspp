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

#if !bitvector_h
#define bitvector_h 1

#include <limits.h>

/* Sets bit Y starting at address X. */
#define SET_BIT(X, Y) 					\
	(((unsigned char *) X)[(Y) / CHAR_BIT] |= 1 << ((Y) % CHAR_BIT))

/* Clears bit Y starting at address X. */
#define CLEAR_BIT(X, Y) 				\
	(((unsigned char *) X)[(Y) / CHAR_BIT] &= ~(1 << ((Y) % CHAR_BIT)))

/* Sets bit Y starting at address X to Z, which is zero/nonzero */
#define SET_BIT_TO(X, Y, Z) 			\
	((Z) ? SET_BIT(X, Y) : CLEAR_BIT(X, Y))

/* Nonzero if bit Y starting at address X is set. */
#define TEST_BIT(X, Y) 					\
	(((unsigned char *) X)[(Y) / CHAR_BIT] & (1 << ((Y) % CHAR_BIT)))

/* Returns 2**X, 0 <= X < 32. */
#define BIT_INDEX(X) (1ul << (X))

#endif /* bitvector.h */
