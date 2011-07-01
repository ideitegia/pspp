/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2007, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "data/case.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

#include "data/value.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/str.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

/* Set this flag to 1 to copy cases instead of ref counting them.
   This is sometimes helpful in debugging situations. */
#define DEBUG_CASEREFS 0

#if DEBUG_CASEREFS
#warning "Caseref debug enabled.  CASES ARE NOT BEING SHARED!!"
#endif

static size_t case_size (const struct caseproto *);
static bool variable_matches_case (const struct ccase *,
                                   const struct variable *);
static void copy_forward (struct ccase *dst, size_t dst_idx,
                          const struct ccase *src, size_t src_idx,
                          size_t n_values);
static void copy_backward (struct ccase *dst, size_t dst_idx,
                           const struct ccase *src, size_t src_idx,
                           size_t n_values);

/* Creates and returns a new case that stores data of the form
   specified by PROTO.  The data in the case have indeterminate
   contents until explicitly written.

   The caller retains ownership of PROTO. */
struct ccase *
case_create (const struct caseproto *proto)
{
  struct ccase *c = case_try_create (proto);
  if (c == NULL)
    xalloc_die ();
  return c;
}

/* Like case_create, but returns a null pointer if not enough
   memory is available. */
struct ccase *
case_try_create (const struct caseproto *proto)
{
  struct ccase *c = malloc (case_size (proto));
  if (c != NULL)
    {
      if (caseproto_try_init_values (proto, c->values))
        {
          c->proto = caseproto_ref (proto);
          c->ref_cnt = 1;
          return c;
        }
      free (c);
    }
  return NULL;
}

/* Creates and returns an unshared copy of case C. */
struct ccase *
case_clone (const struct ccase *c)
{
  return case_unshare (case_ref (c));
}

/* Increments case C's reference count and returns C.  Afterward,
   case C is shared among its reference count holders. */
struct ccase *
case_ref (const struct ccase *c_)
{
  struct ccase *c = CONST_CAST (struct ccase *, c_);
  c->ref_cnt++;
#if DEBUG_CASEREFS
  c = case_unshare__ (c);
#endif
  return c;
}

/* Returns an estimate of the number of bytes of memory that
   would be consumed in creating a case based on PROTO.  The
   estimate includes typical overhead from malloc() in addition
   to the actual size of data. */
size_t
case_get_cost (const struct caseproto *proto)
{
  /* FIXME: improve approximation? */
  return (1 + caseproto_get_n_widths (proto)
          + 3 * caseproto_get_n_long_strings (proto)) * sizeof (union value);
}

/* Changes the prototype for case C, which must not be shared.
   The new PROTO must be conformable with C's current prototype
   (as defined by caseproto_is_conformable).

   Any new values created by this function have indeterminate
   content that the caller is responsible for initializing.

   The caller retains ownership of PROTO.

   Returns a new case that replaces C, which is freed. */
struct ccase *
case_resize (struct ccase *c, const struct caseproto *new_proto)
{
  struct caseproto *old_proto = c->proto;
  size_t old_n_widths = caseproto_get_n_widths (old_proto);
  size_t new_n_widths = caseproto_get_n_widths (new_proto);

  assert (!case_is_shared (c));
  expensive_assert (caseproto_is_conformable (old_proto, new_proto));

  if (old_n_widths != new_n_widths)
    {
      if (new_n_widths < old_n_widths)
        caseproto_reinit_values (old_proto, new_proto, c->values);
      c = xrealloc (c, case_size (new_proto));
      if (new_n_widths > old_n_widths)
        caseproto_reinit_values (old_proto, new_proto, c->values);

      caseproto_unref (old_proto);
      c->proto = caseproto_ref (new_proto);
    }

  return c;
}

/* case_unshare_and_resize(C, PROTO) is equivalent to
   case_resize(case_unshare(C), PROTO), but it is faster if case
   C is shared.

   Any new values created by this function have indeterminate
   content that the caller is responsible for initializing.

   The caller retains ownership of PROTO.

   Returns the new case that replaces C, which is freed. */
struct ccase *
case_unshare_and_resize (struct ccase *c, const struct caseproto *proto)
{
  if (!case_is_shared (c))
    return case_resize (c, proto);
  else
    {
      struct ccase *new = case_create (proto);
      size_t old_n_values = caseproto_get_n_widths (c->proto);
      size_t new_n_values = caseproto_get_n_widths (proto);
      case_copy (new, 0, c, 0, MIN (old_n_values, new_n_values));
      c->ref_cnt--;
      return new;
    }
}

