/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
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

#define UNUSED ATTRIBUTE ((unused))
#define NO_RETURN ATTRIBUTE ((noreturn))
#define PRINTF_FORMAT(FMT, FIRST) ATTRIBUTE ((format (printf, FMT, FIRST)))
#define SCANF_FORMAT(FMT, FIRST) ATTRIBUTE ((format (scanf, FMT, FIRST)))

/* This attribute was added late in the GCC 2.x cycle. */
#if __GNUC__ > 2
#define MALLOC_LIKE ATTRIBUTE ((malloc))
#else
#define MALLOC_LIKE
#endif

#endif /* compiler.h */
