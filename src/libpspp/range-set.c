/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009 Free Software Foundation, Inc.

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

/* Bitmap, implemented as a balanced binary tree. */

/* If you add routines in this file, please add a corresponding
   test to range-set-test.c.  This test program should achieve
   100% coverage of lines and branches in this code, as reported
   by "gcov -b". */

#include <config.h>

#include <libpspp/range-set.h>

#include <limits.h>
#include <stdlib.h>

#include <libpspp/assertion.h>
#include <libpspp/bt.h>
#include <libpspp/compiler.h>
#include <libpspp/pool.h>

#include "xalloc.h"

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

static struct range_set_node *bt_to_rs_node (const struct bt_node *);
static int compare_range_set_nodes (const struct bt_node *,
                                    const struct bt_node *,
                                    const void *aux);
static struct range_set_node *first_node (const struct range_set *);
static struct range_set_node *next_node (const struct range_set *,
                                         const struct range_set_node *);
static void delete_node (struct range_set *, struct range_set_node *);
static struct range_set_node *delete_node_get_next (struct range_set *,
                                                    struct range_set_node *);
static struct range_set_node *find_node_le (const struct range_set *,
                                            unsigned long int position);
static struct range_set_node *insert_node (struct range_set *,
                                           unsigned long int start,
                                           unsigned long int end);
static void insert_just_before (struct range_set *,
                                unsigned long int start, unsigned long int end,
                                struct range_set_node *);
static void merge_node_with_successors (struct range_set *,
                                        struct range_set_node *);
static void destroy_pool (void *);

/* Creates and returns a new, empty range set. */
struct range_set *
range_set_create (void)
{
  return range_set_create_pool (NULL);
}

/* Creates and returns a new, empty range set in the given
   POOL. */
struct range_set *
range_set_create_pool (struct pool *pool)
{
  struct range_set *rs = xmalloc (sizeof *rs);
  rs->pool = pool;
  if (pool != NULL)
    pool_register (pool, destroy_pool, rs);
  bt_init (&rs->bt, compare_range_set_nodes, NULL);
  rs->cache_end = 0;
  return rs;
}

/* Creates and returns a clone of OLD range set in the given POOL
   (which may be null). */
struct range_set *
range_set_clone (const struct range_set *old, struct pool *pool)
{
  struct range_set *new;
  struct range_set_node *node;

  new = range_set_create_pool (pool);
  for (node = first_node (old); node != NULL; node = next_node (old, node))
    insert_node (new, node->start, node->end);
  return new;
}

/* Destroys range set RS. */
void
range_set_destroy (struct range_set *rs)
{
  if (rs != NULL)
    {
      if (rs->pool != NULL)
        pool_unregister (rs->pool, rs);
      while (!range_set_is_empty (rs))
        delete_node (rs, first_node (rs));
      free (rs);
    }
}

/* Inserts the region starting at START and extending for WIDTH
   into RS. */
void
range_set_insert (struct range_set *rs,
                  unsigned long int start, unsigned long int width)
{
  unsigned long int end = start + width;
  struct range_set_node *node;

  assert (width == 0 || start + width - 1 >= start);

  if (width == 0)
    return;

  /* Invalidate cache. */
  rs->cache_end = 0;

  node = find_node_le (rs, start);
  if (node != NULL)
    {
      if (start > node->end)
        {
          /* New region does not overlap NODE, but it might
             overlap the next node. */
          insert_just_before (rs, start, end, next_node (rs, node));
        }
      else if (end > node->end)
        {
          /* New region starts in the middle of NODE and
             continues past its end, so extend NODE, then merge
             it with any following node that it now potentially
             overlaps. */
          node->end = end;
          merge_node_with_successors (rs, node);
        }
      else
        {
          /* New region is completely contained by NODE, so
             there's nothing to do. */
        }
    }
  else
    {
      /* New region starts before any existing region, but it
         might overlap the first region. */
      insert_just_before (rs, start, end, first_node (rs));
    }
}

/* Inserts the region starting at START and extending for WIDTH
   from RS. */
