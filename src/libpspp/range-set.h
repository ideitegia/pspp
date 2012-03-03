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

/* Bitmap, implemented as a balanced binary tree.

   Each operation has O(lg N) cost, where N is the number of
   contiguous regions of 1-bits in the bitmap.  Also, a cache
   reduces the second and subsequent containment tests within a
   single contiguous region to O(1). */

#ifndef LIBPSPP_RANGE_SET_H
#define LIBPSPP_RANGE_SET_H

#include <stdbool.h>
#include "libpspp/bt.h"
#include "libpspp/cast.h"

/* A set of ranges. */
struct range_set
  {
    struct pool *pool;                  /* Pool for freeing range_set. */
    struct bt bt;                       /* Tree of range_set_nodes. */

    /* Cache. */
    unsigned long int cache_start;      /* Start of region. */
    unsigned long int cache_end;        /* One past end of region. */
    bool cache_value;                   /* Is the region in the set? */
  };

/* A node in the range set. */
struct range_set_node
  {
    struct bt_node bt_node;             /* Binary tree node. */
    unsigned long int start;            /* Start of region. */
    unsigned long int end;              /* One past end of region. */
  };

struct range_set *range_set_create (void);
struct range_set *range_set_create_pool (struct pool *);
struct range_set *range_set_clone (const struct range_set *, struct pool *);
void range_set_destroy (struct range_set *);

void range_set_set1 (struct range_set *,
                     unsigned long int start, unsigned long int width);
void range_set_set0 (struct range_set *,
                     unsigned long int start, unsigned long int width);
bool range_set_allocate (struct range_set *, unsigned long int request,
                         unsigned long int *start, unsigned long int *width);
bool range_set_allocate_fully (struct range_set *, unsigned long int request,
                               unsigned long int *start);
bool range_set_contains (const struct range_set *, unsigned long int position);
unsigned long int range_set_scan (const struct range_set *,
                                  unsigned long int start);

static inline bool range_set_is_empty (const struct range_set *);

#define RANGE_SET_FOR_EACH(NODE, RANGE_SET)             \
        for ((NODE) = range_set_first (RANGE_SET);      \
             (NODE) != NULL;                            \
             (NODE) = range_set_next (RANGE_SET, NODE))

static inline const struct range_set_node *range_set_first (
  const struct range_set *);
static inline const struct range_set_node *range_set_next (
  const struct range_set *, const struct range_set_node *);
static inline const struct range_set_node *range_set_last (
  const struct range_set *);
static inline const struct range_set_node *range_set_prev (
  const struct range_set *, const struct range_set_node *);
static inline unsigned long int range_set_node_get_start (
  const struct range_set_node *);
static inline unsigned long int range_set_node_get_end (
  const struct range_set_node *);
static inline unsigned long int range_set_node_get_width (
  const struct range_set_node *);

/* Inline functions. */

static inline struct range_set_node *range_set_node_from_bt__ (
  const struct bt_node *);
static inline struct range_set_node *range_set_next__ (
  const struct range_set *, const struct range_set_node *);
static inline struct range_set_node *range_set_first__ (
  const struct range_set *);
static inline struct range_set_node *range_set_prev__ (
  const struct range_set *, const struct range_set_node *);
static inline struct range_set_node *range_set_last__ (
  const struct range_set *);

/* Returns true if RS contains no 1-bits, false otherwise. */
static inline bool
range_set_is_empty (const struct range_set *rs)
{
  return bt_count (&rs->bt) == 0;
}

/* Returns the node representing the first contiguous region of
   1-bits in RS, or a null pointer if RS is empty.
   Any call to range_set_set1, range_set_set0, or
   range_set_allocate invalidates the returned node. */
static inline const struct range_set_node *
range_set_first (const struct range_set *rs)
{
  return range_set_first__ (rs);
}

/* If NODE is nonnull, returns the node representing the next
   contiguous region of 1-bits in RS following NODE, or a null
   pointer if NODE is the last region in RS.
   If NODE is null, returns the first region in RS, as for
   range_set_first.
   Any call to range_set_set1, range_set_set0, or
   range_set_allocate invalidates the returned node. */
static inline const struct range_set_node *
range_set_next (const struct range_set *rs, const struct range_set_node *node)
{
  return (node != NULL
          ? range_set_next__ (rs, CONST_CAST (struct range_set_node *, node))
          : range_set_first__ (rs));
}

/* Returns the node representing the last contiguous region of
   1-bits in RS, or a null pointer if RS is empty.
   Any call to range_set_set1, range_set_set0, or
   range_set_allocate invalidates the returned node. */
static inline const struct range_set_node *
range_set_last (const struct range_set *rs)
{
  return range_set_last__ (rs);
}

/* If NODE is nonnull, returns the node representing the previous
   contiguous region of 1-bits in RS following NODE, or a null
   pointer if NODE is the first region in RS.
   If NODE is null, returns the last region in RS, as for
   range_set_last.
   Any call to range_set_set1, range_set_set0, or
   range_set_allocate invalidates the returned node. */
static inline const struct range_set_node *
range_set_prev (const struct range_set *rs, const struct range_set_node *node)
{
  return (node != NULL
          ? range_set_prev__ (rs, CONST_CAST (struct range_set_node *, node))
          : range_set_last__ (rs));
}

/* Returns the position of the first 1-bit in NODE. */
static inline unsigned long int
range_set_node_get_start (const struct range_set_node *node)
{
  return node->start;
}

/* Returns one past the position of the last 1-bit in NODE. */
static inline unsigned long int
range_set_node_get_end (const struct range_set_node *node)
{
  return node->end;
}

/* Returns the number of contiguous 1-bits in NODE. */
static inline unsigned long int
range_set_node_get_width (const struct range_set_node *node)
{
  return node->end - node->start;
}

/* Internal helper functions. */

/* Returns the range_set_node corresponding to the given
   BT_NODE.  Returns a null pointer if BT_NODE is null. */
static inline struct range_set_node *
range_set_node_from_bt__ (const struct bt_node *bt_node)
{
  return bt_node ? bt_data (bt_node, struct range_set_node, bt_node) : NULL;
}

/* Returns the next range_set_node in RS after NODE,
   or a null pointer if NODE is the last node in RS. */
static inline struct range_set_node *
range_set_next__ (const struct range_set *rs,
                  const struct range_set_node *node)
{
  return range_set_node_from_bt__ (bt_next (&rs->bt, &node->bt_node));
}

/* Returns the first range_set_node in RS,
   or a null pointer if RS is empty. */
static inline struct range_set_node *
range_set_first__ (const struct range_set *rs)
{
  return range_set_node_from_bt__ (bt_first (&rs->bt));
}

/* Returns the previous range_set_node in RS after NODE,
   or a null pointer if NODE is the first node in RS. */
static inline struct range_set_node *
range_set_prev__ (const struct range_set *rs,
                  const struct range_set_node *node)
{
  return range_set_node_from_bt__ (bt_prev (&rs->bt, &node->bt_node));
}

/* Returns the last range_set_node in RS,
   or a null pointer if RS is empty. */
static inline struct range_set_node *
range_set_last__ (const struct range_set *rs)
{
  return range_set_node_from_bt__ (bt_last (&rs->bt));
}

#endif /* libpspp/range-set.h */
