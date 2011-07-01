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

#include "libpspp/taint.h"

#include <stddef.h>

#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"

#include "gl/xalloc.h"

/* This code maintains two invariants:

   1. If a node is tainted, then all of its successors are
      tainted.

   2. If a node is tainted, then it and all of its predecessors are
      successor-tainted. */

/* A list of pointers to taint structures. */
struct taint_list
  {
    size_t cnt;
    struct taint **taints;
  };

static void taint_list_init (struct taint_list *);
static void taint_list_destroy (struct taint_list *);
static void taint_list_add (struct taint_list *, struct taint *);
static void taint_list_remove (struct taint_list *, const struct taint *);

/* A taint. */
struct taint
  {
    size_t ref_cnt;                     /* Number of owners. */
    struct taint_list successors;       /* Successors in graph. */
    struct taint_list predecessors;     /* Predecessors in graph. */
    bool tainted;                       /* Is this node tainted? */
    bool tainted_successor;             /* Is/was any derived taint tainted? */
  };

static void recursively_set_taint (struct taint *);
static void recursively_set_tainted_successor (struct taint *);

/* Creates and returns a new taint object. */
struct taint *
taint_create (void)
{
  struct taint *taint = xmalloc (sizeof *taint);
  taint->ref_cnt = 1;
  taint_list_init (&taint->successors);
  taint_list_init (&taint->predecessors);
  taint->tainted = false;
  taint->tainted_successor = false;
  return taint;
}

/* Returns a clone of the given TAINT.
   The new and old taint objects are logically indistinguishable,
   as if they were the same object.  (In this implementation,
   they are in fact the same object, but this is not a guarantee
   made by the interface.) */
struct taint *
taint_clone (const struct taint *taint_)
{
  struct taint *taint = CONST_CAST (struct taint *, taint_);

  assert (taint->ref_cnt > 0);
  taint->ref_cnt++;
  return taint;
}

/* Destroys the given TAINT.
   Returns false if TAINT was tainted, true otherwise.
   Any propagation relationships through TAINT are preserved.
   That is, if A taints B and B taints C, then destroying B will
   preserve the transitive relationship, so that tainting A will
   still taint C. */
bool
taint_destroy (struct taint *taint)
{
  if ( taint )
    {
      bool was_tainted = taint_is_tainted (taint);
      if (--taint->ref_cnt == 0)
	{
	  size_t i, j;

	  for (i = 0; i < taint->predecessors.cnt; i++)
	    for (j = 0; j < taint->successors.cnt; j++)
	      taint_propagate (taint->predecessors.taints[i],
			       taint->successors.taints[j]);

	  for (i = 0; i < taint->predecessors.cnt; i++)
	    taint_list_remove (&taint->predecessors.taints[i]->successors, taint);
	  for (i = 0; i < taint->successors.cnt; i++)
	    taint_list_remove (&taint->successors.taints[i]->predecessors, taint);

	  taint_list_destroy (&taint->successors);
	  taint_list_destroy (&taint->predecessors);
	  free (taint);
	}
      return !was_tainted;
    }

  return true;
}

/* Adds a propagation relationship from FROM to TO.  This means
   that, should FROM ever become tainted, then TO will
   automatically be marked tainted as well.  This takes effect
   immediately: if FROM is currently tainted, then TO will be
   tainted after the call completes.

   Taint propagation is transitive: if A propagates to B and B
   propagates to C, then tainting A taints both B and C.  Taint
   propagation is not commutative: propagation from A to B does
   not imply propagation from B to A.  Taint propagation is
   robust against loops, so that if A propagates to B and vice
   versa, whether directly or indirectly, then tainting either A
   or B will cause the other to be tainted, without producing an
   infinite loop. */
void
taint_propagate (const struct taint *from_, const struct taint *to_)
{
  struct taint *from = CONST_CAST (struct taint *, from_);
  struct taint *to = CONST_CAST (struct taint *, to_);

  if (from != to)
    {
      taint_list_add (&from->successors, to);
      taint_list_add (&to->predecessors, from);
      if (from->tainted && !to->tainted)
        recursively_set_taint (to);
      else if (to->tainted_successor && !from->tainted_successor)
        recursively_set_tainted_successor (from);
    }
}

