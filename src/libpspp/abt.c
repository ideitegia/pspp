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

/* Augmented binary tree (ABT) data structure. */

/* These library routines have no external dependencies other
   than the standard C library.

   If you add routines in this file, please add a corresponding
   test to abt-test.c.  This test program should achieve 100%
   coverage of lines and branches in this code, as reported by
   "gcov -b". */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "libpspp/abt.h"

#include <stdbool.h>

#include "libpspp/cast.h"
#include "libpspp/assertion.h"

static struct abt_node **down_link (struct abt *, struct abt_node *);
static struct abt_node *skew (struct abt *, struct abt_node *);
static struct abt_node *split (struct abt *, struct abt_node *);

/* Initializes ABT as an empty ABT that uses COMPARE and
   REAUGMENT functions, passing in AUX as auxiliary data.

   The comparison function is optional.  If it is null, this
   indicates that the tree is being used for its augmentations
   only.  ABT functions that compare nodes may not be used with
   trees that lack comparison functions; contrariwise, other
   functions that could disrupt the ordering of a tree may not be
   used if a comparison function is specified.  Refer to
   individual function descriptions for details. */
void
abt_init (struct abt *abt,
          abt_compare_func *compare,
          abt_reaugment_func *reaugment,
          const void *aux)
{
  assert (reaugment != NULL);
  abt->root = NULL;
  abt->compare = compare;
  abt->reaugment = reaugment;
  abt->aux = aux;
}

/* Inserts the given NODE into ABT.
   Returns a null pointer if successful.
   Returns the existing node already in ABT equal to NODE, on
   failure.
   This function may be used only if ABT has a comparison
   function. */
struct abt_node *
abt_insert (struct abt *abt, struct abt_node *node)
{
  node->down[0] = NULL;
  node->down[1] = NULL;
  node->level = 1;

  if (abt->root == NULL)
    {
      abt->root = node;
      node->up = NULL;
      abt_reaugmented (abt, node);
    }
  else
    {
      struct abt_node *p = abt->root;
      for (;;)
        {
          int cmp, dir;

          cmp = abt->compare (node, p, abt->aux);
          if (cmp == 0)
            return p;

          dir = cmp > 0;
          if (p->down[dir] == NULL)
            {
              p->down[dir] = node;
              node->up = p;
              abt_reaugmented (abt, node);
              break;
            }
          p = p->down[dir];
        }
    }

  while ((node = node->up) != NULL)
    {
      node = skew (abt, node);
      node = split (abt, node);
    }

  return NULL;
}

/* Inserts NODE before or after node P in ABT, depending on the
   value of AFTER.
   If P is null, then the node is inserted as the first node in
   the tree, if AFTER is true, or the last node, if AFTER is
   false. */
static inline void
insert_relative (struct abt *abt, const struct abt_node *p, bool after,
                 struct abt_node *node)
{
  node->down[0] = NULL;
  node->down[1] = NULL;
  node->level = 1;

  if (abt->root == NULL)
    {
      assert (p == NULL);
      abt->root = node;
      node->up = NULL;
      abt_reaugmented (abt, node);
    }
  else
    {
      int dir = after;
      if (p == NULL)
        {
          p = abt->root;
          dir = !after;
        }
      while (p->down[dir] != NULL)
        {
          p = p->down[dir];
          dir = !after;
        }
      CONST_CAST (struct abt_node *, p)->down[dir] = node;
      node->up = CONST_CAST (struct abt_node *, p);
      abt_reaugmented (abt, node);
    }

  while ((node = node->up) != NULL)
    {
      node = skew (abt, node);
      node = split (abt, node);
    }
}

/* Inserts NODE after node P in ABT.
   If P is null, then the node is inserted as the first node in
   the tree.
   This function may be used only if ABT lacks a comparison
   function. */
void
abt_insert_after (struct abt *abt,
                  const struct abt_node *p, struct abt_node *node)
{
  assert (abt->compare == NULL);
  insert_relative (abt, p, true, node);
}

/* Inserts NODE before node P in ABT.
   If P is null, then the node is inserted as the last node in
   the tree.
   This function may be used only if ABT lacks a comparison
   function. */
void
abt_insert_before (struct abt *abt,
                   const struct abt_node *p, struct abt_node *node)
{
  assert (abt->compare == NULL);
  insert_relative (abt, p, false, node);
}

