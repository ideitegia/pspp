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

#include <config.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "alloc.h"

/* Reads a DELIMITER-separated field of any length from file STREAM.
   *LINEPTR is a malloc'd string of size N; if *LINEPTR is NULL, it is
   allocated.  *LINEPTR is allocated/enlarged as necessary.  Returns
   -1 if at eof when entered; otherwise eof causes return of string
   without a terminating DELIMITER.  Normally DELIMITER is the last
   character in *LINEPTR on return (besides the null character which
   is always present).  Returns number of characters read, including
   terminating field delimiter if present. */
long
getdelim (char **lineptr, size_t *n, int delimiter, FILE *stream)
{
  /* Number of characters stored in *lineptr so far. */
  size_t len;

  /* Last character read. */
  int c;

  if (*lineptr == NULL || *n < 2)
    {
      *lineptr = xrealloc (*lineptr, 128);
      *n = 128;
    }
  assert (*n > 0);

  len = 0;
  c = getc (stream);
  if (c == EOF)
    return -1;
  while (1)
    {
      if (len + 1 >= *n)
	{
	  *n *= 2;
	  *lineptr = xrealloc (*lineptr, *n);
	}
      (*lineptr)[len++] = c;

      if (c == delimiter)
	break;

      c = getc (stream);
      if (c == EOF)
	break;
    }
  (*lineptr)[len] = '\0';
  return len;
}
