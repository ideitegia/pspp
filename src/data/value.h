/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2012 Free Software Foundation, Inc.

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

#ifndef DATA_VALUE_H
#define DATA_VALUE_H 1

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "xalloc.h"

/* Maximum length of a "short" string, that is represented in
   "union value" without a separate pointer.

   This is an implementation detail of the "union value" code.
   There is little reason for client code to use it. */
#define MAX_SHORT_STRING 8

/* A numeric or string value.

   The client is responsible for keeping track of the value's
   width.

   This structure is semi-opaque:

       - If the value is a number, clients may access the 'f'
         member directly.

       - Clients should not access other members directly.
*/
union value
  {
    double f;
    uint8_t short_string[MAX_SHORT_STRING];
    uint8_t *long_string;
  };

static inline void value_init (union value *, int width);
static inline void value_clone (union value *, const union value *, int width);
static inline bool value_needs_init (int width);
static inline bool value_try_init (union value *, int width);
static inline void value_destroy (union value *, int width);

static inline double value_num (const union value *);
static inline const uint8_t *value_str (const union value *, int width);
static inline uint8_t *value_str_rw (union value *, int width);

static inline void value_copy (union value *, const union value *, int width);
void value_copy_rpad (union value *, int dst_width,
                      const union value *, int src_width,
                      char pad);
void value_copy_str_rpad (union value *, int dst_width, const uint8_t *,
                          char pad);
void value_copy_buf_rpad (union value *dst, int dst_width,
                          const uint8_t *src, size_t src_len, char pad);
void value_set_missing (union value *, int width);
int value_compare_3way (const union value *, const union value *, int width);
bool value_equal (const union value *, const union value *, int width);
unsigned int value_hash (const union value *, int width, unsigned int basis);

bool value_is_resizable (const union value *, int old_width, int new_width);
bool value_needs_resize (int old_width, int new_width);
void value_resize (union value *, int old_width, int new_width);

bool value_is_spaces (const union value *, int width);

static inline void value_swap (union value *, union value *);

struct pool;
void value_init_pool (struct pool *, union value *, int width);
void value_clone_pool (struct pool *, union value *, const union value *,
                       int width);
void value_resize_pool (struct pool *, union value *,
                        int old_width, int new_width);

/* Initializes V as a value of the given WIDTH, where 0
   represents a numeric value and a positive integer represents a
   string value WIDTH bytes long.

   A WIDTH of -1 is ignored.

   The contents of value V are indeterminate after
   initialization. */
static inline void
value_init (union value *v, int width)
{
  if (width > MAX_SHORT_STRING)
    v->long_string = xmalloc (width);
}

/* Initializes V as a value of the given WIDTH, as with value_init(), and
   copies SRC's value into V as its initial value. */
static inline void
value_clone (union value *v, const union value *src, int width)
{
  if (width <= MAX_SHORT_STRING)
    *v = *src;
  else
    v->long_string = xmemdup (src->long_string, width);
}

/* Returns true if a value of the given WIDTH actually needs to
   have the value_init and value_destroy functions called, false
   if those functions are no-ops for values of the given WIDTH.

   Using this function is only a valuable optimization if a large
   number of values of the given WIDTH are to be initialized*/
static inline bool
value_needs_init (int width)
{
  return width > MAX_SHORT_STRING;
}

/* Same as value_init, except that failure to allocate memory
   causes it to return false instead of terminating the
   program.  On success, returns true. */
static inline bool
value_try_init (union value *v, int width)
{
  if (width > MAX_SHORT_STRING)
    {
      v->long_string = malloc (width);
      return v->long_string != NULL;
    }
  else
    return true;
}

/* Frees any memory allocated by value_init for V, which must
   have the given WIDTH. */
static inline void
value_destroy (union value *v, int width)
{
  if (width > MAX_SHORT_STRING)
    free (v->long_string);
}

/* Returns the numeric value in V, which must have width 0. */
static inline double
value_num (const union value *v)
{
  return v->f;
}

/* Returns the string value in V, which must have width WIDTH.

   The returned value is not null-terminated.

   It is important that WIDTH be the actual value that was passed
   to value_init.  Passing, e.g., a smaller value because only
   that number of bytes will be accessed will not always work. */
static inline const uint8_t *
value_str (const union value *v, int width)
{
  assert (width > 0);
  return (width > MAX_SHORT_STRING ? v->long_string : v->short_string);
}

/* Returns the string value in V, which must have width WIDTH.

   The returned value is not null-terminated.

   It is important that WIDTH be the actual value that was passed
   to value_init.  Passing, e.g., a smaller value because only
   that number of bytes will be accessed will not always work. */
static inline uint8_t *
value_str_rw (union value *v, int width)
{
  assert (width > 0);
  return (width > MAX_SHORT_STRING ? v->long_string : v->short_string);
}

/* Copies SRC to DST, given that they both contain data of the
   given WIDTH. */
static inline void
value_copy (union value *dst, const union value *src, int width)
{
  if (width <= MAX_SHORT_STRING)
    *dst = *src;
  else if (dst != src)
    memcpy (dst->long_string, src->long_string, width);
}

/* Exchanges the contents of A and B. */
static inline void
value_swap (union value *a, union value *b)
{
  union value tmp = *a;
  *a = *b;
  *b = tmp;
}

#endif /* data/value.h */
