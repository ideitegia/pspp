/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010 Free Software Foundation, Inc.

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

/* This is a test program for the sparse array routines defined
   in sparse-array.c.  This test program aims to be as
   comprehensive as possible.  "gcov -b" should report 100%
   coverage of lines and branches in sparse-array.c when compiled
   with -DNDEBUG and BITS_PER_LEVEL is greater than the number of
   bits in a long.  "valgrind --leak-check=yes
   --show-reachable=yes" should give a clean report. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libpspp/sparse-array.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Support preliminaries. */
#if __GNUC__ >= 2 && !defined UNUSED
#define UNUSED __attribute__ ((unused))
#else
#define UNUSED
#endif

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

/* Returns a malloc()'d duplicate of the N bytes starting at
   P. */
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

/* Compares A and B and returns a strcmp-type return value. */
static int
compare_unsigned_longs_noaux (const void *a_, const void *b_)
{
  const unsigned long *a = a_;
  const unsigned long *b = b_;

  return *a < *b ? -1 : *a > *b;
}

/* Checks that SPAR contains the CNT ints in DATA, that its
   structure is correct, and that certain operations on SPAR
   produce the expected results. */
static void
check_sparse_array (struct sparse_array *spar,
                    const unsigned long data[], size_t cnt)
{
  unsigned long idx;
  unsigned long *order;
  unsigned long *p;
  size_t i;

  check (sparse_array_count (spar) == cnt);

  for (i = 0; i < cnt; i++)
    {
      p = sparse_array_get (spar, data[i]);
      check (p != NULL);
      check (*p == data[i]);
    }

  order = xmemdup (data, cnt * sizeof *data);
  qsort (order, cnt, sizeof *order, compare_unsigned_longs_noaux);

  for (i = 0; i < cnt; i++)
    {
      p = sparse_array_get (spar, order[i]);
      check (p != NULL);
      check (*p == order[i]);
    }

  if (cnt > 0 && order[0] - 1 != order[cnt - 1])
    {
      check (sparse_array_get (spar, order[0] - 1) == NULL);
      check (!sparse_array_remove (spar, order[0] - 1));
    }
  if (cnt > 0 && order[0] != order[cnt - 1] + 1)
    {
      check (sparse_array_get (spar, order[cnt - 1] + 1) == NULL);
      check (!sparse_array_remove (spar, order[cnt - 1] + 1));
    }

  for (i = 0, p = sparse_array_first (spar, &idx); i < cnt;
       i++, p = sparse_array_next (spar, idx, &idx))
    {
      check (p != NULL);
      check (idx == order[i]);
      check (*p == order[i]);
    }
  check (p == NULL);

  for (i = 0, p = sparse_array_last (spar, &idx); i < cnt;
       i++, p = sparse_array_prev (spar, idx, &idx))
    {
      check (p != NULL);
      check (idx == order[cnt - i - 1]);
      check (*p == order[cnt - i - 1]);
    }
  check (p == NULL);

  free (order);
}

/* Inserts the CNT values from 0 to CNT - 1 (inclusive) into a
   sparse array in the order specified by INSERTIONS, then
   deletes them in the order specified by DELETIONS, checking the
   array's contents for correctness after each operation. */
static void
test_insert_delete (const unsigned long insertions[],
                    const unsigned long deletions[],
                    size_t cnt)
{
  struct sparse_array *spar;
  size_t i;

  spar = sparse_array_create (sizeof *insertions);
  for (i = 0; i < cnt; i++)
    {
      unsigned long *p = sparse_array_insert (spar, insertions[i]);
      *p = insertions[i];
      check_sparse_array (spar, insertions, i + 1);
    }
  for (i = 0; i < cnt; i++)
    {
      bool deleted = sparse_array_remove (spar, deletions[i]);
      check (deleted);
      check_sparse_array (spar, deletions + i + 1, cnt - (i + 1));
    }
  check_sparse_array (spar, NULL, 0);
  sparse_array_destroy (spar);
}

/* Inserts the CNT values from 0 to CNT - 1 (inclusive) into a
   sparse array in the order specified by INSERTIONS, then
   destroys the sparse array, to check that sparse_cases_destroy
   properly frees all the nodes. */
