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

#ifdef GLOBAL_DEBUGGING
#undef NDEBUG
#else
#define NDEBUG
#endif
#include <assert.h>

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

static inline size_t
case_size (size_t value_cnt) 
{
  return (offsetof (struct case_data, values)
          + value_cnt * sizeof (union value));
}

#ifdef GLOBAL_DEBUGGING
void
case_nullify (struct ccase *c) 
{
  c->case_data = NULL;
  c->this = c;
}
#endif /* GLOBAL_DEBUGGING */

#ifdef GLOBAL_DEBUGGING
int
case_is_null (const struct ccase *c) 
{
  return c->case_data == NULL;
}
#endif /* GLOBAL_DEBUGGING */

void
case_create (struct ccase *c, size_t value_cnt) 
{
  if (!case_try_create (c, value_cnt))
    out_of_memory ();
}

#ifdef GLOBAL_DEBUGGING
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

int
case_try_clone (struct ccase *clone, const struct ccase *orig) 
{
  case_clone (clone, orig);
  return 1;
}

#ifdef GLOBAL_DEBUGGING
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
size_t
case_serial_size (size_t value_cnt) 
{
  return value_cnt * sizeof (union value);
}
#endif /* GLOBAL_DEBUGGING */

#ifdef GLOBAL_DEBUGGING
void
case_serialize (const struct ccase *c, void *output,
                size_t output_size UNUSED) 
{
  assert (c != NULL);
  assert (c->this == c);
  assert (c->case_data != NULL);
  assert (c->case_data->ref_cnt > 0);
  assert (output_size == case_serial_size (c->case_data->value_cnt));
  assert (output != NULL || output_size == 0);

  memcpy (output, c->case_data->values,
          case_serial_size (c->case_data->value_cnt));
}
#endif /* GLOBAL_DEBUGGING */

#ifdef GLOBAL_DEBUGGING
void
case_unserialize (struct ccase *c, const void *input,
                  size_t input_size UNUSED) 
{
  assert (c != NULL);
  assert (c->this == c);
  assert (c->case_data != NULL);
  assert (c->case_data->ref_cnt > 0);
  assert (input_size == case_serial_size (c->case_data->value_cnt));
  assert (input != NULL || input_size == 0);

  if (c->case_data->ref_cnt > 1)
    case_unshare (c);
  memcpy (c->case_data->values, input,
          case_serial_size (c->case_data->value_cnt));
}
#endif /* GLOBAL_DEBUGGING */

#ifdef GLOBAL_DEBUGGING
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
