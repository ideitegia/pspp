/* pspp - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2011, 2012, 2013 Free Software Foundation, Inc.

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
   test to range-tower-test.c.  This test program should achieve
   100% coverage of lines and branches in this code, as reported
   by "gcov -b". */

#include <config.h>

#include "libpspp/range-tower.h"

#include <limits.h>
#include <stdlib.h>

#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/pool.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

static void reaugment_range_tower_node (struct abt_node *, const void *aux);
static void delete_node (struct range_tower *, struct range_tower_node *);

static void destroy_pool (void *);

static void
print_structure (const struct abt_node *node_)
{
  struct range_tower_node *node;

  if (node_ == NULL)
    return;
  node = abt_data (node_, struct range_tower_node, abt_node);
  printf ("%lu+%lu/%d", node->n_zeros, node->n_ones, node->abt_node.level);
  if (node->abt_node.down[0] || node->abt_node.down[1])
    {
      printf ("(");
      print_structure (node->abt_node.down[0]);
      printf (",");
      print_structure (node->abt_node.down[1]);
      printf (")");
    }
}

/* Prints the regions in RT to stdout. */
static void UNUSED
print_regions (const char *title, const struct range_tower *rt)
{
  const struct range_tower_node *node;

  printf ("%s:", title);
  for (node = range_tower_first__ (rt); node != NULL;
       node = range_tower_next__ (rt, node))
    printf (" (%lu,%lu)", node->n_zeros, node->n_ones);
  printf ("\n");
  printf ("structure:");
  print_structure (rt->abt.root);
  printf ("\n");
}

static struct range_tower_node *
range_tower_node_from_abt_node (const struct abt_node *abt_node)
{
  return abt_data (abt_node, struct range_tower_node, abt_node);
}

/* Returns the total width (zeros and ones) of the nodes in the subtree rooted
   at P, or 0 if P is null. */
static unsigned long int
subtree_width (const struct abt_node *p)
{
  return p != NULL ? range_tower_node_from_abt_node (p)->subtree_width : 0;
}

/* Returns the position of the first 1-bit in NODE.

   The performance of this function is O(lg n) in the number of nodes in the
   range tower.  It is often possible to avoid calling this function, either by
   taking advantage of the NODE_START parameter to tower_lookup or by
   incrementally keeping track of height while iterating through a tower.  In
   the former case the asymptotic performance is no different, since
   tower_lookup is also O(lg n), but in the latter case performance improves
   from O(lg n) to O(1). */
unsigned long int
range_tower_node_get_start (const struct range_tower_node *node)
{
  const struct abt_node *p = &node->abt_node;
  unsigned long start = subtree_width (p->down[0]) + range_tower_node_from_abt_node (p)->n_zeros;
  while (p->up != NULL)
    {
      if (p == p->up->down[1])
        {
          const struct range_tower_node *up
            = range_tower_node_from_abt_node (p->up);
          start += subtree_width (p->up->down[0]) + up->n_zeros + up->n_ones;
        }
      p = p->up;
    }
  return start;
}

/* Returns one past the position of the last 1-bit in NODE.

   Like range_tower_node_get_start(), the performance of this function is O(lg
   n) in the number of nodes in the range tower. */
unsigned long int
range_tower_node_get_end (const struct range_tower_node *node)
{
  return range_tower_node_get_start (node) + node->n_ones;
}

/* Creates and returns a new, empty range tower. */
struct range_tower *
range_tower_create (void)
{
  return range_tower_create_pool (NULL);
}

static struct range_tower *
range_tower_create_pool__ (struct pool *pool)
{
  struct range_tower *rt = xmalloc (sizeof *rt);

  rt->pool = pool;
  if (pool != NULL)
    pool_register (pool, destroy_pool, rt);

  abt_init (&rt->abt, NULL, reaugment_range_tower_node, NULL);
  rt->cache_end = 0;

  return rt;
}

/* Creates and returns a new, empty range tower in the given POOL. */
struct range_tower *
range_tower_create_pool (struct pool *pool)
{
  struct range_tower_node *node;
  struct range_tower *rt;

  rt = range_tower_create_pool__ (pool);

  node = xmalloc (sizeof *node);
  node->n_zeros = ULONG_MAX;
  node->n_ones = 0;
  abt_insert_after (&rt->abt, NULL, &node->abt_node);

  return rt;
}

