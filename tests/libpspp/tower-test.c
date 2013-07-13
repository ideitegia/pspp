/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2010, 2013 Free Software Foundation, Inc.

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

/* This is a test program for the routines defined in tower.c.
   This test program aims to be as comprehensive as possible.
   With -DNDEBUG, "gcov -b" should report 100% coverage of lines
   and branches in tower.c routines.  (Without -DNDEBUG, branches
   caused by failed assertions will not be taken.)  "valgrind
   --leak-check=yes --show-reachable=yes" should give a clean
   report, both with and without -DNDEBUG. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libpspp/tower.h>

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpspp/compiler.h>

#include "xalloc.h"

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

/* Node type and support routines. */

/* Test data block. */
struct block
  {
    struct tower_node node;     /* Embedded tower block. */
    int x;                      /* Primary value. */
  };

/* Returns the `struct block' that NODE is embedded within. */
static struct block *
tower_node_to_block (const struct tower_node *node)
{
  return tower_data (node, struct block, node);
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

/* Arranges the CNT blocks in VALUES into the lexicographically
   next greater permutation.  Returns true if successful.
   If VALUES is already the lexicographically greatest
   permutation of its blocks (i.e. ordered from greatest to
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
  /* Disallow N values that overflow on 32-bit machines. */
  assert (n <= 12);
  for (; n > 1; )
    value *= n--;
  return value;
}

/* Returns C(n, k), the number of ways that K choices can be made
   from N items when order is unimportant. */
static unsigned int
binomial_cofficient (unsigned int n, unsigned int k)
{
  assert (n >= k);
  return factorial (n) / factorial (k) / factorial (n - k);
}

/* Tests whether PARTS is a K-part integer composition of N.
   Returns true if so, false otherwise. */
static bool UNUSED
is_k_composition (int n, int k, const int parts[])
{
  int sum;
  int i;

  sum = 0;
  for (i = 0; i < k; i++)
    {
      if (parts[i] < 1 || parts[i] > n)
        return false;
      sum += parts[i];
    }
  return sum == n;
}

/* Advances the K-part integer composition of N stored in PARTS
   to the next lexicographically greater one.
   Returns true if successful, false if the composition was
   already the greatest K-part composition of N (in which case
   PARTS is unaltered). */
static bool
next_k_composition (int n UNUSED, int k, int parts[])
{
  int x, i;

  assert (is_k_composition (n, k, parts));
  if (k == 1)
    return false;

  for (i = k - 1; i > 0; i--)
    if (parts[i] > 1)
      break;
  if (i == 0)
    return false;

  x = parts[i] - 1;
  parts[i] = 1;
  parts[i - 1]++;
  parts[k - 1] = x;

  assert (is_k_composition (n, k, parts));
  return true;
}

/* Sets the K integers in PARTS to the lexicographically first
   K-part composition of N. */
static void
first_k_composition (int n, int k, int parts[])
{
  int i;

  assert (n >= k);

  for (i = 0; i < k; i++)
    parts[i] = 1;
  parts[k - 1] += n - k;
}

/* Advances *K and PARTS to the next integer composition of N.
   Compositions are ordered from shortest to longest and in
   lexicographical order within a given length.
   Before the first call, initialize *K to 0.
   After each successful call, *K contains the length of the
   current composition and the *K blocks in PARTS contain its
   parts.
   Returns true if successful, false if the set of compositions
   has been exhausted. */
static bool
next_composition (int n, int *k, int parts[])
{
  if (*k >= 1 && next_k_composition (n, *k, parts))
    return true;
  else if (*k < n)
    {
      first_k_composition (n, ++*k, parts);
      return true;
    }
  else
    return false;
}

/* A block expected to be found in a tower. */
struct expected_block
  {
    int size;           /* Expected thickness of block. */
    int x;              /* Expected value for `x' member. */
  };

/* Checks that tower T contains the BLOCK_CNT blocks described by
   BLOCKS[]. */
static void
check_tower (struct tower *t,
             struct expected_block blocks[], size_t block_cnt)
{
  int total_height;
  struct tower_node *node;
  size_t i;

