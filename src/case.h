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

#ifndef HEADER_CASE
#define HEADER_CASE

#include <stddef.h>
#include "val.h"

/* Opaque structure that represents a case.  Use accessor
   functions instead of accessing any members directly.  Use
   case_move() or case_clone() instead of copying.  */
struct ccase 
  {
    struct case_data *case_data;
#if GLOBAL_DEBUGGING
    struct ccase *this;
#endif
  };

/* Invisible to user code. */
struct case_data
  {
    size_t value_cnt;
    unsigned ref_cnt;
    union value values[1];
  };

#ifdef GLOBAL_DEBUGGING
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

int case_try_create (struct ccase *, size_t value_cnt);
int case_try_clone (struct ccase *, const struct ccase *);

CASE_INLINE void case_copy (struct ccase *dst, size_t dst_idx,
                            const struct ccase *src, size_t src_idx,
                            size_t cnt);

CASE_INLINE size_t case_serial_size (size_t value_cnt);
CASE_INLINE void case_serialize (const struct ccase *, void *, size_t);
CASE_INLINE void case_unserialize (struct ccase *, const void *, size_t);

CASE_INLINE const union value *case_data (const struct ccase *, size_t idx);
CASE_INLINE double case_num (const struct ccase *, size_t idx);
CASE_INLINE const char *case_str (const struct ccase *, size_t idx);

CASE_INLINE union value *case_data_rw (struct ccase *, size_t idx);

void case_unshare (struct ccase *);

#ifndef GLOBAL_DEBUGGING
#include <stdlib.h>
#include "str.h"

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
  *dst = *src;
  src->case_data = NULL;
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
  if (dst->case_data->ref_cnt > 1)
    case_unshare (dst);
  if (dst->case_data != src->case_data || dst_idx != src_idx) 
    memmove (dst->case_data->values + dst_idx,
             src->case_data->values + src_idx,
             sizeof *dst->case_data->values * value_cnt); 
}

static inline size_t
case_serial_size (size_t value_cnt) 
{
  return value_cnt * sizeof (union value);
}

static inline void
case_serialize (const struct ccase *c, void *output,
                size_t output_size UNUSED) 
{
  memcpy (output, c->case_data->values,
          case_serial_size (c->case_data->value_cnt));
}

static inline void
case_unserialize (struct ccase *c, const void *input,
                  size_t input_size UNUSED) 
{
  if (c->case_data->ref_cnt > 1)
    case_unshare (c);
  memcpy (c->case_data->values, input,
          case_serial_size (c->case_data->value_cnt));
}

static inline const union value *
case_data (const struct ccase *c, size_t idx) 
{
  return &c->case_data->values[idx];
}

static inline double
case_num (const struct ccase *c, size_t idx) 
{
  return c->case_data->values[idx].f;
}

static inline const char *
case_str (const struct ccase *c, size_t idx)
{
  return c->case_data->values[idx].s;
}

static inline union value *
case_data_rw (struct ccase *c, size_t idx)
{
  if (c->case_data->ref_cnt > 1)
    case_unshare (c);
  return &c->case_data->values[idx];
}
#endif /* !GLOBAL_DEBUGGING */

#endif /* case.h */
