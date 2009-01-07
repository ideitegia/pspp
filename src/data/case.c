/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2007, 2009 Free Software Foundation, Inc.

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

#include <data/case.h>

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

#include <data/value.h>
#include <data/variable.h>
#include <libpspp/str.h>

#include "minmax.h"
#include "xalloc.h"

/* Returns the number of bytes needed by a case with N_VALUES
   values. */
static size_t
case_size (size_t n_values)
{
  return offsetof (struct ccase, values) + n_values * sizeof (union value);
}

/* Returns true if case C contains COUNT cases starting at index
   OFS, false if any of those values are out of range for case
   C. */
static inline bool UNUSED
range_is_valid (const struct ccase *c, size_t ofs, size_t count)
{
  return (count <= c->n_values
          && ofs <= c->n_values
          && ofs + count <= c->n_values);
}

/* Creates and returns a new case that can store N_VALUES values.
   The values have indeterminate contents until explicitly
   written. */
struct ccase *
case_create (size_t n_values)
{
  struct ccase *c = case_try_create (n_values);
  if (c == NULL)
    xalloc_die ();
  return c;
}

/* Like case_create, but returns a null pointer if not enough
   memory is available. */
struct ccase *
case_try_create (size_t n_values)
{
  struct ccase *c = malloc (case_size (n_values));
  if (c)
    {
      c->n_values = n_values;
      c->ref_cnt = 1;
    }
  return c;
}

/* Resizes case C, which must not be shared, to N_VALUES union
   values.  If N_VALUES is greater than the current size of case
   C, then the newly added values have indeterminate content that
   the caller is responsible for initializing.  Returns the new
   case. */
struct ccase *
case_resize (struct ccase *c, size_t n_values)
{
  assert (!case_is_shared (c));
  if (n_values != c->n_values)
    {
      c->n_values = n_values;
      return xrealloc (c, case_size (n_values));
    }
  else
    return c;
}

/* case_unshare_and_resize(C, N) is equivalent to
   case_resize(case_unshare(C), N), but it is faster if case C is
   shared.

   Returns the new case.*/
struct ccase *
case_unshare_and_resize (struct ccase *c, size_t n_values)
{
  if (!case_is_shared (c))
    return case_resize (c, n_values);
  else
    {
      struct ccase *new = case_create (n_values);
      case_copy (new, 0, c, 0, MIN (n_values, c->n_values));
      c->ref_cnt--;
      return new;
    }
}

/* Copies N_VALUES values from SRC (starting at SRC_IDX) to DST
   (starting at DST_IDX).

   DST must not be shared. */
void
case_copy (struct ccase *dst, size_t dst_idx,
           const struct ccase *src, size_t src_idx,
           size_t n_values)
{
  assert (!case_is_shared (dst));
  assert (range_is_valid (dst, dst_idx, n_values));
  assert (range_is_valid (src, dst_idx, n_values));

  if (dst != src || dst_idx != src_idx)
    memmove (dst->values + dst_idx, src->values + src_idx,
             sizeof *dst->values * n_values);
}

/* Copies N_VALUES values out of case C to VALUES, starting at
   the given START_IDX. */
void
case_copy_out (const struct ccase *c,
               size_t start_idx, union value *values, size_t n_values)
{
  assert (range_is_valid (c, start_idx, n_values));
  memcpy (values, c->values + start_idx, n_values * sizeof *values);
}

/* Copies N_VALUES values from VALUES into case C, starting at
   the given START_IDX.

   C must not be shared. */
void
case_copy_in (struct ccase *c,
              size_t start_idx, const union value *values, size_t n_values)
{
  assert (!case_is_shared (c));
  assert (range_is_valid (c, start_idx, n_values));
  memcpy (c->values + start_idx, values, n_values * sizeof *values);
}

/* Returns a pointer to the `union value' used for the
   element of C for variable V.
   Case C must be drawn from V's dictionary.
   The caller must not modify the returned data. */
const union value *
case_data (const struct ccase *c, const struct variable *v)
{
  return case_data_idx (c, var_get_case_index (v));
}

/* Returns a pointer to the `union value' used for the element of
   C numbered IDX.  The caller must not modify the returned
   data. */