void
range_set_delete (struct range_set *rs,
                  unsigned long int start, unsigned long int width)
{
  unsigned long int end = start + width;
  struct range_set_node *node;

  assert (width == 0 || start + width - 1 >= start);

  if (width == 0)
    return;

  /* Invalidate cache. */
  rs->cache_end = 0;

  node = find_node_le (rs, start);
  if (node == NULL)
    node = first_node (rs);

  while (node != NULL && end > node->start)
    {
      if (start <= node->start)
        {
          if (end >= node->end)
            {
              /* Region to delete covers entire node. */
              node = delete_node_get_next (rs, node);
            }
          else
            {
              /* Region to delete covers only left part of node. */
              node->start = end;
              break;
            }
        }
      else if (start < node->end)
        {
          if (end < node->end)
            {
              /* Region to delete covers middle of the node, so
                 we have to split the node into two pieces. */
              unsigned long int old_node_end = node->end;
              node->end = start;
              insert_node (rs, end, old_node_end);
              break;
            }
          else
            {
              /* Region to delete covers only right part of
                 node. */
              node->end = start;
              node = next_node (rs, node);
            }
        }
      else
        node = next_node (rs, node);
    }
}

/* Scans RS for its first 1-bit and deletes up to REQUEST
   contiguous 1-bits starting at that position.  Unless RS is
   completely empty, sets *START to the position of the first
   1-bit deleted and *WIDTH to the number actually deleted, which
   may be less than REQUEST if fewer contiguous 1-bits were
   present, and returns true.  If RS is completely empty, returns
   false. */
bool
range_set_allocate (struct range_set *rs, unsigned long int request,
                    unsigned long int *start, unsigned long int *width)
{
  struct range_set_node *node;
  unsigned long int node_width;

  assert (request > 0);

  node = first_node (rs);
  if (node == NULL)
    return false;
  node_width = node->end - node->start;

  *start = node->start;
  if (request < node_width)
    {
      *width = request;
      node->start += request;
    }
  else
    {
      *width = node_width;
      delete_node (rs, node);
    }

  rs->cache_end = 0;

  return true;
}

/* Scans RS for and deletes the first contiguous run of REQUEST
   1-bits.  If successful, sets *START to the position of the
   first 1-bit deleted and returns true If RS does not contain a
   run of REQUEST or more contiguous 1-bits, returns false and
   does not modify RS. */
bool
range_set_allocate_fully (struct range_set *rs, unsigned long int request,
                          unsigned long int *start)
{
  struct range_set_node *node;

  assert (request > 0);

  for (node = first_node (rs); node != NULL; node = next_node (rs, node))
    {
      unsigned long int node_width = node->end - node->start;
      if (node_width >= request)
        {
          *start = node->start;
          if (node_width > request)
            node->start += request;
          else
            delete_node (rs, node);
          rs->cache_end = 0;
          return true;
        }
    }
  return false;
}

/* Returns true if there is a 1-bit at the given POSITION in RS,
   false otherwise. */
bool
range_set_contains (const struct range_set *rs_, unsigned long int position)
{
  struct range_set *rs = (struct range_set *) rs_;
  if (position < rs->cache_end && position >= rs->cache_start)
    return rs->cache_value;
  else
    {
      struct range_set_node *node = find_node_le (rs, position);
      if (node != NULL)
        {
          if (position < node->end)
            {
              rs->cache_start = node->start;
              rs->cache_end = node->end;
              rs->cache_value = true;
              return true;
            }
          else
            {
              struct range_set_node *next = next_node (rs, node);
              rs->cache_start = node->end;
              rs->cache_end = next != NULL ? next->start : ULONG_MAX;
              rs->cache_value = false;
              return false;
            }
        }
      else
        {
          node = first_node (rs);
          rs->cache_start = 0;
          rs->cache_end = node != NULL ? node->start : ULONG_MAX;
          rs->cache_value = false;
          return false;
        }
    }
}

/* Returns true if RS contains no 1-bits, false otherwise. */
bool
range_set_is_empty (const struct range_set *rs)
{
  return bt_count (&rs->bt) == 0;
}

/* Returns the node representing the first contiguous region of
   1-bits in RS, or a null pointer if RS is empty.
   Any call to range_set_insert, range_set_delete, or
   range_set_allocate invalidates the returned node. */
const struct range_set_node *
range_set_first (const struct range_set *rs)
{
  return first_node (rs);
}

/* If NODE is nonnull, returns the node representing the next
   contiguous region of 1-bits in RS following NODE, or a null
   pointer if NODE is the last region in RS.
   If NODE is null, returns the first region in RS, as for
   range_set_first.
   Any call to range_set_insert, range_set_delete, or
   range_set_allocate invalidates the returned node. */
const struct range_set_node *
range_set_next (const struct range_set *rs, const struct range_set_node *node)
{
  return (node != NULL
          ? next_node (rs, (struct range_set_node *) node)
          : first_node (rs));
}

