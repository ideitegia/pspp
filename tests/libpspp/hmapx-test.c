/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2008, 2009, 2010 Free Software Foundation, Inc.

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

/* This is a test program for the hmapx_* routines defined in
   hmapx.c.  This test program aims to be as comprehensive as
   possible.  "gcov -a -b" should report 100% coverage of lines,
   blocks and branches in hmapx.c (when compiled with -DNDEBUG).
   "valgrind --leak-check=yes --show-reachable=yes" should give a
   clean report. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libpspp/hmapx.h>

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpspp/compiler.h>

/* If OK is not true, prints a message about failure on the
   current source file and the given LINE and terminates. */
static void
check_func (bool ok, int line)
{
  if (!ok)
    {
      fprintf (stderr, "%s:%d: check failed\n", __FILE__, line);
      abort ();
    }
}

/* Verifies that EXPR evaluates to true.
   If not, prints a message citing the calling line number and
   terminates. */
#define check(EXPR) check_func ((EXPR), __LINE__)

/* Prints a message about memory exhaustion and exits with a
   failure code. */
static void
xalloc_die (void)
{
  printf ("virtual memory exhausted\n");
  exit (EXIT_FAILURE);
}

/* Allocates and returns N bytes of memory. */
static void *
xmalloc (size_t n)
{
  if (n != 0)
    {
      void *p = malloc (n);
      if (p == NULL)
        xalloc_die ();

      return p;
    }
  else
    return NULL;
}

static void *
xmemdup (const void *p, size_t n)
{
  void *q = xmalloc (n);
  memcpy (q, p, n);
  return q;
}

/* Allocates and returns N * M bytes of memory. */
static void *
xnmalloc (size_t n, size_t m)
{
  if ((size_t) -1 / m <= n)
    xalloc_die ();
  return xmalloc (n * m);
}

/* Node type and support routines. */

/* Test data element. */
struct element
  {
    int data;                 /* Primary value. */
  };

/* Compares A and B and returns a strcmp-type return value. */
static int
compare_ints (const void *a_, const void *b_)
{
  const int *a = a_;
  const int *b = b_;

  return *a < *b ? -1 : *a > *b;
}

/* Swaps *A and *B. */
static void
swap (int *a, int *b)
{
  int t = *a;
  *a = *b;
  *b = t;
}

/* Reverses the order of the CNT integers starting at VALUES. */
static void
reverse (int *values, size_t cnt)
{
  size_t i = 0;
  size_t j = cnt;

  while (j > i)
    swap (&values[i++], &values[--j]);
}

/* Arranges the CNT elements in VALUES into the lexicographically
   next greater permutation.  Returns true if successful.
   If VALUES is already the lexicographically greatest
   permutation of its elements (i.e. ordered from greatest to
   smallest), arranges them into the lexicographically least
   permutation (i.e. ordered from smallest to largest) and
   returns false. */
static bool
next_permutation (int *values, size_t cnt)
{
  if (cnt > 0)
    {
      size_t i = cnt - 1;
      while (i != 0)
        {
          i--;
          if (values[i] < values[i + 1])
            {
              size_t j;
              for (j = cnt - 1; values[i] >= values[j]; j--)
                continue;
              swap (values + i, values + j);
              reverse (values + (i + 1), cnt - (i + 1));
              return true;
            }
        }

      reverse (values, cnt);
    }

  return false;
}

/* Returns N!. */
static unsigned int
factorial (unsigned int n)
{
  unsigned int value = 1;
  while (n > 1)
    value *= n--;
  return value;
}

/* Randomly shuffles the CNT elements in ARRAY, each of which is
   SIZE bytes in size. */
static void
random_shuffle (void *array_, size_t cnt, size_t size)
{
  char *array = array_;
  char *tmp = xmalloc (size);
  size_t i;

  for (i = 0; i < cnt; i++)
    {
      size_t j = rand () % (cnt - i) + i;
      if (i != j)
        {
          memcpy (tmp, array + j * size, size);
          memcpy (array + j * size, array + i * size, size);
          memcpy (array + i * size, tmp, size);
        }
    }

  free (tmp);
}

typedef size_t hash_function (int data);

static size_t
identity_hash (int data) 
{
  return data;
}

static size_t
constant_hash (int data UNUSED) 
{
  return 0x12345678u;
}

