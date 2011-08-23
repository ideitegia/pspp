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

#include <config.h>

#include "libpspp/tower.h"

#include <limits.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"

static struct tower_node *abt_to_tower_node (const struct abt_node *);
static struct tower_node *first_node (const struct tower *);
static struct tower_node *last_node (const struct tower *);
static struct tower_node *next_node (const struct tower *,
                                     const struct tower_node *);
static struct tower_node *prev_node (const struct tower *,
                                     const struct tower_node *);
static unsigned long int get_subtree_size (const struct abt_node *);
static unsigned long int get_subtree_count (const struct abt_node *);
static void reaugment_tower_node (struct abt_node *, const void *aux);

/* Returns the height of the bottom of the given tower NODE.

   The performance of this function is O(lg n) in the number of
   nodes in the tower.  It is often possible to avoid calling
   this function, either by taking advantage of the NODE_START
   parameter to tower_lookup or by incrementally keeping track of
   height while iterating through a tower.  In the former case
   the asymptotic performance is no different, since tower_lookup
   is also O(lg n), but in the latter case performance improves
   from O(lg n) to O(1). */
unsigned long int
tower_node_get_level (const struct tower_node *node)
{
  const struct abt_node *p = &node->abt_node;
  unsigned long level = get_subtree_size (p->down[0]);
  while (p->up != NULL) 
    {
      if (p == p->up->down[1])
        level += (get_subtree_size (p->up->down[0]) 
                  + abt_to_tower_node (p->up)->size);
      p = p->up;
    }
  return level;
}

/* Returns the index of the given tower NODE.

   The performance of this function is O(lg n) in the number of
   nodes in the tower.  It is often possible to avoid calling
   this function by keeping track of the index while iterating
   through a tower.  Doing so when possible will improve
   performance from O(lg n) to O(1). */
unsigned long int
tower_node_get_index (const struct tower_node *node)
{
  const struct abt_node *p = &node->abt_node;
  unsigned long index = get_subtree_count (p->down[0]);
  while (p->up != NULL) 
    {
      if (p == p->up->down[1])
        index += get_subtree_count (p->up->down[0]) + 1;
      p = p->up;
    }
  return index;
}

/* Initializes T as an empty tower. */
void
tower_init (struct tower *t)
{
  abt_init (&t->abt, NULL, reaugment_tower_node, NULL);
  t->cache_bottom = ULONG_MAX;
}

/* Returns true if T contains no nodes, false otherwise. */
bool
tower_is_empty (const struct tower *t)
{
  return t->abt.root == NULL;
}

/* Returns the number of nodes in tower T. */
unsigned long int
tower_count (const struct tower *t)
{
  return get_subtree_count (t->abt.root);
}

/* Returns the total height of tower T. */
unsigned long
tower_height (const struct tower *t)
{
  return get_subtree_size (t->abt.root);
}

/* Inserts node NEW with the specified SIZE into T just below
   node UNDER, or at the top of T if UNDER is a null pointer. */
void
tower_insert (struct tower *t, unsigned long size, struct tower_node *new,
              struct tower_node *under)
{
  assert (size > 0);
  new->size = size;
  abt_insert_before (&t->abt, under ? &under->abt_node : NULL,
                     &new->abt_node);
  t->cache_bottom = ULONG_MAX;
}

/* Deletes NODE from tower T. */
struct tower_node *
tower_delete (struct tower *t, struct tower_node *node)
{
  struct tower_node *next = next_node (t, node);
  abt_delete (&t->abt, &node->abt_node);
  t->cache_bottom = ULONG_MAX;
  return next;
}

/* Changes the size of NODE in tower T to NEW_SIZE. */
void
tower_resize (struct tower *t, struct tower_node *node,
              unsigned long new_size)
{
  assert (new_size > 0);
  node->size = new_size;
  abt_reaugmented (&t->abt, &node->abt_node);
  t->cache_bottom = ULONG_MAX;
}

/* Removes nodes FIRST through LAST (exclusive) from tower SRC
   and splices them into tower DST just below node UNDER, or at
   the top of DST if UNDER is a null pointer.

   It might be better to implement an abt_splice function and
   turn this into a wrapper, but the asymptotic performance would
   be the same. */
void
tower_splice (struct tower *dst, struct tower_node *under,
              struct tower *src,
              struct tower_node *first, struct tower_node *last)
{
  struct tower_node *next;

  /* Conceptually, DST == SRC is valid.
     Practically, it's more difficult to get it right, and our
     client code doesn't need it. */
  assert (dst != src);

  for (; first != last; first = next)
    {
      next = tower_delete (src, first);
      abt_insert_before (&dst->abt, under ? &under->abt_node : NULL,
                         &first->abt_node);
    }
  dst->cache_bottom = src->cache_bottom = ULONG_MAX;
}

/* Returns the node at the given HEIGHT from the bottom of tower
   T.  HEIGHT must be less than T's height (as returned by
   tower_height).  Stores in *NODE_START the height of the bottom
   of the returned node, which may be less than HEIGHT if HEIGHT
   refers to the middle of a node instead of its bottom. */
