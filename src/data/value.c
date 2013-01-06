/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "data/value.h"

#include "data/val-type.h"
#include "data/variable.h"
#include "libpspp/hash-functions.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "gl/unistr.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

/* Copies the contents of string value SRC with width SRC_WIDTH
   to string value DST with width DST_WIDTH.  If SRC_WIDTH is
   greater than DST_WIDTH, then only the first DST_WIDTH bytes
   are copied; if DST_WIDTH is greater than SRC_WIDTH, then DST
   is padded on the right with PAD bytes.

   SRC and DST must be string values; that is, SRC_WIDTH and
   DST_WIDTH must both be positive.

   It is important that SRC_WIDTH and DST_WIDTH be the actual
   widths with which SRC and DST were initialized.  Passing,
   e.g., smaller values in order to copy only a prefix of SRC or
   modify only a prefix of DST will not work in every case. */
void
value_copy_rpad (union value *dst, int dst_width,
                 const union value *src, int src_width,
                 char pad)
{
  u8_buf_copy_rpad (value_str_rw (dst, dst_width), dst_width,
                 value_str (src, src_width), src_width,
                 pad);
}

/* Copies the contents of null-terminated string SRC to string
   value DST with width DST_WIDTH.  If SRC is more than DST_WIDTH
   bytes long, then only the first DST_WIDTH bytes are copied; if
   DST_WIDTH is greater than the length of SRC, then DST is
   padded on the right with PAD bytes.

   DST must be a string value; that is, DST_WIDTH must be
   positive.

   It is important that DST_WIDTH be the actual width with which
   DST was initialized.  Passing, e.g., a smaller value in order
   to modify only a prefix of DST will not work in every case. */
void
value_copy_str_rpad (union value *dst, int dst_width, const uint8_t *src,
                     char pad)
{
  value_copy_buf_rpad (dst, dst_width, src, u8_strlen (src), pad);
}

/* Copies the SRC_LEN bytes at SRC to string value DST with width
   DST_WIDTH.  If SRC_LEN is greater than DST_WIDTH, then only
   the first DST_WIDTH bytes are copied; if DST_WIDTH is greater
   than SRC_LEN, then DST is padded on the right with PAD bytes.

   DST must be a string value; that is, DST_WIDTH must be
   positive.

   It is important that DST_WIDTH be the actual width with which
   DST was initialized.  Passing, e.g., a smaller value in order
   to modify only a prefix of DST will not work in every case. */
void
value_copy_buf_rpad (union value *dst, int dst_width,
                     const uint8_t *src, size_t src_len, char pad)
{
  u8_buf_copy_rpad (value_str_rw (dst, dst_width), dst_width, src, src_len, pad);
}

/* Sets V to the system-missing value for data of the given
   WIDTH. */
void
value_set_missing (union value *v, int width)
{
  if (width != -1)
    {
      if (width == 0)
        v->f = SYSMIS;
      else
        memset (value_str_rw (v, width), ' ', width);
    }
}

/* Compares A and B, which both have the given WIDTH, and returns
   a strcmp()-type result. */
int
value_compare_3way (const union value *a, const union value *b, int width)
{
  return (width == -1 ? 0
          : width == 0 ? (a->f < b->f ? -1 : a->f > b->f)
          : memcmp (value_str (a, width), value_str (b, width), width));
}

/* Returns true if A and B, which must both have the given WIDTH,
   have equal contents, false if their contents differ. */
bool
value_equal (const union value *a, const union value *b, int width)
{
  return (width == -1 ? true
          : width == 0 ? a->f == b->f
          : !memcmp (value_str (a, width), value_str (b, width), width));
}

/* Returns a hash of the data in VALUE, which must have the given
   WIDTH, folding BASIS into the hash value calculation. */
