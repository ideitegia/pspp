/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2010 Free Software Foundation, Inc.

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
   range-map.c.  This test program aims to be as comprehensive as
   possible.  With -DNDEBUG, "gcov -b" should report 100%
   coverage of lines and branches in range-map.c routines.
   (Without -DNDEBUG, branches caused by failed assertions will
   not be taken.)  "valgrind --leak-check=yes
   --show-reachable=yes" should give a clean report, both with
   and without -DNDEBUG. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libpspp/range-map.h>

#include <assert.h>
#include <limits.h>
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

/* Test data element. */
struct element
  {
    struct range_map_node node; /* Embedded tower block. */
    int x;                      /* Primary value. */
  };

static struct element *
range_map_node_to_element (struct range_map_node *node)
{
  return range_map_data (node, struct element, node);
}

/* Element we expect to find. */
struct expected_element
  {
    int x;                      /* Primary value. */
    unsigned long int start;    /* Start of region. */
    unsigned long int end;      /* End of region. */
  };

/* Compares expected_element A and B and returns a strcmp()-type
   result. */
static int
compare_expected_element (const void *a_, const void *b_)
{
  const struct expected_element *a = (const struct expected_element *) a_;
  const struct expected_element *b = (const struct expected_element *) b_;
  return a->start < b->start ? -1 : a->start > b->start;
}

/* Checks that RM contains the ELEM_CNT elements described by
   ELEMENTS[]. */
static void
check_range_map (struct range_map *rm,
                 struct expected_element elements[], size_t elem_cnt)
{
  struct expected_element *sorted;
  struct range_map_node *node;
  size_t i;

  sorted = xnmalloc (elem_cnt, sizeof *sorted);
  memcpy (sorted, elements, elem_cnt * sizeof *elements);
  qsort (sorted, elem_cnt, sizeof *sorted, compare_expected_element);

  check (range_map_is_empty (rm) == (elem_cnt == 0));

  for (i = 0; i < elem_cnt; i++)
    {
      struct expected_element *e = &sorted[i];
      unsigned long int position;

      /* Check that range_map_lookup finds all the positions
         within the element. */
      for (position = e->start; position < e->end; position++)
        {
          struct range_map_node *found = range_map_lookup (rm, position);
          check (found != NULL);
          check (range_map_node_to_element (found)->x == e->x);
          check (range_map_node_get_start (found) == e->start);
          check (range_map_node_get_end (found) == e->end);
          check (range_map_node_get_width (found) == e->end - e->start);
        }

      /* If there shouldn't be any elements in the positions just
         before or after the element, verify that
         range_map_lookup doesn't find any there. */
      if (e->start > 0 && (i == 0 || e[-1].end < e->start))
        check (range_map_lookup (rm, e->start - 1) == NULL);
      if (i == elem_cnt - 1 || e->end < e[1].start)
        check (range_map_lookup (rm, e->end) == NULL);
    }

  for (node = (rand () % 2 ? range_map_first (rm) : range_map_next (rm, NULL)),
         i = 0;
       node != NULL;
       node = range_map_next (rm, node), i++)
    {
      struct expected_element *e = &sorted[i];
      check (range_map_node_to_element (node)->x == e->x);
    }
  check (i == elem_cnt);

  free (sorted);
}

/* Tests inserting all possible sets of ranges into a range map
   in all possible orders, up to a specified maximum overall
   range. */