static inline uint32_t
md4_round (uint32_t a, uint32_t b, uint32_t c, uint32_t d,
           uint32_t data, uint32_t n)
{
  uint32_t x = a + (d ^ (b & (c ^ d))) + data;
  return (x << n) | (x >> (32 - n));
}

static size_t
random_hash (int data)
{
  uint32_t a = data;
  uint32_t b = data;
  uint32_t c = data;
  uint32_t d = data;
  a = md4_round (a, b, c, d, 0, 3);
  d = md4_round (d, a, b, c, 1, 7);
  c = md4_round (c, d, a, b, 2, 11);
  b = md4_round (b, c, d, a, 3, 19);
  return a ^ b ^ c ^ d;
}

static struct hmapx_node *
find_element (struct hmapx *hmapx, int data, hash_function *hash)
{
  struct hmapx_node *node;
  struct element *e;
  HMAPX_FOR_EACH_WITH_HASH (e, node, hash (data), hmapx)
    if (e->data == data)
      break;
  return node;
}

/* Checks that HMAPX contains the CNT ints in DATA, that its
   structure is correct, and that certain operations on HMAPX
   produce the expected results. */
static void
check_hmapx (struct hmapx *hmapx, const int data[], size_t cnt,
            hash_function *hash)
{
  size_t i, j;
  int *order;

  check (hmapx_is_empty (hmapx) == (cnt == 0));
  check (hmapx_count (hmapx) == cnt);
  check (cnt <= hmapx_capacity (hmapx));

  order = xmemdup (data, cnt * sizeof *data);
  qsort (order, cnt, sizeof *order, compare_ints);

  for (i = 0; i < cnt; i = j)
    {
      struct hmapx_node *node;
      struct element *e;
      int count;

      for (j = i + 1; j < cnt; j++)
        if (order[i] != order[j])
          break;

      count = 0;
      HMAPX_FOR_EACH_WITH_HASH (e, node, hash (order[i]), hmapx)
        if (e->data == order[i]) 
          count++; 

      check (count == j - i);
    }

  check (find_element (hmapx, -1, hash) == NULL);

  if (cnt == 0)
    check (hmapx_first (hmapx) == NULL);
  else
    {
      struct hmapx_node *p;
      int left;

      left = cnt;
      for (p = hmapx_first (hmapx), i = 0; i < cnt;
           p = hmapx_next (hmapx, p), i++)
        {
          struct element *e = hmapx_node_data (p);
          size_t j;

          check (hmapx_node_hash (p) == hash (e->data));
          for (j = 0; j < left; j++)
            if (order[j] == e->data) 
              {
                order[j] = order[--left];
                goto next;
              }
          abort ();

        next: ;
        }
      check (p == NULL);
    }

  free (order);
}

/* Inserts the CNT values from 0 to CNT - 1 (inclusive) into an
   HMAPX in the order specified by INSERTIONS, then deletes them in
   the order specified by DELETIONS, checking the HMAPX's contents
   for correctness after each operation.  Uses HASH as the hash
   function. */
static void
test_insert_delete (const int insertions[],
                    const int deletions[],
                    size_t cnt,
                    hash_function *hash,
                    size_t reserve)
{
  struct element *elements;
  struct hmapx_node **nodes;
  struct hmapx hmapx;
  size_t i;

  elements = xnmalloc (cnt, sizeof *elements);
  nodes = xnmalloc (cnt, sizeof *nodes);
  for (i = 0; i < cnt; i++)
    elements[i].data = i;

  hmapx_init (&hmapx);
  hmapx_reserve (&hmapx, reserve);
  check_hmapx (&hmapx, NULL, 0, hash);
  for (i = 0; i < cnt; i++)
    {
      struct hmapx_node *(*insert) (struct hmapx *, void *, size_t hash);
      size_t capacity;

      /* Insert the node.  Use hmapx_insert_fast if we have not
         yet exceeded the reserve. */
      insert = i < reserve ? hmapx_insert_fast : hmapx_insert;
      nodes[insertions[i]] = insert (&hmapx, &elements[insertions[i]],
                                     hash (insertions[i]));
      check_hmapx (&hmapx, insertions, i + 1, hash);

      /* A series of insertions should not produce a shrinkable hmapx. */
      if (i >= reserve) 
        {
          capacity = hmapx_capacity (&hmapx);
          hmapx_shrink (&hmapx);
          check (capacity == hmapx_capacity (&hmapx)); 
        }
    }
  for (i = 0; i < cnt; i++)
    {
      hmapx_delete (&hmapx, nodes[deletions[i]]);
      check_hmapx (&hmapx, deletions + i + 1, cnt - i - 1, hash);
    }
  hmapx_destroy (&hmapx);

  free (elements);
  free (nodes);
}

