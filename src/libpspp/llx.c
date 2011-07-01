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

/* External, circular doubly linked list. */

/* These library routines have no external dependencies, other
   than ll.c and the standard C library.

   If you add routines in this file, please add a corresponding
   test to llx-test.c.  This test program should achieve 100%
   coverage of lines and branches in this code, as reported by
   "gcov -b". */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "libpspp/llx.h"
#include "libpspp/compiler.h"
#include <assert.h>
#include <stdlib.h>

/* Destroys LIST and frees all of its nodes using MANAGER.
   If DESTRUCTOR is non-null, each node in the list will be
   passed to it in list order, with AUX as auxiliary data, before
   that node is destroyed. */
void
llx_destroy (struct llx_list *list, llx_action_func *destructor, void *aux,
             const struct llx_manager *manager)
{
  struct llx *llx, *next;

  for (llx = llx_head (list); llx != llx_null (list); llx = next)
    {
      next = llx_next (llx);
      if (destructor != NULL)
        destructor (llx_data (llx), aux);
      manager->release (llx, manager->aux);
    }
}

/* Returns the number of nodes in LIST (not counting the null
   node).  Executes in O(n) time in the length of the list. */
size_t
llx_count (const struct llx_list *list)
{
  return llx_count_range (llx_head (list), llx_null (list));
}

/* Inserts DATA at the head of LIST.
   Returns the new node (allocated with MANAGER), or a null
   pointer if memory allocation failed. */
struct llx *
llx_push_head (struct llx_list *list, void *data,
               const struct llx_manager *manager)
{
  return llx_insert (llx_head (list), data, manager);
}

/* Inserts DATA at the tail of LIST.
   Returns the new node (allocated with MANAGER), or a null
   pointer if memory allocation failed. */
struct llx *
llx_push_tail (struct llx_list *list, void *data,
               const struct llx_manager *manager)
{
  return llx_insert (llx_null (list), data, manager);
}

/* Removes the first node in LIST, which must not be empty,
   and returns the data that the node contained.
   Frees the node removed with MANAGER. */
void *
llx_pop_head (struct llx_list *list, const struct llx_manager *manager)
{
  struct llx *llx = llx_from_ll (ll_head (&list->ll_list));
  void *data = llx_data (llx);
  llx_remove (llx, manager);
  return data;
}

/* Removes the last node in LIST, which must not be empty,
   and returns the data that the node contained.
   Frees the node removed with MANAGER. */
void *
llx_pop_tail (struct llx_list *list, const struct llx_manager *manager)
{
  struct llx *llx = llx_from_ll (ll_tail (&list->ll_list));
  void *data = llx_data (llx);
  llx_remove (llx, manager);
  return data;
}

/* Inserts DATA before BEFORE.
   Returns the new node (allocated with MANAGER), or a null
   pointer if memory allocation failed. */
struct llx *
llx_insert (struct llx *before, void *data, const struct llx_manager *manager)
{
  struct llx *llx = manager->allocate (manager->aux);
  if (llx != NULL)
    {
      llx->data = data;
      ll_insert (&before->ll, &llx->ll);
    }
  return llx;
}

/* Removes R0...R1 from their current list
   and inserts them just before BEFORE. */
void
llx_splice (struct llx *before, struct llx *r0, struct llx *r1)
{
  ll_splice (&before->ll, &r0->ll, &r1->ll);
}

/* Exchanges the positions of A and B,
   which may be in the same list or different lists. */
void
llx_swap (struct llx *a, struct llx *b)
{
  ll_swap (&a->ll, &b->ll);
}

/* Exchanges the positions of A0...A1 and B0...B1,
   which may be in the same list or different lists but must not
   overlap. */
void
llx_swap_range (struct llx *a0, struct llx *a1,
                struct llx *b0, struct llx *b1)
{
  ll_swap_range (&a0->ll, &a1->ll, &b0->ll, &b1->ll);
}

/* Removes LLX from its list
   and returns the node that formerly followed it.
   Frees the node removed with MANAGER. */
struct llx *
llx_remove (struct llx *llx, const struct llx_manager *manager)
{
  struct llx *next = llx_next (llx);
  ll_remove (&llx->ll);
  manager->release (llx, manager->aux);
  return next;
}

