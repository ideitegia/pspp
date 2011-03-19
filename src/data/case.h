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

#ifndef DATA_CASE_H
#define DATA_CASE_H 1

#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

#include "libpspp/compiler.h"
#include "data/caseproto.h"

struct variable;

/* A count of cases or the index of a case within a collection of
   them. */
#define CASENUMBER_MAX LONG_MAX
typedef long int casenumber;

/* Reference-counted case implementation.

   A newly created case has a single owner (the code that created
   it), represented by an initial reference count of 1.  Other
   code that receives the case may keep a virtual copy of it by
   calling case_ref, which increments the case's reference count.
   When this is done, the case becomes shared between its
   original owner and each piece of code that incremented the
   reference count.

   A shared case (one whose reference count is greater than 1)
   must not be modified, because this would make the case change
   in the view of every reference count holder, not just the one
   that intended to change the case.  Because the purpose of
   keeping the reference count is to make a virtual copy of the
   case, this is undesirable behavior.  The case_unshare function
   provides a solution, by making a new, unshared copy of a
   shared case. */
struct ccase
  {
    struct caseproto *proto;    /* Case prototype. */
    size_t ref_cnt;             /* Reference count. */
    union value values[1];      /* Values. */
  };

struct ccase *case_create (const struct caseproto *) MALLOC_LIKE;
struct ccase *case_try_create (const struct caseproto *) MALLOC_LIKE;
struct ccase *case_clone (const struct ccase *) MALLOC_LIKE;

static inline struct ccase *case_unshare (struct ccase *) WARN_UNUSED_RESULT;
struct ccase *case_ref (const struct ccase *) WARN_UNUSED_RESULT;
static inline void case_unref (struct ccase *);
static inline bool case_is_shared (const struct ccase *);

static inline size_t case_get_value_cnt (const struct ccase *);
static inline const struct caseproto *case_get_proto (const struct ccase *);

size_t case_get_cost (const struct caseproto *);

struct ccase *case_resize (struct ccase *, const struct caseproto *)
  WARN_UNUSED_RESULT;
struct ccase *case_unshare_and_resize (struct ccase *,
                                       const struct caseproto *)
  WARN_UNUSED_RESULT;

void case_set_missing (struct ccase *);

void case_copy (struct ccase *dst, size_t dst_idx,
                const struct ccase *src, size_t src_idx,
                size_t cnt);
void case_copy_out (const struct ccase *,
                    size_t start_idx, union value *, size_t n_values);
void case_copy_in (struct ccase *,
                   size_t start_idx, const union value *, size_t n_values);

const union value *case_data (const struct ccase *, const struct variable *);
const union value *case_data_idx (const struct ccase *, size_t idx);
union value *case_data_rw (struct ccase *, const struct variable *);
union value *case_data_rw_idx (struct ccase *, size_t idx);

double case_num (const struct ccase *, const struct variable *);
double case_num_idx (const struct ccase *, size_t idx);

const uint8_t *case_str (const struct ccase *, const struct variable *);
const uint8_t *case_str_idx (const struct ccase *, size_t idx);
uint8_t *case_str_rw (struct ccase *, const struct variable *);
uint8_t *case_str_rw_idx (struct ccase *, size_t idx);

int case_compare (const struct ccase *, const struct ccase *,
                  const struct variable *const *, size_t n_vars);
int case_compare_2dict (const struct ccase *, const struct ccase *,
                        const struct variable *const *,
			const struct variable *const *,
                        size_t n_vars);

const union value *case_data_all (const struct ccase *);
union value *case_data_all_rw (struct ccase *);

struct ccase *case_unshare__ (struct ccase *);
void case_unref__ (struct ccase *);

/* If C is a shared case, that is, if it has a reference count
   greater than 1, makes a new unshared copy and returns it,
   decrementing C's reference count.  If C is not shared (its
   reference count is 1), returns C.

   This function should be used before attempting to modify any
   of the data in a case that might be shared, e.g.:
        c = case_unshare (c);              // Make sure that C is not shared.
        case_data_rw (c, myvar)->f = 1;    // Modify data in C.
*/
static inline struct ccase *
case_unshare (struct ccase *c)
{
  if (case_is_shared (c))
    c = case_unshare__ (c);
  return c;
}

/* Decrements case C's reference count.  Frees C if its
   reference count drops to 0.

   If C is a null pointer, this function has no effect. */
static inline void
case_unref (struct ccase *c)
{
  if (c != NULL && !--c->ref_cnt)
    case_unref__ (c);
}

/* Returns true if case C is shared.  A case that is shared
   cannot be modified directly.  Instead, an unshared copy must
   first be made with case_unshare(). */
static inline bool
case_is_shared (const struct ccase *c)
{
  return c->ref_cnt > 1;
}

/* Returns the number of union values in C. */
static inline size_t
case_get_value_cnt (const struct ccase *c)
{
  return caseproto_get_n_widths (c->proto);
}

/* Returns the prototype that describes the format of case C.
   The caller must not unref the returned prototype. */
static inline const struct caseproto *
case_get_proto (const struct ccase *c)
{
  return c->proto;
}

#endif /* data/case.h */
