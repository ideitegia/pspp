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

/* Expands to a void expression that checks that POINTER is an
   expression whose type is a qualified or unqualified version of
   a type compatible with TYPE (a pointer type) and, if not,
   causes a compiler warning to be issued (on typical compilers).

   Examples:

   int *ip;
   const int *cip;
   const int **cipp;
   int ***ippp;
   double *dp;

   // None of these causes a warning:
   CHECK_POINTER_HAS_TYPE (ip, int *);
   CHECK_POINTER_HAS_TYPE (ip, const int *);
   CHECK_POINTER_HAS_TYPE (cip, int *);
   CHECK_POINTER_HAS_TYPE (cip, const int *);
   CHECK_POINTER_HAS_TYPE (dp, double *);
   CHECK_POINTER_HAS_TYPE (dp, const double *);
   CHECK_POINTER_HAS_TYPE (cipp, const int **);
   CHECK_POINTER_HAS_TYPE (cipp, const int *const *);
   CHECK_POINTER_HAS_TYPE (ippp, int ***);
   CHECK_POINTER_HAS_TYPE (ippp, int **const *);

   // None of these causes a warning either, although it is unusual to
   // const-qualify a pointer like this (it's like declaring a "const int",
   // for example).
   CHECK_POINTER_HAS_TYPE (ip, int *const);
   CHECK_POINTER_HAS_TYPE (ip, const int *const);
   CHECK_POINTER_HAS_TYPE (cip, int *const);
   CHECK_POINTER_HAS_TYPE (cip, const int *const);
   CHECK_POINTER_HAS_TYPE (cipp, const int **const);
   CHECK_POINTER_HAS_TYPE (cipp, const int *const *const);
   CHECK_POINTER_HAS_TYPE (ippp, int ***const);
   CHECK_POINTER_HAS_TYPE (ippp, int **const *const);

   // Provokes a warning because "int" is not compatible with "double":
   CHECK_POINTER_HAS_TYPE (dp, int *);

   // Provoke warnings because C's type compatibility rules only allow
   // adding a "const" qualifier to the outermost pointer:
   CHECK_POINTER_HAS_TYPE (ippp, const int ***);
   CHECK_POINTER_HAS_TYPE (ippp, int *const**);
*/
#define CHECK_POINTER_HAS_TYPE(POINTER, TYPE)           \
        ((void) sizeof ((TYPE) (POINTER) == (POINTER)))

/* Given expressions A and B, both of which have pointer type,
   expands to a void expression that causes a compiler warning if
   A and B are not pointers to qualified or unqualified versions
   of compatible types.

   Examples similar to those given for CHECK_POINTER_HAS_TYPE,
   above, can easily be devised. */
#define CHECK_POINTER_COMPATIBILITY(A, B) ((void) sizeof ((A) == (B)))

/* Given POINTER, a pointer to the given MEMBER within structure
   STRUCT, returns the address of the STRUCT. */
#define UP_CAST(POINTER, STRUCT, MEMBER)                                \
        (CHECK_POINTER_COMPATIBILITY (&((STRUCT *) 0)->MEMBER, POINTER), \
         (STRUCT *) ((char *) (POINTER) - offsetof (STRUCT, MEMBER)))

#endif /* libpspp/cast.h */