/* Inserts values into an HMAPX in each possible order, then
   removes them in each possible order, up to a specified maximum
   size, using hash function HASH. */
static void
test_insert_any_remove_any (hash_function *hash)
{
  const int max_elems = 5;
  int cnt;

  for (cnt = 0; cnt <= max_elems; cnt++)
    {
      int *insertions, *deletions;
      unsigned int ins_perm_cnt;
      int i;

      insertions = xnmalloc (cnt, sizeof *insertions);
      deletions = xnmalloc (cnt, sizeof *deletions);
      for (i = 0; i < cnt; i++)
        insertions[i] = i;

      for (ins_perm_cnt = 0;
           ins_perm_cnt == 0 || next_permutation (insertions, cnt);
           ins_perm_cnt++)
        {
          unsigned int del_perm_cnt;
          int i;

          for (i = 0; i < cnt; i++)
            deletions[i] = i;

          for (del_perm_cnt = 0;
               del_perm_cnt == 0 || next_permutation (deletions, cnt);
               del_perm_cnt++)
            test_insert_delete (insertions, deletions, cnt, hash, 1);

          check (del_perm_cnt == factorial (cnt));
        }
      check (ins_perm_cnt == factorial (cnt));

      free (insertions);
      free (deletions);
    }
}

static void
test_insert_any_remove_any_random_hash (void) 
{
  test_insert_any_remove_any (random_hash);
}

static void
test_insert_any_remove_any_identity_hash (void) 
{
  test_insert_any_remove_any (identity_hash);
}

static void
test_insert_any_remove_any_constant_hash (void) 
{
  test_insert_any_remove_any (constant_hash);
}

/* Inserts values into an HMAPX in each possible order, then
   removes them in the same order, up to a specified maximum
   size, using hash function HASH. */
static void
test_insert_any_remove_same (hash_function *hash)
{
  const int max_elems = 7;
  int cnt;

  for (cnt = 0; cnt <= max_elems; cnt++)
    {
      int *values;
      unsigned int permutation_cnt;
      int i;

      values = xnmalloc (cnt, sizeof *values);
      for (i = 0; i < cnt; i++)
        values[i] = i;

      for (permutation_cnt = 0;
           permutation_cnt == 0 || next_permutation (values, cnt);
           permutation_cnt++)
        test_insert_delete (values, values, cnt, hash, cnt / 2);
      check (permutation_cnt == factorial (cnt));

      free (values);
    }
}

static void
test_insert_any_remove_same_random_hash (void) 
{
  test_insert_any_remove_same (random_hash);
}

static void
test_insert_any_remove_same_identity_hash (void) 
{
  test_insert_any_remove_same (identity_hash);
}

static void
test_insert_any_remove_same_constant_hash (void) 
{
  test_insert_any_remove_same (constant_hash);
}

/* Inserts values into an HMAPX in each possible order, then
   removes them in reverse order, up to a specified maximum
   size, using hash function HASH. */
static void
test_insert_any_remove_reverse (hash_function *hash)
{
  const int max_elems = 7;
  int cnt;

  for (cnt = 0; cnt <= max_elems; cnt++)
    {
      int *insertions, *deletions;
      unsigned int permutation_cnt;
      int i;

      insertions = xnmalloc (cnt, sizeof *insertions);
      deletions = xnmalloc (cnt, sizeof *deletions);
      for (i = 0; i < cnt; i++)
        insertions[i] = i;

      for (permutation_cnt = 0;
           permutation_cnt == 0 || next_permutation (insertions, cnt);
           permutation_cnt++)
        {
          memcpy (deletions, insertions, sizeof *insertions * cnt);
          reverse (deletions, cnt);

          test_insert_delete (insertions, deletions, cnt, hash, cnt);
        }
      check (permutation_cnt == factorial (cnt));

      free (insertions);
      free (deletions);
    }
}

