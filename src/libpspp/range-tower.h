/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2011 Free Software Foundation, Inc.

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

/* Bitmap, implemented as an augmented binary tree.

   Beyond the usual features of a bitmap, a range tower can efficiently
   implement a "splice" operation that shifts ranges of bits left or right.
   This feature does cost memory and time, so use a range tower only if this
   feature is actually needed.  Otherwise, use a range set (see range-set.h),
   which can do everything that a range tower can do except the "splice"
   operation.

   Each operation has O(lg N) cost, where N is the number of contiguous regions
   of 1-bits in the bitmap.  Also, a cache reduces the second and subsequent
   containment tests within a single contiguous region to O(1).  */

#ifndef LIBPSPP_RANGE_TOWER_H
#define LIBPSPP_RANGE_TOWER_H

#include <limits.h>
#include <stdbool.h>
#include "libpspp/abt.h"
#include "libpspp/cast.h"

/* A tower of ranges. */
struct range_tower
  {
    struct pool *pool;          /* Pool for freeing range_tower. */
    struct abt abt;             /* Tree of range_tower_nodes. */

    /* Cache. */
    unsigned long int cache_start; /* Start of region. */
    unsigned long int cache_end;   /* One past end of region. */
    bool cache_value;              /* Is the region in the tower? */
  };

/* A node in the range tower. */
struct range_tower_node
  {
    struct abt_node abt_node;        /* Augmented binary tree node. */
    unsigned long int n_zeros;       /* Number of leading zeros. */
    unsigned long int n_ones;        /* Number of ones following the zeros. */
    unsigned long int subtree_width; /* n_zeros + n_ones + sum of descendants. */
  };

struct range_tower *range_tower_create (void);
struct range_tower *range_tower_create_pool (struct pool *);
struct range_tower *range_tower_clone (const struct range_tower *,
                                       struct pool *);
void range_tower_destroy (struct range_tower *);

void range_tower_splice (struct range_tower *,
                         unsigned long int start,
                         unsigned long int old_width,
                         unsigned long int new_width);

void range_tower_set1 (struct range_tower *,
                       unsigned long int start, unsigned long int width);
void range_tower_set0 (struct range_tower *,
                       unsigned long int start, unsigned long int width);

void range_tower_insert1 (struct range_tower *,
                          unsigned long int start, unsigned long int width);
void range_tower_insert0 (struct range_tower *,
                          unsigned long int start, unsigned long int width);

void range_tower_delete (struct range_tower *,
                         unsigned long int start, unsigned long int width);

void range_tower_move (struct range_tower *,
                       unsigned long int old_start,
                       unsigned long int new_start,
                       unsigned long int width);

bool range_tower_contains (const struct range_tower *,
                           unsigned long int position);
unsigned long int range_tower_scan (const struct range_tower *,
                                    unsigned long int start);

static inline bool range_tower_is_empty (const struct range_tower *);

#define RANGE_TOWER_FOR_EACH(NODE, START, RANGE_TOWER)                  \
        for ((NODE) = range_tower_first (RANGE_TOWER), (START) = 0;     \
             (NODE) && ((START) += (NODE)->n_zeros, true);              \
             (START) += (NODE)->n_ones,                                 \
               (NODE) = range_tower_next (RANGE_TOWER, NODE))

static inline const struct range_tower_node *range_tower_first (
  const struct range_tower *);
static inline const struct range_tower_node *range_tower_next (
  const struct range_tower *, const struct range_tower_node *);
static inline const struct range_tower_node *range_tower_last (
  const struct range_tower *);
static inline const struct range_tower_node *range_tower_prev (
  const struct range_tower *, const struct range_tower_node *);
unsigned long int range_tower_node_get_start (const struct range_tower_node *);
unsigned long int range_tower_node_get_end (const struct range_tower_node *);
static inline unsigned long int range_tower_node_get_width (
  const struct range_tower_node *);

/* Inline functions. */

static inline struct range_tower_node *range_tower_node_from_abt__ (
  const struct abt_node *);
static inline struct range_tower_node *range_tower_next__ (
  const struct range_tower *, const struct range_tower_node *);
static inline struct range_tower_node *range_tower_first__ (
  const struct range_tower *);
static inline struct range_tower_node *range_tower_prev__ (
  const struct range_tower *, const struct range_tower_node *);
static inline struct range_tower_node *range_tower_last__ (
  const struct range_tower *);