/* Creates and returns a clone of OLD range tower in the given POOL
   (which may be null). */
struct range_tower *
range_tower_clone (const struct range_tower *old, struct pool *pool)
{
  const struct range_tower_node *old_node;
  struct abt_node *prev_node;
  struct range_tower *new;

  new = range_tower_create_pool__ (pool);
  prev_node = NULL;
  for (old_node = range_tower_first__ (old); old_node != NULL;
       old_node = range_tower_next__ (old, old_node))
    {
      struct range_tower_node *new_node;

      new_node = xmalloc (sizeof *new_node);
      new_node->n_zeros = old_node->n_zeros;
      new_node->n_ones = old_node->n_ones;

      abt_insert_after (&new->abt, prev_node, &new_node->abt_node);
      prev_node = &new_node->abt_node;
    }
  return new;
}

/* Destroys range tower RT. */
void
range_tower_destroy (struct range_tower *rt)
{
  if (rt != NULL)
    {
      if (rt->pool != NULL)
        pool_unregister (rt->pool, rt);
      while (!abt_is_empty (&rt->abt))
        delete_node (rt, range_tower_first__ (rt));
      free (rt);
    }
}

/* Sets the WIDTH bits starting at START in RT to 1-bits. */
void
range_tower_set1 (struct range_tower *rt,
                    unsigned long int start, unsigned long int width)
{
  struct range_tower_node *node;
  unsigned long int node_start;

  assert (width == 0 || start + width - 1 >= start);

  node = range_tower_lookup (rt, start, &node_start);
  while (width > 0)
    {
      unsigned long int node_ofs = start - node_start;

      if (node_ofs >= node->n_zeros)
        {
          /* There are already some 1-bits here, so skip them. */
          unsigned long ones_left = (node->n_zeros + node->n_ones) - node_ofs;
          if (width <= ones_left)
            return;

          start += ones_left;
          width -= ones_left;
          node_start += node->n_zeros + node->n_ones;
          node_ofs = 0;
          node = range_tower_next__ (rt, node);
        }

      /* Invalidate cache. */
      rt->cache_end = 0;

      if (node_ofs == 0)
        {
          if (node_start > 0)
            {
              struct range_tower_node *prev = range_tower_prev__ (rt, node);
              if (width >= node->n_zeros)
                {
                  /* All zeros in NODE are replaced by ones.  Change NODE's
                     entire width into PREV's trailing ones, e.g. 00001111
                     00001111 becomes 0000111111111111. */
                  int node_width = node->n_zeros + node->n_ones;
                  delete_node (rt, node);
                  prev->n_ones += node_width;
                  abt_reaugmented (&rt->abt, &prev->abt_node);
                  if (width <= node_width)
                    return;

                  /* Go around again with NODE replaced by PREV's new
                     successor. */
                  width -= node_width;
                  start += node_width;
                  node = range_tower_next__ (rt, prev);
                  node_start += node_width;
                }
              else
                {
                  /* Leading zeros in NODE change into trailing ones in PREV,
                     but trailing zeros in NODE remain, e.g. 00001111 00001111
                     becomes 0000111111 001111.  */
                  node->n_zeros -= width;
                  abt_reaugmented (&rt->abt, &node->abt_node);

                  prev->n_ones += width;
                  abt_reaugmented (&rt->abt, &prev->abt_node);
                  return;
                }
            }
          else
            {
              if (width >= node->n_zeros)
                {
                  /* All zeros in NODE are replaced by ones, e.g. 00001111
                     becomes 11111111. */
                  node->n_ones += node->n_zeros;
                  node->n_zeros = 0;
                  if (width <= node->n_ones)
                    return;

                  start += node->n_ones;
                  node_start += node->n_ones;
                  width -= node->n_ones;
                  node = range_tower_next__ (rt, node);
                }
              else
                {
                  /* Leading zeros in NODE (which starts at offset 0) are
                     replaced by ones, but some zeros remain.  This requires a
                     node split, e.g. 00001111 becomes 11 001111. */
                  struct range_tower_node *new_node;

                  node->n_zeros -= width;
                  abt_reaugmented (&rt->abt, &node->abt_node);

                  new_node = xmalloc (sizeof *new_node);
                  new_node->n_zeros = 0;
                  new_node->n_ones = width;
                  abt_insert_before (&rt->abt, &node->abt_node,
                                     &new_node->abt_node);
                  return;
                }
            }
        }
      else
        {
          unsigned long int zeros_left = node->n_zeros - node_ofs;
          if (width >= zeros_left)
            {
              /* Trailing zeros in NODE are replaced by ones, but leading
                 zeros remain, e.g. 00001111 becomes 00111111. */
              node->n_zeros -= zeros_left;
              node->n_ones += zeros_left;
              if (width <= node->n_ones)
                return;
              start += node->n_ones;
              width -= node->n_ones;
              node_start += node->n_zeros + node->n_ones;
              node = range_tower_next__ (rt, node);
            }
          else
            {
              /* Zeros that are neither leading or trailing turn into ones.
                 Split the node into two nodes, e.g. 00001111 becomes 011
                 01111.  */
              struct range_tower_node *new_node;

              new_node = xmalloc (sizeof *new_node);
              new_node->n_ones = node->n_ones;
              new_node->n_zeros = zeros_left - width;

              node->n_zeros = node_ofs;
              node->n_ones = width;
              abt_reaugmented (&rt->abt, &node->abt_node);

              abt_insert_after (&rt->abt, &node->abt_node,
                                &new_node->abt_node);
              return;
            }
        }
    }
}