  check (tower_count (t) == block_cnt);
  check (tower_is_empty (t) == (block_cnt == 0));

  total_height = 0;
  for (i = 0; i < block_cnt; i++)
    {
      unsigned long int level;
      for (level = total_height;
           level < total_height + blocks[i].size;
           level++)
        {
          struct tower_node *found;
          unsigned long int block_start;
          found = tower_lookup (t, level, &block_start);
          check (found != NULL);
          check (tower_node_to_block (found)->x == blocks[i].x);
          check (block_start == total_height);
          check (tower_node_get_level (found) == total_height);
          check (tower_node_get_index (found) == i);
          check (tower_get (t, i) == found);
        }
      total_height += blocks[i].size;
    }
  check (tower_height (t) == total_height);

  for (node = tower_first (t), i = 0;
       node != NULL;
       node = tower_next (t, node), i++)
    {
      check (tower_node_get_size (node) == blocks[i].size);
      check (tower_node_to_block (node)->x == blocks[i].x);
    }
  check (i == block_cnt);

  for (node = tower_last (t), i = block_cnt - 1;
       node != NULL;
       node = tower_prev (t, node), i--)
    {
      check (tower_node_get_size (node) == blocks[i].size);
      check (tower_node_to_block (node)->x == blocks[i].x);
    }
  check (i == SIZE_MAX);
}

/* Tests inserting all possible sets of block heights into a
   tower in all possible orders, up to a specified maximum tower
   height. */
static void
test_insert (void)
{
  const int max_height = 7;
  int cnt;

  for (cnt = 1; cnt <= max_height; cnt++)
    {
      unsigned int composition_cnt;
      struct expected_block *expected;
      int *sizes;
      int block_cnt;
      int *order;
      struct block *blocks;

      expected = xnmalloc (cnt, sizeof *expected);
      sizes = xnmalloc (cnt, sizeof *sizes);
      order = xnmalloc (cnt, sizeof *order);
      blocks = xnmalloc (cnt, sizeof *blocks);

      block_cnt = 0;
      composition_cnt = 0;
      while (next_composition (cnt, &block_cnt, sizes))
        {
          int i, j;
          unsigned int permutation_cnt;

          for (i = 0; i < block_cnt; i++)
            order[i] = i;

          permutation_cnt = 0;
          while (permutation_cnt == 0 || next_permutation (order, block_cnt))
            {
              struct tower t;

              /* Inserts the block_cnt blocks with the given
                 sizes[] into T in the order given by order[]. */
              tower_init (&t);
              for (i = 0; i < block_cnt; i++)
                {
                  struct block *under;
                  int idx;

                  idx = order[i];
                  blocks[idx].x = idx;

                  under = NULL;
                  for (j = 0; j < i; j++)
                    if (idx < order[j]
                        && (under == NULL || under->x > order[j]))
                      under = &blocks[order[j]];

                  tower_insert (&t, sizes[idx], &blocks[idx].node,
                                under != NULL ? &under->node : NULL);
                }

              /* Check that the result is what we expect. */
              for (i = 0; i < block_cnt; i++)
                {
                  expected[i].size = sizes[i];
                  expected[i].x = i;
                }
              check_tower (&t, expected, block_cnt);

              permutation_cnt++;
            }
          check (permutation_cnt == factorial (block_cnt));

          composition_cnt++;
        }
      check (composition_cnt == 1 << (cnt - 1));

      free (expected);
      free (sizes);
      free (order);
      free (blocks);
    }
}

/* Tests deleting blocks from towers that initially contain all
   possible sets of block sizes into a tower in all possible
   orders, up to a specified maximum tower height. */