static void
test_insert_any_remove_reverse_random_hash (void)
{
  test_insert_any_remove_reverse (random_hash);
}

static void
test_insert_any_remove_reverse_identity_hash (void)
{
  test_insert_any_remove_reverse (identity_hash);
}

static void
test_insert_any_remove_reverse_constant_hash (void)
{
  test_insert_any_remove_reverse (constant_hash);
}

/* Inserts and removes up to MAX_ELEMS values in an hmapx, in
   random order, using hash function HASH. */
static void
test_random_sequence (int max_elems, hash_function *hash)
{
  const int max_trials = 8;
  int cnt;

  for (cnt = 0; cnt <= max_elems; cnt += 2)
    {
      int *insertions, *deletions;
      int trial;
      int i;

      insertions = xnmalloc (cnt, sizeof *insertions);
      deletions = xnmalloc (cnt, sizeof *deletions);
      for (i = 0; i < cnt; i++)
        insertions[i] = i;
      for (i = 0; i < cnt; i++)
        deletions[i] = i;

      for (trial = 0; trial < max_trials; trial++)
        {
          random_shuffle (insertions, cnt, sizeof *insertions);
          random_shuffle (deletions, cnt, sizeof *deletions);

          test_insert_delete (insertions, deletions, cnt, hash, 0);
        }

      free (insertions);
      free (deletions);
    }
}

static void
test_random_sequence_random_hash (void) 
{
  test_random_sequence (64, random_hash);
}

static void
test_random_sequence_identity_hash (void) 
{
  test_random_sequence (64, identity_hash);
}

static void
test_random_sequence_constant_hash (void) 
{
  test_random_sequence (32, constant_hash);
}

/* Inserts MAX_ELEMS elements into an HMAPX in ascending order,
   then delete in ascending order and shrink the hmapx at each
   step, using hash function HASH. */
static void
test_insert_ordered (int max_elems, hash_function *hash)
{
  struct element *elements;
  struct hmapx_node **nodes;
  int *values;
  struct hmapx hmapx;
  int i;

  hmapx_init (&hmapx);
  elements = xnmalloc (max_elems, sizeof *elements);
  nodes = xnmalloc (max_elems, sizeof *nodes);
  values = xnmalloc (max_elems, sizeof *values);
  for (i = 0; i < max_elems; i++)
    {
      values[i] = elements[i].data = i;
      nodes[i] = hmapx_insert (&hmapx, &elements[i], hash (elements[i].data));
      check_hmapx (&hmapx, values, i + 1, hash);

      if (hash == identity_hash) 
        {
          /* Check that every every hash bucket has (almost) the
             same number of nodes in it.  */
          int min = INT_MAX;
          int max = INT_MIN;
          int j;

          for (j = 0; j <= hmapx.hmap.mask; j++) 
            {
              int count = 0;
              struct hmap_node *node;

              for (node = hmapx.hmap.buckets[j]; node != NULL;
                   node = node->next)
                count++;
              if (count < min)
                min = count;
              if (count > max)
                max = count;
            }
          check (max - min <= 1);
        }
    }
  for (i = 0; i < max_elems; i++)
    {
      hmapx_delete (&hmapx, nodes[i]);
      hmapx_shrink (&hmapx);
      check_hmapx (&hmapx, values + i + 1, max_elems - i - 1, hash);
    }
  hmapx_destroy (&hmapx);
  free (elements);
  free (nodes);
  free (values);
}

static void
test_insert_ordered_random_hash (void)
{
  test_insert_ordered (1024, random_hash);
}

static void
test_insert_ordered_identity_hash (void)
{
  test_insert_ordered (1024, identity_hash);
}

static void
test_insert_ordered_constant_hash (void)
{
  test_insert_ordered (128, constant_hash);
}

/* Inserts up to MAX_ELEMS elements into an HMAPX, then moves the
   nodes around in memory, using hash function HASH. */
