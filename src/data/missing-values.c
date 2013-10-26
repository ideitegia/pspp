/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2009, 2011, 2013 Free Software Foundation, Inc.

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

#include "data/missing-values.h"

#include <assert.h>
#include <stdlib.h>

#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/str.h"

/* Types of user-missing values.
   Invisible--use access functions defined below instead. */
enum mv_type
  {
    MVT_NONE = 0,                /* No user-missing values. */
    MVT_1 = 1,                   /* One user-missing value. */
    MVT_2 = 2,                   /* Two user-missing values. */
    MVT_3 = 3,                   /* Three user-missing values. */
    MVT_RANGE = 4,               /* A range of user-missing values. */
    MVT_RANGE_1 = 5              /* A range plus an individual value. */
  };

/* Initializes MV as a set of missing values for a variable of
   the given WIDTH.  MV should be destroyed with mv_destroy when
   it is no longer needed. */
void
mv_init (struct missing_values *mv, int width)
{
  int i;

  assert (width >= 0 && width <= MAX_STRING);
  mv->type = MVT_NONE;
  mv->width = width;
  for (i = 0; i < 3; i++)
    value_init (&mv->values[i], width);
}

/* Initializes MV as a set of missing values for a variable of
   the given WIDTH.  MV will be automatically destroyed along
   with POOL; it must not be passed to mv_destroy for explicit
   destruction. */
void
mv_init_pool (struct pool *pool, struct missing_values *mv, int width)
{
  int i;

  assert (width >= 0 && width <= MAX_STRING);
  mv->type = MVT_NONE;
  mv->width = width;
  for (i = 0; i < 3; i++)
    value_init_pool (pool, &mv->values[i], width);
}

/* Frees any storage allocated by mv_init for MV. */
void
mv_destroy (struct missing_values *mv)
{
  if (mv != NULL)
    {
      int i;

      for (i = 0; i < 3; i++)
        value_destroy (&mv->values[i], mv->width);
    }
}

/* Removes any missing values from MV. */
void
mv_clear (struct missing_values *mv)
{
  mv->type = MVT_NONE;
}

/* Initializes MV as a copy of SRC. */
void
mv_copy (struct missing_values *mv, const struct missing_values *src)
{
  int i;

  mv_init (mv, src->width);
  mv->type = src->type;
  for (i = 0; i < 3; i++)
    value_copy (&mv->values[i], &src->values[i], mv->width);
}

/* Returns true if VALUE, of the given WIDTH, may be added to a
   missing value set also of the given WIDTH.  This is normally
   the case, but string missing values over MV_MAX_STRING bytes
   long must consist solely of spaces after the first
   MV_MAX_STRING bytes.  */
bool
mv_is_acceptable (const union value *value, int width)
{
  int i;

  for (i = MV_MAX_STRING; i < width; i++)
    if (value_str (value, width)[i] != ' ')
      return false;
  return true;
}

/* Returns true if MV is an empty set of missing values. */
bool
mv_is_empty (const struct missing_values *mv)
{
  return mv->type == MVT_NONE;
}

/* Returns the width of the missing values that MV may
   contain. */
int
mv_get_width (const struct missing_values *mv)
{
  return mv->width;
}

/* Attempts to add individual value V to the set of missing
   values MV.  Returns true if successful, false if MV has no
   more room for missing values or if V is not an acceptable
   missing value. */
bool
mv_add_value (struct missing_values *mv, const union value *v)
{
  if (!mv_is_acceptable (v, mv->width))
    return false;

  switch (mv->type)
    {
    case MVT_NONE:
    case MVT_1:
    case MVT_2:
    case MVT_RANGE:
      value_copy (&mv->values[mv->type & 3], v, mv->width);
      mv->type++;
      return true;

    case MVT_3:
    case MVT_RANGE_1:
      return false;
    }
  NOT_REACHED ();
}

/* Attempts to add S, which is LEN bytes long, to the set of string missing
   values MV.  Returns true if successful, false if MV has no more room for
   missing values or if S is not an acceptable missing value. */
bool
mv_add_str (struct missing_values *mv, const uint8_t s[], size_t len)
{
  union value v;
  bool ok;

  assert (mv->width > 0);
  while (len > mv->width)
    if (s[--len] != ' ')
      return false;

  value_init (&v, mv->width);
  buf_copy_rpad (CHAR_CAST (char *, value_str_rw (&v, mv->width)), mv->width,
                 CHAR_CAST (char *, s), len, ' ');
  ok = mv_add_value (mv, &v);
  value_destroy (&v, mv->width);

  return ok;
}