/* Sets all of the numeric values in case C to the system-missing
   value, and all of the string values to spaces. */
void
case_set_missing (struct ccase *c)
{
  size_t i;

  assert (!case_is_shared (c));
  for (i = 0; i < caseproto_get_n_widths (c->proto); i++)
    value_set_missing (&c->values[i], caseproto_get_width (c->proto, i));
}

/* Copies N_VALUES values from SRC (starting at SRC_IDX) to DST
   (starting at DST_IDX).  Each value that is copied into must
   have the same width as the value that it is copied from.

   Properly handles overlapping ranges when DST == SRC.

   DST must not be shared. */
void
case_copy (struct ccase *dst, size_t dst_idx,
           const struct ccase *src, size_t src_idx,
           size_t n_values)
{
  assert (!case_is_shared (dst));
  assert (caseproto_range_is_valid (dst->proto, dst_idx, n_values));
  assert (caseproto_range_is_valid (src->proto, src_idx, n_values));
  assert (caseproto_equal (dst->proto, dst_idx, src->proto, src_idx,
                           n_values));

  if (dst != src)
    {
      if (!dst->proto->n_long_strings || !src->proto->n_long_strings)
        memcpy (&dst->values[dst_idx], &src->values[src_idx],
                sizeof dst->values[0] * n_values);
      else
        copy_forward (dst, dst_idx, src, src_idx, n_values);
    }
  else if (dst_idx != src_idx)
    {
      if (!dst->proto->n_long_strings)
        memmove (&dst->values[dst_idx], &src->values[src_idx],
                 sizeof dst->values[0] * n_values);
      else if (dst_idx < src_idx)
        copy_forward (dst, dst_idx, src, src_idx, n_values);
      else /* dst_idx > src_idx */
        copy_backward (dst, dst_idx, src, src_idx, n_values);
    }
}

/* Copies N_VALUES values out of case C to VALUES, starting at
   the given START_IDX. */
void
case_copy_out (const struct ccase *c,
               size_t start_idx, union value *values, size_t n_values)
{
  size_t i;

  assert (caseproto_range_is_valid (c->proto, start_idx, n_values));

  for (i = 0; i < n_values; i++)
    value_copy (&values[i], &c->values[start_idx + i],
                caseproto_get_width (c->proto, start_idx + i));
}

/* Copies N_VALUES values from VALUES into case C, starting at
   the given START_IDX.

   C must not be shared. */
void
case_copy_in (struct ccase *c,
              size_t start_idx, const union value *values, size_t n_values)
{
  size_t i;

  assert (!case_is_shared (c));
  assert (caseproto_range_is_valid (c->proto, start_idx, n_values));

  for (i = 0; i < n_values; i++)
    value_copy (&c->values[start_idx + i], &values[i],
                caseproto_get_width (c->proto, start_idx + i));
}

/* Returns a pointer to the `union value' used for the
   element of C for variable V.
   Case C must be drawn from V's dictionary.
   The caller must not modify the returned data. */
const union value *
case_data (const struct ccase *c, const struct variable *v)
{
  assert (variable_matches_case (c, v));
  return &c->values[var_get_case_index (v)];
}

/* Returns a pointer to the `union value' used for the element of
   C numbered IDX.  The caller must not modify the returned
   data. */
const union value *
case_data_idx (const struct ccase *c, size_t idx)
{
  assert (idx < c->proto->n_widths);
  return &c->values[idx];
}

/* Returns a pointer to the `union value' used for the element of
   C for variable V.  Case C must be drawn from V's dictionary.
   The caller is allowed to modify the returned data.

   Case C must not be shared. */
union value *
case_data_rw (struct ccase *c, const struct variable *v)
{
  assert (variable_matches_case (c, v));
  assert (!case_is_shared (c));
  return &c->values[var_get_case_index (v)];
}

/* Returns a pointer to the `union value' used for the
   element of C numbered IDX.
   The caller is allowed to modify the returned data.

   Case C must not be shared. */
union value *
case_data_rw_idx (struct ccase *c, size_t idx)
{
  assert (idx < c->proto->n_widths);
  assert (!case_is_shared (c));
  return &c->values[idx];
}

/* Returns the numeric value of the `union value' in C for
   variable V.
   Case C must be drawn from V's dictionary. */
