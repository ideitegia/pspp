/* PSPP - computes sample statistics.
   Copyright (C) 1997, 1998 Free Software Foundation, Inc.
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

#include <stddef.h>

/* Some old versions of Linux libc prototype stpcpy() in string.h but
   fail to include it in their C library.  By not including string.h
   on these systems we can avoid conflicting prototypes.  Of course,
   in theory this might be dangerous, if the prototype specifies some
   weird calling convention, but for GNU/Linux at least it shouldn't
   cause problems.

   This might be needed for systems other than GNU/Linux; let me
   know. */

#ifdef __linux__
void *memcpy (void *, const void *, size_t);
size_t strlen (const char *);
#else
#include "str.h"
#endif

/* Copies SRC to DEST, returning the address of the terminating '\0'
   in DEST. */
char *
stpcpy (char *dest, const char *src)
{
  int len = strlen (src);
  memcpy (dest, src, len + 1);
  return &dest[len];
}