static void
test_delete (void)
{
  const int max_height = 7;
  int cnt;

  for (cnt = 1; cnt <= max_height; cnt++)
    {
      unsigned int composition_cnt;
      struct expected_block *expected;
      int *sizes;
      int block_cnt;
      int *order;
      struct block *blocks;

      expected = xnmalloc (cnt, sizeof *expected);
      sizes = xnmalloc (cnt, sizeof *sizes);
      order = xnmalloc (cnt, sizeof *order);
      blocks = xnmalloc (cnt, sizeof *blocks);

      block_cnt = 0;
      composition_cnt = 0;
      while (next_composition (cnt, &block_cnt, sizes))
        {
          int i;
          unsigned int permutation_cnt;

          for (i = 0; i < block_cnt; i++)
            order[i] = i;

          permutation_cnt = 0;
          while (permutation_cnt == 0 || next_permutation (order, block_cnt))
            {
              struct tower t;

              /* Insert blocks into tower in ascending order. */
              tower_init (&t);
              for (i = 0; i < block_cnt; i++)
                {
                  blocks[i].x = i;
                  tower_insert (&t, sizes[i], &blocks[i].node, NULL);
                  expected[i].x = i;
                  expected[i].size = sizes[i];
                }
              check_tower (&t, expected, block_cnt);

              /* Delete blocks from tower in the order of
                 order[]. */
              for (i = 0; i < block_cnt; i++)
                {
                  int idx = order[i];
                  int j;
                  tower_delete (&t, &blocks[idx].node);
                  for (j = 0; ; j++)
                    {
                      assert (j < block_cnt - i);
                      if (expected[j].x == idx)
                        {
                          memmove (&expected[j], &expected[j + 1],
                                   sizeof *expected * (block_cnt - i - j - 1));
                          break;
                        }
                    }
                  check_tower (&t, expected, block_cnt - i - 1);
                }

              permutation_cnt++;
            }
          check (permutation_cnt == factorial (block_cnt));

          composition_cnt++;
        }
      check (composition_cnt == 1 << (cnt - 1));

      free (expected);
      free (sizes);
      free (order);
      free (blocks);
    }
}

/* Tests towers containing all possible block sizes, resizing
   the blocks to all possible sizes that conserve the total
   tower height, up to a maximum total tower height. */
static void
test_resize (void)
{
  const int max_height = 9;
  int cnt;

  for (cnt = 1; cnt <= max_height; cnt++)
    {
      unsigned int composition_cnt;
      struct expected_block *expected;
      int *sizes, *new_sizes;
      int block_cnt;
      int *order;
      struct block *blocks;

      expected = xnmalloc (cnt, sizeof *expected);
      sizes = xnmalloc (cnt, sizeof *sizes);
      new_sizes = xnmalloc (cnt, sizeof *new_sizes);
      order = xnmalloc (cnt, sizeof *order);
      blocks = xnmalloc (cnt, sizeof *blocks);

      block_cnt = 0;
      composition_cnt = 0;
      while (next_composition (cnt, &block_cnt, sizes))
        {
          int i;
          unsigned int resizes = 0;

          for (resizes = 0, first_k_composition (cnt, block_cnt, new_sizes);
               (resizes == 0
                || next_k_composition (cnt, block_cnt, new_sizes));
               resizes++)
            {
              struct tower t;

              /* Insert blocks into tower in ascending order. */
              tower_init (&t);
              for (i = 0; i < block_cnt; i++)
                {
                  blocks[i].x = i;
                  tower_insert (&t, sizes[i], &blocks[i].node, NULL);
                  expected[i].x = i;
                  expected[i].size = sizes[i];
                }
              check_tower (&t, expected, block_cnt);

              /* Resize all the blocks. */
              for (i = 0; i < block_cnt; i++)
                {
                  if (expected[i].size != new_sizes[i] || rand () % 2)
                    tower_resize (&t, &blocks[i].node, new_sizes[i]);
                  expected[i].size = new_sizes[i];
                }
              check_tower (&t, expected, block_cnt);
            }
          check (resizes == binomial_cofficient (cnt - 1, block_cnt - 1));

          composition_cnt++;
        }
      check (composition_cnt == 1 << (cnt - 1));

      free (expected);
      free (new_sizes);
      free (sizes);
      free (order);
      free (blocks);
    }
}

/* Tests splicing all possible contiguous sets of blocks out of one
   tower into a second, initially empty tower. */