const union value *
case_data_idx (const struct ccase *c, size_t idx)
{
  assert (idx < c->n_values);
  return &c->values[idx];
}

/* Returns a pointer to the `union value' used for the element of
   C for variable V.  Case C must be drawn from V's dictionary.
   The caller is allowed to modify the returned data.

   Case C must not be shared. */
union value *
case_data_rw (struct ccase *c, const struct variable *v)
{
  return case_data_rw_idx (c, var_get_case_index (v));
}

/* Returns a pointer to the `union value' used for the
   element of C numbered IDX.
   The caller is allowed to modify the returned data.

   Case C must not be shared. */
union value *
case_data_rw_idx (struct ccase *c, size_t idx)
{
  assert (!case_is_shared (c));
  assert (idx < c->n_values);
  return &c->values[idx];
}

/* Returns the numeric value of the `union value' in C for
   variable V.
   Case C must be drawn from V's dictionary. */
double
case_num (const struct ccase *c, const struct variable *v)
{
  return case_num_idx (c, var_get_case_index (v));
}

/* Returns the numeric value of the `union value' in C numbered
   IDX. */
double
case_num_idx (const struct ccase *c, size_t idx)
{
  assert (idx < c->n_values);
  return c->values[idx].f;
}

/* Returns the string value of the `union value' in C for
   variable V.  Case C must be drawn from V's dictionary.  The
   caller must not modify the return value.

   Like all "union value"s, the return value is not
   null-terminated. */
const char *
case_str (const struct ccase *c, const struct variable *v)
{
  return case_str_idx (c, var_get_case_index (v));
}

/* Returns the string value of the `union value' in C numbered
   IDX.  The caller must not modify the return value.

   Like all "union value"s, the return value is not
   null-terminated. */
const char *
case_str_idx (const struct ccase *c, size_t idx)
{
  assert (idx < c->n_values);
  return c->values[idx].s;
}

/* Compares the values of the N_VARS variables in VP
   in cases A and B and returns a strcmp()-type result. */
int
case_compare (const struct ccase *a, const struct ccase *b,
              const struct variable *const *vp, size_t n_vars)
{
  return case_compare_2dict (a, b, vp, vp, n_vars);
}

/* Compares the values of the N_VARS variables in VAP in case CA
   to the values of the N_VARS variables in VBP in CB
   and returns a strcmp()-type result. */
int
case_compare_2dict (const struct ccase *ca, const struct ccase *cb,
                    const struct variable *const *vap,
                    const struct variable *const *vbp,
                    size_t n_vars)
{
  for (; n_vars-- > 0; vap++, vbp++)
    {
      const struct variable *va = *vap;
      const struct variable *vb = *vbp;

      assert (var_get_width (va) == var_get_width (vb));

      if (var_get_width (va) == 0)
        {
          double af = case_num (ca, va);
          double bf = case_num (cb, vb);

          if (af != bf)
            return af > bf ? 1 : -1;
        }
      else
        {
          const char *as = case_str (ca, va);
          const char *bs = case_str (cb, vb);
          int cmp = memcmp (as, bs, var_get_width (va));

          if (cmp != 0)
            return cmp;
        }
    }
  return 0;
}

/* Returns a pointer to the array of `union value's used for C.
   The caller must *not* modify the returned data.

   This function breaks the case abstraction.  It should *not* be
   commonly used.  Prefer the other case functions. */
const union value *
case_data_all (const struct ccase *c)
{
  return c->values;
}

/* Returns a pointer to the array of `union value's used for C.
   The caller is allowed to modify the returned data.

   Case C must not be shared.

   This function breaks the case abstraction.  It should *not* be
   commonly used.  Prefer the other case functions. */
union value *
case_data_all_rw (struct ccase *c)
{
  assert (!case_is_shared (c));
  return c->values;
}

/* Internal helper function for case_unshare. */
struct ccase *
case_unshare__ (struct ccase *old)
{
  struct ccase *new = case_create (old->n_values);
  memcpy (new->values, old->values, old->n_values * sizeof old->values[0]);
  --old->ref_cnt;
  return new;
}