/* Removes R0...R1 from their list.
   Frees the removed nodes with MANAGER. */
void
llx_remove_range (struct llx *r0, struct llx *r1,
                  const struct llx_manager *manager)
{
  struct llx *llx;

  for (llx = r0; llx != r1; )
    llx = llx_remove (llx, manager);
}

/* Removes from R0...R1 all the nodes that equal TARGET
   according to COMPARE given auxiliary data AUX.
   Frees the removed nodes with MANAGER.
   Returns the number of nodes removed. */
size_t
llx_remove_equal (struct llx *r0, struct llx *r1, const void *target,
                  llx_compare_func *compare, void *aux,
                  const struct llx_manager *manager)
{
  struct llx *x;
  size_t count;

  count = 0;
  for (x = r0; x != r1; )
    if (compare (llx_data (x), target, aux) == 0)
      {
        x = llx_remove (x, manager);
        count++;
      }
    else
      x = llx_next (x);

  return count;
}

/* Removes from R0...R1 all the nodes for which PREDICATE returns
   true given auxiliary data AUX.
   Frees the removed nodes with MANAGER.
   Returns the number of nodes removed. */
size_t
llx_remove_if (struct llx *r0, struct llx *r1,
               llx_predicate_func *predicate, void *aux,
               const struct llx_manager *manager)
{
  struct llx *x;
  size_t count;

  count = 0;
  for (x = r0; x != r1; )
    if (predicate (llx_data (x), aux))
      {
        x = llx_remove (x, manager);
        count++;
      }
    else
      x = llx_next (x);

  return count;
}

/* Returns the first node in R0...R1 that has data TARGET.
   Returns NULL if no node in R0...R1 equals TARGET. */
struct llx *
llx_find (const struct llx *r0, const struct llx *r1, const void *target)
{
  const struct llx *x;

  for (x = r0; x != r1; x = llx_next (x))
    if (llx_data (x) == target)
      return CONST_CAST (struct llx *, x);

  return NULL;
}

/* Returns the first node in R0...R1 that equals TARGET
   according to COMPARE given auxiliary data AUX.
   Returns R1 if no node in R0...R1 equals TARGET. */
struct llx *
llx_find_equal (const struct llx *r0, const struct llx *r1,
                const void *target,
                llx_compare_func *compare, void *aux)
{
  const struct llx *x;

  for (x = r0; x != r1; x = llx_next (x))
    if (compare (llx_data (x), target, aux) == 0)
      break;
  return CONST_CAST (struct llx *, x);
}

/* Returns the first node in R0...R1 for which PREDICATE returns
   true given auxiliary data AUX.
   Returns R1 if PREDICATE does not return true for any node in
   R0...R1 . */
struct llx *
llx_find_if (const struct llx *r0, const struct llx *r1,
             llx_predicate_func *predicate, void *aux)
{
  const struct llx *x;

  for (x = r0; x != r1; x = llx_next (x))
    if (predicate (llx_data (x), aux))
      break;
  return CONST_CAST (struct llx *, x);
}

/* Compares each pair of adjacent nodes in R0...R1
   using COMPARE with auxiliary data AUX
   and returns the first node of the first pair that compares
   equal.
   Returns R1 if no pair compares equal. */
struct llx *
llx_find_adjacent_equal (const struct llx *r0, const struct llx *r1,
                         llx_compare_func *compare, void *aux)
{
  if (r0 != r1)
    {
      const struct llx *x, *y;

      for (x = r0, y = llx_next (x); y != r1; x = y, y = llx_next (y))
        if (compare (llx_data (x), llx_data (y), aux) == 0)
          return CONST_CAST (struct llx *, x);
    }

  return CONST_CAST (struct llx *, r1);
}

/* Returns the number of nodes in R0...R1.
   Executes in O(n) time in the return value. */
size_t
llx_count_range (const struct llx *r0, const struct llx *r1)
{
  return ll_count_range (&r0->ll, &r1->ll);
}

/* Counts and returns the number of nodes in R0...R1 that equal
   TARGET according to COMPARE given auxiliary data AUX. */
size_t
llx_count_equal (const struct llx *r0, const struct llx *r1,
                 const void *target,
                 llx_compare_func *compare, void *aux)
{
  const struct llx *x;
  size_t count;

  count = 0;
  for (x = r0; x != r1; x = llx_next (x))
    if (compare (llx_data (x), target, aux) == 0)
      count++;
  return count;
}