/* Deletes P from ABT. */
void
abt_delete (struct abt *abt, struct abt_node *p)
{
  struct abt_node **q = down_link (abt, p);
  struct abt_node *r = p->down[1];
  if (r == NULL)
    {
      *q = NULL;
      p = p->up;
    }
  else if (r->down[0] == NULL)
    {
      r->down[0] = p->down[0];
      *q = r;
      r->up = p->up;
      if (r->down[0] != NULL)
        r->down[0]->up = r;
      r->level = p->level;
      p = r;
    }
  else
    {
      struct abt_node *s = r->down[0];
      while (s->down[0] != NULL)
        s = s->down[0];
      r = s->up;
      r->down[0] = s->down[1];
      s->down[0] = p->down[0];
      s->down[1] = p->down[1];
      *q = s;
      s->down[0]->up = s;
      s->down[1]->up = s;
      s->up = p->up;
      s->level = p->level;
      if (r->down[0] != NULL)
        r->down[0]->up = r;
      p = r;
    }
  abt_reaugmented (abt, p);

  for (; p != NULL; p = p->up)
    if ((p->down[0] != NULL ? p->down[0]->level : 0) < p->level - 1
        || (p->down[1] != NULL ? p->down[1]->level : 0) < p->level - 1)
      {
        p->level--;
        if (p->down[1] != NULL && p->down[1]->level > p->level)
          p->down[1]->level = p->level;

        p = skew (abt, p);
        skew (abt, p->down[1]);
        if (p->down[1]->down[1] != NULL)
          skew (abt, p->down[1]->down[1]);

        p = split (abt, p);
        split (abt, p->down[1]);
      }
}

/* Returns the node with minimum value in ABT, or a null pointer
   if ABT is empty. */
struct abt_node *
abt_first (const struct abt *abt)
{
  struct abt_node *p = abt->root;
  if (p != NULL)
    while (p->down[0] != NULL)
      p = p->down[0];
  return p;
}

/* Returns the node with maximum value in ABT, or a null pointer
   if ABT is empty. */
struct abt_node *
abt_last (const struct abt *abt)
{
  struct abt_node *p = abt->root;
  if (p != NULL)
    while (p->down[1] != NULL)
      p = p->down[1];
  return p;
}

/* Searches ABT for a node equal to TARGET.
   Returns the node if found, or a null pointer otherwise.
   This function may be used only if ABT has a comparison
   function. */
struct abt_node *
abt_find (const struct abt *abt, const struct abt_node *target)
{
  const struct abt_node *p;
  int cmp;

  for (p = abt->root; p != NULL; p = p->down[cmp > 0])
    {
      cmp = abt->compare (target, p, abt->aux);
      if (cmp == 0)
        return CONST_CAST (struct abt_node *, p);
    }

  return NULL;
}

/* Returns the node in ABT following P in in-order.
   If P is null, returns the minimum node in ABT.
   Returns a null pointer if P is the maximum node in ABT or if P
   is null and ABT is empty. */
struct abt_node *
abt_next (const struct abt *abt, const struct abt_node *p)
{
  if (p == NULL)
    return abt_first (abt);
  else if (p->down[1] == NULL)
    {
      struct abt_node *q;
      for (q = p->up; ; p = q, q = q->up)
        if (q == NULL || p == q->down[0])
          return q;
    }
  else
    {
      p = p->down[1];
      while (p->down[0] != NULL)
        p = p->down[0];
      return CONST_CAST (struct abt_node *, p);
    }
}

/* Returns the node in ABT preceding P in in-order.
   If P is null, returns the maximum node in ABT.
   Returns a null pointer if P is the minimum node in ABT or if P
   is null and ABT is empty. */
struct abt_node *
abt_prev (const struct abt *abt, const struct abt_node *p)
{
  if (p == NULL)
    return abt_last (abt);
  else if (p->down[0] == NULL)
    {
      struct abt_node *q;
      for (q = p->up; ; p = q, q = q->up)
        if (q == NULL || p == q->down[1])
          return q;
    }
  else
    {
      p = p->down[0];
      while (p->down[1] != NULL)
        p = p->down[1];
      return CONST_CAST (struct abt_node *, p);
    }
}

