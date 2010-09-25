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

/* This is a test program for the hmap_* routines defined in
   hmap.c.  This test program aims to be as comprehensive as
   possible.  "gcov -a -b" should report 100% coverage of lines,
   blocks and branches in hmap.c (when compiled with -DNDEBUG).
   "valgrind --leak-check=yes --show-reachable=yes" should give a
   clean report. */

/* GCC 4.3 miscompiles some of the tests below, so we do not run
   these tests on GCC 4.3.  This is a bug in GCC 4.3 triggered by
   the test program, not a bug in the library under test.  GCC
   4.2 or earlier and GCC 4.4 or later do not have this bug.

   Here is a minimal test program that demonstrates the same or a
   similar bug in GCC 4.3:

   #include <stdio.h>
   #include <stdlib.h>

   struct node
     {
       struct node *next;
       unsigned int data1;
       int data2;
     };
   struct list
     {
       struct node *head;
       int dummy;
     };

   static void *
   xmalloc (int n)
   {
     return malloc (n);
   }

   static void
   check_list (struct list *list)
   {
     int i __attribute__((unused));
     struct node *e;
     for (e = list->head; e != NULL; e = e->next)
       if (e->data1 != e->data2)
         abort ();
   }

   int
   main (void)
   {
   #define MAX_ELEMS 2
     struct node *elements = xmalloc (MAX_ELEMS * sizeof *elements);
     int *values = xmalloc (MAX_ELEMS * sizeof *values);
     struct list list;
     int i;

     list.head = NULL;
     for (i = 0; i < MAX_ELEMS; i++)
       {
         values[i] = elements[i].data2 = i;
         elements[i].data1 = elements[i].data2;
         elements[i].next = list.head;
         list.head = &elements[i];
       }
     check_list (&list);
     return 0;
   }
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libpspp/hmap.h>

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpspp/compiler.h>

/* Exit with a failure code.
   (Place a breakpoint on this function while debugging.) */
static void
check_die (void)
{
  exit (EXIT_FAILURE);
}

/* If OK is not true, prints a message about failure on the
   current source file and the given LINE and terminates. */
