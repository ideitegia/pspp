/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009, 2011 Free Software Foundation, Inc.

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

/* Embedded, circular doubly linked list. */

/* These library routines have no external dependencies other
   than the standard C library.

   If you add routines in this file, please add a corresponding
   test to ll-test.c.  This test program should achieve 100%
   coverage of lines and branches in this code, as reported by
   "gcov -b". */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "libpspp/ll.h"

#include <assert.h>

/* Returns the number of nodes in LIST (not counting the null
   node).  Executes in O(n) time in the length of the list. */
size_t
ll_count (const struct ll_list *list)
{
  return ll_count_range (ll_head (list), ll_null (list));
}

/* Removes R0...R1 from their current list
   and inserts them just before BEFORE. */
void
ll_splice (struct ll *before, struct ll *r0, struct ll *r1)
{
  if (before != r0 && r0 != r1)
    {
      /* Change exclusive range to inclusive. */
      r1 = ll_prev (r1);

      /* Remove R0...R1 from its list. */
      r0->prev->next = r1->next;
      r1->next->prev = r0->prev;

      /* Insert R0...R1 before BEFORE. */
      r0->prev = before->prev;
      r1->next = before;
      before->prev->next = r0;
      before->prev = r1;
    }
}

/* Exchanges the positions of A and B,
   which may be in the same list or different lists. */
void
ll_swap (struct ll *a, struct ll *b)
{
  if (a != b)
    {
      if (ll_next (a) != b)
        {
          struct ll *a_next = ll_remove (a);
          struct ll *b_next = ll_remove (b);
          ll_insert (b_next, a);
          ll_insert (a_next, b);
        }
      else
        {
          ll_remove (b);
          ll_insert (a, b);
        }
    }
}

/* Exchanges the positions of A0...A1 and B0...B1,
   which may be in the same list or different lists but must not
   overlap. */
void
ll_swap_range (struct ll *a0, struct ll *a1, struct ll *b0, struct ll *b1)
{
  if (a0 == a1 || a1 == b0)
    ll_splice (a0, b0, b1);
  else if (b0 == b1 || b1 == a0)
    ll_splice (b0, a0, a1);
  else
    {
      struct ll *x0 = ll_prev (a0), *x1 = a1;
      struct ll *y0 = ll_prev (b0), *y1 = b1;
      a1 = ll_prev (a1);
      b1 = ll_prev (b1);
      x0->next = b0;
      b0->prev = x0;
      b1->next = x1;
      x1->prev = b1;
      y0->next = a0;
      a0->prev = y0;
      a1->next = y1;
      y1->prev = a1;
    }
}

/* Removes from R0...R1 all the nodes that equal TARGET
   according to COMPARE given auxiliary data AUX.
   Returns the number of nodes removed. */
size_t
ll_remove_equal (struct ll *r0, struct ll *r1, struct ll *target,
                 ll_compare_func *compare, void *aux)
{
  struct ll *x;
  size_t count;

  count = 0;
  for (x = r0; x != r1; )
    if (compare (x, target, aux) == 0)
      {
        x = ll_remove (x);
        count++;
      }
    else
      x = ll_next (x);

  return count;
}

/* Removes from R0...R1 all the nodes for which PREDICATE returns
   true given auxiliary data AUX.
   Returns the number of nodes removed. */
size_t
ll_remove_if (struct ll *r0, struct ll *r1,
              ll_predicate_func *predicate, void *aux)
{
  struct ll *x;
  size_t count;

  count = 0;
  for (x = r0; x != r1; )
    if (predicate (x, aux))
      {
        x = ll_remove (x);
        count++;
      }
    else
      x = ll_next (x);

  return count;
}

/* Returns the first node in R0...R1 that equals TARGET
   according to COMPARE given auxiliary data AUX.
   Returns R1 if no node in R0...R1 equals TARGET. */
struct ll *
ll_find_equal (const struct ll *r0, const struct ll *r1,
               const struct ll *target,
               ll_compare_func *compare, void *aux)
{
  const struct ll *x;

  for (x = r0; x != r1; x = ll_next (x))
    if (compare (x, target, aux) == 0)
      break;
  return CONST_CAST (struct ll *, x);
}

/* Returns the first node in R0...R1 for which PREDICATE returns
   true given auxiliary data AUX.
   Returns R1 if PREDICATE does not return true for any node in
   R0...R1. */
struct ll *
ll_find_if (const struct ll *r0, const struct ll *r1,
            ll_predicate_func *predicate, void *aux)
{
  const struct ll *x;

  for (x = r0; x != r1; x = ll_next (x))
    if (predicate (x, aux))
      break;
  return CONST_CAST (struct ll *, x);
}

