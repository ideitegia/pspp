/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2010, 2011 Free Software Foundation, Inc.

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

/* This is a test program for the abt_* routines defined in
   abt.c.  This test program aims to be as comprehensive as
   possible.  "gcov -b" should report 100% coverage of lines and
   branches in the abt_* routines.  "valgrind --leak-check=yes
   --show-reachable=yes" should give a clean report. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libpspp/abt.h>

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
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
    struct abt_node node;       /* Embedded binary tree element. */
    int data;                   /* Primary value. */
    int count;                  /* Number of nodes in subtree,
                                   including this node. */
  };

static int aux_data;

/* Returns the `struct element' that NODE is embedded within. */
static struct element *
abt_node_to_element (const struct abt_node *node)
{
  return abt_data (node, struct element, node);
}

/* Compares the `x' values in A and B and returns a strcmp-type
   return value.  Verifies that AUX points to aux_data. */
static int
compare_elements (const struct abt_node *a_, const struct abt_node *b_,
                  const void *aux)
{
  const struct element *a = abt_node_to_element (a_);
  const struct element *b = abt_node_to_element (b_);

  check (aux == &aux_data);
  return a->data < b->data ? -1 : a->data > b->data;
}

/* Recalculates the count for NODE's subtree by adding up the
   counts for its LEFT and RIGHT child subtrees. */
static void
reaugment_elements (struct abt_node *node_, const void *aux)
{
  struct element *node = abt_node_to_element (node_);

  check (aux == &aux_data);

  node->count = 1;
  if (node->node.down[0] != NULL)
    node->count += abt_node_to_element (node->node.down[0])->count;
  if (node->node.down[1] != NULL)
    node->count += abt_node_to_element (node->node.down[1])->count;
}

/* Compares A and B and returns a strcmp-type return value. */
static int
compare_ints_noaux (const void *a_, const void *b_)
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

/* Finds and returns the element in ABT that is in the given
   0-based POSITION in in-order. */
static struct element *
find_by_position (struct abt *abt, int position)
{
  struct abt_node *p;
  for (p = abt->root; p != NULL; )
    {
      int p_pos = p->down[0] ? abt_node_to_element (p->down[0])->count : 0;
      if (position == p_pos)
        return abt_node_to_element (p);
      else if (position < p_pos)
        p = p->down[0];
      else
        {
          p = p->down[1];
          position -= p_pos + 1;
        }
    }
  return NULL;
}

/* Checks that all the augmentations are correct in the subtree
   rooted at P.  Returns the number of nodes in the subtree. */
static int
check_augmentations (struct abt_node *p_)
{
  if (p_ == NULL)
    return 0;
  else
    {
      struct element *p = abt_node_to_element (p_);
      int left_count = check_augmentations (p->node.down[0]);
      int right_count = check_augmentations (p->node.down[1]);
      int total = left_count + right_count + 1;
      check (p->count == total);
      return total;
    }
}

/* Check that the levels are correct in the subtree rooted at P. */
static void
check_levels (struct abt_node *p)
{
  if (p != NULL)
    {
      int i, j;

      check_levels (p->down[0]);
      check_levels (p->down[1]);

      check (p->level >= 1);
      if (p->level > 1)
        {
          struct abt_node *q = p->down[1];
          check (q != NULL);
          check (q->level == p->level || q->level == p->level - 1);
        }

      for (i = 0; i < 2; i++)
        if (p->down[i] != NULL)
          for (j = 0; j < 2; j++)
            if (p->down[i]->down[j] != NULL)
              check (p->down[i]->down[j]->level < p->level);
    }
}

/* Checks that ABT contains the CNT ints in DATA, that its
   structure is correct, and that certain operations on ABT
   produce the expected results. */