double
case_num (const struct ccase *c, const struct variable *v)
{
  assert (variable_matches_case (c, v));
  return c->values[var_get_case_index (v)].f;
}

/* Returns the numeric value of the `union value' in C numbered
   IDX. */
double
case_num_idx (const struct ccase *c, size_t idx)
{
  assert (idx < c->proto->n_widths);
  return c->values[idx].f;
}

/* Returns the string value of the `union value' in C for
   variable V.  Case C must be drawn from V's dictionary.  The
   caller must not modify the return value.

   Like the strings embedded in all "union value"s, the return
   value is not null-terminated. */
const uint8_t *
case_str (const struct ccase *c, const struct variable *v)
{
  size_t idx = var_get_case_index (v);
  assert (variable_matches_case (c, v));
  return value_str (&c->values[idx], caseproto_get_width (c->proto, idx));
}

/* Returns the string value of the `union value' in C numbered
   IDX.  The caller must not modify the return value.

   Like the strings embedded in all "union value"s, the return
   value is not null-terminated. */
const uint8_t *
case_str_idx (const struct ccase *c, size_t idx)
{
  assert (idx < c->proto->n_widths);
  return value_str (&c->values[idx], caseproto_get_width (c->proto, idx));
}

/* Returns the string value of the `union value' in C for
   variable V.  Case C must be drawn from V's dictionary.  The
   caller may modify the return value.

   Case C must not be shared.

   Like the strings embedded in all "union value"s, the return
   value is not null-terminated. */
uint8_t *
case_str_rw (struct ccase *c, const struct variable *v)
{
  size_t idx = var_get_case_index (v);
  assert (variable_matches_case (c, v));
  assert (!case_is_shared (c));
  return value_str_rw (&c->values[idx], caseproto_get_width (c->proto, idx));
}

/* Returns the string value of the `union value' in C numbered
   IDX.  The caller may modify the return value.

   Case C must not be shared.

   Like the strings embedded in all "union value"s, the return
   value is not null-terminated. */
uint8_t *
case_str_rw_idx (struct ccase *c, size_t idx)
{
  assert (idx < c->proto->n_widths);
  assert (!case_is_shared (c));
  return value_str_rw (&c->values[idx], caseproto_get_width (c->proto, idx));
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
  int cmp = 0;
  for (; !cmp && n_vars-- > 0; vap++, vbp++)
    {
      const union value *va = case_data (ca, *vap);
      const union value *vb = case_data (cb, *vbp);
      assert (var_get_width (*vap) == var_get_width (*vbp));
      cmp = value_compare_3way (va, vb, var_get_width (*vap)); 
    }
  return cmp;
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
  struct ccase *new = case_create (old->proto);
  case_copy (new, 0, old, 0, caseproto_get_n_widths (new->proto));
  --old->ref_cnt;
  return new;
}

/* Internal helper function for case_unref. */
void
case_unref__ (struct ccase *c)
{
  caseproto_destroy_values (c->proto, c->values);
  caseproto_unref (c->proto);
  free (c);
}

/* Returns the number of bytes needed by a case for case
   prototype PROTO. */
static size_t
case_size (const struct caseproto *proto)
{
  return (offsetof (struct ccase, values)
          + caseproto_get_n_widths (proto) * sizeof (union value));
}

/* Returns true if C contains a value at V's case index with the
   same width as V; that is, if V may plausibly be used to read
   or write data in C.

   Useful in assertions. */
static bool UNUSED
variable_matches_case (const struct ccase *c, const struct variable *v)
{
  size_t case_idx = var_get_case_index (v);
  return (case_idx < caseproto_get_n_widths (c->proto)
          && caseproto_get_width (c->proto, case_idx) == var_get_width (v));
}

/* Internal helper function for case_copy(). */
static void
copy_forward (struct ccase *dst, size_t dst_idx,
              const struct ccase *src, size_t src_idx,
              size_t n_values)
{
  size_t i;

  for (i = 0; i < n_values; i++)
    value_copy (&dst->values[dst_idx + i], &src->values[src_idx + i],
                caseproto_get_width (dst->proto, dst_idx + i));
}

/* Internal helper function for case_copy(). */
static void
copy_backward (struct ccase *dst, size_t dst_idx,
               const struct ccase *src, size_t src_idx,
               size_t n_values)
{
  size_t i;

  for (i = n_values; i-- != 0; )
    value_copy (&dst->values[dst_idx + i], &src->values[src_idx + i],
                caseproto_get_width (dst->proto, dst_idx + i));
}