/* Attempts to add D to the set of numeric missing values MV.
   Returns true if successful, false if MV has no more room for
   missing values.  */
bool
mv_add_num (struct missing_values *mv, double d)
{
  union value v;
  bool ok;

  assert (mv->width == 0);
  value_init (&v, 0);
  v.f = d;
  ok = mv_add_value (mv, &v);
  value_destroy (&v, 0);

  return ok;
}

/* Attempts to add range [LOW, HIGH] to the set of numeric
   missing values MV.  Returns true if successful, false if MV
   has no room for a range, or if LOW > HIGH. */
bool
mv_add_range (struct missing_values *mv, double low, double high)
{
  assert (mv->width == 0);
  if (low <= high && (mv->type == MVT_NONE || mv->type == MVT_1))
    {
      mv->values[1].f = low;
      mv->values[2].f = high;
      mv->type |= 4;
      return true;
    }
  else
    return false;
}

/* Returns true if MV contains an individual value,
   false if MV is empty (or contains only a range). */
bool
mv_has_value (const struct missing_values *mv)
{
  return mv_n_values (mv) > 0;
}

/* Removes one individual value from MV and stores it in V, which
   must have been initialized as a value with the same width as MV.
   MV must contain an individual value (as determined by
   mv_has_value()).

   We remove the first value from MV, not the last, because the
   common use for this function is in iterating through a set of
   missing values.  If we remove the last value then we'll output
   the missing values in order opposite of that in which they
   were added, so that a GET followed by a SAVE would reverse the
   order of missing values in the system file, a weird effect. */
void
mv_pop_value (struct missing_values *mv, union value *v)
{
  union value tmp;

  assert (mv_has_value (mv));

  value_copy (v, &mv->values[0], mv->width);
  tmp = mv->values[0];
  mv->values[0] = mv->values[1];
  mv->values[1] = mv->values[2];
  mv->values[2] = tmp;
  mv->type--;
}

/* Returns MV's discrete value with index IDX.  The caller must
   not modify or free this value, or access it after MV is
   modified or freed.
   IDX must be less than the number of discrete values in MV, as
   reported by mv_n_values. */
const union value *
mv_get_value (const struct missing_values *mv, int idx)
{
  assert (idx >= 0 && idx < mv_n_values (mv));
  return &mv->values[idx];
}

/* Replaces MV's discrete value with index IDX by a copy of V,
   which must have the same width as MV.
   IDX must be less than the number of discrete values in MV, as
   reported by mv_n_values. */
bool
mv_replace_value (struct missing_values *mv, const union value *v, int idx)
{
  assert (idx >= 0) ;
  assert (idx < mv_n_values(mv));

  if (!mv_is_acceptable (v, mv->width))
    return false;

  value_copy (&mv->values[idx], v, mv->width);
  return true;
}

/* Returns the number of individual (not part of a range) missing
   values in MV. */
int
mv_n_values (const struct missing_values *mv)
{
  return mv->type & 3;
}


/* Returns true if MV contains a numeric range,
   false if MV is empty (or contains only individual values). */
bool
mv_has_range (const struct missing_values *mv)
{
  return mv->type == MVT_RANGE || mv->type == MVT_RANGE_1;
}

/* Removes the numeric range from MV and stores it in *LOW and
   *HIGH.  MV must contain a individual range (as determined by
   mv_has_range()). */
void
mv_pop_range (struct missing_values *mv, double *low, double *high)
{
  assert (mv_has_range (mv));
  *low = mv->values[1].f;
  *high = mv->values[2].f;
  mv->type &= 3;
}

/* Returns the numeric range from MV  into *LOW and
   *HIGH.  MV must contain a individual range (as determined by
   mv_has_range()). */
void
mv_get_range (const struct missing_values *mv, double *low, double *high)
{
  assert (mv_has_range (mv));
  *low = mv->values[1].f;
  *high = mv->values[2].f;
}

/* Returns true if values[IDX] is in use when the `type' member
   is set to TYPE (in struct missing_values),
   false otherwise. */
