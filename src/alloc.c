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

#include <config.h>
#include "alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include "error.h"
#include "str.h"

/* Public functions. */

/* Allocates a block of SIZE bytes and returns it.
   If SIZE is 0, returns a null pointer.
   Aborts if unsuccessful. */
void *
xmalloc (size_t size)
{
  void *vp;
  if (size == 0)
    return NULL;

  vp = malloc (size);
  if (!vp)
    out_of_memory ();

  return vp;
}

/* Allocates a block of SIZE bytes, fill it with all-bits-0, and
   returns it.
   If SIZE is 0, returns a null pointer.
   Aborts if unsuccessful. */
void *
xcalloc (size_t size)
{
  void *vp = xmalloc (size);
  memset (vp, 0, size);
  return vp;
}

/* If SIZE is 0, then block PTR is freed and a null pointer is
   returned.
   Otherwise, if PTR is a null pointer, then a new block is allocated
   and returned.
   Otherwise, block PTR is reallocated to be SIZE bytes in size and
   the new location of the block is returned.
   Aborts if unsuccessful. */
void *
xrealloc (void *ptr, size_t size)
{
  void *vp;
  if (!size)
    {
      if (ptr)
	free (ptr);

      return NULL;
    }

  if (ptr)
    vp = realloc (ptr, size);
  else
    vp = malloc (size);

  if (!vp)
    out_of_memory ();

  return vp;
}

/* Makes a copy of string S in malloc()'d memory and returns the copy.
   S must not be a null pointer. */
char *
xstrdup (const char *s)
{
  size_t size;
  char *t;

  assert (s != NULL);

  size = strlen (s) + 1;

  t = malloc (size);
  if (!t)
    out_of_memory ();

  memcpy (t, s, size);
  return t;
}

/* Report an out-of-memory condition and abort execution. */
void
out_of_memory (void)
{
  fprintf (stderr, "virtual memory exhausted\n");
  exit (EXIT_FAILURE);
}
