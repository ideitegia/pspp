/* PSPP - a program for statistical analysis.
   Copyright (C) 2009 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_CAST_H
#define LIBPSPP_CAST_H 1

#include <stddef.h>

/* Given expressions A and B, both of which have pointer type,
   expands to a void expression that causes a compiler warning if
   A and B are not pointers to qualified or unqualified versions
   of compatible types. */
#define CHECK_POINTER_COMPATIBILITY(A, B) ((void) sizeof ((A) == (B)))

/* Given POINTER, a pointer to the given MEMBER within structure
   STRUCT, returns the address of the STRUCT. */
#define UP_CAST(POINTER, STRUCT, MEMBER)                                \
        (CHECK_POINTER_COMPATIBILITY (&((STRUCT *) 0)->MEMBER, POINTER), \
         (STRUCT *) ((char *) (POINTER) - offsetof (STRUCT, MEMBER)))

#endif /* libpspp/cast.h */
