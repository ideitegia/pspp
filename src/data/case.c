/* PSPP - computes sample statistics.
   Copyright (C) 2004, 2007 Free Software Foundation, Inc.

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

#include <data/case.h>

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

#include <data/value.h>
#include <data/variable.h>
#include <libpspp/alloc.h>
#include <libpspp/str.h>

#include "minmax.h"

/* Reference-counted case implementation. */
struct case_data
  {
    size_t value_cnt;                   /* Number of values. */
    unsigned ref_cnt;                   /* Reference count. */
    union value values[1];              /* Values. */
  };

/* Ensures that C does not share data with any other case. */
static void
case_unshare (struct ccase *c) 
{
  if (c->case_data->ref_cnt > 1)
    {
      struct case_data *cd = c->case_data;
      cd->ref_cnt--;
      case_create (c, cd->value_cnt);
      memcpy (c->case_data->values, cd->values,
              sizeof *cd->values * cd->value_cnt); 
    }
}

/* Returns the number of bytes needed by a case with VALUE_CNT
   values. */
static size_t
case_size (size_t value_cnt) 
{
  return (offsetof (struct case_data, values)
          + value_cnt * sizeof (union value));
}

/* Initializes C as a null case. */
void
case_nullify (struct ccase *c) 
{
  c->case_data = NULL;
}

/* Returns true iff C is a null case. */
bool
case_is_null (const struct ccase *c) 
{
  return c->case_data == NULL;
}

/* Initializes C as a new case that can store VALUE_CNT values.
   The values have indeterminate contents until explicitly
   written. */
void
case_create (struct ccase *c, size_t value_cnt) 
{
  if (!case_try_create (c, value_cnt))
    xalloc_die ();
}

/* Initializes CLONE as a copy of ORIG. */
void
case_clone (struct ccase *clone, const struct ccase *orig)
{
  assert (orig->case_data->ref_cnt > 0);

  if (clone != orig) 
    *clone = *orig;
  orig->case_data->ref_cnt++;
#ifdef DEBUGGING
  case_unshare (clone);
#endif
}

/* Replaces DST by SRC and nullifies SRC.
   DST and SRC must be initialized cases at entry. */
void
case_move (struct ccase *dst, struct ccase *src) 
{
  assert (src->case_data->ref_cnt > 0);
  
  if (dst != src) 
    {
      *dst = *src;
      case_nullify (src); 
    }
}

/* Destroys case C. */
void
case_destroy (struct ccase *c) 
{
  struct case_data *cd;
  
  cd = c->case_data;
  if (cd != NULL && --cd->ref_cnt == 0) 
    {
      memset (cd->values, 0xcc, sizeof *cd->values * cd->value_cnt);
      cd->value_cnt = 0xdeadbeef;
      free (cd); 
    }
}

/* Returns the number of union values in C. */
size_t
case_get_value_cnt (const struct ccase *c) 
{
  return c->case_data->value_cnt;
}

/* Resizes case C to NEW_CNT union values. */
void
case_resize (struct ccase *c, size_t new_cnt) 
{
  size_t old_cnt = case_get_value_cnt (c);
  if (old_cnt != new_cnt)
    {
      struct ccase new;

      case_create (&new, new_cnt);
      case_copy (&new, 0, c, 0, MIN (old_cnt, new_cnt));
      case_swap (&new, c);
      case_destroy (&new);
    }
}

/* Swaps cases A and B. */
void
case_swap (struct ccase *a, struct ccase *b) 
{
  struct case_data *t = a->case_data;
  a->case_data = b->case_data;
  b->case_data = t;
}

/* Attempts to create C as a new case that holds VALUE_CNT
   values.  Returns true if successful, false if memory
   allocation failed. */
bool
case_try_create (struct ccase *c, size_t value_cnt) 
{
  c->case_data = malloc (case_size (value_cnt));
  if (c->case_data != NULL) 
    {
      c->case_data->value_cnt = value_cnt;
      c->case_data->ref_cnt = 1;
      return true;
    }
  
  return false;
}

/* Tries to initialize CLONE as a copy of ORIG.
   Returns true if successful, false if memory allocation
   failed. */
bool
case_try_clone (struct ccase *clone, const struct ccase *orig) 
{
  case_clone (clone, orig);
  return true;
}

/* Copies VALUE_CNT values from SRC (starting at SRC_IDX) to DST
   (starting at DST_IDX). */
void
case_copy (struct ccase *dst, size_t dst_idx,
           const struct ccase *src, size_t src_idx,
           size_t value_cnt)
{
  assert (dst->case_data->ref_cnt > 0);
  assert (dst_idx + value_cnt <= dst->case_data->value_cnt);

  assert (src->case_data->ref_cnt > 0);
  assert (src_idx + value_cnt <= src->case_data->value_cnt);

  if (dst->case_data != src->case_data || dst_idx != src_idx) 
    {
      case_unshare (dst);
      memmove (dst->case_data->values + dst_idx,
               src->case_data->values + src_idx,
               sizeof *dst->case_data->values * value_cnt); 
    }
}