static void
test_moved (int max_elems, hash_function *hash)
{
  struct element *e[2];
  int cur;
  int *values;
  struct hmapx_node **nodes;
  struct hmapx hmapx;
  int i, j;

  hmapx_init (&hmapx);
  e[0] = xnmalloc (max_elems, sizeof *e[0]);
  e[1] = xnmalloc (max_elems, sizeof *e[1]);
  values = xnmalloc (max_elems, sizeof *values);
  nodes = xnmalloc (max_elems, sizeof *nodes);
  cur = 0;
  for (i = 0; i < max_elems; i++)
    {
      values[i] = e[cur][i].data = i;
      nodes[i] = hmapx_insert (&hmapx, &e[cur][i], hash (e[cur][i].data));
      check_hmapx (&hmapx, values, i + 1, hash);

      for (j = 0; j <= i; j++)
        {
          e[!cur][j] = e[cur][j];
          hmapx_move (nodes[j], &e[cur][j]);
          check_hmapx (&hmapx, values, i + 1, hash);
        }
      cur = !cur;
    }
  hmapx_destroy (&hmapx);
  free (e[0]);
  free (e[1]);
  free (values);
  free (nodes);
}

static void
test_moved_random_hash (void) 
{
  test_moved (128, random_hash);
}

static void
test_moved_identity_hash (void) 
{
  test_moved (128, identity_hash);
}

static void
test_moved_constant_hash (void) 
{
  test_moved (32, constant_hash);
}

/* Inserts values into an HMAPX, then changes their values, using
   hash function HASH. */
static void
test_changed (hash_function *hash)
{
  const int max_elems = 6;
  int cnt;

  for (cnt = 0; cnt <= max_elems; cnt++)
    {
      int *values, *changed_values;
      struct hmapx_node **nodes;
      struct element *elements;
      unsigned int permutation_cnt;
      int i;

      values = xnmalloc (cnt, sizeof *values);
      changed_values = xnmalloc (cnt, sizeof *changed_values);
      elements = xnmalloc (cnt, sizeof *elements);
      nodes = xnmalloc (cnt, sizeof *nodes);
      for (i = 0; i < cnt; i++)
        values[i] = i;

      for (permutation_cnt = 0;
           permutation_cnt == 0 || next_permutation (values, cnt);
           permutation_cnt++)
        {
          for (i = 0; i < cnt; i++)
            {
              int j, k;
              for (j = 0; j <= cnt; j++)
                {
                  struct hmapx hmapx;

                  hmapx_init (&hmapx);

                  /* Add to HMAPX in order. */
                  for (k = 0; k < cnt; k++)
                    {
                      int n = values[k];
                      elements[n].data = n;
                      nodes[n] = hmapx_insert (&hmapx, &elements[n],
                                               hash (elements[n].data));
                    }
                  check_hmapx (&hmapx, values, cnt, hash);

                  /* Change value i to j. */
                  elements[i].data = j;
                  hmapx_changed (&hmapx, nodes[i], 
                                 hash (elements[i].data));
                  for (k = 0; k < cnt; k++)
                    changed_values[k] = k;
                  changed_values[i] = j;
                  check_hmapx (&hmapx, changed_values, cnt, hash);

                  hmapx_destroy (&hmapx);
                }
            }
        }
      check (permutation_cnt == factorial (cnt));

      free (values);
      free (changed_values);
      free (elements);
      free (nodes);
    }
}

static void
test_changed_random_hash (void)
{
  test_changed (random_hash);
}

static void
test_changed_identity_hash (void)
{
  test_changed (identity_hash);
}

static void
test_changed_constant_hash (void)
{
  test_changed (constant_hash);
}

/* Inserts values into an HMAPX, then changes and moves their
   values, using hash function HASH. */
