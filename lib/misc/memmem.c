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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <stddef.h>

int memcmp ();

/* Finds the first NEEDLE of length NEEDLE_LEN in a HAYSTACK of length
   HAYSTACK_LEN.  Returns a pointer to the match or NULL on
   failure. */
void *
memmem (const void *haystack, size_t haystack_len,
	const void *needle, size_t needle_len)
{
  size_t i;
  
  if (needle_len > haystack_len)
    return NULL;
  
  for (i = 0; i <= haystack_len - needle_len; i++)
    if (!memcmp (needle, &((const char *) haystack)[i], needle_len))
      return (void *) (&((const char *) haystack)[i]);
  
  return NULL;
}