/* Copies case C to OUTPUT.
   OUTPUT_SIZE is the number of `union values' in OUTPUT,
   which must match the number of `union values' in C. */
void
case_to_values (const struct ccase *c, union value *output,
                size_t output_size UNUSED) 
{
  assert (c->case_data->ref_cnt > 0);
  assert (output_size == c->case_data->value_cnt);
  assert (output != NULL || output_size == 0);

  memcpy (output, c->case_data->values,
          c->case_data->value_cnt * sizeof *output);
}

/* Copies INPUT into case C.
   INPUT_SIZE is the number of `union values' in INPUT,
   which must match the number of `union values' in C. */
void
case_from_values (struct ccase *c, const union value *input,
                  size_t input_size UNUSED) 
{
  assert (c->case_data->ref_cnt > 0);
  assert (input_size == c->case_data->value_cnt);
  assert (input != NULL || input_size == 0);

  case_unshare (c);
  memcpy (c->case_data->values, input,
          c->case_data->value_cnt * sizeof *input);
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

/* Returns the numeric value of the `union value' in C for
   variable V.
   Case C must be drawn from V's dictionary. */
double
case_num (const struct ccase *c, const struct variable *v) 
{
  return case_num_idx (c, var_get_case_index (v));
}

/* Returns the string value of the `union value' in C for
   variable V.
   Case C must be drawn from V's dictionary.
   (Note that the value is not null-terminated.)
   The caller must not modify the return value. */
const char *
case_str (const struct ccase *c, const struct variable *v) 
{
  return case_str_idx (c, var_get_case_index (v));
}

/* Returns a pointer to the `union value' used for the
   element of C for variable V.
   Case C must be drawn from V's dictionary.   
   The caller is allowed to modify the returned data. */
union value *
case_data_rw (struct ccase *c, const struct variable *v) 
{
  return case_data_rw_idx (c, var_get_case_index (v));
}

/* Returns a pointer to the `union value' used for the
   element of C numbered IDX.
   The caller must not modify the returned data. */
const union value *
case_data_idx (const struct ccase *c, size_t idx) 
{
  assert (c->case_data->ref_cnt > 0);
  assert (idx < c->case_data->value_cnt);

  return &c->case_data->values[idx];
}

/* Returns the numeric value of the `union value' in C numbered
   IDX. */
double
case_num_idx (const struct ccase *c, size_t idx) 
{
  assert (c->case_data->ref_cnt > 0);
  assert (idx < c->case_data->value_cnt);

  return c->case_data->values[idx].f;
}

/* Returns the string value of the `union value' in C numbered
   IDX.
   (Note that the value is not null-terminated.)
   The caller must not modify the return value. */
const char *
case_str_idx (const struct ccase *c, size_t idx) 
{
  assert (c->case_data->ref_cnt > 0);
  assert (idx < c->case_data->value_cnt);

  return c->case_data->values[idx].s;
}

/* Returns a pointer to the `union value' used for the
   element of C numbered IDX.
   The caller is allowed to modify the returned data. */
union value *
case_data_rw_idx (struct ccase *c, size_t idx) 
{
  assert (c->case_data->ref_cnt > 0);
  assert (idx < c->case_data->value_cnt);

  case_unshare (c);
  return &c->case_data->values[idx];
}

/* Compares the values of the VAR_CNT variables in VP
   in cases A and B and returns a strcmp()-type result. */
int
case_compare (const struct ccase *a, const struct ccase *b,
              struct variable *const *vp, size_t var_cnt)
{
  return case_compare_2dict (a, b, vp, vp, var_cnt);
}

/* Compares the values of the VAR_CNT variables in VAP in case CA
   to the values of the VAR_CNT variables in VBP in CB
   and returns a strcmp()-type result. */
int
case_compare_2dict (const struct ccase *ca, const struct ccase *cb,
                    struct variable *const *vap, struct variable *const *vbp,
                    size_t var_cnt) 
{
  for (; var_cnt-- > 0; vap++, vbp++) 
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

   NOTE: This function breaks the case abstraction.  It should
   *not* be used often.  Prefer the other case functions. */
const union value *
case_data_all (const struct ccase *c) 
{
  assert (c->case_data->ref_cnt > 0);

  return c->case_data->values;
}

/* Returns a pointer to the array of `union value's used for C.
   The caller is allowed to modify the returned data.

   NOTE: This function breaks the case abstraction.  It should
   *not* be used often.  Prefer the other case functions. */
union value *
case_data_all_rw (struct ccase *c) 
{
  assert (c->case_data->ref_cnt > 0);

  case_unshare (c);
  return c->case_data->values;
}