/* Sets the WIDTH bits starting at START in RT to 0-bits. */
void
range_tower_set0 (struct range_tower *rt,
                    unsigned long int start, unsigned long int width)
{
  struct range_tower_node *node;
  unsigned long int node_start;

  assert (width == 0 || start + width - 1 >= start);

  node = range_tower_lookup (rt, start, &node_start);
  while (width > 0)
    {
      unsigned long int node_ofs = start - node_start;

      if (node_ofs < node->n_zeros)
        {
          /* Deleting zeros is a no-op, so skip them. */
          unsigned long zeros_left = node->n_zeros - node_ofs;
          if (zeros_left >= width)
            {
              /* We are deleting only existing zeros.  Nothing to do. */
              return;
            }

          width -= zeros_left;
          start += zeros_left;
          node_ofs = node->n_zeros;
        }

      rt->cache_end = 0;

      if (node_ofs == node->n_zeros)
        {
          if (node->n_ones > width)
            {
              /* DELTA leading ones within NODE turn into zeros, but some ones
                 remain, e.g. 00001111 becomes 00111111.  No reaugmentation
                 because n_zeros + n_ones doesn't change. */
              node->n_zeros += width;
              node->n_ones -= width;
              return;
            }
          else
            {
              /* All ones in NODE turn into zeros, so merge NODE with the
                 following node, e.g. 00001111 00001111 becomes
                 0000000000001111, and then do it again with the merged
                 node. */
              unsigned long int next_zeros, next_ones;
              struct range_tower_node *next;

              next = range_tower_next__ (rt, node);
              if (next == NULL)
                {
                  node->n_zeros += node->n_ones;
                  node->n_ones = 0;
                  return;
                }

              next_zeros = next->n_zeros;
              next_ones = next->n_ones;
              delete_node (rt, next);

              node->n_zeros += node->n_ones + next_zeros;
              node->n_ones = next_ones;
              abt_reaugmented (&rt->abt, &node->abt_node);
            }
        }
      else if (node_ofs + width >= node->n_zeros + node->n_ones)
        {
          /* Trailing ones in NODE turn into zeros, but leading ones remain,
             e.g. 000011{11} 00001111 becomes 000011 {00}00001111.  Give the
             trailing ones to the next node as zeros and go around again with
             the next node. */
          struct range_tower_node *next;
          unsigned long int delta;

          delta = node->n_ones - (node_ofs - node->n_zeros);
          node->n_ones -= delta;
          abt_reaugmented (&rt->abt, &node->abt_node);

          next = range_tower_next__ (rt, node);
          if (next == NULL)
            {
              struct range_tower_node *new_node;

              new_node = xmalloc (sizeof *new_node);
              new_node->n_zeros = delta;
              new_node->n_ones = 0;

              abt_insert_before (&rt->abt, NULL, &new_node->abt_node);
              return;
            }

          next->n_zeros += delta;
          abt_reaugmented (&rt->abt, &next->abt_node);

          node_start += node->n_zeros + node->n_ones;
          start = node_start;
          node = next;
        }
      else
        {
          /* Ones that are neither leading or trailing turn into zeros,
             e.g. 00001111 becomes 00001 001.  Split the node into two nodes
             and we're done. */
          unsigned long int end = start + width;
          struct range_tower_node *new_node;

          new_node = xmalloc (sizeof *new_node);
          new_node->n_zeros = width;
          new_node->n_ones = (node_start + node->n_zeros + node->n_ones) - end;

          node->n_ones = node_ofs - node->n_zeros;
          abt_reaugmented (&rt->abt, &node->abt_node);

          abt_insert_after (&rt->abt, &node->abt_node, &new_node->abt_node);
          return;
        }
    }
}