/* Returns true if TAINT is tainted, false otherwise. */
bool
taint_is_tainted (const struct taint *taint)
{
  return taint->tainted;
}

/* Marks TAINT tainted and propagates the taint to all of its
   successors. */
void
taint_set_taint (const struct taint *taint_)
{
  struct taint *taint = CONST_CAST (struct taint *, taint_);
  if (!taint->tainted)
    recursively_set_taint (taint);
}

/* Returns true if TAINT is successor-tainted, that is, if it or
   any of its successors is or ever has been tainted.  (A
   "successor" of a taint object X is any taint object that can
   be reached by following propagation relationships starting
   from X.) */
bool
taint_has_tainted_successor (const struct taint *taint)
{
  return taint->tainted_successor;
}

/* Attempts to reset the successor-taint on TAINT.  This is
   successful only if TAINT currently has no tainted successor. */
void
taint_reset_successor_taint (const struct taint *taint_)
{
  struct taint *taint = CONST_CAST (struct taint *, taint_);

  if (taint->tainted_successor)
    {
      size_t i;

      for (i = 0; i < taint->successors.cnt; i++)
        if (taint->successors.taints[i]->tainted_successor)
          return;

      taint->tainted_successor = false;
    }
}

/* Initializes LIST as an empty list of taints. */
static void
taint_list_init (struct taint_list *list)
{
  list->cnt = 0;
  list->taints = NULL;
}

/* Destroys LIST. */
static void
taint_list_destroy (struct taint_list *list)
{
  free (list->taints);
}

/* Returns true if TAINT is in LIST, false otherwise. */
static bool
taint_list_contains (const struct taint_list *list, const struct taint *taint)
{
  size_t i;

  for (i = 0; i < list->cnt; i++)
    if (list->taints[i] == taint)
      return true;

  return false;
}

/* Returns true if X is zero or a power of 2, false otherwise. */
static bool
is_zero_or_power_of_2 (size_t x)
{
  return (x & (x - 1)) == 0;
}

/* Adds TAINT to LIST, if it isn't already in the list. */
static void
taint_list_add (struct taint_list *list, struct taint *taint)
{
  if (!taint_list_contains (list, taint))
    {
      /* To save a few bytes of memory per list, we don't store
         the list capacity as a separate member.  Instead, the
         list capacity is always zero or a power of 2.  Thus, if
         the list count is one of these threshold values, we need
         to allocate more memory. */
      if (is_zero_or_power_of_2 (list->cnt))
        list->taints = xnrealloc (list->taints,
                                  list->cnt == 0 ? 1 : 2 * list->cnt,
                                  sizeof *list->taints);
      list->taints[list->cnt++] = taint;
    }
}

/* Removes TAINT from LIST (which must contain it). */
static void
taint_list_remove (struct taint_list *list, const struct taint *taint)
{
  size_t i;

  for (i = 0; i < list->cnt; i++)
    if (list->taints[i] == taint)
      {
        remove_element (list->taints, list->cnt, sizeof *list->taints, i);
        list->cnt--;
        return;
      }

  NOT_REACHED ();
}

/* Marks TAINT as tainted, as well as all of its successors
   recursively.  Also marks TAINT's predecessors as
   successor-tainted, recursively. */
static void
recursively_set_taint (struct taint *taint)
{
  size_t i;

  taint->tainted = taint->tainted_successor = true;
   for (i = 0; i < taint->successors.cnt; i++)
    {
      struct taint *s = taint->successors.taints[i];
      if (!s->tainted)
        recursively_set_taint (s);
    }
  for (i = 0; i < taint->predecessors.cnt; i++)
    {
      struct taint *p = taint->predecessors.taints[i];
      if (!p->tainted_successor)
        recursively_set_tainted_successor (p);
    }
}

/* Marks TAINT as successor-tainted, as well as all of its
   predecessors recursively. */
static void
recursively_set_tainted_successor (struct taint *taint)
{
  size_t i;

  taint->tainted_successor = true;
  for (i = 0; i < taint->predecessors.cnt; i++)
    {
      struct taint *p = taint->predecessors.taints[i];
      if (!p->tainted_successor)
        recursively_set_tainted_successor (p);
    }
}