static void
check_abt (struct abt *abt, const int data[], size_t cnt)
{
  struct element e;
  size_t i;
  int *order;

  order = xmemdup (data, cnt * sizeof *data);
  qsort (order, cnt, sizeof *order, compare_ints_noaux);

  if (abt->compare != NULL)
    {
      for (i = 0; i < cnt; i++)
        {
          struct abt_node *p;

          e.data = data[i];
          if (rand () % 2)
            p = abt_find (abt, &e.node);
          else
            p = abt_insert (abt, &e.node);
          check (p != NULL);
          check (p != &e.node);
          check (abt_node_to_element (p)->data == data[i]);
        }

      e.data = -1;
      check (abt_find (abt, &e.node) == NULL);
    }

  check_levels (abt->root);
  check_augmentations (abt->root);
  for (i = 0; i < cnt; i++)
    check (find_by_position (abt, i)->data == order[i]);

  if (cnt == 0)
    {
      check (abt_first (abt) == NULL);
      check (abt_last (abt) == NULL);
      check (abt_next (abt, NULL) == NULL);
      check (abt_prev (abt, NULL) == NULL);
    }
  else
    {
      struct abt_node *p;

      for (p = abt_first (abt), i = 0; i < cnt; p = abt_next (abt, p), i++)
        check (abt_node_to_element (p)->data == order[i]);
      check (p == NULL);

      for (p = abt_last (abt), i = 0; i < cnt; p = abt_prev (abt, p), i++)
        check (abt_node_to_element (p)->data == order[cnt - i - 1]);
      check (p == NULL);
    }
  check (abt_is_empty (abt) == (cnt == 0));

  free (order);
}

/* Ways that nodes can be inserted. */
enum insertion_method
  {
    INSERT,             /* With abt_insert. */
    INSERT_AFTER,       /* With abt_insert_after. */
    INSERT_BEFORE       /* With abt_insert_before. */
  };

/* Inserts INSERT into ABT with the given METHOD. */
static void
insert_node (struct abt *abt, struct element *insert,
             enum insertion_method method)
{
  if (method == INSERT)
    check (abt_insert (abt, &insert->node) == NULL);
  else
    {
      struct abt_node *p = abt->root;
      int dir = 0;
      if (p != NULL)
        for (;;)
          {
            dir = insert->data > abt_node_to_element (p)->data;
            if (p->down[dir] == NULL)
              break;
            p = p->down[dir];
          }
      if (method == INSERT_AFTER)
        {
          if (p != NULL && (dir != 1 || p->down[1] != NULL))
            p = abt_prev (abt, p);
          abt_insert_after (abt, p, &insert->node);
        }
      else
        {
          if (p != NULL && (dir != 0 || p->down[0] != NULL))
            p = abt_next (abt, p);
          abt_insert_before (abt, p, &insert->node);
        }
    }
}


/* Inserts the CNT values from 0 to CNT - 1 (inclusive) into an
   ABT in the order specified by INSERTIONS using the given
   METHOD, then deletes them in the order specified by DELETIONS,
   checking the ABT's contents for correctness after each
   operation. */
static void
do_test_insert_delete (enum insertion_method method,
                       const int insertions[],
                       const int deletions[],
                       size_t cnt)
{
  struct element *elements;
  struct abt abt;
  size_t i;

  elements = xnmalloc (cnt, sizeof *elements);
  for (i = 0; i < cnt; i++)
    elements[i].data = i;

  abt_init (&abt, method == INSERT ? compare_elements : NULL,
            reaugment_elements, &aux_data);
  check_abt (&abt, NULL, 0);
  for (i = 0; i < cnt; i++)
    {
      insert_node (&abt, &elements[insertions[i]], method);
      check_abt (&abt, insertions, i + 1);
    }
  for (i = 0; i < cnt; i++)
    {
      abt_delete (&abt, &elements[deletions[i]].node);
      check_abt (&abt, deletions + i + 1, cnt - i - 1);
    }

  free (elements);
}

/* Inserts the CNT values from 0 to CNT - 1 (inclusive) into an
   ABT in the order specified by INSERTIONS, then deletes them in
   the order specified by DELETIONS, checking the ABT's contents
   for correctness after each operation. */
static void
test_insert_delete (const int insertions[],
                    const int deletions[],
                    size_t cnt)
{
  do_test_insert_delete (INSERT, insertions, deletions, cnt);
  do_test_insert_delete (INSERT_AFTER, insertions, deletions, cnt);
  do_test_insert_delete (INSERT_BEFORE, insertions, deletions, cnt);
}

/* Inserts values into an ABT in each possible order, then
   removes them in each possible order, up to a specified maximum
   size. */
static void
test_insert_any_remove_any (void)
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
            test_insert_delete (insertions, deletions, cnt);

          check (del_perm_cnt == factorial (cnt));
        }
      check (ins_perm_cnt == factorial (cnt));

      free (insertions);
      free (deletions);
    }
}

/* Inserts values into an ABT in each possible order, then
   removes them in the same order, up to a specified maximum
   size. */
static void
test_insert_any_remove_same (void)
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
        test_insert_delete (values, values, cnt);
      check (permutation_cnt == factorial (cnt));

      free (values);
    }
}

