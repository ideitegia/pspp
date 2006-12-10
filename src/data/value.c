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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include "value.h"

#include <libpspp/hash.h>
#include <libpspp/str.h>

#include "xalloc.h"

/* Duplicate a value.
   The caller is responsible for freeing the returned value. */
union value *
value_dup (const union value *val, int width)
{
  return xmemdup (val, MAX (width, sizeof *val));
}

/* Compares A and B, which both have the given WIDTH, and returns
   a strcmp()-type result.
   Only the short string portion of longer strings are
   compared. */
int
compare_values (const union value *a, const union value *b, int width) 
{
  return (width == 0
          ? (a->f < b->f ? -1 : a->f > b->f)
          : memcmp (a->s, b->s, MIN (MAX_SHORT_STRING, width)));
}

/* Create a hash of V, which has the given WIDTH.
   Only the short string portion of a longer string is hashed. */
unsigned 
hash_value (const union value *v, int width)
{
  return (width == 0
          ? hsh_hash_double (v->f)
          : hsh_hash_bytes (v->s, MIN (MAX_SHORT_STRING, width)));
}
