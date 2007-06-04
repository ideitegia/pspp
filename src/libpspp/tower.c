/* PSPP - computes sample statistics.
   Copyright (C) 2007 Free Software Foundation, Inc.

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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>

#include <libpspp/tower.h>

#include <limits.h>

#include <libpspp/assertion.h>
#include <libpspp/compiler.h>

static struct tower_node *abt_to_tower_node (const struct abt_node *);
static struct tower_node *first_node (const struct tower *);
static struct tower_node *next_node (const struct tower *,
                                     const struct tower_node *);
static unsigned long int get_subtree_height (const struct abt_node *);
static void reaugment_tower_node (struct abt_node *,
                                  const struct abt_node *,
                                  const struct abt_node *,
                                  const void *aux);

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

/* Returns the total height of tower T. */
unsigned long
tower_height (const struct tower *t) 
{
  return get_subtree_height (t->abt.root);
}

/* Inserts node NEW with the specified HEIGHT into T just below
   node UNDER, or at the top of T if UNDER is a null pointer. */
void
tower_insert (struct tower *t, unsigned long height, struct tower_node *new,
              struct tower_node *under) 
{
  assert (height > 0);
  new->height = height;
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

/* Changes the height of NODE in tower T to NEW_HEIGHT. */
void
tower_resize (struct tower *t, struct tower_node *node,
              unsigned long new_height)
{
  assert (new_height > 0);
  node->height = new_height;
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
  struct tower *t = (struct tower *) t_;
  struct abt_node *p;

  assert (height < tower_height (t));

  if (height >= t->cache_bottom && height - t->cache_bottom < t->cache->height)
    {
      *node_start = t->cache_bottom;
      return t->cache; 
    }

  *node_start = 0;
  p = t->abt.root;
  for (;;)
    {
      unsigned long left_height = get_subtree_height (p->down[0]);
      if (height < left_height) 
        {
          /* Our goal height must lie within the left subtree. */
          p = p->down[0];
        }
      else 
        {
          /* Our goal height cannot be in the left subtree. */
          struct tower_node *node = abt_to_tower_node (p);
          unsigned long int node_height = node->height;

          height -= left_height;
          *node_start += left_height;
          if (height < node_height) 
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
              height -= node_height;
              *node_start += node_height; 
            }
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

/* If NODE is nonnull, returns the node just above NODE in tower
   T, or a null pointer if NODE is the topmost node in T.
   If NODE is null, acts like tower_first. */
struct tower_node *
tower_next (const struct tower *t, const struct tower_node *node) 
{
  return node != NULL ? next_node (t, node) : first_node (t);
}

/* Returns the tower node corresponding to the given ABT_NODE. */
static struct tower_node *
abt_to_tower_node (const struct abt_node *abt_node) 
{
  return abt_data (abt_node, struct tower_node, abt_node);
}

/* Returns the first node in TOWER. */
static struct tower_node *
first_node (const struct tower *t) 
{
  struct abt_node *abt_node = abt_first (&t->abt);
  return abt_node != NULL ? abt_to_tower_node (abt_node) : NULL;
}

/* Returns the next node in TOWER after NODE. */
static struct tower_node *
next_node (const struct tower *t, const struct tower_node *node) 
{
  struct abt_node *abt_node = abt_next (&t->abt, &node->abt_node);
  return abt_node != NULL ? abt_to_tower_node (abt_node) : NULL;
}

/* Returns the total height of the nodes in the subtree rooted at
   P, or 0 if P is null. */
static unsigned long int
get_subtree_height (const struct abt_node *p) 
{
  return p != NULL ? abt_to_tower_node (p)->subtree_height : 0;
}

/* Recalculates the subtree_height of NODE based on its LEFT and
   RIGHT children's subtree_heights. */
static void
reaugment_tower_node (struct abt_node *node_,
                      const struct abt_node *left,
                      const struct abt_node *right,
                      const void *aux UNUSED)
{
  struct tower_node *node = abt_to_tower_node (node_);
  node->subtree_height = node->height;
  if (left != NULL)
    node->subtree_height += abt_to_tower_node (left)->subtree_height;
  if (right != NULL)
    node->subtree_height += abt_to_tower_node (right)->subtree_height;
}