struct tower_node *
tower_lookup (const struct tower *t_,
              unsigned long height,
              unsigned long *node_start)
{
  struct tower *t = CONST_CAST (struct tower *, t_);
  struct abt_node *p;

  assert (height < tower_height (t));

  if (height >= t->cache_bottom && height - t->cache_bottom < t->cache->size)
    {
      *node_start = t->cache_bottom;
      return t->cache;
    }

  *node_start = 0;
  p = t->abt.root;
  for (;;)
    {
      unsigned long left_size = get_subtree_size (p->down[0]);
      if (height < left_size)
        {
          /* Our goal height must lie within the left subtree. */
          p = p->down[0];
        }
      else
        {
          /* Our goal height cannot be in the left subtree. */
          struct tower_node *node = abt_to_tower_node (p);
          unsigned long int node_size = node->size;

          height -= left_size;
          *node_start += left_size;
          if (height < node_size)
            {
              /* Our goal height is in P. */
              t->cache = node;
              t->cache_bottom = *node_start;
              return node;
            }
          else
            {
              /* Our goal height is in the right subtree. */
              p = p->down[1];
              height -= node_size;
              *node_start += node_size;
            }
        }
    }
}

/* Returns the node with the given 0-based INDEX, which must be
   less than the number of nodes in T (as returned by
   tower_count). */
struct tower_node *
tower_get (const struct tower *t_, unsigned long int index) 
{
  struct tower *t = CONST_CAST (struct tower *, t_);
  struct abt_node *p;

  assert (index < tower_count (t));

  p = t->abt.root;
  for (;;)
    {
      unsigned long left_count = get_subtree_count (p->down[0]);
      if (index < left_count)
        p = p->down[0];
      else if (index == left_count)
        return abt_to_tower_node (p);
      else
        {
          p = p->down[1];
          index -= left_count + 1;
        }
    }
}

/* Returns the node at height 0 in tower T, or a null pointer if
   T is empty. */
struct tower_node *
tower_first (const struct tower *t)
{
  return first_node (t);
}

/* Returns the node at the top of tower T, or a null pointer if T
   is empty. */
struct tower_node *
tower_last (const struct tower *t)
{
  return last_node (t);
}

/* If NODE is nonnull, returns the node just above NODE in tower
   T, or a null pointer if NODE is the topmost node in T.
   If NODE is null, acts like tower_first. */
struct tower_node *
tower_next (const struct tower *t, const struct tower_node *node)
{
  return node != NULL ? next_node (t, node) : first_node (t);
}

/* If NODE is nonnull, returns the node just below NODE in tower
   T, or a null pointer if NODE is the bottommost node in T.
   If NODE is null, acts like tower_last. */
struct tower_node *
tower_prev (const struct tower *t, const struct tower_node *node)
{
  return node != NULL ? prev_node (t, node) : last_node (t);
}

/* Returns the tower node corresponding to the given ABT_NODE. */
static struct tower_node *
abt_to_tower_node (const struct abt_node *abt_node)
{
  return abt_data (abt_node, struct tower_node, abt_node);
}

/* Returns the tower node corresponding to the given ABT_NODE. */
static struct tower_node *
abt_to_tower_node_null (const struct abt_node *abt_node)
{
  return abt_node != NULL ? abt_to_tower_node (abt_node) : NULL;
}

/* Returns the first node in TOWER. */
static struct tower_node *
first_node (const struct tower *t)
{
  return abt_to_tower_node_null (abt_first (&t->abt));
}

/* Returns the first node in TOWER. */
static struct tower_node *
last_node (const struct tower *t)
{
  return abt_to_tower_node_null (abt_last (&t->abt));
}

/* Returns the next node in TOWER after NODE. */
static struct tower_node *
next_node (const struct tower *t, const struct tower_node *node)
{
  return abt_to_tower_node_null (abt_next (&t->abt, &node->abt_node));
}

/* Returns the previous node in TOWER before NODE. */
static struct tower_node *
prev_node (const struct tower *t, const struct tower_node *node)
{
  return abt_to_tower_node_null (abt_prev (&t->abt, &node->abt_node));
}

/* Returns the total size of the nodes in the subtree rooted at
   P, or 0 if P is null. */
static unsigned long int
get_subtree_size (const struct abt_node *p)
{
  return p != NULL ? abt_to_tower_node (p)->subtree_size : 0;
}

/* Returns the total number of nodes in the subtree rooted at P,
   or 0 if P is null. */
static unsigned long int
get_subtree_count (const struct abt_node *p)
{
  return p != NULL ? abt_to_tower_node (p)->subtree_count : 0;
}

/* Recalculates the subtree_size of NODE based on the subtree_sizes of its
   children. */
static void
reaugment_tower_node (struct abt_node *node_, const void *aux UNUSED)
{
  struct tower_node *node = abt_to_tower_node (node_);
  node->subtree_size = node->size;
  node->subtree_count = 1;

  if (node->abt_node.down[0] != NULL)
    {
      struct tower_node *left = abt_to_tower_node (node->abt_node.down[0]);
      node->subtree_size += left->subtree_size;
      node->subtree_count += left->subtree_count;
    }

  if (node->abt_node.down[1] != NULL)
    {
      struct tower_node *right = abt_to_tower_node (node->abt_node.down[1]);
      node->subtree_size += right->subtree_size;
      node->subtree_count += right->subtree_count;
    }
}
