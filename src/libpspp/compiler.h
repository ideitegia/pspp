/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009 Free Software Foundation, Inc.

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

#ifndef COMPILER_H
#define COMPILER_H 1

/* GNU C allows the programmer to declare that certain functions take
   printf-like arguments, never return, etc.  Conditionalize these
   declarations on whether gcc is in use. */
#if __GNUC__ > 1
#define ATTRIBUTE(X) __attribute__ (X)

/* Only necessary because of a wart in gnulib's xalloc.h. */
#define __attribute__(X) __attribute__ (X)
#else
#define ATTRIBUTE(X)
#endif

/* Marks a function argument as possibly not used. */
#define UNUSED ATTRIBUTE ((unused))

/* Marks a function that will never return. */
#define NO_RETURN ATTRIBUTE ((noreturn))

/* Mark a function as taking a printf- or scanf-like format
   string as its FMT'th argument and that the FIRST'th argument
   is the first one to be checked against the format string. */
#define PRINTF_FORMAT(FMT, FIRST) ATTRIBUTE ((format (__printf__, FMT, FIRST)))
#define SCANF_FORMAT(FMT, FIRST) ATTRIBUTE ((format (__scanf__, FMT, FIRST)))

/* Tells the compiler that a function may be treated as if any
   non-`NULL' pointer it returns cannot alias any other pointer
   valid when the function returns. */
#if __GNUC__ > 2
#define MALLOC_LIKE ATTRIBUTE ((__malloc__))
#else
#define MALLOC_LIKE
#endif

/* This attribute was added in GCC 4.0. */
#if __GNUC__ >= 4
#define WARN_UNUSED_RESULT ATTRIBUTE ((warn_unused_result))
#else
#define WARN_UNUSED_RESULT
#endif

/* This attribute indicates that the function does not examine
   any values except its arguments, and has no effects except the
   return value.  A function that has pointer arguments and
   examines the data pointed to must _not_ be declared
   `const'.  */
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 5)
#define CONST_FUNCTION ATTRIBUTE ((const))
#else
#define CONST_FUNCTION
#endif

/* This attribute indicates that the function has no effects
   except the return value and its return value depends only on
   the parameters and/or global variables. */
#if __GNUC__ > 2
#define PURE_FUNCTION ATTRIBUTE ((pure))
#else
#define PURE_FUNCTION
#endif

/* This attribute indicates that the argument with the given
   IDX must be a null pointer.  IDX counts backward in the
   argument list, so that 0 is the last argument, 1 is the
   second-from-last argument, and so on. */
#if __GNUC__ > 3
#define SENTINEL(IDX) ATTRIBUTE ((sentinel(IDX)))
#else
#define SENTINEL
#endif

#endif /* compiler.h */