/* Compares each pair of adjacent nodes in R0...R1
   using COMPARE with auxiliary data AUX
   and returns the first node of the first pair that compares
   equal.
   Returns R1 if no pair compares equal. */
struct ll *
ll_find_adjacent_equal (const struct ll *r0, const struct ll *r1,
                        ll_compare_func *compare, void *aux)
{
  if (r0 != r1)
    {
      const struct ll *x, *y;

      for (x = r0, y = ll_next (x); y != r1; x = y, y = ll_next (y))
        if (compare (x, y, aux) == 0)
          return CONST_CAST (struct ll *, x);
    }

  return CONST_CAST (struct ll *, r1);
}

/* Returns the number of nodes in R0...R1.
   Executes in O(n) time in the return value. */
size_t
ll_count_range (const struct ll *r0, const struct ll *r1)
{
  const struct ll *x;
  size_t count;

  count = 0;
  for (x = r0; x != r1; x = ll_next (x))
    count++;
  return count;
}

/* Counts and returns the number of nodes in R0...R1 that equal
   TARGET according to COMPARE given auxiliary data AUX. */
size_t
ll_count_equal (const struct ll *r0, const struct ll *r1,
                const struct ll *target,
                ll_compare_func *compare, void *aux)
{
  const struct ll *x;
  size_t count;

  count = 0;
  for (x = r0; x != r1; x = ll_next (x))
    if (compare (x, target, aux) == 0)
      count++;
  return count;
}

/* Counts and returns the number of nodes in R0...R1 for which
   PREDICATE returns true given auxiliary data AUX. */
size_t
ll_count_if (const struct ll *r0, const struct ll *r1,
             ll_predicate_func *predicate, void *aux)
{
  const struct ll *x;
  size_t count;

  count = 0;
  for (x = r0; x != r1; x = ll_next (x))
    if (predicate (x, aux))
      count++;
  return count;
}

/* Returns the greatest node in R0...R1 according to COMPARE
   given auxiliary data AUX.
   Returns the first of multiple, equal maxima. */
struct ll *
ll_max (const struct ll *r0, const struct ll *r1,
        ll_compare_func *compare, void *aux)
{
  const struct ll *max = r0;
  if (r0 != r1)
    {
      const struct ll *x;

      for (x = ll_next (r0); x != r1; x = ll_next (x))
        if (compare (x, max, aux) > 0)
          max = x;
    }
  return CONST_CAST (struct ll *, max);
}

/* Returns the least node in R0...R1 according to COMPARE given
   auxiliary data AUX.
   Returns the first of multiple, equal minima. */
struct ll *
ll_min (const struct ll *r0, const struct ll *r1,
        ll_compare_func *compare, void *aux)
{
  const struct ll *min = r0;
  if (r0 != r1)
    {
      const struct ll *x;

      for (x = ll_next (r0); x != r1; x = ll_next (x))
        if (compare (x, min, aux) < 0)
          min = x;
    }
  return CONST_CAST (struct ll *, min);
}

/* Lexicographically compares A0...A1 to B0...B1.
   Returns negative if A0...A1 < B0...B1,
   zero if A0...A1 == B0...B1, and
   positive if A0...A1 > B0...B1
   according to COMPARE given auxiliary data AUX. */
int
ll_lexicographical_compare_3way (const struct ll *a0, const struct ll *a1,
                                 const struct ll *b0, const struct ll *b1,
                                 ll_compare_func *compare, void *aux)
{
  for (;;)
    if (b0 == b1)
      return a0 != a1;
    else if (a0 == a1)
      return -1;
    else
      {
        int cmp = compare (a0, b0, aux);
        if (cmp != 0)
          return cmp;

        a0 = ll_next (a0);
        b0 = ll_next (b0);
      }
}

/* Calls ACTION with auxiliary data AUX
   for every node in R0...R1 in order. */
void
ll_apply (struct ll *r0, struct ll *r1, ll_action_func *action, void *aux)
{
  struct ll *ll;

  for (ll = r0; ll != r1; ll = ll_next (ll))
    action (ll, aux);
}

/* Reverses the order of nodes R0...R1. */
void
ll_reverse (struct ll *r0, struct ll *r1)
{
  if (r0 != r1 && ll_next (r0) != r1)
    {
      struct ll *ll;

      for (ll = r0; ll != r1; ll = ll->prev)
        {
          struct ll *tmp = ll->next;
          ll->next = ll->prev;
          ll->prev = tmp;
        }
      r0->next->next = r1->prev;
      r1->prev->prev = r0->next;
      r0->next = r1;
      r1->prev = r0;
    }
}

/* Arranges R0...R1 into the lexicographically next greater
   permutation.  Returns true if successful.
   If R0...R1 is already the lexicographically greatest
   permutation of its elements (i.e. ordered from greatest to
   smallest), arranges them into the lexicographically least
   permutation (i.e. ordered from smallest to largest) and
   returns false.
   COMPARE with auxiliary data AUX is used to compare nodes. */