static void
test_change (hash_function *hash)
{
  const int max_elems = 6;
  int cnt;

  for (cnt = 0; cnt <= max_elems; cnt++)
    {
      int *values, *changed_values;
      struct hmapx_node **nodes;
      struct element *elements;
      struct element replacement;
      unsigned int permutation_cnt;
      int i;

      values = xnmalloc (cnt, sizeof *values);
      changed_values = xnmalloc (cnt, sizeof *changed_values);
      elements = xnmalloc (cnt, sizeof *elements);
      nodes = xnmalloc (cnt, sizeof *nodes);
      for (i = 0; i < cnt; i++)
        values[i] = i;

      for (permutation_cnt = 0;
           permutation_cnt == 0 || next_permutation (values, cnt);
           permutation_cnt++)
        {
          for (i = 0; i < cnt; i++)
            {
              int j, k;
              for (j = 0; j <= cnt; j++)
                {
                  struct hmapx hmapx;

                  hmapx_init (&hmapx);

                  /* Add to HMAPX in order. */
                  for (k = 0; k < cnt; k++)
                    {
                      int n = values[k];
                      elements[n].data = n;
                      nodes[n] = hmapx_insert (&hmapx, &elements[n],
                                               hash (elements[n].data));
                    }
                  check_hmapx (&hmapx, values, cnt, hash);

                  /* Change value i to j. */
                  replacement.data = j;
                  hmapx_change (&hmapx, nodes[i], &replacement, hash (j));
                  for (k = 0; k < cnt; k++)
                    changed_values[k] = k;
                  changed_values[i] = j;
                  check_hmapx (&hmapx, changed_values, cnt, hash);

                  hmapx_destroy (&hmapx);
                }
            }
        }
      check (permutation_cnt == factorial (cnt));

      free (values);
      free (changed_values);
      free (elements);
      free (nodes);
    }
}

static void
test_change_random_hash (void)
{
  test_change (random_hash);
}

static void
test_change_identity_hash (void)
{
  test_change (identity_hash);
}

static void
test_change_constant_hash (void)
{
  test_change (constant_hash);
}

static void
test_swap (int max_elems, hash_function *hash) 
{
  struct element *elements;
  int *values;
  struct hmapx a, b;
  struct hmapx *working, *empty;
  int i;

  hmapx_init (&a);
  hmapx_init (&b);
  working = &a;
  empty = &b;
  elements = xnmalloc (max_elems, sizeof *elements);
  values = xnmalloc (max_elems, sizeof *values);
  for (i = 0; i < max_elems; i++)
    {
      struct hmapx *tmp;
      values[i] = elements[i].data = i;
      hmapx_insert (working, &elements[i], hash (elements[i].data));
      check_hmapx (working, values, i + 1, hash);
      check_hmapx (empty, NULL, 0, hash);
      hmapx_swap (&a, &b);
      tmp = working;
      working = empty;
      empty = tmp;
    }
  hmapx_destroy (&a);
  hmapx_destroy (&b);
  free (elements);
  free (values);
}

static void
test_swap_random_hash (void) 
{
  test_swap (128, random_hash);
}

/* Inserts elements into an HMAPX in ascending order, then clears the hash
   table using hmapx_clear(). */
static void
test_clear (void)
{
  const int max_elems = 128;
  struct element *elements;
  struct hmapx_node **nodes;
  int *values;
  struct hmapx hmapx;
  int cnt;

  elements = xnmalloc (max_elems, sizeof *elements);
  nodes = xnmalloc (max_elems, sizeof *nodes);
  values = xnmalloc (max_elems, sizeof *values);

  hmapx_init (&hmapx);
  for (cnt = 0; cnt <= max_elems; cnt++)
    {
      int i;

      for (i = 0; i < cnt; i++)
        {
          values[i] = elements[i].data = i;
          nodes[i] = hmapx_insert (&hmapx, &elements[i],
                                   random_hash (elements[i].data));
          check_hmapx (&hmapx, values, i + 1, random_hash);
        }
      hmapx_clear (&hmapx);
      check_hmapx (&hmapx, NULL, 0, random_hash);
    }
  hmapx_destroy (&hmapx);

  free (elements);
  free (nodes);
  free (values);
}

static void
test_destroy_null (void) 
{
  hmapx_destroy (NULL);
}

/* Test shrinking an empty hash table. */
static void
test_shrink_empty (void)
{
  struct hmapx hmapx;

  hmapx_init (&hmapx);
  hmapx_reserve (&hmapx, 123);
  hmapx_shrink (&hmapx);
  hmapx_destroy (&hmapx);
}

/* Main program. */

struct test
  {
    const char *name;
    const char *description;
    void (*function) (void);
  };