/* Counts and returns the number of nodes in R0...R1 for which
   PREDICATE returns true given auxiliary data AUX. */
size_t
llx_count_if (const struct llx *r0, const struct llx *r1,
              llx_predicate_func *predicate, void *aux)
{
  const struct llx *x;
  size_t count;

  count = 0;
  for (x = r0; x != r1; x = llx_next (x))
    if (predicate (llx_data (x), aux))
      count++;
  return count;
}

/* Returns the greatest node in R0...R1 according to COMPARE
   given auxiliary data AUX.
   Returns the first of multiple, equal maxima. */
struct llx *
llx_max (const struct llx *r0, const struct llx *r1,
         llx_compare_func *compare, void *aux)
{
  const struct llx *max = r0;
  if (r0 != r1)
    {
      struct llx *x;

      for (x = llx_next (r0); x != r1; x = llx_next (x))
        if (compare (llx_data (x), llx_data (max), aux) > 0)
          max = x;
    }
  return CONST_CAST (struct llx *, max);
}

/* Returns the least node in R0...R1 according to COMPARE given
   auxiliary data AUX.
   Returns the first of multiple, equal minima. */
struct llx *
llx_min (const struct llx *r0, const struct llx *r1,
         llx_compare_func *compare, void *aux)
{
  const struct llx *min = r0;
  if (r0 != r1)
    {
      struct llx *x;

      for (x = llx_next (r0); x != r1; x = llx_next (x))
        if (compare (llx_data (x), llx_data (min), aux) < 0)
          min = x;
    }
  return CONST_CAST (struct llx *, min);
}

/* Lexicographically compares A0...A1 to B0...B1.
   Returns negative if A0...A1 < B0...B1,
   zero if A0...A1 == B0...B1, and
   positive if A0...A1 > B0...B1
   according to COMPARE given auxiliary data AUX. */
int
llx_lexicographical_compare_3way (const struct llx *a0, const struct llx *a1,
                                  const struct llx *b0, const struct llx *b1,
                                  llx_compare_func *compare, void *aux)
{
  for (;;)
    if (b0 == b1)
      return a0 != a1;
    else if (a0 == a1)
      return -1;
    else
      {
        int cmp = compare (llx_data (a0), llx_data (b0), aux);
        if (cmp != 0)
          return cmp;

        a0 = llx_next (a0);
        b0 = llx_next (b0);
      }
}

/* Calls ACTION with auxiliary data AUX
   for every node in R0...R1 in order. */
void
llx_apply (struct llx *r0, struct llx *r1,
           llx_action_func *action, void *aux)
{
  struct llx *llx;

  for (llx = r0; llx != r1; llx = llx_next (llx))
    action (llx_data (llx), aux);
}