/* Returns true if RS contains no 1-bits, false otherwise. */
static inline bool
range_tower_is_empty (const struct range_tower *rs)
{
  const struct range_tower_node *node =
    abt_data (rs->abt.root, struct range_tower_node, abt_node);

  return node->n_zeros == ULONG_MAX;
}

/* Returns the node representing the first contiguous region of
   1-bits in RS, or a null pointer if RS is empty.
   Any call to range_tower_set1, range_tower_set0, or
   range_tower_allocate invalidates the returned node. */
static inline const struct range_tower_node *
range_tower_first (const struct range_tower *rs)
{
  const struct range_tower_node *node = range_tower_first__ (rs);
  return node->n_ones ? node : NULL;
}

/* If NODE is nonnull, returns the node representing the next
   contiguous region of 1-bits in RS following NODE, or a null
   pointer if NODE is the last region in RS.
   If NODE is null, returns the first region in RS, as for
   range_tower_first.
   Any call to range_tower_set1, range_tower_set0, or
   range_tower_allocate invalidates the returned node. */
static inline const struct range_tower_node *
range_tower_next (const struct range_tower *rs,
                  const struct range_tower_node *node)
{
  if (node != NULL)
    {
      const struct range_tower_node *next = range_tower_next__ (rs, node);
      return next != NULL && next->n_ones ? next : NULL;
    }
  else
    return range_tower_first (rs);
}

/* Returns the node representing the last contiguous region of
   1-bits in RS, or a null pointer if RS is empty.
   Any call to range_tower_set1, range_tower_set0, or
   range_tower_allocate invalidates the returned node. */
static inline const struct range_tower_node *
range_tower_last (const struct range_tower *rs)
{
  const struct range_tower_node *node = range_tower_last__ (rs);
  return node->n_ones ? node : range_tower_prev__(rs, node);
}

/* If NODE is nonnull, returns the node representing the previous
   contiguous region of 1-bits in RS following NODE, or a null
   pointer if NODE is the first region in RS.
   If NODE is null, returns the last region in RS, as for
   range_tower_last.
   Any call to range_tower_set1, range_tower_set0, or
   range_tower_allocate invalidates the returned node. */
static inline const struct range_tower_node *
range_tower_prev (const struct range_tower *rs,
                  const struct range_tower_node *node)
{
  return node != NULL ? range_tower_prev__ (rs, node) : range_tower_last (rs);
}

/* Returns the number of contiguous 1-bits in NODE. */
static inline unsigned long int
range_tower_node_get_width (const struct range_tower_node *node)
{
  return node->n_ones;
}

/* Internal helper functions. */

/* Returns the range_tower_node corresponding to the given
   ABT_NODE.  Returns a null pointer if ABT_NODE is null. */
static inline struct range_tower_node *
range_tower_node_from_abt__ (const struct abt_node *abt_node)
{
  return (abt_node
          ? abt_data (abt_node, struct range_tower_node, abt_node)
          : NULL);
}

/* Returns the next range_tower_node in RS after NODE,
   or a null pointer if NODE is the last node in RS. */
static inline struct range_tower_node *
range_tower_next__ (const struct range_tower *rs,
                    const struct range_tower_node *node)
{
  return range_tower_node_from_abt__ (abt_next (&rs->abt, &node->abt_node));
}

/* Returns the first range_tower_node in RS,
   or a null pointer if RS is empty. */
static inline struct range_tower_node *
range_tower_first__ (const struct range_tower *rs)
{
  return range_tower_node_from_abt__ (abt_first (&rs->abt));
}

/* Returns the previous range_tower_node in RS after NODE,
   or a null pointer if NODE is the first node in RS. */
static inline struct range_tower_node *
range_tower_prev__ (const struct range_tower *rs,
                    const struct range_tower_node *node)
{
  return range_tower_node_from_abt__ (abt_prev (&rs->abt, &node->abt_node));
}

/* Returns the last range_tower_node in RS,
   or a null pointer if RS is empty. */
static inline struct range_tower_node *
range_tower_last__ (const struct range_tower *rs)
{
  return range_tower_node_from_abt__ (abt_last (&rs->abt));
}

struct range_tower_node *range_tower_lookup (
  const struct range_tower *, unsigned long int position,
  unsigned long int *node_start);

#endif /* libpspp/range-tower.h */