static void
test_insert (void)
{
  const int max_range = 7;
  int cnt;

  for (cnt = 1; cnt <= max_range; cnt++)
    {
      unsigned int composition_cnt;
      struct expected_element *expected;
      int *widths;
      int elem_cnt;
      int *order;
      struct element *elements;

      expected = xnmalloc (cnt, sizeof *expected);
      widths = xnmalloc (cnt, sizeof *widths);
      order = xnmalloc (cnt, sizeof *order);
      elements = xnmalloc (cnt, sizeof *elements);

      elem_cnt = 0;
      composition_cnt = 0;
      while (next_composition (cnt, &elem_cnt, widths))
        {
          int i, j;
          unsigned int permutation_cnt;

          for (i = 0; i < elem_cnt; i++)
            order[i] = i;

          permutation_cnt = 0;
          while (permutation_cnt == 0 || next_permutation (order, elem_cnt))
            {
              struct range_map rm;

              /* Inserts the elem_cnt elements with the given
                 widths[] into T in the order given by order[]. */
              range_map_init (&rm);
              for (i = 0; i < elem_cnt; i++)
                {
                  unsigned long int start, end;
                  int idx;

                  idx = order[i];
                  elements[idx].x = idx;

                  /* Find start and end of element. */
                  start = 0;
                  for (j = 0; j < idx; j++)
                    start += widths[j];
                  end = start + widths[j];

                  /* Insert. */
                  range_map_insert (&rm, start, end - start,
                                    &elements[idx].node);

                  /* Check map contents. */
                  expected[i].x = idx;
                  expected[i].start = start;
                  expected[i].end = end;
                  check_range_map (&rm, expected, i + 1);
                }
              permutation_cnt++;
            }
          check (permutation_cnt == factorial (elem_cnt));

          composition_cnt++;
        }
      check (composition_cnt == 1 << (cnt - 1));

      free (expected);
      free (widths);
      free (order);
      free (elements);
    }
}

/* Tests deleting ranges from a range map in all possible orders,
   up to a specified maximum overall range. */
static void
test_delete (int gap)
{
  const int max_range = 7;
  int cnt;

  for (cnt = 1; cnt <= max_range; cnt++)
    {
      unsigned int composition_cnt;
      struct expected_element *expected;
      int *widths;
      int elem_cnt;
      int *order;
      struct element *elements;

      expected = xnmalloc (cnt, sizeof *expected);
      widths = xnmalloc (cnt, sizeof *widths);
      order = xnmalloc (cnt, sizeof *order);
      elements = xnmalloc (cnt, sizeof *elements);

      elem_cnt = 0;
      composition_cnt = 0;
      while (next_composition (cnt, &elem_cnt, widths))
        {
          int i, j;
          unsigned int permutation_cnt;

          for (i = 0; i < elem_cnt; i++)
            order[i] = i;

          permutation_cnt = 0;
          while (permutation_cnt == 0 || next_permutation (order, elem_cnt))
            {
              struct range_map rm;
              unsigned long int start;

              /* Insert all the elements. */
              range_map_init (&rm);
              start = 0;
              for (i = 0; i < elem_cnt; i++)
                {
                  int width = widths[i] > gap ? widths[i] - gap : widths[i];
                  unsigned long int end = start + width;

                  elements[i].x = i;
                  range_map_insert (&rm, start, end - start,
                                    &elements[i].node);

                  for (j = 0; ; j++)
                    {
                      assert (j < elem_cnt);
                      if (order[j] == i)
                        {
                          expected[j].x = i;
                          expected[j].start = start;
                          expected[j].end = end;
                          break;
                        }
                    }

                  start += widths[i];
                }
              check_range_map (&rm, expected, elem_cnt);

              /* Delete the elements in the specified order. */
              for (i = 0; i < elem_cnt; i++)
                {
                  range_map_delete (&rm, &elements[order[i]].node);
                  check_range_map (&rm, expected + i + 1, elem_cnt - i - 1);
                }

              permutation_cnt++;
            }
          check (permutation_cnt == factorial (elem_cnt));

          composition_cnt++;
        }
      check (composition_cnt == 1 << (cnt - 1));

      free (expected);
      free (widths);
      free (order);
      free (elements);
    }
}

/* Tests deleting ranges from a range map filled with contiguous
   ranges in all possible orders, up to a specified maximum
   overall range. */
static void
test_delete_contiguous (void)
{
  test_delete (0);
}

/* Tests deleting ranges from a range map filled with ranges
   sometimes separated by gaps in all possible orders, up to a
   specified maximum overall range. */
static void
test_delete_gaps (void)
{
  test_delete (1);
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
      "delete-contiguous",
      "delete from contiguous ranges",
      test_delete_contiguous
    },
    {
      "delete-gaps",
      "delete from ranges separated by gaps",
      test_delete_gaps
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
      printf ("%s: test range map library\n"
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