bool
ll_next_permutation (struct ll *r0, struct ll *r1,
                     ll_compare_func *compare, void *aux)
{
  if (r0 != r1)
    {
      struct ll *i = ll_prev (r1);
      while (i != r0)
        {
          i = ll_prev (i);
          if (compare (i, ll_next (i), aux) < 0)
            {
              struct ll *j;
              for (j = ll_prev (r1); compare (i, j, aux) >= 0; j = ll_prev (j))
                continue;
              ll_swap (i, j);
              ll_reverse (ll_next (j), r1);
              return true;
            }
        }

      ll_reverse (r0, r1);
    }

  return false;
}

/* Arranges R0...R1 into the lexicographically next lesser
   permutation.  Returns true if successful.
   If R0...R1 is already the lexicographically least
   permutation of its elements (i.e. ordered from smallest to
   greatest), arranges them into the lexicographically greatest
   permutation (i.e. ordered from largest to smallest) and
   returns false.
   COMPARE with auxiliary data AUX is used to compare nodes. */
bool
ll_prev_permutation (struct ll *r0, struct ll *r1,
                     ll_compare_func *compare, void *aux)
{
  if (r0 != r1)
    {
      struct ll *i = ll_prev (r1);
      while (i != r0)
        {
          i = ll_prev (i);
          if (compare (i, ll_next (i), aux) > 0)
            {
              struct ll *j;
              for (j = ll_prev (r1); compare (i, j, aux) <= 0; j = ll_prev (j))
                continue;
              ll_swap (i, j);
              ll_reverse (ll_next (j), r1);
              return true;
            }
        }

      ll_reverse (r0, r1);
    }

  return false;
}

/* Sorts R0...R1 into ascending order
   according to COMPARE given auxiliary data AUX.
   In use, keep in mind that R0 may move during the sort, so that
   afterward R0...R1 may denote a different range.
   (On the other hand, R1 is fixed in place.)
   The sort is stable; that is, it will not change the relative
   order of nodes that compare equal.
   Runs in O(n lg n) time in the number of nodes in the range. */
void
ll_sort (struct ll *r0, struct ll *r1, ll_compare_func *compare, void *aux)
{
  struct ll *pre_r0;
  size_t output_run_cnt;

  if (r0 == r1 || ll_next (r0) == r1)
    return;

  pre_r0 = ll_prev (r0);
  do
    {
      struct ll *a0 = ll_next (pre_r0);
      for (output_run_cnt = 1; ; output_run_cnt++)
        {
          struct ll *a1 = ll_find_run (a0, r1, compare, aux);
          struct ll *a2 = ll_find_run (a1, r1, compare, aux);
          if (a1 == a2)
            break;

          a0 = ll_merge (a0, a1, a1, a2, compare, aux);
        }
    }
  while (output_run_cnt > 1);
}

/* Finds the extent of a run of nodes of increasing value
   starting at R0 and extending no farther than R1.
   Returns the first node in R0...R1 that is less than the
   preceding node, or R1 if R0...R1 are arranged in nondecreasing
   order. */
struct ll *
ll_find_run (const struct ll *r0, const struct ll *r1,
             ll_compare_func *compare, void *aux)
{
  if (r0 != r1)
    {
      do
        {
          r0 = ll_next (r0);
        }
      while (r0 != r1 && compare (ll_prev (r0), r0, aux) <= 0);
    }

  return CONST_CAST (struct ll *, r0);
}

/* Merges B0...B1 into A0...A1 according to COMPARE given
   auxiliary data AUX.
   The ranges may be in the same list or different lists, but
   must not overlap.
   Returns the end of the merged range.
   The merge is "stable" if A0...A1 is considered to precede
   B0...B1, regardless of their actual ordering.
   Runs in O(n) time in the total number of nodes in the ranges. */
struct ll *
ll_merge (struct ll *a0, struct ll *a1, struct ll *b0, struct ll *b1,
          ll_compare_func *compare, void *aux)
{
  if (a0 != a1 && b0 != b1)
    {
      a1 = ll_prev (a1);
      b1 = ll_prev (b1);
      for (;;)
        if (compare (a0, b0, aux) <= 0)
          {
            if (a0 == a1)
              {
                ll_splice (ll_next (a0), b0, ll_next (b1));
                return ll_next (b1);
              }
            a0 = ll_next (a0);
          }
        else
          {
            if (b0 != b1)
              {
                struct ll *x = b0;
                b0 = ll_remove (b0);
                ll_insert (a0, x);
              }
            else
              {
                ll_splice (a0, b0, ll_next (b0));
                return ll_next (a1);
              }
          }
    }
  else
    {
      ll_splice (a0, b0, b1);
      return b1;
    }
}