/* Calls ABT's reaugmentation function to compensate for
   augmentation data in P having been modified.  Use abt_changed,
   instead, if the key data in P has changed.

   It is not safe to update more than one node's augmentation
   data, then to call this function for each node.  Instead,
   update a single node's data, call this function, update
   another node's data, and so on.  Alternatively, remove all
   affected nodes from the tree, update their values, then
   re-insert all of them. */
void
abt_reaugmented (const struct abt *abt, struct abt_node *p)
{
  for (; p != NULL; p = p->up)
    abt->reaugment (p, abt->aux);
}

/* Moves P around in ABT to compensate for its key having
   changed.  Returns a null pointer if successful.  If P's new
   value is equal to that of some other node in ABT, returns the
   other node after removing P from ABT.

   This function is an optimization only if it is likely that P
   can actually retain its relative position in ABT, e.g. its key
   has only been adjusted slightly.  Otherwise, it is more
   efficient to simply remove P from ABT, change its key, and
   re-insert P.

   It is not safe to update more than one node's key, then to
   call this function for each node.  Instead, update a single
   node's key, call this function, update another node's key, and
   so on.  Alternatively, remove all affected nodes from the
   tree, update their keys, then re-insert all of them.

   This function may be used only if ABT has a comparison
   function.  If it doesn't, then you probably just want
   abt_reaugmented. */
struct abt_node *
abt_changed (struct abt *abt, struct abt_node *p)
{
  struct abt_node *prev = abt_prev (abt, p);
  struct abt_node *next = abt_next (abt, p);

  if ((prev != NULL && abt->compare (prev, p, abt->aux) >= 0)
      || (next != NULL && abt->compare (p, next, abt->aux) >= 0))
    {
      abt_delete (abt, p);
      return abt_insert (abt, p);
    }
  else
    {
      abt_reaugmented (abt, p);
      return NULL;
    }
}

/* ABT nodes may be moved around in memory as necessary, e.g. as
   the result of an realloc operation on a block that contains a
   node.  Once this is done, call this function passing node P
   that was moved and its ABT before attempting any other
   operation on ABT.

   It is not safe to move more than one node, then to call this
   function for each node.  Instead, move a single node, call
   this function, move another node, and so on.  Alternatively,
   remove all affected nodes from the tree, move them, then
   re-insert all of them.

   This function may be used only if ABT has a comparison
   function. */
void
abt_moved (struct abt *abt, struct abt_node *p)
{
  if (p->up != NULL)
    {
      int d = p->up->down[0] == NULL || abt->compare (p, p->up, abt->aux) > 0;
      p->up->down[d] = p;
    }
  else
    abt->root = p;
  if (p->down[0] != NULL)
    p->down[0]->up = p;
  if (p->down[1] != NULL)
    p->down[1]->up = p;
}

/* Returns the address of the pointer that points down to P
   within ABT. */
static struct abt_node **
down_link (struct abt *abt, struct abt_node *p)
{
  return (p->up != NULL
          ? &p->up->down[p->up->down[0] != p]
          : &abt->root);
}

/* Remove a left "horizontal link" at A, if present.
   Returns the node that occupies the position previously
   occupied by A. */
static struct abt_node *
skew (struct abt *abt, struct abt_node *a)
{
  struct abt_node *b = a->down[0];
  if (b != NULL && b->level == a->level)
    {
      /* Rotate right. */
      a->down[0] = b->down[1];
      b->down[1] = a;
      *down_link (abt, a) = b;

      if (a->down[0] != NULL)
        a->down[0]->up = a;
      b->up = a->up;
      a->up = b;

      abt->reaugment (a, abt->aux);
      abt->reaugment (b, abt->aux);

      return b;
    }
  else
    return a;
}

/* Removes a pair of consecutive right "horizontal links" at A,
   if present.
   Returns the node that occupies the position previously
   occupied by A. */
static struct abt_node *
split (struct abt *abt, struct abt_node *a)
{
  struct abt_node *b = a->down[1];
  if (b != NULL && b->down[1] != NULL && b->down[1]->level == a->level)
    {
      /* Rotate left. */
      a->down[1] = b->down[0];
      b->down[0] = a;
      *down_link (abt, a) = b;

      if (a->down[1] != NULL)
        a->down[1]->up = a;
      b->up = a->up;
      a->up = b;

      b->level++;

      abt->reaugment (a, abt->aux);
      abt->reaugment (b, abt->aux);

      return b;
    }
  else
    return a;
}