static void
test_destroy (const unsigned long insertions[], size_t cnt)
{
  struct sparse_array *spar;
  size_t i;

  spar = sparse_array_create (sizeof *insertions);
  for (i = 0; i < cnt; i++)
    {
      unsigned long *p = sparse_array_insert (spar, insertions[i]);
      *p = insertions[i];
      check_sparse_array (spar, insertions, i + 1);
    }
  sparse_array_destroy (spar);
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

/* Tests inserting and deleting elements whose values are
   determined by starting from various offsets and skipping
   across various strides, and doing so in various orders. */
static void
test_insert_delete_strides (void)
{
  static const unsigned long strides[] =
    {
      1, 2, 4, 16, 64, 4096, 262144, 16777216,
      3, 5, 17, 67, 4099, 262147, 16777259,
    };
  const size_t stride_cnt = sizeof strides / sizeof *strides;

  static const unsigned long offsets[] =
    {
      0,
      1024ul * 1024 + 1,
      1024ul * 1024 * 512 + 23,
      ULONG_MAX - 59,
    };
  const size_t offset_cnt = sizeof offsets / sizeof *offsets;

  int cnt = 100;
  unsigned long *insertions, *deletions;
  const unsigned long *stride, *offset;

  insertions = xnmalloc (cnt, sizeof *insertions);
  deletions = xnmalloc (cnt, sizeof *deletions);
  for (stride = strides; stride < strides + stride_cnt; stride++)
    {
      printf ("%lu\n", *stride);
      for (offset = offsets; offset < offsets + offset_cnt; offset++)
        {
          int k;

          for (k = 0; k < cnt; k++)
            insertions[k] = *stride * k + *offset;

          test_insert_delete (insertions, insertions, cnt);
          test_destroy (insertions, cnt);

          for (k = 0; k < cnt; k++)
            deletions[k] = insertions[cnt - k - 1];
          test_insert_delete (insertions, deletions, cnt);

          random_shuffle (insertions, cnt, sizeof *insertions);
          test_insert_delete (insertions, insertions, cnt);
          test_insert_delete (insertions, deletions, cnt);
        }
    }
  free (insertions);
  free (deletions);
}

/* Returns the index in ARRAY of the (CNT+1)th element that has
   the TARGET value. */
static int
scan_bools (bool target, bool array[], size_t cnt)
{
  size_t i;

  for (i = 0; ; i++)
    if (array[i] == target && cnt-- == 0)
      return i;
}

/* Performs a random sequence of insertions and deletions in a
   sparse array. */
static void
test_random_insert_delete (void)
{
  unsigned long int values[] =
    {
      0, 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
      8192, 16384, 32768, 65536, 131072, 262144, 4194304, 8388608,
      16777216, 33554432, 67108864, 134217728, 268435456, 536870912,
      1073741824, 2147483648,

      3, 7, 15, 31, 63, 127, 257, 511, 1023, 2047, 4095,
      8191, 16383, 32767, 65535, 131071, 262143, 4194303, 8388607,
      16777215, 33554431, 67108863, 134217727, 268435455, 536870911,
      1073741823, 2147483647, 4294967295,
    };
  const int max_values = sizeof values / sizeof *values;

  const int num_actions = 250000;
  struct sparse_array *spar;
  bool *has_values;
  int cnt;
  int insert_chance;
  int i;

  has_values = xnmalloc (max_values, sizeof *has_values);
  memset (has_values, 0, max_values * sizeof *has_values);

  cnt = 0;
  insert_chance = 5;

  spar = sparse_array_create (sizeof *values);
  for (i = 0; i < num_actions; i++)
    {
      enum { INSERT, DELETE } action;
      unsigned long *p;
      int j;

      if (cnt == 0)
        {
          action = INSERT;
          if (insert_chance < 9)
            insert_chance++;
        }
      else if (cnt == max_values)
        {
          action = DELETE;
          if (insert_chance > 0)
            insert_chance--;
        }
      else
        action = rand () % 10 < insert_chance ? INSERT : DELETE;

      if (action == INSERT)
        {
          int ins_index;

          ins_index = scan_bools (false, has_values,
                                  rand () % (max_values - cnt));
          assert (has_values[ins_index] == false);
          has_values[ins_index] = true;

          p = sparse_array_insert (spar, values[ins_index]);
          check (p != NULL);
          *p = values[ins_index];

          cnt++;
        }
      else if (action == DELETE)
        {
          int del_index;

          del_index = scan_bools (true, has_values, rand () % cnt);
          assert (has_values[del_index] == true);
          has_values[del_index] = false;

          check (sparse_array_remove (spar, values[del_index]));
          cnt--;
        }
      else
        abort ();

      check (sparse_array_count (spar) == cnt);
      for (j = 0; j < max_values; j++)
        {
          p = sparse_array_get (spar, values[j]);
          if (has_values[j])
            {
              check (p != NULL);
              check (*p == values[j]);
            }
          else
            {
              check (p == NULL);
              if (rand () % 10 == 0)
                sparse_array_remove (spar, values[j]);
            }
        }
    }
  sparse_array_destroy (spar);
  free (has_values);
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
      "random-insert-delete",
      "random insertions and deletions",
      test_random_insert_delete
    },
    {
      "insert-delete-strides",
      "insert in ascending order with strides and offset",
      test_insert_delete_strides
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
      printf ("%s: test sparse array library\n"
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
