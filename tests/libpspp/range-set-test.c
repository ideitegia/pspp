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
   range-set.c.  This test program aims to be as comprehensive as
   possible.  With -DNDEBUG, "gcov -b" should report 100%
   coverage of lines and branches in range-set.c routines.
   (Without -DNDEBUG, branches caused by failed assertions will
   not be taken.)  "valgrind --leak-check=yes
   --show-reachable=yes" should give a clean report, both with
   and without -DNDEBUG. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libpspp/range-set.h>

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpspp/compiler.h>
#include <libpspp/pool.h>

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
next_1bit (unsigned int pattern, unsigned int offset)
{
  for (; offset < UINT_BIT; offset++)
    if (pattern & (1u << offset))
      return offset;
  return ULONG_MAX;
}

/* Prints the regions in RS to stdout. */
static void UNUSED
print_regions (const struct range_set *rs)
{
  const struct range_set_node *node;

  printf ("result:");
  RANGE_SET_FOR_EACH (node, rs)
    printf (" (%lu,%lu)",
            range_set_node_get_start (node), range_set_node_get_end (node));
  printf ("\n");
}

/* Checks that the regions in RS match the bits in PATTERN. */
static void
check_pattern (const struct range_set *rs, unsigned int pattern)
{
  const struct range_set_node *node;
  unsigned long int start, width;
  unsigned long int s1, s2;
  int i;

  for (node = rand () % 2 ? range_set_first (rs) : range_set_next (rs, NULL),
         start = width = 0;
       next_region (pattern, start + width, &start, &width);
       node = range_set_next (rs, node))
    {
      check (node != NULL);
      check (range_set_node_get_start (node) == start);
      check (range_set_node_get_end (node) == start + width);
      check (range_set_node_get_width (node) == width);
    }
  check (node == NULL);

  for (node = rand () % 2 ? range_set_last (rs) : range_set_prev (rs, NULL),
         start = UINT_BIT;
       prev_region (pattern, start, &start, &width);
       node = range_set_prev (rs, node))
    {
      check (node != NULL);
      check (range_set_node_get_start (node) == start);
      check (range_set_node_get_end (node) == start + width);
      check (range_set_node_get_width (node) == width);
    }
  check (node == NULL);

  /* Scan from all possible positions, resetting the cache each
     time, to ensure that we get the correct answers without
     caching. */
  for (start = 0; start <= 32; start++)
    {
      struct range_set *nonconst_rs = CONST_CAST (struct range_set *, rs);
      nonconst_rs->cache_end = 0;
      s1 = range_set_scan (rs, start);
      s2 = next_1bit (pattern, start);
      check (s1 == s2);
    }

  /* Scan in forward order to exercise expected cache behavior. */
  for (s1 = range_set_scan (rs, 0), s2 = next_1bit (pattern, 0); ;
       s1 = range_set_scan (rs, s1 + 1), s2 = next_1bit (pattern, s2 + 1))
    {
      check (s1 == s2);
      if (s1 == ULONG_MAX)
        break;
    }

  /* Scan in random order to frustrate cache. */
  for (i = 0; i < 32; i++)
    {
      start = rand () % 32;
      s1 = range_set_scan (rs, start);
      s2 = next_1bit (pattern, start);
      check (s1 == s2);
    }

  /* Test range_set_scan() with negative cache. */
  check (!range_set_contains (rs, 999));
  check (range_set_scan (rs, 1111) == ULONG_MAX);

  for (i = 0; i < UINT_BIT; i++)
    check (range_set_contains (rs, i) == ((pattern & (1u << i)) != 0));
  check (!range_set_contains (rs,
                              UINT_BIT + rand () % (ULONG_MAX - UINT_BIT)));

  check (range_set_is_empty (rs) == (pattern == 0));
}

/* Creates and returns a range set that contains regions for the
   bits set in PATTERN. */
static struct range_set *
make_pattern (unsigned int pattern)
{
  unsigned long int start = 0;
  unsigned long int width = 0;
  struct range_set *rs = range_set_create_pool (NULL);
  while (next_region (pattern, start + width, &start, &width))
    range_set_set1 (rs, start, width);
  check_pattern (rs, pattern);
  return rs;
}

/* Returns an unsigned int with bits OFS...OFS+CNT (exclusive)
   set to 1, other bits set to 0. */
static unsigned int
bit_range (unsigned int ofs, unsigned int cnt)
{
  assert (ofs < UINT_BIT);
  assert (cnt <= UINT_BIT);
  assert (ofs + cnt <= UINT_BIT);

  return cnt < UINT_BIT ? ((1u << cnt) - 1) << ofs : UINT_MAX;
}

/* Tests inserting all possible patterns into all possible range
   sets (up to a small maximum number of bits). */
static void
test_insert (void)
{
  const int positions = 9;
  unsigned int init_pat;
  int i, j;

#if __GNUC__ == 4 && __GNUC_MINOR__ == 2 && __llvm__
  /* This test seems to trigger a bug in llvm-gcc 4.2 on Mac OS X 10.8.0.
     Exit code 77 tells the Autotest framework that the test was skipped. */
  exit (77);
#endif

  for (init_pat = 0; init_pat < (1u << positions); init_pat++)
    for (i = 0; i < positions + 1; i++)
      for (j = i; j <= positions + 1; j++)
        {
          struct range_set *rs, *rs2;
          unsigned int final_pat;

          rs = make_pattern (init_pat);
          range_set_set1 (rs, i, j - i);
          final_pat = init_pat | bit_range (i, j - i);
          check_pattern (rs, final_pat);
          rs2 = range_set_clone (rs, NULL);
          check_pattern (rs2, final_pat);
          range_set_destroy (rs);
          range_set_destroy (rs2);
        }
}