/* Inserts values into an ABT in each possible order, then
   removes them in reverse order, up to a specified maximum
   size. */
static void
test_insert_any_remove_reverse (void)
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

          test_insert_delete (insertions, deletions, cnt);
        }
      check (permutation_cnt == factorial (cnt));

      free (insertions);
      free (deletions);
    }
}

/* Inserts and removes values in an ABT in random orders. */
static void
test_random_sequence (void)
{
  const int max_elems = 128;
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

          test_insert_delete (insertions, deletions, cnt);
        }

      free (insertions);
      free (deletions);
    }
}

/* Inserts elements into an ABT in ascending order. */
static void
test_insert_ordered (void)
{
  const int max_elems = 1024;
  struct element *elements;
  int *values;
  struct abt abt;
  int i;

  abt_init (&abt, compare_elements, reaugment_elements, &aux_data);
  elements = xnmalloc (max_elems, sizeof *elements);
  values = xnmalloc (max_elems, sizeof *values);
  for (i = 0; i < max_elems; i++)
    {
      values[i] = elements[i].data = i;
      check (abt_insert (&abt, &elements[i].node) == NULL);
      check_abt (&abt, values, i + 1);
    }
  free (elements);
  free (values);
}

/* Inserts elements into an ABT, then moves the nodes around in
   memory. */
static void
test_moved (void)
{
  const int max_elems = 128;
  struct element *e[2];
  int cur;
  int *values;
  struct abt abt;
  int i, j;

  abt_init (&abt, compare_elements, reaugment_elements, &aux_data);
  e[0] = xnmalloc (max_elems, sizeof *e[0]);
  e[1] = xnmalloc (max_elems, sizeof *e[1]);
  values = xnmalloc (max_elems, sizeof *values);
  cur = 0;
  for (i = 0; i < max_elems; i++)
    {
      values[i] = e[cur][i].data = i;
      check (abt_insert (&abt, &e[cur][i].node) == NULL);
      check_abt (&abt, values, i + 1);

      for (j = 0; j <= i; j++)
        {
          e[!cur][j] = e[cur][j];
          abt_moved (&abt, &e[!cur][j].node);
          check_abt (&abt, values, i + 1);
        }
      cur = !cur;
    }
  free (e[0]);
  free (e[1]);
  free (values);
}

/* Inserts values into an ABT, then changes their values. */
static void
test_changed (void)
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
                  struct abt abt;
                  struct abt_node *changed_retval;

                  abt_init (&abt, compare_elements, reaugment_elements,
                            &aux_data);

                  /* Add to ABT in order. */
                  for (k = 0; k < cnt; k++)
                    {
                      int n = values[k];
                      elements[n].data = n;
                      check (abt_insert (&abt, &elements[n].node) == NULL);
                    }
                  check_abt (&abt, values, cnt);

                  /* Change value i to j. */
                  elements[i].data = j;
                  for (k = 0; k < cnt; k++)
                    changed_values[k] = k;
                  changed_retval = abt_changed (&abt, &elements[i].node);
                  if (i != j && j < cnt)
                    {
                      /* Will cause duplicate. */
                      check (changed_retval == &elements[j].node);
                      changed_values[i] = changed_values[cnt - 1];
                      check_abt (&abt, changed_values, cnt - 1);
                    }
                  else
                    {
                      /* Succeeds. */
                      check (changed_retval == NULL);
                      changed_values[i] = j;
                      check_abt (&abt, changed_values, cnt);
                    }
                }
            }
        }
      check (permutation_cnt == factorial (cnt));

      free (values);
      free (changed_values);
      free (elements);
    }
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
      "insert-any-remove-any",
      "insert any order, delete any order",
      test_insert_any_remove_any
    },
    {
      "insert-any-remove-same",
      "insert any order, delete same order",
      test_insert_any_remove_same
    },
    {
      "insert-any-remove-reverse",
      "insert any order, delete reverse order",
      test_insert_any_remove_reverse
    },
    {
      "random-sequence",
      "insert and delete in random sequence",
      test_random_sequence
    },
    {
      "insert-ordered",
      "insert in ascending order",
      test_insert_ordered
    },
    {
      "moved",
      "move elements around in memory",
      test_moved
    },
    {
      "changed",
      "change key data in nodes",
      test_changed
    }
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
      printf ("%s: test augmented binary tree\n"
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
