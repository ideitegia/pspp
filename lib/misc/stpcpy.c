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

void *memcpy (void *, const void *, size_t);
size_t strlen (const char *);

/* Copies SRC to DEST, returning the address of the terminating '\0'
   in DEST. */
char *
stpcpy (char *dest, const char *src)
{
  int len = strlen (src);
  memcpy (dest, src, len + 1);
  return &dest[len];
}