/* Returns true if R0...R1 is sorted in ascending order according
   to COMPARE given auxiliary data AUX, false otherwise. */
bool
ll_is_sorted (const struct ll *r0, const struct ll *r1,
              ll_compare_func *compare, void *aux)
{
  return ll_find_run (r0, r1, compare, aux) == r1;
}

/* Removes all but the first in each group of sequential
   duplicates in R0...R1.  Duplicates are determined using
   COMPARE given auxiliary data AUX.  Removed duplicates are
   inserted before DUPS if it is nonnull; otherwise, their
   identities are lost.
   Only sequential duplicates are removed.  ll_sort() may be used
   to bring duplicates together, or ll_sort_unique() can do both
   at once. */
size_t
ll_unique (struct ll *r0, struct ll *r1, struct ll *dups,
           ll_compare_func *compare, void *aux)
{
  size_t count = 0;

  if (r0 != r1)
    {
      struct ll *x = r0;
      for (;;)
        {
          struct ll *y = ll_next (x);
          if (y == r1)
            {
              count++;
              break;
            }

          if (compare (x, y, aux) == 0)
            {
              ll_remove (y);
              if (dups != NULL)
                ll_insert (dups, y);
            }
          else
            {
              x = y;
              count++;
            }
        }
    }

  return count;
}

/* Sorts R0...R1 and removes duplicates.
   Removed duplicates are inserted before DUPS if it is nonnull;
   otherwise, their identities are lost.
   Comparisons are made with COMPARE given auxiliary data AUX.
   In use, keep in mind that R0 may move during the sort, so that
   afterward R0...R1 may denote a different range.
   (On the other hand, R1 is fixed in place.)
   Runs in O(n lg n) time in the number of nodes in the range. */
void
ll_sort_unique (struct ll *r0, struct ll *r1, struct ll *dups,
                ll_compare_func *compare, void *aux)
{
  struct ll *pre_r0 = ll_prev (r0);
  ll_sort (r0, r1, compare, aux);
  ll_unique (ll_next (pre_r0), r1, dups, compare, aux);
}

/* Inserts NEW_ELEM in the proper position in R0...R1, which must
   be sorted according to COMPARE given auxiliary data AUX.
   If NEW_ELEM is equal to one or more existing nodes in R0...R1,
   then it is inserted after the existing nodes it equals.
   Runs in O(n) time in the number of nodes in the range. */
void
ll_insert_ordered (struct ll *r0, struct ll *r1, struct ll *new_elem,
                   ll_compare_func *compare, void *aux)
{
  struct ll *x;

  for (x = r0; x != r1; x = ll_next (x))
    if (compare (x, new_elem, aux) > 0)
      break;
  ll_insert (x, new_elem);
}

/* Partitions R0...R1 into those nodes for which PREDICATE given
   auxiliary data AUX returns true, followed by those for which
   PREDICATE returns false.
   Returns the first node in the "false" group, or R1 if
   PREDICATE is true for every node in R0...R1.
   The partition is "stable" in that the nodes in each group
   retain their original relative order.
   Runs in O(n) time in the number of nodes in the range. */
struct ll *
ll_partition (struct ll *r0, struct ll *r1,
              ll_predicate_func *predicate, void *aux)
{
  struct ll *t0, *t1;

  for (;;)
    {
      if (r0 == r1)
        return r0;
      else if (!predicate (r0, aux))
        break;

      r0 = ll_next (r0);
    }

  for (t0 = r0;; t0 = t1)
    {
      do
        {
          t0 = ll_next (t0);
          if (t0 == r1)
            return r0;
        }
      while (!predicate (t0, aux));

      t1 = t0;
      do
        {
          t1 = ll_next (t1);
          if (t1 == r1)
            {
              ll_splice (r0, t0, t1);
              return r0;
            }
        }
      while (predicate (t1, aux));

      ll_splice (r0, t0, t1);
    }
}

/* Verifies that R0...R1 is parititioned into a sequence of nodes
   for which PREDICATE given auxiliary data AUX returns true,
   followed by those for which PREDICATE returns false.
   Returns a null pointer if R0...R1 is not partitioned this way.
   Otherwise, returns the first node in the "false" group, or R1
   if PREDICATE is true for every node in R0...R1. */
struct ll *
ll_find_partition (const struct ll *r0, const struct ll *r1,
                   ll_predicate_func *predicate, void *aux)
{
  const struct ll *partition, *x;

  for (partition = r0; partition != r1; partition = ll_next (partition))
    if (!predicate (partition, aux))
      break;

  for (x = partition; x != r1; x = ll_next (x))
    if (predicate (x, aux))
      return NULL;

  return CONST_CAST (struct ll *, partition);
}