/* Tests deleting all possible patterns from all possible range
   sets (up to a small maximum number of bits). */
static void
test_delete (void)
{
  const int positions = 9;
  unsigned int init_pat;
  int i, j;

#if __GNUC__ == 4 && __GNUC_MINOR__ == 2 && __llvm__
  /* This test seems to trigger a bug in llvm-gcc 4.2 on Mac OS X 10.8.0.
     Exit code 77 tells the Autotest framework that the test was skipped. */
  exit (77);
#endif

  for (init_pat = 0; init_pat < (1u << positions); init_pat++)
    for (i = 0; i < positions + 1; i++)
      for (j = i; j <= positions + 1; j++)
        {
          struct range_set *rs;
          unsigned int final_pat;

          rs = make_pattern (init_pat);
          range_set_set0 (rs, i, j - i);
          final_pat = init_pat & ~bit_range (i, j - i);
          check_pattern (rs, final_pat);
          range_set_destroy (rs);
        }
}

/* Tests all possible allocation in all possible range sets (up
   to a small maximum number of bits). */
static void
test_allocate (void)
{
  const int positions = 9;
  unsigned int init_pat;
  int request;

#if __GNUC__ == 4 && __GNUC_MINOR__ == 2 && __llvm__
  /* This test seems to trigger a bug in llvm-gcc 4.2 on Mac OS X 10.8.0.
     Exit code 77 tells the Autotest framework that the test was skipped. */
  exit (77);
#endif

  for (init_pat = 0; init_pat < (1u << positions); init_pat++)
    for (request = 1; request <= positions + 1; request++)
      {
        struct range_set *rs;
        unsigned long int start, width, expect_start, expect_width;
        bool success, expect_success;
        unsigned int final_pat;
        int i;

        /* Figure out expected results. */
        expect_success = false;
        expect_start = expect_width = 0;
        final_pat = init_pat;
        for (i = 0; i < positions; i++)
          if (init_pat & (1u << i))
            {
              expect_success = true;
              expect_start = i;
              expect_width = count_one_bits (init_pat >> i);
              if (expect_width > request)
                expect_width = request;
              final_pat &= ~bit_range (expect_start, expect_width);
              break;
            }

        /* Test. */
        rs = make_pattern (init_pat);
        success = range_set_allocate (rs, request, &start, &width);
        check_pattern (rs, final_pat);
        range_set_destroy (rs);

        /* Check results. */
        check (success == expect_success);
        if (expect_success)
          {
            check (start == expect_start);
            check (width == expect_width);
          }
      }
}

/* Tests all possible full allocations in all possible range sets
   (up to a small maximum number of bits). */
static void
test_allocate_fully (void)
{
  const int positions = 9;
  unsigned int init_pat;
  int request;

#if __GNUC__ == 4 && __GNUC_MINOR__ == 2 && __llvm__
  /* This test seems to trigger a bug in llvm-gcc 4.2 on Mac OS X 10.8.0.
     Exit code 77 tells the Autotest framework that the test was skipped. */
  exit (77);
#endif

  for (init_pat = 0; init_pat < (1u << positions); init_pat++)
    for (request = 1; request <= positions + 1; request++)
      {
        struct range_set *rs;
        unsigned long int start, expect_start;
        bool success, expect_success;
        unsigned int final_pat;
        int i;

        /* Figure out expected results. */
        expect_success = false;
        expect_start = 0;
        final_pat = init_pat;
        for (i = 0; i < positions - request + 1; i++)
          {
            int j;

            final_pat = init_pat;
            for (j = i; j < i + request; j++)
              {
                if (!(init_pat & (1u << j)))
                  goto next;
                final_pat &= ~(1u << j);
              }

            expect_success = true;
            expect_start = i;
            break;
          next:
            final_pat = init_pat;
          }

        /* Test. */
        rs = make_pattern (init_pat);
        success = range_set_allocate_fully (rs, request, &start);
        check_pattern (rs, final_pat);
        range_set_destroy (rs);

        /* Check results. */
        check (success == expect_success);
        if (expect_success)
          check (start == expect_start);
      }
}

/* Tests freeing a range set through a pool. */
static void
test_pool (void)
{
  struct pool *pool;
  struct range_set *rs;

  /* Destroy the range set, then the pool.
     Makes sure that this doesn't cause a double-free. */
  pool = pool_create ();
  rs = range_set_create_pool (pool);
  range_set_set1 (rs, 1, 10);
  range_set_destroy (rs);
  pool_destroy (pool);

  /* Just destroy the pool.
     Makes sure that this doesn't cause a leak. */
  pool = pool_create ();
  rs = range_set_create_pool (pool);
  range_set_set1 (rs, 1, 10);
  pool_destroy (pool);
}

/* Tests range_set_destroy(NULL). */
static void
test_destroy_null (void)
{
  range_set_destroy (NULL);
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
      "allocate",
      "allocate",
      test_allocate
    },
    {
      "allocate-fully",
      "allocate_fully",
      test_allocate_fully
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
      printf ("%s: test range set library\n"
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