static void
test_splice_out (void)
{
  const int max_height = 9;
  int cnt;

  for (cnt = 1; cnt <= max_height; cnt++)
    {
      unsigned int composition_cnt;
      struct expected_block *expected;
      int *sizes, *new_sizes;
      int block_cnt;
      int *order;
      struct block *blocks;

      expected = xnmalloc (cnt, sizeof *expected);
      sizes = xnmalloc (cnt, sizeof *sizes);
      new_sizes = xnmalloc (cnt, sizeof *new_sizes);
      order = xnmalloc (cnt, sizeof *order);
      blocks = xnmalloc (cnt, sizeof *blocks);

      block_cnt = 0;
      composition_cnt = 0;
      while (next_composition (cnt, &block_cnt, sizes))
        {
          int i, j;

          for (i = 0; i < block_cnt; i++)
            for (j = i; j <= block_cnt; j++)
              {
                struct tower src, dst;
                int k;

                tower_init (&src);
                tower_init (&dst);

                /* Insert blocks into SRC and DST in ascending order. */
                for (k = 0; k < block_cnt; k++)
                  {
                    blocks[k].x = k;
                    tower_insert (&src, sizes[k], &blocks[k].node, NULL);
                    expected[k].x = k;
                    expected[k].size = sizes[k];
                  }
                check_tower (&src, expected, block_cnt);

                /* Splice blocks I...J into DST. */
                tower_splice (&dst, NULL, &src, &blocks[i].node,
                              j < block_cnt ? &blocks[j].node : NULL);
                check_tower (&dst, &expected[i], j - i);
                memmove (&expected[i], &expected[j],
                         sizeof *expected * (block_cnt - j));
                check_tower (&src, expected, block_cnt - (j - i));
              }
           composition_cnt++;
        }
      check (composition_cnt == 1 << (cnt - 1));

      free (expected);
      free (new_sizes);
      free (sizes);
      free (order);
      free (blocks);
    }
}

/* Tests splicing all of the contents of a tower into all
   possible positions in a second tower. */
static void
test_splice_in (void)
{
  const int max_height = 9;
  int cnt;

  for (cnt = 1; cnt <= max_height; cnt++)
    {
      unsigned int composition_cnt;
      struct expected_block *expected;
      int *sizes, *new_sizes;
      int block_cnt;
      int *order;
      struct block *blocks;

      expected = xnmalloc (cnt, sizeof *expected);
      sizes = xnmalloc (cnt, sizeof *sizes);
      new_sizes = xnmalloc (cnt, sizeof *new_sizes);
      order = xnmalloc (cnt, sizeof *order);
      blocks = xnmalloc (cnt, sizeof *blocks);

      block_cnt = 0;
      composition_cnt = 0;
      while (next_composition (cnt, &block_cnt, sizes))
        {
          int i, j;

          for (i = 0; i < block_cnt; i++)
            for (j = i; j <= block_cnt; j++)
              {
                struct tower src, dst;
                int k;

                tower_init (&src);
                tower_init (&dst);

                /* Insert blocks into SRC and DST in ascending order. */
                for (k = 0; k < block_cnt; k++)
                  {
                    blocks[k].x = k;
                    tower_insert (k >= i && k < j ? &src : &dst,
                                  sizes[k], &blocks[k].node, NULL);
                    expected[k].x = k;
                    expected[k].size = sizes[k];
                  }

                /* Splice SRC into DST. */
                tower_splice (&dst, j < block_cnt ? &blocks[j].node : NULL,
                              &src, i != j ? &blocks[i].node : NULL, NULL);
                check_tower (&dst, expected, block_cnt);
              }
           composition_cnt++;
        }
      check (composition_cnt == 1 << (cnt - 1));

      free (expected);
      free (new_sizes);
      free (sizes);
      free (order);
      free (blocks);
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
      "insert",
      "insert",
      test_insert
    },
    {
      "delete",
      "delete",
      test_delete
    },
    {
      "resize",
      "resize",
      test_resize
    },
    {
      "splice-out",
      "splice out",
      test_splice_out
    },
    {
      "splice-in",
      "splice in",
      test_splice_in
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
      printf ("%s: test tower library\n"
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