static void
check_func (bool ok, int line)
{
  if (!ok)
    {
      fprintf (stderr, "%s:%d: check failed\n", __FILE__, line);
      check_die ();
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

static void *xmalloc (size_t n) MALLOC_LIKE;
static void *xnmalloc (size_t n, size_t m) MALLOC_LIKE;
static void *xmemdup (const void *p, size_t n) MALLOC_LIKE;

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
    struct hmap_node node;    /* Embedded hash table element. */
    int data;                 /* Primary value. */
  };

/* Returns the `struct element' that NODE is embedded within. */
static struct element *
hmap_node_to_element (const struct hmap_node *node)
{
  return HMAP_DATA (node, struct element, node);
}

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

static struct hmap_node *
find_element (struct hmap *hmap, int data, hash_function *hash)
{
  struct element *e;
  HMAP_FOR_EACH_WITH_HASH (e, struct element, node, hash (data), hmap)
    if (e->data == data)
      break;
  return &e->node;
}

/* Checks that HMAP contains the CNT ints in DATA, that its
   structure is correct, and that certain operations on HMAP
   produce the expected results. */
static void
check_hmap (struct hmap *hmap, const int data[], size_t cnt,
            hash_function *hash)
{
  size_t i, j;
  int *order;

  check (hmap_is_empty (hmap) == (cnt == 0));
  check (hmap_count (hmap) == cnt);
  check (cnt <= hmap_capacity (hmap));

  order = xmemdup (data, cnt * sizeof *data);
  qsort (order, cnt, sizeof *order, compare_ints);

  for (i = 0; i < cnt; i = j)
    {
      struct element *e;
      int count;

      for (j = i + 1; j < cnt; j++)
        if (order[i] != order[j])
          break;

      count = 0;
      HMAP_FOR_EACH_WITH_HASH (e, struct element, node, hash (order[i]), hmap)
        if (e->data == order[i]) 
          count++;

      check (count == j - i);
    }

  check (find_element (hmap, -1, hash) == NULL);

  if (cnt == 0)
    check (hmap_first (hmap) == NULL);
  else
    {
      struct hmap_node *p;
      int left;

      left = cnt;
      for (p = hmap_first (hmap), i = 0; i < cnt; p = hmap_next (hmap, p), i++)
        {
          struct element *e = hmap_node_to_element (p);

          check (hmap_node_hash (&e->node) == hash (e->data));
          for (j = 0; j < left; j++)
            if (order[j] == e->data) 
              {
                order[j] = order[--left];
                goto next;
              }
          check_die ();

        next: ;
        }
      check (p == NULL);
    }

  free (order);
}

/* Inserts the CNT values from 0 to CNT - 1 (inclusive) into an
   HMAP in the order specified by INSERTIONS, then deletes them in
   the order specified by DELETIONS, checking the HMAP's contents
   for correctness after each operation.  Uses HASH as the hash
   function. */
static void
test_insert_delete (const int insertions[],
                    const int deletions[],
                    size_t cnt,
                    hash_function *hash)
{
  struct element *elements;
  struct hmap hmap;
  size_t i;

  elements = xnmalloc (cnt, sizeof *elements);
  for (i = 0; i < cnt; i++)
    elements[i].data = i;

  hmap_init (&hmap);
  hmap_reserve (&hmap, 1);
  check_hmap (&hmap, NULL, 0, hash);
  for (i = 0; i < cnt; i++)
    {
      size_t capacity;
      hmap_insert (&hmap, &elements[insertions[i]].node, hash (insertions[i]));
      check_hmap (&hmap, insertions, i + 1, hash);

      /* A series of insertions should not produce a shrinkable hmap. */
      capacity = hmap_capacity (&hmap);
      hmap_shrink (&hmap);
      check (capacity == hmap_capacity (&hmap));
    }
  for (i = 0; i < cnt; i++)
    {
      hmap_delete (&hmap, &elements[deletions[i]].node);
      check_hmap (&hmap, deletions + i + 1, cnt - i - 1, hash);
    }
  hmap_destroy (&hmap);

  free (elements);
}

/* Inserts values into an HMAP in each possible order, then
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
            test_insert_delete (insertions, deletions, cnt, hash);

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

/* Inserts values into an HMAP in each possible order, then
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
        test_insert_delete (values, values, cnt, hash);
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

/* Inserts values into an HMAP in each possible order, then
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

          test_insert_delete (insertions, deletions, cnt, hash);
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

/* Inserts and removes up to MAX_ELEMS values in an hmap, in
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

          test_insert_delete (insertions, deletions, cnt, hash);
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

/* Inserts MAX_ELEMS elements into an HMAP in ascending order,
   then delete in ascending order and shrink the hmap at each
   step, using hash function HASH. */
static void
test_insert_ordered (int max_elems, hash_function *hash)
{
  struct element *elements;
  int *values;
  struct hmap hmap;
  int i;

#if __GNUC__ == 4 && __GNUC_MINOR__ == 3
  /* This tells the Autotest framework that the test was skipped. */
  exit (77);
#endif

  hmap_init (&hmap);
  elements = xnmalloc (max_elems, sizeof *elements);
  values = xnmalloc (max_elems, sizeof *values);
  for (i = 0; i < max_elems; i++)
    {
      values[i] = elements[i].data = i;
      hmap_insert (&hmap, &elements[i].node, hash (elements[i].data));
      check_hmap (&hmap, values, i + 1, hash);

      if (hash == identity_hash) 
        {
          /* Check that every every hash bucket has (almost) the
             same number of nodes in it.  */
          int min = INT_MAX;
          int max = INT_MIN;
          int j;

          for (j = 0; j <= hmap.mask; j++) 
            {
              int count = 0;
              struct hmap_node *node;

              for (node = hmap.buckets[j]; node != NULL; node = node->next)
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
      hmap_delete (&hmap, &elements[i].node);
      hmap_shrink (&hmap);
      check_hmap (&hmap, values + i + 1, max_elems - i - 1, hash);
    }
  hmap_destroy (&hmap);
  free (elements);
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

/* Inserts up to MAX_ELEMS elements into an HMAP, then moves the
   nodes around in memory, using hash function HASH. */
static void
test_moved (int max_elems, hash_function *hash)
{
  struct element *e[2];
  int cur;
  int *values;
  struct hmap hmap;
  int i, j;

#if __GNUC__ == 4 && __GNUC_MINOR__ == 3
  /* This tells the Autotest framework that the test was skipped. */
  exit (77);
#endif

  hmap_init (&hmap);
  e[0] = xnmalloc (max_elems, sizeof *e[0]);
  e[1] = xnmalloc (max_elems, sizeof *e[1]);
  values = xnmalloc (max_elems, sizeof *values);
  cur = 0;
  for (i = 0; i < max_elems; i++)
    {
      values[i] = e[cur][i].data = i;
      hmap_insert (&hmap, &e[cur][i].node, hash (e[cur][i].data));
      check_hmap (&hmap, values, i + 1, hash);

      for (j = 0; j <= i; j++)
        {
          e[!cur][j] = e[cur][j];
          hmap_moved (&hmap, &e[!cur][j].node, &e[cur][j].node);
          check_hmap (&hmap, values, i + 1, hash);
        }
      cur = !cur;
    }
  hmap_destroy (&hmap);
  free (e[0]);
  free (e[1]);
  free (values);
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

/* Inserts values into an HMAP, then changes their values, using
   hash function HASH. */
static void
test_changed (hash_function *hash)
{
  const int max_elems = 6;
  int cnt;

  for (cnt = 0; cnt <= max_elems; cnt++)
    {
      int *values, *changed_values;
      struct element *elements;
      unsigned int permutation_cnt;
      int i;

      values = xnmalloc (cnt, sizeof *values);
      changed_values = xnmalloc (cnt, sizeof *changed_values);
      elements = xnmalloc (cnt, sizeof *elements);
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
                  struct hmap hmap;

                  hmap_init (&hmap);

                  /* Add to HMAP in order. */
                  for (k = 0; k < cnt; k++)
                    {
                      int n = values[k];
                      elements[n].data = n;
                      hmap_insert (&hmap, &elements[n].node,
                                   hash (elements[n].data));
                    }
                  check_hmap (&hmap, values, cnt, hash);

                  /* Change value i to j. */
                  elements[i].data = j;
                  hmap_changed (&hmap, &elements[i].node,
                                hash (elements[i].data));
                  for (k = 0; k < cnt; k++)
                    changed_values[k] = k;
                  changed_values[i] = j;
                  check_hmap (&hmap, changed_values, cnt, hash);

                  hmap_destroy (&hmap);
                }
            }
        }
      check (permutation_cnt == factorial (cnt));

      free (values);
      free (changed_values);
      free (elements);
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

static void
test_swap (int max_elems, hash_function *hash) 
{
  struct element *elements;
  int *values;
  struct hmap a, b;
  struct hmap *working, *empty;
  int i;

#if __GNUC__ == 4 && __GNUC_MINOR__ == 3
  /* This tells the Autotest framework that the test was skipped. */
  exit (77);
#endif

  hmap_init (&a);
  hmap_init (&b);
  working = &a;
  empty = &b;
  elements = xnmalloc (max_elems, sizeof *elements);
  values = xnmalloc (max_elems, sizeof *values);
  for (i = 0; i < max_elems; i++)
    {
      struct hmap *tmp;
      values[i] = elements[i].data = i;
      hmap_insert (working, &elements[i].node, hash (elements[i].data));
      check_hmap (working, values, i + 1, hash);
      check_hmap (empty, NULL, 0, hash);
      hmap_swap (&a, &b);
      tmp = working;
      working = empty;
      empty = tmp;
    }
  hmap_destroy (&a);
  hmap_destroy (&b);
  free (elements);
  free (values);
}

static void
test_swap_random_hash (void) 
{
  test_swap (128, random_hash);
}

/* Inserts elements into an hmap in ascending order, then clears the hash table
   using hmap_clear(). */
static void
test_clear (void)
{
  const int max_elems = 128;
  struct element *elements;
  int *values;
  struct hmap hmap;
  int cnt;

#if __GNUC__ == 4 && __GNUC_MINOR__ == 3
  /* This tells the Autotest framework that the test was skipped. */
  exit (77);
#endif

  elements = xnmalloc (max_elems, sizeof *elements);
  values = xnmalloc (max_elems, sizeof *values);

  for (cnt = 0; cnt <= max_elems; cnt++)
    {
      int i;

      hmap_init (&hmap);
      for (i = 0; i < cnt; i++)
        {
          values[i] = elements[i].data = i;
          hmap_insert (&hmap, &elements[i].node,
                       random_hash (elements[i].data));
          check_hmap (&hmap, values, i + 1, random_hash);
        }
      hmap_clear (&hmap);
      check_hmap (&hmap, NULL, 0, random_hash);
      hmap_destroy (&hmap);
    }

  free (elements);
  free (values);
}

static void
test_destroy_null (void) 
{
  hmap_destroy (NULL);
}

/* Test shrinking an empty hash table. */
static void
test_shrink_empty (void)
{
  struct hmap hmap;

  hmap_init (&hmap);
  hmap_reserve (&hmap, 123);
  hmap_shrink (&hmap);
  hmap_destroy (&hmap);
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
      printf ("%s: test hash map\n"
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