static const struct test tests[] =
  {
    {
      "insert-any-remove-any-random-hash",
      "insert any order, delete any order (random hash)",
      test_insert_any_remove_any_random_hash
    },
    {
      "insert-any-remove-any-identity-hash",
      "insert any order, delete any order (identity hash)",
      test_insert_any_remove_any_identity_hash
    },
    {
      "insert-any-remove-any-constant-hash",
      "insert any order, delete any order (constant hash)",
      test_insert_any_remove_any_constant_hash
    },
    {
      "insert-any-remove-same-random-hash",
      "insert any order, delete same order (random hash)",
      test_insert_any_remove_same_random_hash
    },
    {
      "insert-any-remove-same-identity-hash",
      "insert any order, delete same order (identity hash)",
      test_insert_any_remove_same_identity_hash
    },
    {
      "insert-any-remove-same-constant-hash",
      "insert any order, delete same order (constant hash)",
      test_insert_any_remove_same_constant_hash
    },
    {
      "insert-any-remove-reverse-random-hash",
      "insert any order, delete reverse order (random hash)",
      test_insert_any_remove_reverse_random_hash
    },
    {
      "insert-any-remove-reverse-identity-hash",
      "insert any order, delete reverse order (identity hash)",
      test_insert_any_remove_reverse_identity_hash
    },
    {
      "insert-any-remove-reverse-constant-hash",
      "insert any order, delete reverse order (constant hash)",
      test_insert_any_remove_reverse_constant_hash
    },
    {
      "random-sequence-random-hash",
      "insert and delete in random sequence (random hash)",
      test_random_sequence_random_hash
    },
    {
      "random-sequence-identity-hash",
      "insert and delete in random sequence (identity hash)",
      test_random_sequence_identity_hash
    },
    {
      "random-sequence-constant-hash",
      "insert and delete in random sequence (constant hash)",
      test_random_sequence_constant_hash
    },
    {
      "insert-ordered-random-hash",
      "insert in ascending order (random hash)",
      test_insert_ordered_random_hash
    },
    {
      "insert-ordered-identity-hash",
      "insert in ascending order (identity hash)",
      test_insert_ordered_identity_hash
    },
    {
      "insert-ordered-constant-hash",
      "insert in ascending order (constant hash)",
      test_insert_ordered_constant_hash
    },
    {
      "moved-random-hash",
      "move elements around in memory (random hash)",
      test_moved_random_hash
    },
    {
      "moved-identity-hash",
      "move elements around in memory (identity hash)",
      test_moved_identity_hash
    },
    {
      "moved-constant-hash",
      "move elements around in memory (constant hash)",
      test_moved_constant_hash
    },
    {
      "changed-random-hash",
      "change key data in nodes (random hash)",
      test_changed_random_hash
    },
    {
      "changed-identity-hash",
      "change key data in nodes (identity hash)",
      test_changed_identity_hash
    },
    {
      "changed-constant-hash",
      "change key data in nodes (constant hash)",
      test_changed_constant_hash
    },
    {
      "change-random-hash",
      "change and move key data in nodes (random hash)",
      test_change_random_hash
    },
    {
      "change-identity-hash",
      "change and move key data in nodes (identity hash)",
      test_change_identity_hash
    },
    {
      "change-constant-hash",
      "change and move key data in nodes (constant hash)",
      test_change_constant_hash
    },
    {
      "swap-random-hash",
      "test swapping tables",
      test_swap_random_hash
    },
    {
      "clear",
      "test clearing hash table",
      test_clear
    },
    {
      "destroy-null",
      "test destroying null table",
      test_destroy_null
    },
    {
      "shrink-empty",
      "test shrinking an empty table",
      test_shrink_empty
    },
  };

enum { N_TESTS = sizeof tests / sizeof *tests };

int
main (int argc, char *argv[])
{
  int i;

  if (argc != 2)
    {
      fprintf (stderr, "exactly one argument required; use --help for help\n");
      return EXIT_FAILURE;
    }
  else if (!strcmp (argv[1], "--help"))
    {
      printf ("%s: test hash map of pointers\n"
              "usage: %s TEST-NAME\n"
              "where TEST-NAME is one of the following:\n",
              argv[0], argv[0]);
      for (i = 0; i < N_TESTS; i++)
        printf ("  %s\n    %s\n", tests[i].name, tests[i].description);
      return 0;
    }
  else
    {
      for (i = 0; i < N_TESTS; i++)
        if (!strcmp (argv[1], tests[i].name))
          {
            tests[i].function ();
            return 0;
          }

      fprintf (stderr, "unknown test %s; use --help for help\n", argv[1]);
      return EXIT_FAILURE;
    }
}