static void
range_tower_delete__ (struct range_tower *rt,
                      unsigned long int start, unsigned long int width)
{
  struct range_tower_node *node;
  unsigned long int node_start;

  rt->cache_end = 0;
  node = range_tower_lookup (rt, start, &node_start);
  for (;;)
    {
      unsigned long int node_ofs = start - node_start;

      if (node_ofs < node->n_zeros)
        {
          if (node_ofs + width < node->n_zeros)
            {
              node->n_zeros -= width;
              abt_reaugmented (&rt->abt, &node->abt_node);
              break;
            }
          else if (node_ofs > 0)
            {
              width -= node->n_zeros - node_ofs;
              node->n_zeros = node_ofs;
              abt_reaugmented (&rt->abt, &node->abt_node);
              if (width == 0)
                break;
              /* Continue with 1-bits. */
            }
          else if (width < node->n_zeros + node->n_ones)
            {
              struct range_tower_node *prev = range_tower_prev__ (rt, node);
              unsigned long int ones_left;

              ones_left = (node->n_zeros + node->n_ones) - width;
              if (prev != NULL)
                {
                  delete_node (rt, node);
                  prev->n_ones += ones_left;
                  abt_reaugmented (&rt->abt, &prev->abt_node);
                }
              else
                {
                  node->n_zeros = 0;
                  node->n_ones = ones_left;
                  abt_reaugmented (&rt->abt, &node->abt_node);
                }
              break;
            }
          else
            {
              /* Delete entire node. */
              struct range_tower_node *next = range_tower_next__ (rt, node);

              width -= node->n_zeros + node->n_ones;
              delete_node (rt, node);
              if (next == NULL)
                break;

              node = next;
              continue;
            }
        }

      if (node_ofs + width < node->n_zeros + node->n_ones)
        {
          node->n_ones -= width;
          abt_reaugmented (&rt->abt, &node->abt_node);
          break;
        }
      else if (node_ofs > node->n_zeros)
        {
          unsigned long int ones_ofs = node_ofs - node->n_zeros;
          width -= node->n_ones - ones_ofs;
          node->n_ones = ones_ofs;
          abt_reaugmented (&rt->abt, &node->abt_node);
          if (width == 0)
            break;
          /* continue with next node */
          node_start += node->n_zeros + node->n_ones;
          node = range_tower_next__ (rt, node);
        }
      else
        {
          /* Merge with next node */
          struct range_tower_node *next = range_tower_next__ (rt, node);
          if (next != NULL)
            {
              unsigned long int next_zeros = next->n_zeros;
              unsigned long int next_ones = next->n_ones;

              delete_node (rt, next);

              width -= node->n_ones;

              node->n_zeros += next_zeros;
              node->n_ones = next_ones;
              abt_reaugmented (&rt->abt, &node->abt_node);

              if (width == 0)
                break;
            }
          else
            {
              node->n_ones = 0;
              abt_reaugmented (&rt->abt, &node->abt_node);
              break;
            }
        }
    }
}

