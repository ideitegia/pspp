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

/* Bitmap, implemented as a balanced binary tree. */

/* If you add routines in this file, please add a corresponding
   test to range-set-test.c.  This test program should achieve
   100% coverage of lines and branches in this code, as reported
   by "gcov -b". */

#include <config.h>

#include "libpspp/range-set.h"

#include <limits.h>
#include <stdlib.h>

#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/pool.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

static int compare_range_set_nodes (const struct bt_node *,
                                    const struct bt_node *,
                                    const void *aux);
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
  for (node = range_set_first__ (old); node != NULL;
       node = range_set_next__ (old, node))
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
        delete_node (rs, range_set_first__ (rs));
      free (rs);
    }
}

/* Inserts the region starting at START and extending for WIDTH
   into RS. */
void
range_set_set1 (struct range_set *rs,
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
          insert_just_before (rs, start, end, range_set_next__ (rs, node));
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
      insert_just_before (rs, start, end, range_set_first__ (rs));
    }
}

/* Deletes the region starting at START and extending for WIDTH
   from RS. */
void
range_set_set0 (struct range_set *rs,
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
    node = range_set_first__ (rs);

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
              node = range_set_next__ (rs, node);
            }
        }
      else
        node = range_set_next__ (rs, node);
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

  node = range_set_first__ (rs);
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

  for (node = range_set_first__ (rs); node != NULL;
       node = range_set_next__ (rs, node))
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
  struct range_set *rs = CONST_CAST (struct range_set *, rs_);
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
              struct range_set_node *next = range_set_next__ (rs, node);
              rs->cache_start = node->end;
              rs->cache_end = next != NULL ? next->start : ULONG_MAX;
              rs->cache_value = false;
              return false;
            }
        }
      else
        {
          node = range_set_first__ (rs);
          rs->cache_start = 0;
          rs->cache_end = node != NULL ? node->start : ULONG_MAX;
          rs->cache_value = false;
          return false;
        }
    }
}

/* Returns the smallest position of a 1-bit greater than or
   equal to START.  Returns ULONG_MAX if there is no 1-bit with
   position greater than or equal to START. */
unsigned long int
range_set_scan (const struct range_set *rs_, unsigned long int start)
{
  struct range_set *rs = CONST_CAST (struct range_set *, rs_);
  unsigned long int retval = ULONG_MAX;
  struct bt_node *bt_node;

  if (start < rs->cache_end && start >= rs->cache_start && rs->cache_value)
    return start;
  bt_node = rs->bt.root;
  while (bt_node != NULL)
    {
      struct range_set_node *node = range_set_node_from_bt__ (bt_node);
      if (start < node->start)
        {
          retval = node->start;
          bt_node = node->bt_node.down[0];
        }
      else if (start >= node->end)
        bt_node = node->bt_node.down[1];
      else
        {
          rs->cache_start = node->start;
          rs->cache_end = node->end;
          rs->cache_value = true;
          return start;
        }
    }
  return retval;
}

/* Compares `range_set_node's A and B and returns a strcmp-like
   return value. */
static int
compare_range_set_nodes (const struct bt_node *a_,
                         const struct bt_node *b_,
                         const void *aux UNUSED)
{
  const struct range_set_node *a = range_set_node_from_bt__ (a_);
  const struct range_set_node *b = range_set_node_from_bt__ (b_);

  return a->start < b->start ? -1 : a->start > b->start;
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
  struct range_set_node *next = range_set_next__ (rs, node);
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
  return range_set_node_from_bt__ (bt_find_le (&rs->bt, &tmp.bt_node));
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

  while ((next = range_set_next__ (rs, node)) != NULL
         && node->end >= next->start)
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
