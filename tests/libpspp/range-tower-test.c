/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

/* This is a test program for the routines defined in
   range-tower.c.  This test program aims to be as comprehensive as
   possible.  With -DNDEBUG, "gcov -b" should report 100%
   coverage of lines and branches in range-tower.c routines.
   (Without -DNDEBUG, branches caused by failed assertions will
   not be taken.)  "valgrind --leak-check=yes
   --show-reachable=yes" should give a clean report, both with
   and without -DNDEBUG. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "libpspp/range-tower.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/compiler.h"
#include "libpspp/pool.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

/* Exit with a failure code.
   (Place a breakpoint on this function while debugging.) */
static void
check_die (void)
{
  abort ();
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

/* A contiguous region. */
struct region
  {
    unsigned long int start;    /* Start of region. */
    unsigned long int end;      /* One past the end. */
  };

/* Number of bits in an unsigned int. */
#define UINT_BIT (CHAR_BIT * sizeof (unsigned int))

/* Returns the number of contiguous 1-bits in X starting from bit
   0.
   This implementation is designed to be obviously correct, not
   to be efficient. */
static int
count_one_bits (unsigned long int x)
{
  int count = 0;
  while (x & 1)
    {
      count++;
      x >>= 1;
    }
  return count;
}

/* Searches the bits in PATTERN from right to left starting from
   bit OFFSET for one or more 1-bits.  If any are found, sets
   *START to the bit index of the first and *WIDTH to the number
   of contiguous 1-bits and returns true.  Otherwise, returns
   false.
   This implementation is designed to be obviously correct, not
   to be efficient. */
static bool
next_region (unsigned int pattern, unsigned int offset,
             unsigned long int *start, unsigned long int *width)
{
  unsigned int i;

  assert (offset <= UINT_BIT);
  for (i = offset; i < UINT_BIT; i++)
    if (pattern & (1u << i))
      {
        *start = i;
        *width = count_one_bits (pattern >> i);
        return true;
      }
  return false;
}

/* Searches the bits in PATTERN from left to right starting from
   just beyond bit OFFSET for one or more 1-bits.  If any are
   found, sets *START to the bit index of the first and *WIDTH to
   the number of contiguous 1-bits and returns true.  Otherwise,
   returns false.
   This implementation is designed to be obviously correct, not
   to be efficient. */
static bool
prev_region (unsigned int pattern, unsigned int offset,
             unsigned long int *start, unsigned long int *width)
{
  unsigned int i;

  assert (offset <= UINT_BIT);
  for (i = offset; i-- > 0; )
    if (pattern & (1u << i))
      {
        *start = i;
        *width = 1;
        while (i-- > 0 && pattern & (1u << i))
          {
            ++*width;
            --*start;
          }
        return true;
      }
  return false;
}

/* Searches the bits in PATTERN from right to left starting from
   bit OFFSET.  Returns the bit index of the first 1-bit found,
   or ULONG_MAX if none is found. */
static unsigned long int
next_1bit (unsigned int pattern, unsigned int offset,
           unsigned long int pattern_offset)
{
  for (; offset < UINT_BIT; offset++)
    if (pattern & (1u << offset))
      return offset + pattern_offset;
  return ULONG_MAX;
}

static void
print_structure (const struct abt_node *node_)
{
  struct range_tower_node *node;

  if (node_ == NULL)
    return;
  node = abt_data (node_, struct range_tower_node, abt_node);
  printf ("%lu+%lu/%d", node->n_zeros, node->n_ones, node->abt_node.level);
  if (node->abt_node.down[0] || node->abt_node.down[1])
    {
      printf ("(");
      print_structure (node->abt_node.down[0]);
      printf (",");
      print_structure (node->abt_node.down[1]);
      printf (")");
    }
}

/* Prints the regions in RT to stdout. */
static void UNUSED
print_regions (const struct range_tower *rt)
{
  const struct range_tower_node *node;

  printf ("contents:");
  for (node = range_tower_first__ (rt); node != NULL;
       node = range_tower_next__ (rt, node))
    printf (" (%lu,%lu)", node->n_zeros, node->n_ones);
  printf ("\n");
  printf ("structure:");
  print_structure (rt->abt.root);
  printf ("\n");
}

static void
check_tree (const struct abt_node *abt_node, unsigned long int *subtree_width)
{
  const struct range_tower_node *node = range_tower_node_from_abt__ (abt_node);
  unsigned long int left_width, right_width;

  if (node == NULL)
    {
      *subtree_width = 0;
      return;
    }

  check_tree (node->abt_node.down[0], &left_width);
  check_tree (node->abt_node.down[1], &right_width);

  *subtree_width = node->n_zeros + node->n_ones + left_width + right_width;
  check (node->subtree_width == *subtree_width);
}

/* Checks that the regions in RT match the bits in PATTERN. */
static void
check_pattern (const struct range_tower *rt, unsigned int pattern,
               unsigned long int offset)
{
  const struct range_tower_node *node;
  unsigned long int start, start2, width;
  unsigned long int tree_width;
  unsigned long int s1, s2;
  int i;

  check_tree (rt->abt.root, &tree_width);
  check (tree_width == ULONG_MAX);

  if (offset > ULONG_MAX - 32)
    {
      pattern <<= offset - (ULONG_MAX - 32);
      offset = ULONG_MAX - 32;
    }

  for (node = rand () % 2 ? range_tower_first (rt) : range_tower_next (rt, NULL),
         start = width = 0;
       next_region (pattern, start + width, &start, &width);
       node = range_tower_next (rt, node))
    {
      unsigned long int node_start;
      unsigned long int x;

      check (node != NULL);
      check (range_tower_node_get_start (node) == start + offset);
      check (range_tower_node_get_end (node) == start + offset + width);
      check (range_tower_node_get_width (node) == width);

      x = start + offset - node->n_zeros;
      check (range_tower_lookup (rt, x, &node_start) == node);
      check (node_start == start + offset - node->n_zeros);

      x = start + offset + width - 1;
      check (range_tower_lookup (rt, x, &node_start) == node);
      check (node_start == start + offset - node->n_zeros);
    }
  check (node == NULL);

  start = width = 0;
  RANGE_TOWER_FOR_EACH (node, start2, rt)
    {
      check (next_region (pattern, start + width, &start, &width));
      check (start + offset == start2);
      check (range_tower_node_get_width (node) == width);
    }
  check (!next_region (pattern, start + width, &start, &width));

  for (node = rand () % 2 ? range_tower_last (rt) : range_tower_prev (rt, NULL),
         start = UINT_BIT;
       prev_region (pattern, start, &start, &width);
       node = range_tower_prev (rt, node))
    {
      check (node != NULL);
      check (range_tower_node_get_start (node) == offset + start);
      check (range_tower_node_get_end (node) == offset + start + width);
      check (range_tower_node_get_width (node) == width);
    }
  check (node == NULL);

  /* Scan from all possible positions, resetting the cache each
     time, to ensure that we get the correct answers without
     caching. */
  for (start = 0; start <= 32; start++)
    {
      struct range_tower *nonconst_rt = CONST_CAST (struct range_tower *, rt);

      nonconst_rt->cache_end = 0;
      s1 = range_tower_scan (rt, offset + start);
      s2 = next_1bit (pattern, start, offset);
      check (s1 == s2);
    }

  /* Scan in forward order to exercise expected cache behavior. */
  for (s1 = range_tower_scan (rt, 0), s2 = next_1bit (pattern, 0, offset); ;
       s1 = range_tower_scan (rt, s1 + 1), s2 = next_1bit (pattern, (s2 - offset) + 1, offset))
    {
      check (s1 == s2);
      if (s1 == ULONG_MAX)
        break;
    }

  /* Scan in random order to frustrate cache. */
  for (i = 0; i < 32; i++)
    {
      start = rand () % 32;
      s1 = range_tower_scan (rt, start + offset);
      s2 = next_1bit (pattern, start, offset);
      check (s1 == s2);
    }

  /* Test range_tower_scan() with negative cache. */
  check (!range_tower_contains (rt, 999));
  if (offset < 1111)
    check (range_tower_scan (rt, 1111) == ULONG_MAX);

  /* Check for containment without caching. */
  for (i = 0; i < UINT_BIT; i++)
    {
      struct range_tower *nonconst_rt = CONST_CAST (struct range_tower *, rt);
      nonconst_rt->cache_end = 0;
      check (range_tower_contains (rt, i + offset)
             == ((pattern & (1u << i)) != 0));
    }

  /* Check for containment with caching. */
  for (i = 0; i < UINT_BIT; i++)
    check (range_tower_contains (rt, i + offset)
             == ((pattern & (1u << i)) != 0));

  check (!range_tower_contains (rt,
                                UINT_BIT + rand () % (ULONG_MAX - UINT_BIT * 2)));

  check (range_tower_is_empty (rt) == (pattern == 0));
}

/* Creates and returns a range tower that contains regions for the
   bits tower in PATTERN. */
static struct range_tower *
make_pattern (unsigned int pattern, unsigned long int offset)
{
  unsigned long int start = 0;
  unsigned long int width = 0;
  struct range_tower *rt = range_tower_create_pool (NULL);
  while (next_region (pattern, start + width, &start, &width))
    range_tower_set1 (rt, start + offset, width);
  check_pattern (rt, pattern, offset);
  return rt;
}

/* Returns an unsigned int with bits OFS...OFS+CNT (exclusive)
   tower to 1, other bits tower to 0. */
static unsigned int
bit_range (unsigned int ofs, unsigned int cnt)
{
  assert (ofs < UINT_BIT);
  assert (cnt <= UINT_BIT);
  assert (ofs + cnt <= UINT_BIT);

  return cnt < UINT_BIT ? ((1u << cnt) - 1) << ofs : UINT_MAX;
}

/* Tests setting all possible ranges of 1s into all possible range sets (up to
   a small maximum number of bits). */
static void
test_set1 (void)
{
  const int positions = 9;
  unsigned int init_pat;
  int start, width;
  int k;

  for (k = 0; k < 2; k++)
    for (init_pat = 0; init_pat < (1u << positions); init_pat++)
      for (start = 0; start < positions; start++)
        for (width = 0; width + start <= positions; width++)
          {
            unsigned long int offset = k ? ULONG_MAX - positions : 0;
            struct range_tower *rt, *rt2;
            unsigned int final_pat;

            rt = make_pattern (init_pat, offset);
            range_tower_set1 (rt, offset + start, width);
            final_pat = init_pat | bit_range (start, width);
            check_pattern (rt, final_pat, offset);
            rt2 = range_tower_clone (rt, NULL);
            check_pattern (rt2, final_pat, offset);
            range_tower_destroy (rt);
            range_tower_destroy (rt2);
          }
}

/* Tests setting all possible ranges of 0s into all possible range sets (up to
   a small maximum number of bits). */
static void
test_set0 (void)
{
  const int positions = 9;
  unsigned int init_pat;
  int start, width, k;

  for (k = 0; k < 2; k++)
    for (init_pat = 0; init_pat < (1u << positions); init_pat++)
      for (start = 0; start < positions; start++)
        for (width = 0; start + width <= positions; width++)
          {
            unsigned long int offset = k ? ULONG_MAX - positions : 0;
            struct range_tower *rt;
            unsigned int final_pat;

            rt = make_pattern (init_pat, offset);
            range_tower_set0 (rt, offset + start, width);
            final_pat = init_pat & ~bit_range (start, width);
            check_pattern (rt, final_pat, offset);
            range_tower_destroy (rt);
          }
}

/* Tests inserting all possible ranges of 0s into all possible range sets (up
   to a small maximum number of bits). */
static void
test_insert0 (void)
{
  const int positions = 9;
  unsigned int init_pat;
  int start, width, k;

  for (k = 0; k < 2; k++)
    for (init_pat = 0; init_pat < (1u << positions); init_pat++)
      for (start = 0; start < positions; start++)
        for (width = 0; start + width <= positions; width++)
          {
            unsigned long int offset = k ? ULONG_MAX - positions : 0;
            struct range_tower *rt;
            unsigned int final_pat;

            rt = make_pattern (init_pat, offset);
            range_tower_insert0 (rt, offset + start, width);
            final_pat = init_pat & bit_range (0, start);
            final_pat |= (init_pat & bit_range (start, positions - start)) << width;
            check_pattern (rt, final_pat, offset);
            range_tower_destroy (rt);
          }
}

/* Tests inserting all possible ranges of 1s into all possible range sets (up
   to a small maximum number of bits). */
static void
test_insert1 (void)
{
  const int positions = 9;
  unsigned int init_pat;
  int start, width, k;

  for (k = 0; k < 2; k++)
    for (init_pat = 0; init_pat < (1u << positions); init_pat++)
      for (start = 0; start < positions; start++)
        for (width = 0; start + width <= positions; width++)
          {
            struct range_tower *rt;
            unsigned int final_pat;

            rt = make_pattern (init_pat, 0);
            range_tower_insert1 (rt, start, width);
            final_pat = init_pat & bit_range (0, start);
            final_pat |= bit_range (start, width);
            final_pat |= (init_pat & bit_range (start, positions - start)) << width;
            check_pattern (rt, final_pat, 0);
            range_tower_destroy (rt);
          }
}

/* Tests setting all possible ranges from all possible range sets (up to a
   small maximum number of bits). */
static void
test_delete (void)
{
  const int positions = 9;
  unsigned int init_pat;
  int start, width, k;

  for (k = 0; k < 2; k++)
    for (init_pat = 0; init_pat < (1u << positions); init_pat++)
      for (start = 0; start < positions; start++)
        for (width = 0; start + width <= positions; width++)
          {
            unsigned long int offset = k ? ULONG_MAX - positions : 0;
            struct range_tower *rt;
            unsigned int final_pat;

            rt = make_pattern (init_pat, offset);
            range_tower_delete (rt, start + offset, width);
            final_pat = init_pat & bit_range (0, start);
            final_pat |= (init_pat & (UINT_MAX << (start + width))) >> width;
            check_pattern (rt, final_pat, offset);
            range_tower_destroy (rt);
          }
}

/* Tests moving all possible ranges (up to a small maximum number of bits). */
static void
test_move (void)
{
  const int positions = 9;
  unsigned int init_pat;
  int new_start, old_start, width, k;

  for (k = 0; k < 2; k++)
    for (init_pat = 0; init_pat < (1u << positions); init_pat++)
      for (width = 0; width <= positions; width++)
        for (new_start = 0; new_start + width <= positions; new_start++)
          for (old_start = 0; old_start + width <= positions; old_start++)
            {
              unsigned long int offset = k ? ULONG_MAX - positions : 0;
              struct range_tower *rt;
              unsigned int final_pat;

              if (new_start == old_start || width == 0)
                final_pat = init_pat;
              else if (new_start < old_start)
                {
                  final_pat = init_pat & bit_range (0, new_start);
                  final_pat |= (init_pat & bit_range (old_start, width)) >> (old_start - new_start);
                  final_pat |= (init_pat & bit_range (new_start, old_start - new_start)) << width;
                  final_pat |= init_pat & bit_range (old_start + width, positions - (old_start + width));
                }
              else
                {
                  final_pat = init_pat & bit_range (0, old_start);
                  final_pat |= (init_pat & bit_range (old_start + width, new_start - old_start)) >> width;
                  final_pat |= (init_pat & bit_range (old_start, width)) << (new_start - old_start);
                  final_pat |= init_pat & bit_range (new_start + width, positions - (new_start + width));
                }

              rt = make_pattern (init_pat, offset);
              range_tower_move (rt, old_start + offset, new_start + offset,
                                width);
              check_pattern (rt, final_pat, offset);
              range_tower_destroy (rt);
            }
}

/* Tests freeing a range tower through a pool. */
static void
test_pool (void)
{
  struct pool *pool;
  struct range_tower *rt;

  /* Destroy the range tower, then the pool.
     Makes sure that this doesn't cause a double-free. */
  pool = pool_create ();
  rt = range_tower_create_pool (pool);
  range_tower_set1 (rt, 1, 10);
  range_tower_destroy (rt);
  pool_destroy (pool);

  /* Just destroy the pool.
     Makes sure that this doesn't cause a leak. */
  pool = pool_create ();
  rt = range_tower_create_pool (pool);
  range_tower_set1 (rt, 1, 10);
  pool_destroy (pool);
}

/* Tests range_tower_destroy(NULL). */
static void
test_destroy_null (void)
{
  range_tower_destroy (NULL);
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
      "set1",
      "set1",
      test_set1
    },
    {
      "set0",
      "set0",
      test_set0
    },
    {
      "insert0",
      "insert0",
      test_insert0
    },
    {
      "insert1",
      "insert1",
      test_insert1
    },
    {
      "delete",
      "delete",
      test_delete
    },
    {
      "move",
      "move",
      test_move
    },
    {
      "pool",
      "pool",
      test_pool
    },
    {
      "destroy-null",
      "destroy null",
      test_destroy_null
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
      printf ("%s: test range tower library\n"
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
