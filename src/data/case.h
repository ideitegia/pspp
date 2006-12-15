/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.

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

#ifndef HEADER_CASE
#define HEADER_CASE

#include <stddef.h>
#include <stdbool.h>
#include "value.h"
#include "variable.h"

/* Opaque structure that represents a case.  Use accessor
   functions instead of accessing any members directly.  Use
   case_move() or case_clone() instead of copying.  */
struct ccase 
  {
    struct case_data *case_data;        /* Actual data. */
  };

/* Invisible to user code. */
struct case_data
  {
    size_t value_cnt;                   /* Number of values. */
    unsigned ref_cnt;                   /* Reference count. */
    union value values[1];              /* Values. */
  };

#ifdef DEBUGGING
#define CASE_INLINE
#else
#define CASE_INLINE static
#endif

CASE_INLINE void case_nullify (struct ccase *);
CASE_INLINE int case_is_null (const struct ccase *);

void case_create (struct ccase *, size_t value_cnt);
CASE_INLINE void case_clone (struct ccase *, const struct ccase *);
CASE_INLINE void case_move (struct ccase *, struct ccase *);
CASE_INLINE void case_destroy (struct ccase *);

void case_resize (struct ccase *, size_t old_cnt, size_t new_cnt);
void case_swap (struct ccase *, struct ccase *);

bool case_try_create (struct ccase *, size_t value_cnt);
bool case_try_clone (struct ccase *, const struct ccase *);

CASE_INLINE void case_copy (struct ccase *dst, size_t dst_idx,
                            const struct ccase *src, size_t src_idx,
                            size_t cnt);

CASE_INLINE void case_to_values (const struct ccase *, union value *, size_t);
CASE_INLINE void case_from_values (struct ccase *,
                                   const union value *, size_t);

static inline const union value *case_data (const struct ccase *,
                                            const struct variable *);
static inline double case_num (const struct ccase *, const struct variable *);
static inline const char *case_str (const struct ccase *,
                                    const struct variable *);
static inline union value *case_data_rw (struct ccase *,
                                         const struct variable *);

CASE_INLINE const union value *case_data_idx (const struct ccase *,
                                              size_t idx);
CASE_INLINE double case_num_idx (const struct ccase *, size_t idx);
CASE_INLINE const char *case_str_idx (const struct ccase *, size_t idx);
CASE_INLINE union value *case_data_rw_idx (struct ccase *, size_t idx);

struct variable;
int case_compare (const struct ccase *, const struct ccase *,
                  struct variable *const *, size_t var_cnt);
int case_compare_2dict (const struct ccase *, const struct ccase *,
                        struct variable *const *, struct variable *const *,
                        size_t var_cnt);

const union value *case_data_all (const struct ccase *);
union value *case_data_all_rw (struct ccase *);

void case_unshare (struct ccase *);

#ifndef DEBUGGING
#include <stdlib.h>
#include <libpspp/str.h>

static inline void
case_nullify (struct ccase *c) 
{
  c->case_data = NULL;
}

static inline int
case_is_null (const struct ccase *c) 
{
  return c->case_data == NULL;
}

static inline void
case_clone (struct ccase *clone, const struct ccase *orig)
{
  *clone = *orig;
  orig->case_data->ref_cnt++;
}

static inline void
case_move (struct ccase *dst, struct ccase *src) 
{
  if (dst != src) 
    {
      *dst = *src;
      src->case_data = NULL; 
    }
}

static inline void
case_destroy (struct ccase *c) 
{
  struct case_data *cd = c->case_data;
  if (cd != NULL && --cd->ref_cnt == 0)
    free (cd);
}

static inline void
case_copy (struct ccase *dst, size_t dst_idx,
           const struct ccase *src, size_t src_idx,
           size_t value_cnt) 
{
  if (dst->case_data != src->case_data || dst_idx != src_idx) 
    {
      if (dst->case_data->ref_cnt > 1)
        case_unshare (dst);
      memmove (dst->case_data->values + dst_idx,
               src->case_data->values + src_idx,
               sizeof *dst->case_data->values * value_cnt); 
    }
}

static inline void
case_to_values (const struct ccase *c, union value *output,
                size_t output_size ) 
{
  memcpy (output, c->case_data->values,
          output_size * sizeof *output);
}

static inline void
case_from_values (struct ccase *c, const union value *input,
                  size_t input_size UNUSED) 
{
  if (c->case_data->ref_cnt > 1)
    case_unshare (c);
  memcpy (c->case_data->values, input,
          c->case_data->value_cnt * sizeof *input);
}

static inline const union value *
case_data_idx (const struct ccase *c, size_t idx) 
{
  return &c->case_data->values[idx];
}

static inline double
case_num_idx (const struct ccase *c, size_t idx) 
{
  return c->case_data->values[idx].f;
}

static inline const char *
case_str_idx (const struct ccase *c, size_t idx)
{
  return c->case_data->values[idx].s;
}

static inline union value *
case_data_rw_idx (struct ccase *c, size_t idx)
{
  if (c->case_data->ref_cnt > 1)
    case_unshare (c);
  return &c->case_data->values[idx];
}
#endif /* !DEBUGGING */

/* Returns a pointer to the `union value' used for the
   element of C for variable V.
   Case C must be drawn from V's dictionary.
   The caller must not modify the returned data. */
static inline const union value *
case_data (const struct ccase *c, const struct variable *v)
{
  return case_data_idx (c, var_get_case_index (v));
}

/* Returns the numeric value of the `union value' in C for
   variable V.
   Case C must be drawn from V's dictionary. */
static inline double
case_num (const struct ccase *c, const struct variable *v) 
{
  return case_num_idx (c, var_get_case_index (v));
}

/* Returns the string value of the `union value' in C for
   variable V.
   Case C must be drawn from V's dictionary.
   (Note that the value is not null-terminated.)
   The caller must not modify the return value. */
static inline const char *
case_str (const struct ccase *c, const struct variable *v) 
{
  return case_str_idx (c, var_get_case_index (v));
}

/* Returns a pointer to the `union value' used for the
   element of C for variable V.
   Case C must be drawn from V's dictionary.   
   The caller is allowed to modify the returned data. */
static inline union value *
case_data_rw (struct ccase *c, const struct variable *v) 
{
  return case_data_rw_idx (c, var_get_case_index (v));
}

#endif /* case.h */
