/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
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
#include "case.h"
#include <limits.h>
#include <stdlib.h>
#include "val.h"
#include "alloc.h"
#include "str.h"
#include "var.h"

#ifdef GLOBAL_DEBUGGING
#undef NDEBUG
#else
#ifndef NDEBUG
#define NDEBUG
#endif
#endif
#include <assert.h>

/* Changes C not to share data with any other case.
   C must be a case with a reference count greater than 1.
   There should be no reason for external code to call this
   function explicitly.  It will be called automatically when
   needed. */
void
case_unshare (struct ccase *c) 
{
  struct case_data *cd;
  
  assert (c != NULL);
  assert (c->this == c);
  assert (c->case_data != NULL);
  assert (c->case_data->ref_cnt > 1);

  cd = c->case_data;
  cd->ref_cnt--;
  case_create (c, c->case_data->value_cnt);
  memcpy (c->case_data->values, cd->values,
          sizeof *cd->values * cd->value_cnt); 
}

/* Returns the number of bytes needed by a case with VALUE_CNT
   values. */
static inline size_t
case_size (size_t value_cnt) 
{
  return (offsetof (struct case_data, values)
          + value_cnt * sizeof (union value));
}

#ifdef GLOBAL_DEBUGGING
/* Initializes C as a null case. */
void
case_nullify (struct ccase *c) 
{
  c->case_data = NULL;
  c->this = c;
}
#endif /* GLOBAL_DEBUGGING */

#ifdef GLOBAL_DEBUGGING
/* Returns true iff C is a null case. */
int
case_is_null (const struct ccase *c) 
{
  return c->case_data == NULL;
}
#endif /* GLOBAL_DEBUGGING */

/* Initializes C as a new case that can store VALUE_CNT values.
   The values have indeterminate contents until explicitly
   written. */
void
case_create (struct ccase *c, size_t value_cnt) 
{
  if (!case_try_create (c, value_cnt))
    out_of_memory ();
}

#ifdef GLOBAL_DEBUGGING
/* Initializes CLONE as a copy of ORIG. */
void
case_clone (struct ccase *clone, const struct ccase *orig)
{
  assert (orig != NULL);
  assert (orig->this == orig);
  assert (orig->case_data != NULL);
  assert (orig->case_data->ref_cnt > 0);
  assert (clone != NULL);

  if (clone != orig) 
    {
      *clone = *orig;
      clone->this = clone;
    }
  orig->case_data->ref_cnt++;
}
#endif /* GLOBAL_DEBUGGING */

#ifdef GLOBAL_DEBUGGING
/* Replaces DST by SRC and nullifies SRC.
   DST and SRC must be initialized cases at entry. */
void
case_move (struct ccase *dst, struct ccase *src) 
{
  assert (src != NULL);
  assert (src->this == src);
  assert (src->case_data != NULL);
  assert (src->case_data->ref_cnt > 0);
  assert (dst != NULL);

  *dst = *src;
  dst->this = dst;
  case_nullify (src);
}
#endif /* GLOBAL_DEBUGGING */

#ifdef GLOBAL_DEBUGGING
/* Destroys case C. */
void
case_destroy (struct ccase *c) 
{
  struct case_data *cd;
  
  assert (c != NULL);
  assert (c->this == c);

  cd = c->case_data;
  if (cd != NULL && --cd->ref_cnt == 0) 
    {
      memset (cd->values, 0xcc, sizeof *cd->values * cd->value_cnt);
      cd->value_cnt = 0xdeadbeef;
      free (cd); 
    }
}
#endif /* GLOBAL_DEBUGGING */