static bool
using_element (unsigned type, int idx)
{
  assert (idx >= 0 && idx < 3);

  switch (type)
    {
    case MVT_NONE:
      return false;
    case MVT_1:
      return idx < 1;
    case MVT_2:
      return idx < 2;
    case MVT_3:
      return true;
    case MVT_RANGE:
      return idx > 0;
    case MVT_RANGE_1:
      return true;
    }
  NOT_REACHED ();
}

/* Returns true if MV can be resized to the given WIDTH with
   mv_resize(), false otherwise.  Resizing is possible only when
   each value in MV (if any) is resizable from MV's current width
   to WIDTH, as determined by value_is_resizable. */
bool
mv_is_resizable (const struct missing_values *mv, int width)
{
  int i;

  for (i = 0; i < 3; i++)
    if (using_element (mv->type, i)
        && !value_is_resizable (&mv->values[i], mv->width, width))
      return false;

  return true;
}

/* Resizes MV to the given WIDTH.  WIDTH must fit the constraints
   explained for mv_is_resizable. */
void
mv_resize (struct missing_values *mv, int width)
{
  int i;

  assert (mv_is_resizable (mv, width));
  for (i = 0; i < 3; i++)
    if (using_element (mv->type, i))
      value_resize (&mv->values[i], mv->width, width);
    else
      {
        value_destroy (&mv->values[i], mv->width);
        value_init (&mv->values[i], width);
      }
  mv->width = width;
}

/* Returns true if D is a missing value in MV, false otherwise.
   MV must be a set of numeric missing values. */
static bool
is_num_user_missing (const struct missing_values *mv, double d)
{
  const union value *v = mv->values;
  assert (mv->width == 0);
  switch (mv->type)
    {
    case MVT_NONE:
      return false;
    case MVT_1:
      return v[0].f == d;
    case MVT_2:
      return v[0].f == d || v[1].f == d;
    case MVT_3:
      return v[0].f == d || v[1].f == d || v[2].f == d;
    case MVT_RANGE:
      return v[1].f <= d && d <= v[2].f;
    case MVT_RANGE_1:
      return v[0].f == d || (v[1].f <= d && d <= v[2].f);
    }
  NOT_REACHED ();
}

/* Returns true if S[] is a missing value in MV, false otherwise.
   MV must be a set of string missing values.
   S[] must contain exactly as many characters as MV's width. */
static bool
is_str_user_missing (const struct missing_values *mv, const uint8_t s[])
{
  const union value *v = mv->values;
  assert (mv->width > 0);
  switch (mv->type)
    {
    case MVT_NONE:
      return false;
    case MVT_1:
      return !memcmp (value_str (&v[0], mv->width), s, mv->width);
    case MVT_2:
      return (!memcmp (value_str (&v[0], mv->width), s, mv->width)
              || !memcmp (value_str (&v[1], mv->width), s, mv->width));
    case MVT_3:
      return (!memcmp (value_str (&v[0], mv->width), s, mv->width)
              || !memcmp (value_str (&v[1], mv->width), s, mv->width)
              || !memcmp (value_str (&v[2], mv->width), s, mv->width));
    case MVT_RANGE:
    case MVT_RANGE_1:
      NOT_REACHED ();
    }
  NOT_REACHED ();
}

/* Returns true if V is a missing value in the given CLASS in MV,
   false otherwise. */
bool
mv_is_value_missing (const struct missing_values *mv, const union value *v,
                     enum mv_class class)
{
  return (mv->width == 0
          ? mv_is_num_missing (mv, v->f, class)
          : mv_is_str_missing (mv, value_str (v, mv->width), class));
}

/* Returns true if D is a missing value in the given CLASS in MV,
   false otherwise.
   MV must be a set of numeric missing values. */
bool
mv_is_num_missing (const struct missing_values *mv, double d,
                   enum mv_class class)
{
  assert (mv->width == 0);
  return ((class & MV_SYSTEM && d == SYSMIS)
          || (class & MV_USER && is_num_user_missing (mv, d)));
}

/* Returns true if S[] is a missing value in the given CLASS in
   MV, false otherwise.
   MV must be a set of string missing values.
   S[] must contain exactly as many characters as MV's width. */
bool
mv_is_str_missing (const struct missing_values *mv, const uint8_t s[],
                   enum mv_class class)
{
  assert (mv->width > 0);
  return class & MV_USER && is_str_user_missing (mv, s);
}