void
range_tower_delete (struct range_tower *rt,
                    unsigned long int start, unsigned long int width)
{
  struct range_tower_node *node;

  if (width == 0)
    return;

  assert (start + width - 1 >= start);

  range_tower_delete__ (rt, start, width);

  node = range_tower_last__ (rt);
  if (node != NULL && node->n_ones == 0)
    {
      node->n_zeros += width;
      abt_reaugmented (&rt->abt, &node->abt_node);
    }
  else
    {
      struct range_tower_node *new_node;

      new_node = xmalloc (sizeof *new_node);
      new_node->n_zeros = width;
      new_node->n_ones = 0;

      abt_insert_before (&rt->abt, NULL, &new_node->abt_node);
    }
}

static struct range_tower_node *
range_tower_insert0__ (struct range_tower *rt, struct range_tower_node *node,
                       unsigned long int *node_startp,
                       unsigned long int start, unsigned long int width)
{
  unsigned long int node_ofs = start - *node_startp;

  if (node_ofs <= node->n_zeros)
    {
      /* 00+00+001111 => 0000001111. */
      node->n_zeros += width;
      abt_reaugmented (&rt->abt, &node->abt_node);

      return node;
    }
  else
    {
      /* 000011+00+11 => 000011 0011. */
      struct range_tower_node *new_node;

      new_node = xmalloc (sizeof *new_node);
      new_node->n_zeros = width;
      new_node->n_ones = node->n_zeros + node->n_ones - node_ofs;

      node->n_ones -= new_node->n_ones;
      abt_reaugmented (&rt->abt, &node->abt_node);
      abt_insert_after (&rt->abt, &node->abt_node, &new_node->abt_node);

      *node_startp += node->n_zeros + node->n_ones;
      return new_node;
    }
}

void
range_tower_insert0 (struct range_tower *rt,
                     unsigned long int start, unsigned long int width)
{
  if (width == 0)
    return;

  assert (width == 0 || start + width - 1 >= start);

  if (start + width == ULONG_MAX)
    range_tower_set0 (rt, start, width);
  else
    {
      struct range_tower_node *node;
      unsigned long int node_start;

      range_tower_delete__ (rt, ULONG_MAX - width, width);

      node = range_tower_lookup (rt, start, &node_start);
      range_tower_insert0__ (rt, node, &node_start, start, width);
    }
}

static struct range_tower_node *
range_tower_insert1__ (struct range_tower *rt, struct range_tower_node *node,
                       unsigned long int *node_startp,
                       unsigned long int start, unsigned long int width)
{

  unsigned long int node_start = *node_startp;
  unsigned long int node_ofs = start - node_start;

  if (node_ofs >= node->n_zeros)
    {
      node->n_ones += width;
      abt_reaugmented (&rt->abt, &node->abt_node);
      return node;
    }
  else if (node_ofs == 0 && node_start > 0)
    {
      struct range_tower_node *prev = range_tower_prev__ (rt, node);

      prev->n_ones += width;
      abt_reaugmented (&rt->abt, &prev->abt_node);

      *node_startp += width;
      return node;
    }
  else
    {
      /* 00001111 => 0+11+0001111 => 011 0001111 */
      struct range_tower_node *new_node;

      new_node = xmalloc (sizeof *new_node);
      new_node->n_zeros = node->n_zeros - node_ofs;
      new_node->n_ones = node->n_ones;

      node->n_zeros = node_ofs;
      node->n_ones = width;
      abt_reaugmented (&rt->abt, &node->abt_node);

      abt_insert_after (&rt->abt, &node->abt_node, &new_node->abt_node);

      *node_startp += node->n_zeros + node->n_ones;
      return new_node;
    }
}

void
range_tower_insert1 (struct range_tower *rt,
                     unsigned long int start, unsigned long int width)
{
  struct range_tower_node *node;
  unsigned long int node_start;

  if (width == 0)
    return;

  range_tower_delete__ (rt, ULONG_MAX - width, width);

  assert (width == 0 || start + width - 1 >= start);

  node = range_tower_lookup (rt, start, &node_start);
  range_tower_insert1__ (rt, node, &node_start, start, width);
}

