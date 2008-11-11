/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#include <config.h>
#include <data/value.h>

#include <data/val-type.h>
#include <libpspp/hash.h>
#include <libpspp/str.h>
#include "variable.h"

#include "xalloc.h"

/* Duplicate a value.
   The caller is responsible for freeing the returned value. */
union value *
value_dup (const union value *val, int width)
{
  return xmemdup (val, MAX (width, sizeof *val));
}


/* Create a value of specified width.
   The caller is responsible for freeing the returned value. */
union value *
value_create (int width)
{
  return xnmalloc (value_cnt_from_width (width), sizeof (union value));
}


/* Compares A and B, which both have the given WIDTH, and returns
   a strcmp()-type result.
   Only the short string portion of longer strings are
   compared. */
int
compare_values (const void *a_, const void *b_, const void *var_)
{
  const union value *a = a_;
  const union value *b = b_;
  const struct variable *var = var_;
  int width = var_get_width (var);
  return (width == 0
          ? (a->f < b->f ? -1 : a->f > b->f)
          : memcmp (a->s, b->s, MIN (MAX_SHORT_STRING, width)));
}

/* Create a hash of V, which has the given WIDTH.
   Only the short string portion of a longer string is hashed. */
unsigned
hash_value (const void *v_, const void *var_)
{
  const union value *v = v_;
  const struct variable *var = var_;
  int width = var_get_width (var);
  return (width == 0
          ? hsh_hash_double (v->f)
	  : hsh_hash_bytes (v->s, width));
}


/* Copies SRC to DST, given that they both contain data of the
   given WIDTH. */
void
value_copy (union value *dst, const union value *src, int width)
{
  if (width == 0)
    dst->f = src->f;
  else
    memcpy (dst->s, src->s, width);
}

/* Sets V to the system-missing value for data of the given
   WIDTH. */
void
value_set_missing (union value *v, int width)
{
  if (width == 0)
    v->f = SYSMIS;
  else
    memset (v->s, ' ', width);
}

/* Tests whether VALUE may be resized from OLD_WIDTH to
   NEW_WIDTH, using the following rules that match those for
   resizing missing values and value labels.  First, OLD_WIDTH
   and NEW_WIDTH must be both numeric or both string.  Second, if
   NEW_WIDTH is less than OLD_WIDTH, then the bytes that would be
   trimmed off the right end of VALUE must be all spaces. */
bool
value_is_resizable (const union value *value, int old_width, int new_width)
{
  int i;

  if (val_type_from_width (old_width) != val_type_from_width (new_width))
    return false;
  for (i = new_width; i < old_width; i++)
    if (value->s[i] != ' ')
      return false;
  return true;
}

/* Resizes VALUE from OLD_WIDTH to NEW_WIDTH.  The arguments must
   satisfy the rules specified above for value_is_resizable. */
void
value_resize (union value *value, int old_width, int new_width)
{
  assert (value_is_resizable (value, old_width, new_width));
  if (new_width > old_width)
    memset (&value->s[old_width], ' ', new_width - old_width);
}