/* Returns the position of the first 1-bit in NODE. */
unsigned long int
range_set_node_get_start (const struct range_set_node *node)
{
  return node->start;
}

/* Returns one past the position of the last 1-bit in NODE. */
unsigned long int
range_set_node_get_end (const struct range_set_node *node)
{
  return node->end;
}

/* Returns the number of contiguous 1-bits in NODE. */
unsigned long int
range_set_node_get_width (const struct range_set_node *node)
{
  return node->end - node->start;
}

/* Returns the range_set_node corresponding to the given
   BT_NODE.  Returns a null pointer if BT_NODE is null. */
static struct range_set_node *
bt_to_rs_node (const struct bt_node *bt_node)
{
  return bt_node ? bt_data (bt_node, struct range_set_node, bt_node) : NULL;
}

/* Compares `range_set_node's A and B and returns a strcmp-like
   return value. */
static int
compare_range_set_nodes (const struct bt_node *a_,
                         const struct bt_node *b_,
                         const void *aux UNUSED)
{
  const struct range_set_node *a = bt_to_rs_node (a_);
  const struct range_set_node *b = bt_to_rs_node (b_);

  return a->start < b->start ? -1 : a->start > b->start;
}

/* Returns the first range_set_node in RS,
   or a null pointer if RS is empty. */
static struct range_set_node *
first_node (const struct range_set *rs)
{
  return bt_to_rs_node (bt_first (&rs->bt));
}

/* Returns the next range_set_node in RS after NODE,
   or a null pointer if NODE is the last node in RS. */
static struct range_set_node *
next_node (const struct range_set *rs, const struct range_set_node *node)
{
  return bt_to_rs_node (bt_next (&rs->bt, &node->bt_node));
}

/* Deletes NODE from RS and frees it. */
static void
delete_node (struct range_set *rs, struct range_set_node *node)
{
  bt_delete (&rs->bt, &node->bt_node);
  free (node);
}

/* Deletes NODE from RS, frees it, and returns the following
   node. */
static struct range_set_node *
delete_node_get_next (struct range_set *rs, struct range_set_node *node)
{
  struct range_set_node *next = next_node (rs, node);
  delete_node (rs, node);
  return next;
}

/* Returns the node in RS that would be just before POSITION if a
   range_set_node with `start' as POSITION were inserted.
   Returns a null pointer if POSITION is before any existing node
   in RS.  If POSITION would duplicate an existing region's
   start, returns that region. */
static struct range_set_node *
find_node_le (const struct range_set *rs, unsigned long int position)
{
  struct range_set_node tmp;
  tmp.start = position;
  return bt_to_rs_node (bt_find_le (&rs->bt, &tmp.bt_node));
}

/* Creates a new region with the given START and END, inserts the
   region into RS, and returns the new region. */
static struct range_set_node *
insert_node (struct range_set *rs,
             unsigned long int start, unsigned long int end)
{
  struct range_set_node *node = xmalloc (sizeof *node);
  struct bt_node *dummy;
  node->start = start;
  node->end = end;
  dummy = bt_insert (&rs->bt, &node->bt_node);
  assert (dummy == NULL);
  return node;
}

/* Inserts the region START...END (exclusive) into RS given that
   the new region is "just before" NODE, that is, if NODE is
   nonnull, the new region is known not to overlap any node
   preceding NODE, and START precedes NODE's start; if NODE is
   null, then the new region is known to follow any and all
   regions already in RS. */
static void
insert_just_before (struct range_set *rs,
                    unsigned long int start, unsigned long int end,
                    struct range_set_node *node)
{
  assert (node == NULL || start < node->start);
  if (node != NULL && end >= node->start)
    {
      /* New region overlaps NODE, so just extend NODE. */
      node->start = start;
      if (end > node->end)
        {
          node->end = end;
          merge_node_with_successors (rs, node);
        }
    }
  else
    {
      /* New region does not overlap NODE. */
      insert_node (rs, start, end);
    }
}

/* Merges NODE in RS with its successors. */
static void
merge_node_with_successors (struct range_set *rs, struct range_set_node *node)
{
  struct range_set_node *next;

  while ((next = next_node (rs, node)) != NULL && node->end >= next->start)
    {
      if (next->end > node->end)
        node->end = next->end;
      delete_node (rs, next);
    }
}

/* Destroys range set RS.
   Helper function for range_set_create_pool. */
static void
destroy_pool (void *rs_)
{
  struct range_set *rs = rs_;
  rs->pool = NULL;
  range_set_destroy (rs);
}