void
range_tower_move (struct range_tower *rt,
                  unsigned long int old_start,
                  unsigned long int new_start,
                  unsigned long int width)
{
  unsigned long int node_start;

  if (width == 0 || old_start == new_start)
    return;

  assert (old_start + width - 1 >= old_start);
  assert (new_start + width - 1 >= new_start);

  do
    {
      struct range_tower_node *node;
      unsigned long int node_ofs;
      unsigned long int zeros, ones;

      node = range_tower_lookup (rt, old_start, &node_start);
      node_ofs = old_start - node_start;

      if (node_ofs >= node->n_zeros)
        {
          unsigned long int max_ones;

          zeros = 0;
          max_ones = (node->n_zeros + node->n_ones) - node_ofs;
          ones = MIN (width, max_ones);
        }
      else
        {
          unsigned long int max_zeros;

          max_zeros = node->n_zeros - node_ofs;
          zeros = MIN (width, max_zeros);
          if (zeros < width)
            ones = MIN (width - zeros, node->n_ones);
          else
            ones = 0;
        }

      node->n_zeros -= zeros;
      node->n_ones -= ones;
      abt_reaugmented (&rt->abt, &node->abt_node);

      if (node->n_zeros == 0)
        {
          if (node->n_ones == 0)
            delete_node (rt, node);
          else if (node_start > 0)
            {
              /* Merge with previous. */
              unsigned long int n_ones = node->n_ones;
              struct range_tower_node *prev = range_tower_prev__ (rt, node);

              delete_node (rt, node);
              prev->n_ones += n_ones;
              abt_reaugmented (&rt->abt, &prev->abt_node);
            }
        }
      else if (node->n_ones == 0)
        {
          struct range_tower_node *next = range_tower_next__ (rt, node);
          if (next != NULL)
            {
              /* Merge with next. */
              unsigned long int n_zeros = node->n_zeros;

              delete_node (rt, node);
              next->n_zeros += n_zeros;
              abt_reaugmented (&rt->abt, &next->abt_node);
            }
        }

      if (new_start < old_start)
        {
          node = range_tower_lookup (rt, new_start, &node_start);
          if (zeros)
            {
              node = range_tower_insert0__ (rt, node, &node_start,
                                            new_start, zeros);
              old_start += zeros;
              new_start += zeros;
            }

          if (ones)
            {
              node = range_tower_insert1__ (rt, node, &node_start,
                                            new_start, ones);
              old_start += ones;
              new_start += ones;
            }
        }
      else
        {
          unsigned long int remaining = width - (zeros + ones);

          if (new_start + remaining < ULONG_MAX - (zeros + ones))
            {
              node = range_tower_lookup (rt, new_start + remaining,
                                         &node_start);
              if (zeros)
                {
                  node = range_tower_insert0__ (rt, node, &node_start,
                                                new_start + remaining, zeros);
                  new_start += zeros;
                }

              if (ones)
                {
                  node = range_tower_insert1__ (rt, node, &node_start,
                                                new_start + remaining, ones);
                  new_start += ones;
                }
            }
          else
            {
              node = range_tower_last__ (rt);
              if (zeros)
                {
                  if (node->n_ones)
                    {
                      struct range_tower_node *new_node;

                      new_node = xmalloc (sizeof *new_node);
                      new_node->n_zeros = zeros;
                      new_node->n_ones = 0;

                      abt_insert_after (&rt->abt, &node->abt_node,
                                        &new_node->abt_node);

                      node_start += node->n_zeros + node->n_ones;
                      node = new_node;
                    }
                  else
                    {
                      node->n_zeros += zeros;
                      abt_reaugmented (&rt->abt, &node->abt_node);
                    }
                }
              if (ones)
                {
                  node->n_ones += ones;
                  abt_reaugmented (&rt->abt, &node->abt_node);
                }

              new_start += zeros + ones;
            }
        }
      width -= zeros + ones;
    }
  while (width > 0);
}