/* Resizes case C from OLD_CNT to NEW_CNT values. */
void
case_resize (struct ccase *c, size_t old_cnt, size_t new_cnt) 
{
  struct ccase new;

  case_create (&new, new_cnt);
  case_copy (&new, 0, c, 0, old_cnt < new_cnt ? old_cnt : new_cnt);
  case_swap (&new, c);
  case_destroy (&new);
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
   values.  Returns nonzero if successful, zero if memory
   allocation failed. */
int
case_try_create (struct ccase *c, size_t value_cnt) 
{
  c->case_data = malloc (case_size (value_cnt));
  if (c->case_data != NULL) 
    {
#ifdef GLOBAL_DEBUGGING
      c->this = c;
#endif
      c->case_data->value_cnt = value_cnt;
      c->case_data->ref_cnt = 1;
      return 1;
    }
  else 
    {
#ifdef GLOBAL_DEBUGGING
      c->this = c;
#endif
      return 0;
    }
}

/* Tries to initialize CLONE as a copy of ORIG.
   Returns nonzero if successful, zero if memory allocation
   failed. */
int
case_try_clone (struct ccase *clone, const struct ccase *orig) 
{
  case_clone (clone, orig);
  return 1;
}

#ifdef GLOBAL_DEBUGGING
/* Copies VALUE_CNT values from SRC (starting at SRC_IDX) to DST
   (starting at DST_IDX). */
void
case_copy (struct ccase *dst, size_t dst_idx,
           const struct ccase *src, size_t src_idx,
           size_t value_cnt)
{
  assert (dst != NULL);
  assert (dst->this == dst);
  assert (dst->case_data != NULL);
  assert (dst->case_data->ref_cnt > 0);
  assert (dst_idx + value_cnt <= dst->case_data->value_cnt);

  assert (src != NULL);
  assert (src->this == src);
  assert (src->case_data != NULL);
  assert (src->case_data->ref_cnt > 0);
  assert (src_idx + value_cnt <= dst->case_data->value_cnt);

  if (dst->case_data->ref_cnt > 1)
    case_unshare (dst);
  if (dst->case_data != src->case_data || dst_idx != src_idx) 
    memmove (dst->case_data->values + dst_idx,
             src->case_data->values + src_idx,
             sizeof *dst->case_data->values * value_cnt); 
}
#endif /* GLOBAL_DEBUGGING */

#ifdef GLOBAL_DEBUGGING
/* Copies case C to OUTPUT.
   OUTPUT_SIZE is the number of `union values' in OUTPUT,
   which must match the number of `union values' in C. */
void
case_to_values (const struct ccase *c, union value *output,
                size_t output_size UNUSED) 
{
  assert (c != NULL);
  assert (c->this == c);
  assert (c->case_data != NULL);
  assert (c->case_data->ref_cnt > 0);
  assert (output_size == c->case_data->value_cnt);
  assert (output != NULL || output_size == 0);

  memcpy (output, c->case_data->values,
          c->case_data->value_cnt * sizeof *output);
}
#endif /* GLOBAL_DEBUGGING */

#ifdef GLOBAL_DEBUGGING
/* Copies INPUT into case C.
   INPUT_SIZE is the number of `union values' in INPUT,
   which must match the number of `union values' in C. */
void
case_from_values (struct ccase *c, const union value *input,
                  size_t input_size UNUSED) 
{
  assert (c != NULL);
  assert (c->this == c);
  assert (c->case_data != NULL);
  assert (c->case_data->ref_cnt > 0);
  assert (input_size == c->case_data->value_cnt);
  assert (input != NULL || input_size == 0);

  if (c->case_data->ref_cnt > 1)
    case_unshare (c);
  memcpy (c->case_data->values, input,
          c->case_data->value_cnt * sizeof *input);
}
#endif /* GLOBAL_DEBUGGING */

#ifdef GLOBAL_DEBUGGING
/* Returns a pointer to the `union value' used for the
   element of C numbered IDX.
   The caller must not modify the returned data. */
const union value *
case_data (const struct ccase *c, size_t idx) 
{
  assert (c != NULL);
  assert (c->this == c);
  assert (c->case_data != NULL);
  assert (c->case_data->ref_cnt > 0);
  assert (idx < c->case_data->value_cnt);

  return &c->case_data->values[idx];
}
#endif /* GLOBAL_DEBUGGING */

#ifdef GLOBAL_DEBUGGING
/* Returns the numeric value of the `union value' in C numbered
   IDX. */
double
case_num (const struct ccase *c, size_t idx) 
{
  assert (c != NULL);
  assert (c->this == c);
  assert (c->case_data != NULL);
  assert (c->case_data->ref_cnt > 0);
  assert (idx < c->case_data->value_cnt);

  return c->case_data->values[idx].f;
}
#endif /* GLOBAL_DEBUGGING */

#ifdef GLOBAL_DEBUGGING
/* Returns the string value of the `union value' in C numbered
   IDX.
   (Note that the value is not null-terminated.)
   The caller must not modify the return value. */
const char *
case_str (const struct ccase *c, size_t idx) 
{
  assert (c != NULL);
  assert (c->this == c);
  assert (c->case_data != NULL);
  assert (c->case_data->ref_cnt > 0);
  assert (idx < c->case_data->value_cnt);

  return c->case_data->values[idx].s;
}
#endif /* GLOBAL_DEBUGGING */

#ifdef GLOBAL_DEBUGGING
/* Returns a pointer to the `union value' used for the
   element of C numbered IDX.
   The caller is allowed to modify the returned data. */
union value *
case_data_rw (struct ccase *c, size_t idx) 
{
  assert (c != NULL);
  assert (c->this == c);
  assert (c->case_data != NULL);
  assert (c->case_data->ref_cnt > 0);
  assert (idx < c->case_data->value_cnt);

  if (c->case_data->ref_cnt > 1)
    case_unshare (c);
  return &c->case_data->values[idx];
}
#endif /* GLOBAL_DEBUGGING */

/* Compares the values of the VAR_CNT variables in VP
   in cases A and B and returns a strcmp()-type result. */
int
case_compare (const struct ccase *a, const struct ccase *b,
              struct variable *const *vp, size_t var_cnt)
{
  for (; var_cnt-- > 0; vp++) 
    {
      struct variable *v = *vp;

      if (v->width == 0) 
        {
          double af = case_num (a, v->fv);
          double bf = case_num (b, v->fv);

          if (af != bf) 
            return af > bf ? 1 : -1;
        }
      else 
        {
          const char *as = case_str (a, v->fv);
          const char *bs = case_str (b, v->fv);
          int cmp = memcmp (as, bs, v->width);

          if (cmp != 0)
            return cmp;
        }
    }
  return 0;
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

      assert (va->type == vb->type);
      assert (va->width == vb->width);
      
      if (va->width == 0) 
        {
          double af = case_num (ca, va->fv);
          double bf = case_num (cb, vb->fv);

          if (af != bf) 
            return af > bf ? 1 : -1;
        }
      else 
        {
          const char *as = case_str (ca, va->fv);
          const char *bs = case_str (cb, vb->fv);
          int cmp = memcmp (as, bs, va->width);

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
  assert (c != NULL);
  assert (c->this == c);
  assert (c->case_data != NULL);
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
  assert (c != NULL);
  assert (c->this == c);
  assert (c->case_data != NULL);
  assert (c->case_data->ref_cnt > 0);

  if (c->case_data->ref_cnt > 1)
    case_unshare (c);
  return c->case_data->values;
}