/* Reverses the order of nodes R0...R1. */
void
llx_reverse (struct llx *r0, struct llx *r1)
{
  ll_reverse (&r0->ll, &r1->ll);
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
llx_next_permutation (struct llx *r0, struct llx *r1,
                      llx_compare_func *compare, void *aux)
{
  if (r0 != r1)
    {
      struct llx *i = llx_prev (r1);
      while (i != r0)
        {
          i = llx_prev (i);
          if (compare (llx_data (i), llx_data (llx_next (i)), aux) < 0)
            {
              struct llx *j;
              for (j = llx_prev (r1);
                   compare (llx_data (i), llx_data (j), aux) >= 0;
                   j = llx_prev (j))
                continue;
              llx_swap (i, j);
              llx_reverse (llx_next (j), r1);
              return true;
            }
        }

      llx_reverse (r0, r1);
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
llx_prev_permutation (struct llx *r0, struct llx *r1,
                      llx_compare_func *compare, void *aux)
{
  if (r0 != r1)
    {
      struct llx *i = llx_prev (r1);
      while (i != r0)
        {
          i = llx_prev (i);
          if (compare (llx_data (i), llx_data (llx_next (i)), aux) > 0)
            {
              struct llx *j;
              for (j = llx_prev (r1);
                   compare (llx_data (i), llx_data (j), aux) <= 0;
                   j = llx_prev (j))
                continue;
              llx_swap (i, j);
              llx_reverse (llx_next (j), r1);
              return true;
            }
        }

      llx_reverse (r0, r1);
    }

  return false;
}

/* Sorts R0...R1 into ascending order
   according to COMPARE given auxiliary data AUX.
   In use, keep in mind that R0 may move during the sort, so that
   afterward R0...R1 may denote a different range.
   (On the other hand, R1 is fixed in place.)
   Runs in O(n lg n) time in the number of nodes in the range. */
void
llx_sort (struct llx *r0, struct llx *r1, llx_compare_func *compare, void *aux)
{
  struct llx *pre_r0;
  size_t output_run_cnt;

  if (r0 == r1 || llx_next (r0) == r1)
    return;

  pre_r0 = llx_prev (r0);
  do
    {
      struct llx *a0 = llx_next (pre_r0);
      for (output_run_cnt = 1; ; output_run_cnt++)
        {
          struct llx *a1 = llx_find_run (a0, r1, compare, aux);
          struct llx *a2 = llx_find_run (a1, r1, compare, aux);
          if (a1 == a2)
            break;

          a0 = llx_merge (a0, a1, a1, a2, compare, aux);
        }
    }
  while (output_run_cnt > 1);
}

/* Finds the extent of a run of nodes of increasing value
   starting at R0 and extending no farther than R1.
   Returns the first node in R0...R1 that is less than the
   preceding node, or R1 if R0...R1 are arranged in nondecreasing
   order. */
struct llx *
llx_find_run (const struct llx *r0, const struct llx *r1,
              llx_compare_func *compare, void *aux)
{
  if (r0 != r1)
    {
      do
        {
          r0 = llx_next (r0);
        }
      while (r0 != r1 && compare (llx_data (llx_prev (r0)),
                                  llx_data (r0), aux) <= 0);
    }

  return CONST_CAST (struct llx *, r0);
}

/* Merges B0...B1 into A0...A1 according to COMPARE given
   auxiliary data AUX.
   The ranges may be in the same list or different lists, but
   must not overlap.
   The merge is "stable" if A0...A1 is considered to precede
   B0...B1, regardless of their actual ordering.
   Runs in O(n) time in the total number of nodes in the ranges. */
struct llx *
llx_merge (struct llx *a0, struct llx *a1, struct llx *b0, struct llx *b1,
           llx_compare_func *compare, void *aux)
{
  if (a0 != a1 && b0 != b1)
    {
      a1 = llx_prev (a1);
      b1 = llx_prev (b1);
      for (;;)
        if (compare (llx_data (a0), llx_data (b0), aux) <= 0)
          {
            if (a0 == a1)
              {
                llx_splice (llx_next (a0), b0, llx_next (b1));
                return llx_next (b1);
              }
            a0 = llx_next (a0);
          }
        else
          {
            if (b0 != b1)
              {
                struct llx *x = b0;
                b0 = llx_next (b0);
                llx_splice (a0, x, b0);
              }
            else
              {
                llx_splice (a0, b0, llx_next (b0));
                return llx_next (a1);
              }
          }
    }
  else
    {
      llx_splice (a0, b0, b1);
      return b1;
    }
}

/* Returns true if R0...R1 is sorted in ascending order according
   to COMPARE given auxiliary data AUX,
   false otherwise. */
bool
llx_is_sorted (const struct llx *r0, const struct llx *r1,
               llx_compare_func *compare, void *aux)
{
  return llx_find_run (r0, r1, compare, aux) == r1;
}

/* Removes all but the first in each group of sequential
   duplicates in R0...R1.  Duplicates are determined using
   COMPARE given auxiliary data AUX.  Removed duplicates are
   inserted before DUPS if it is nonnull; otherwise, the removed
   duplicates are freed with MANAGER.
   Only sequential duplicates are removed.  llx_sort() may be used
   to bring duplicates together, or llx_sort_unique() can do both
   at once. */
size_t
llx_unique (struct llx *r0, struct llx *r1, struct llx *dups,
            llx_compare_func *compare, void *aux,
            const struct llx_manager *manager)
{
  size_t count = 0;

  if (r0 != r1)
    {
      struct llx *x = r0;
      for (;;)
        {
          struct llx *y = llx_next (x);
          if (y == r1)
            {
              count++;
              break;
            }

          if (compare (llx_data (x), llx_data (y), aux) == 0)
            {
              if (dups != NULL)
                llx_splice (dups, y, llx_next (y));
              else
                llx_remove (y, manager);
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
   otherwise, the removed duplicates are freed with MANAGER.
   Comparisons are made with COMPARE given auxiliary data AUX.
   In use, keep in mind that R0 may move during the sort, so that
   afterward R0...R1 may denote a different range.
   (On the other hand, R1 is fixed in place.)
   Runs in O(n lg n) time in the number of nodes in the range. */
void
llx_sort_unique (struct llx *r0, struct llx *r1, struct llx *dups,
                 llx_compare_func *compare, void *aux,
                 const struct llx_manager *manager)
{
  struct llx *pre_r0 = llx_prev (r0);
  llx_sort (r0, r1, compare, aux);
  llx_unique (llx_next (pre_r0), r1, dups, compare, aux, manager);
}

/* Inserts DATA in the proper position in R0...R1, which must
   be sorted according to COMPARE given auxiliary data AUX.
   If DATA is equal to one or more existing nodes in R0...R1,
   then it is inserted after the existing nodes it equals.
   Returns the new node (allocated with MANAGER), or a null
   pointer if memory allocation failed.
   Runs in O(n) time in the number of nodes in the range. */
struct llx *
llx_insert_ordered (struct llx *r0, struct llx *r1, void *data,
                    llx_compare_func *compare, void *aux,
                    const struct llx_manager *manager)
{
  struct llx *x;

  for (x = r0; x != r1; x = llx_next (x))
    if (compare (llx_data (x), data, aux) > 0)
      break;
  return llx_insert (x, data, manager);
}

/* Partitions R0...R1 into those nodes for which PREDICATE given
   auxiliary data AUX returns true, followed by those for which
   PREDICATE returns false.
   Returns the first node in the "false" group, or R1 if
   PREDICATE is true for every node in R0...R1.
   The partition is "stable" in that the nodes in each group
   retain their original relative order.
   Runs in O(n) time in the number of nodes in the range. */
struct llx *
llx_partition (struct llx *r0, struct llx *r1,
               llx_predicate_func *predicate, void *aux)
{
  struct llx *t0, *t1;

  for (;;)
    {
      if (r0 == r1)
        return r0;
      else if (!predicate (llx_data (r0), aux))
        break;

      r0 = llx_next (r0);
    }

  for (t0 = r0;; t0 = t1)
    {
      do
        {
          t0 = llx_next (t0);
          if (t0 == r1)
            return r0;
        }
      while (!predicate (llx_data (t0), aux));

      t1 = t0;
      do
        {
          t1 = llx_next (t1);
          if (t1 == r1)
            {
              llx_splice (r0, t0, t1);
              return r0;
            }
        }
      while (predicate (llx_data (t1), aux));

      llx_splice (r0, t0, t1);
    }
}

/* Verifies that R0...R1 is parititioned into a sequence of nodes
   for which PREDICATE given auxiliary data AUX returns true,
   followed by those for which PREDICATE returns false.
   Returns a null pointer if R0...R1 is not partitioned this way.
   Otherwise, returns the first node in the "false" group, or R1
   if PREDICATE is true for every node in R0...R1. */
struct llx *
llx_find_partition (const struct llx *r0, const struct llx *r1,
                    llx_predicate_func *predicate, void *aux)
{
  const struct llx *partition, *x;

  for (partition = r0; partition != r1; partition = llx_next (partition))
    if (!predicate (llx_data (partition), aux))
      break;

  for (x = partition; x != r1; x = llx_next (x))
    if (predicate (llx_data (x), aux))
      return NULL;

  return CONST_CAST (struct llx *, partition);
}

/* Allocates and returns a node using malloc. */
static struct llx *
malloc_allocate_node (void *aux UNUSED)
{
  return malloc (sizeof (struct llx));
}

/* Releases node LLX with free. */
static void
malloc_release_node (struct llx *llx, void *aux UNUSED)
{
  free (llx);
}

/* Manager that uses the standard malloc and free routines. */
const struct llx_manager llx_malloc_mgr =
  {
    malloc_allocate_node,
    malloc_release_node,
    NULL,
  };