unsigned int
value_hash (const union value *value, int width, unsigned int basis)
{
  return (width == -1 ? basis
          : width == 0 ? hash_double (value->f, basis)
          : hash_bytes (value_str (value, width), width, basis));
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
  if (old_width == new_width)
    return true;
  else if (val_type_from_width (old_width) != val_type_from_width (new_width))
    return false;
  else
    {
      const uint8_t *str = value_str (value, old_width);
      int i;

      for (i = new_width; i < old_width; i++)
        if (str[i] != ' ')
          return false;
      return true;
    }
}

/* Resizes VALUE from OLD_WIDTH to NEW_WIDTH.  The arguments must
   satisfy the rules specified above for value_is_resizable. */
void
value_resize (union value *value, int old_width, int new_width)
{
  assert (value_is_resizable (value, old_width, new_width));
  if (new_width != old_width)
    {
      union value tmp;
      value_init (&tmp, new_width);
      value_copy_rpad (&tmp, new_width, value, old_width, ' ');
      value_destroy (value, old_width);
      *value = tmp;
    }
}

/* Returns true if VALUE, with the given WIDTH, is all spaces, false otherwise.
   Returns false if VALUE is numeric. */
bool
value_is_spaces (const union value *value, int width)
{
  const uint8_t *s = value_str (value, width);
  int i;

  for (i = 0; i < width; i++)
    if (s[i] != ' ')
      return false;

  return true;
}

/* Returns true if resizing a value from OLD_WIDTH to NEW_WIDTH
   actually changes anything, false otherwise.  If false is
   returned, calls to value_resize() with the specified
   parameters may be omitted without any ill effects.

   This is generally useful only if many values can skip being
   resized from OLD_WIDTH to NEW_WIDTH.  Otherwise you might as
   well just call value_resize directly. */
bool
value_needs_resize (int old_width, int new_width)
{
  assert (val_type_from_width (old_width) == val_type_from_width (new_width));

  /* We need to call value_resize if either the new width is
     longer than the old width (in which case the new characters
     must be set to spaces) or if either width is a long string.
     (We could omit resizing if both the old and new widths were
     long and the new width was shorter, but we choose to do so
     anyway in hopes of saving memory.) */
  return (old_width != new_width
           && (new_width > old_width
               || old_width > MAX_SHORT_STRING
               || new_width > MAX_SHORT_STRING));
}

/* Same as value_init, except that memory for VALUE (if
   necessary) is allocated from POOL and will be freed
   automatically when POOL is destroyed.

   VALUE must not be freed manually by calling value_destroy.  If
   it needs to be resized, it must be done using
   value_resize_pool instead of value_resize. */
void
value_init_pool (struct pool *pool, union value *value, int width)
{
  if (width > MAX_SHORT_STRING)
    value->long_string = pool_alloc_unaligned (pool, width);
}

/* Same as value_clone(), except that memory for VALUE (if necessary) is
   allocated from POOL and will be freed automatically when POOL is destroyed.

   VALUE must not be freed manually by calling value_destroy().  If it needs to
   be resized, it must be done using value_resize_pool() instead of
   value_resize(). */
void
value_clone_pool (struct pool *pool,
                  union value *value, const union value *src, int width)
{
  if (width > MAX_SHORT_STRING)
    value->long_string = pool_clone_unaligned (pool, src->long_string, width);
  else
    *value = *src;
}

/* Same as value_resize, except that VALUE must have been
   allocated from POOL using value_init_pool.

   This function causes some memory in POOL to be wasted in some
   cases (until the pool is freed), so it should only be done if
   this is acceptable. */
void
value_resize_pool (struct pool *pool, union value *value,
                   int old_width, int new_width)
{
  assert (value_is_resizable (value, old_width, new_width));
  if (new_width > old_width)
    {
      if (new_width > MAX_SHORT_STRING)
        {
          uint8_t *new_long_string = pool_alloc_unaligned (pool, new_width);
          memcpy (new_long_string, value_str (value, old_width), old_width);
          value->long_string = new_long_string;
        }
      memset (value_str_rw (value, new_width) + old_width, ' ',
              new_width - old_width);
    }
}
