/* PSPP - a program for statistical analysis.
   Copyright (C) 2005 Free Software Foundation, Inc.

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
#include "missing-values.h"
#include <assert.h>
#include <stdlib.h>
#include <libpspp/assertion.h>
#include "variable.h"
#include <libpspp/str.h>

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
   the given WIDTH.  Although only numeric variables and short
   string variables may have missing values, WIDTH may be any
   valid variable width. */
void
mv_init (struct missing_values *mv, int width)
{
  assert (width >= 0 && width <= MAX_STRING);
  mv->type = MVT_NONE;
  mv->width = width;
}

/* Removes any missing values from MV. */
void
mv_clear (struct missing_values *mv)
{
  mv->type = MVT_NONE;
}

/* Copies SRC to MV. */
void
mv_copy (struct missing_values *mv, const struct missing_values *src)
{
  assert(src);

  *mv = *src;
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
   more room for missing values.  (Long string variables never
   accept missing values.) */
bool
mv_add_value (struct missing_values *mv, const union value *v)
{
  if (mv->width > MAX_SHORT_STRING)
    return false;
  switch (mv->type)
    {
    case MVT_NONE:
    case MVT_1:
    case MVT_2:
    case MVT_RANGE:
      mv->values[mv->type & 3] = *v;
      mv->type++;
      return true;

    case MVT_3:
    case MVT_RANGE_1:
      return false;
    }
  NOT_REACHED ();
}

/* Attempts to add S to the set of string missing values MV.  S
   must contain exactly as many characters as MV's width.
   Returns true if successful, false if MV has no more room for
   missing values.  (Long string variables never accept missing
   values.) */
bool
mv_add_str (struct missing_values *mv, const char s[])
{
  assert (mv->width > 0);
  return mv_add_value (mv, (union value *) s);
}

/* Attempts to add D to the set of numeric missing values MV.
   Returns true if successful, false if MV has no more room for
   missing values.  */
bool
mv_add_num (struct missing_values *mv, double d)
{
  assert (mv->width == 0);
  return mv_add_value (mv, (union value *) &d);
}

/* Attempts to add range [LOW, HIGH] to the set of numeric
   missing values MV.  Returns true if successful, false if MV
   has no room for a range, or if LOW > HIGH. */
bool
mv_add_num_range (struct missing_values *mv, double low, double high)
{
  assert (mv->width == 0);
  if (low > high)
    return false;
  switch (mv->type)
    {
    case MVT_NONE:
    case MVT_1:
      mv->values[1].f = low;
      mv->values[2].f = high;
      mv->type |= 4;
      return true;

    case MVT_2:
    case MVT_3:
    case MVT_RANGE:
    case MVT_RANGE_1:
      return false;
    }
  NOT_REACHED ();
}

/* Returns true if MV contains an individual value,
   false if MV is empty (or contains only a range). */
bool
mv_has_value (const struct missing_values *mv)
{
  switch (mv->type)
    {
    case MVT_1:
    case MVT_2:
    case MVT_3:
    case MVT_RANGE_1:
      return true;

    case MVT_NONE:
    case MVT_RANGE:
      return false;
    }
  NOT_REACHED ();
}

/* Removes one individual value from MV and stores it in *V.
   MV must contain an individual value (as determined by
   mv_has_value()). */
void
mv_pop_value (struct missing_values *mv, union value *v)
{
  assert (mv_has_value (mv));
  mv->type--;
  *v = mv->values[mv->type & 3];
}

/* Stores  a value  in *V.
   MV must contain an individual value (as determined by
   mv_has_value()).
   IDX is the zero based index of the value to get
*/
void
mv_peek_value (const struct missing_values *mv, union value *v, int idx)
{
  assert (idx >= 0 ) ;
  assert (idx < 3);

  assert (mv_has_value (mv));
  *v = mv->values[idx];
}

void
mv_replace_value (struct missing_values *mv, const union value *v, int idx)
{
  assert (idx >= 0) ;
  assert (idx < mv_n_values(mv));

  mv->values[idx] = *v;
}



int
mv_n_values (const struct missing_values *mv)
{
  assert(mv_has_value(mv));
  return mv->type & 3;
}


/* Returns true if MV contains a numeric range,
   false if MV is empty (or contains only individual values). */
bool
mv_has_range (const struct missing_values *mv)
{
  switch (mv->type)
    {
    case MVT_RANGE:
    case MVT_RANGE_1:
      return true;

    case MVT_NONE:
    case MVT_1:
    case MVT_2:
    case MVT_3:
      return false;
    }
  NOT_REACHED ();
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
mv_peek_range (const struct missing_values *mv, double *low, double *high)
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

/* Returns true if S contains only spaces between indexes
   NEW_WIDTH (inclusive) and OLD_WIDTH (exclusive),
   false otherwise. */
static bool
can_resize_string (const char *s, int old_width, int new_width)
{
  int i;

  assert (new_width < old_width);
  for (i = new_width; i < old_width; i++)
    if (s[i] != ' ')
      return false;
  return true;
}

/* Returns true if MV can be resized to the given WIDTH with
   mv_resize(), false otherwise.  Resizing to the same width is
   always possible.  Resizing to a long string WIDTH is only
   possible if MV is an empty set of missing values; otherwise,
   resizing to a larger WIDTH is always possible.  Resizing to a
   shorter width is possible only when each missing value
   contains only spaces in the characters that will be
   trimmed. */
bool
mv_is_resizable (const struct missing_values *mv, int width)
{
  if ( var_type_from_width (width) != var_type_from_width (mv->width) )
    return false;

  if (width > MAX_SHORT_STRING && mv->type != MVT_NONE)
    return false;

  if (width >= mv->width)
    return true;
  else
    {
      int i;

      for (i = 0; i < 3; i++)
        if (using_element (mv->type, i)
            && !can_resize_string (mv->values[i].s, mv->width, width))
          return false;
      return true;
    }
}

/* Resizes MV to the given WIDTH.  WIDTH must fit the constraints
   explained for mv_is_resizable(). */
void
mv_resize (struct missing_values *mv, int width)
{
  assert (mv_is_resizable (mv, width));
  if (width > mv->width && mv->type != MVT_NONE)
    {
      int i;

      for (i = 0; i < 3; i++)
        memset (mv->values[i].s + mv->width, ' ', width - mv->width);
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
is_str_user_missing (const struct missing_values *mv,
                        const char s[])
{
  const union value *v = mv->values;
  assert (mv->width > 0);
  switch (mv->type)
    {
    case MVT_NONE:
      return false;
    case MVT_1:
      return !memcmp (v[0].s, s, mv->width);
    case MVT_2:
      return (!memcmp (v[0].s, s, mv->width)
              || !memcmp (v[1].s, s, mv->width));
    case MVT_3:
      return (!memcmp (v[0].s, s, mv->width)
              || !memcmp (v[1].s, s, mv->width)
              || !memcmp (v[2].s, s, mv->width));
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
          : mv_is_str_missing (mv, v->s, class));
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
mv_is_str_missing (const struct missing_values *mv, const char s[],
                   enum mv_class class)
{
  assert (mv->width > 0);
  return class & MV_USER && is_str_user_missing (mv, s);
}