/* Returns true if there is a 1-bit at the given POSITION in RT,
   false otherwise. */
bool
range_tower_contains (const struct range_tower *rt_, unsigned long int position)
{
  struct range_tower *rt = CONST_CAST (struct range_tower *, rt_);
  if (position >= rt->cache_end || position < rt->cache_start)
    {
      struct range_tower_node *node;
      unsigned long int node_start;

      node = range_tower_lookup (rt, position, &node_start);
      if (position < node_start + node->n_zeros)
        {
          rt->cache_start = node_start;
          rt->cache_end = node_start + node->n_zeros;
          rt->cache_value = false;
        }
      else
        {
          rt->cache_start = node_start + node->n_zeros;
          rt->cache_end = rt->cache_start + node->n_ones;
          rt->cache_value = true;
        }
    }
  return rt->cache_value;
}

/* Returns the smallest position of a 1-bit greater than or
   equal to START.  Returns ULONG_MAX if there is no 1-bit with
   position greater than or equal to START. */
unsigned long int
range_tower_scan (const struct range_tower *rt_, unsigned long int start)
{
  struct range_tower *rt = CONST_CAST (struct range_tower *, rt_);

  if (start < rt->cache_end && start >= rt->cache_start && rt->cache_value)
    return start;

  if (start != ULONG_MAX)
    {
      struct range_tower_node *node;
      unsigned long int node_start;

      node = range_tower_lookup (rt, start, &node_start);
      if (node->n_ones)
        {
          rt->cache_start = node_start + node->n_zeros;
          rt->cache_end = rt->cache_start + node->n_ones;
          rt->cache_value = true;
          return MAX (start, rt->cache_start);
        }
      else
        {
          rt->cache_start = node_start;
          rt->cache_end = ULONG_MAX;
          rt->cache_value = false;
        }
    }
  return ULONG_MAX;
}

/* Recalculates the subtree_width of NODE based on its LEFT and
   RIGHT children's "subtree_width"s. */
static void
reaugment_range_tower_node (struct abt_node *node_, const void *aux UNUSED)
{
  struct range_tower_node *node = range_tower_node_from_abt_node (node_);

  node->subtree_width = node->n_zeros + node->n_ones;
  if (node->abt_node.down[0] != NULL)
    {
      struct range_tower_node *left;

      left = range_tower_node_from_abt_node (node->abt_node.down[0]);
      node->subtree_width += left->subtree_width;
    }
  if (node->abt_node.down[1] != NULL)
    {
      struct range_tower_node *right;

      right = range_tower_node_from_abt_node (node->abt_node.down[1]);
      node->subtree_width += right->subtree_width;
    }
}

/* Deletes NODE from RT and frees it. */
static void
delete_node (struct range_tower *rt, struct range_tower_node *node)
{
  abt_delete (&rt->abt, &node->abt_node);
  free (node);
}

struct range_tower_node *
range_tower_lookup (const struct range_tower *rt, unsigned long int position,
                    unsigned long int *node_start)
{
  const struct range_tower_node *node;
  const struct abt_node *abt_node;

  abt_node = rt->abt.root;
  node = range_tower_node_from_abt_node (abt_node);
  *node_start = subtree_width (abt_node->down[0]);
  for (;;)
    {
      unsigned long int left_width = subtree_width (abt_node->down[0]);

      if (position < left_width)
        {
          abt_node = abt_node->down[0];
          *node_start -= left_width - subtree_width (abt_node->down[0]);
        }
      else
        {
          unsigned long int node_width = node->n_zeros + node->n_ones;

          position -= left_width;
          if (position < node_width)
            return CONST_CAST (struct range_tower_node *, node);

          position -= node_width;
          abt_node = abt_node->down[1];
          left_width = subtree_width (abt_node->down[0]);
          *node_start += node_width + left_width;
        }
      node = range_tower_node_from_abt_node (abt_node);
    }
}

/* Destroys range tower RT.
   Helper function for range_tower_create_pool. */
static void
destroy_pool (void *rt_)
{
  struct range_tower *rt = rt_;
  rt->pool = NULL;
  range_tower_destroy (rt);
}
