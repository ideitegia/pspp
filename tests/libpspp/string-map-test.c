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

/* This is a test program for the string_map_* routines defined in
   string-map.c.  This test program aims to be as comprehensive as possible.
   "gcov -a -b" should report almost complete coverage of lines, blocks and
   branches in string-map.c, except that one branch caused by hash collision is
   not exercised because our hash function has so few collisions.  "valgrind
   --leak-check=yes --show-reachable=yes" should give a clean report. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libpspp/string-map.h>

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpspp/hash-functions.h>
#include <libpspp/compiler.h>
#include <libpspp/string-set.h>

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

/* Clone STRING.  */
static char *
xstrdup (const char *string)
{
  return xmemdup (string, strlen (string) + 1);
}

/* Allocates and returns N * M bytes of memory. */
static void *
xnmalloc (size_t n, size_t m)
{
  if ((size_t) -1 / m <= n)
    xalloc_die ();
  return xmalloc (n * m);
}

/* Support routines. */

enum {
  IDX_BITS = 10,
  MAX_IDX = 1 << IDX_BITS,
  KEY_MASK = (MAX_IDX - 1),
  KEY_SHIFT = 0,
  VALUE_MASK = (MAX_IDX - 1) << IDX_BITS,
  VALUE_SHIFT = IDX_BITS
};

static char *string_table[MAX_IDX];

static const char *
get_string (int idx)
{
  char **s;

  assert (idx >= 0 && idx < MAX_IDX);
  s = &string_table[idx];
  if (*s == NULL)
    {
      *s = xmalloc (16);
      sprintf (*s, "%d", idx);
    }
  return *s;
}

static void
free_strings (void)
{
  int i;

  for (i = 0; i < MAX_IDX; i++)
    free (string_table[i]);
}

static const char *
make_key (int value)
{
  return get_string ((value & KEY_MASK) >> KEY_SHIFT);
}

static const char *
make_value (int value)
{
  return get_string ((value & VALUE_MASK) >> VALUE_SHIFT);
}

static int
random_value (unsigned int seed, int basis)
{
  return hash_int (seed, basis) & VALUE_MASK;
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

/* Arranges the CNT elements in VALUES into the lexicographically next greater
   permutation.  Returns true if successful.  If VALUES is already the
   lexicographically greatest permutation of its elements (i.e. ordered from
   greatest to smallest), arranges them into the lexicographically least
   permutation (i.e. ordered from smallest to largest) and returns false.

   Comparisons among elements of VALUES consider only the bits in KEY_MASK. */
static bool
next_permutation (int *values, size_t cnt)
{
  if (cnt > 0)
    {
      size_t i = cnt - 1;
      while (i != 0)
        {
          i--;
          if ((values[i] & KEY_MASK) < (values[i + 1] & KEY_MASK))
            {
              size_t j;
              for (j = cnt - 1;
                   (values[i] & KEY_MASK) >= (values[j] & KEY_MASK);
                   j--)
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

/* Checks that MAP contains the CNT strings in DATA, that its structure is
   correct, and that certain operations on MAP produce the expected results. */
static void
check_string_map (struct string_map *map, const int data[], size_t cnt)
{
  size_t i;

  check (string_map_is_empty (map) == (cnt == 0));
  check (string_map_count (map) == cnt);

  for (i = 0; i < cnt; i++)
    {
      struct string_map_node *node;
      const char *key = make_key (data[i]);
      const char *value = make_value (data[i]);
      const char *found_value;

      check (string_map_contains (map, key));

      node = string_map_find_node (map, key);
      check (node != NULL);
      check (!strcmp (key, string_map_node_get_key (node)));
      check (!strcmp (value, string_map_node_get_value (node)));

      check (node == string_map_insert (map, key, "abc"));
      check (!strcmp (value, string_map_node_get_value (node)));

      check (node == string_map_insert_nocopy (map, xstrdup (key),
                                               xstrdup ("def")));
      check (!strcmp (value, string_map_node_get_value (node)));

      found_value = string_map_find (map, key);
      check (found_value != NULL);
      check (!strcmp (found_value, value));
    }

  check (!string_map_contains (map, "xxx"));
  check (string_map_find (map, "z") == NULL);
  check (string_map_find_node (map, "") == NULL);
  check (!string_map_delete (map, "xyz"));

  if (cnt == 0)
    check (string_map_first (map) == NULL);
  else
    {
      const struct string_map_node *node;
      int *data_copy;
      int left;

      data_copy = xmemdup (data, cnt * sizeof *data);
      left = cnt;
      for (node = string_map_first (map), i = 0; i < cnt;
           node = string_map_next (map, node), i++)
        {
          const char *key = string_map_node_get_key (node);
          const char *value = string_map_node_get_value (node);
          size_t j;

          for (j = 0; j < left; j++)
            if (!strcmp (key, make_key (data_copy[j]))
                || !strcmp (value, make_value (data_copy[j])))
              {
                data_copy[j] = data_copy[--left];
                goto next;
              }
          check_die ();

        next: ;
        }
      check (node == NULL);
      free (data_copy);
    }
}

/* Inserts the CNT strings from 0 to CNT - 1 (inclusive) into a map in the
   order specified by INSERTIONS, then deletes them in the order specified by
   DELETIONS, checking the map's contents for correctness after each
   operation.  */
static void
test_insert_delete (const int insertions[],
                    const int deletions[],
                    size_t cnt)
{
  struct string_map map;
  size_t i;

  string_map_init (&map);
  check_string_map (&map, NULL, 0);
  for (i = 0; i < cnt; i++)
    {
      check (string_map_insert (&map, make_key (insertions[i]),
                                make_value (insertions[i])));
      check_string_map (&map, insertions, i + 1);
    }
  for (i = 0; i < cnt; i++)
    {
      check (string_map_delete (&map, make_key (deletions[i])));
      check_string_map (&map, deletions + i + 1, cnt - i - 1);
    }
  string_map_destroy (&map);
}

/* Inserts strings into a map in each possible order, then removes them in each
   possible order, up to a specified maximum size. */
static void
test_insert_any_remove_any (void)
{
  const int basis = 0;
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
        insertions[i] = i | random_value (i, basis);

      for (ins_perm_cnt = 0;
           ins_perm_cnt == 0 || next_permutation (insertions, cnt);
           ins_perm_cnt++)
        {
          unsigned int del_perm_cnt;
          int i;

          for (i = 0; i < cnt; i++)
            deletions[i] = i | random_value (i, basis);

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

/* Inserts strings into a map in each possible order, then removes them in the
   same order, up to a specified maximum size. */
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
        values[i] = i | random_value (i, 1);

      for (permutation_cnt = 0;
           permutation_cnt == 0 || next_permutation (values, cnt);
           permutation_cnt++)
        test_insert_delete (values, values, cnt);
      check (permutation_cnt == factorial (cnt));

      free (values);
    }
}

/* Inserts strings into a map in each possible order, then
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
        insertions[i] = i | random_value (i, 2);

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

/* Inserts and removes strings in a map, in random order. */
static void
test_random_sequence (void)
{
  const int basis = 3;
  const int max_elems = 64;
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
        insertions[i] = i | random_value (i, basis);
      for (i = 0; i < cnt; i++)
        deletions[i] = i | random_value (i, basis);

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

/* Inserts strings into a map in ascending order, then delete in ascending
   order. */
static void
test_insert_ordered (void)
{
  const int max_elems = 64;
  int *values;
  struct string_map map;
  int i;

  string_map_init (&map);
  values = xnmalloc (max_elems, sizeof *values);
  for (i = 0; i < max_elems; i++)
    {
      values[i] = i | random_value (i, 4);
      string_map_insert_nocopy (&map, xstrdup (make_key (values[i])),
                                xstrdup (make_value (values[i])));
      check_string_map (&map, values, i + 1);
    }
  for (i = 0; i < max_elems; i++)
    {
      string_map_delete (&map, make_key (i));
      check_string_map (&map, values + i + 1, max_elems - i - 1);
    }
  string_map_destroy (&map);
  free (values);
}

/* Inserts and replaces strings in a map, in random order. */
static void
test_replace (void)
{
  const int basis = 15;
  enum { MAX_ELEMS = 16 };
  const int max_trials = 8;
  int cnt;

  for (cnt = 0; cnt <= MAX_ELEMS; cnt++)
    {
      int insertions[MAX_ELEMS];
      int trial;
      int i;

      for (i = 0; i < cnt; i++)
        insertions[i] = (i / 2) | random_value (i, basis);

      for (trial = 0; trial < max_trials; trial++)
        {
          struct string_map map;
          int data[MAX_ELEMS];
          int n_data;

          /* Insert with replacement in random order. */
          n_data = 0;
          string_map_init (&map);
          random_shuffle (insertions, cnt, sizeof *insertions);
          for (i = 0; i < cnt; i++)
            {
              const char *key = make_key (insertions[i]);
              const char *value = make_value (insertions[i]);
              int j;

              for (j = 0; j < n_data; j++)
                if ((data[j] & KEY_MASK) == (insertions[i] & KEY_MASK))
                  {
                    data[j] = insertions[i];
                    goto found;
                  }
              data[n_data++] = insertions[i];
            found:

              if (i % 2)
                string_map_replace (&map, key, value);
              else
                string_map_replace_nocopy (&map,
                                           xstrdup (key), xstrdup (value));
              check_string_map (&map, data, n_data);
            }

          /* Delete in original order. */
          for (i = 0; i < cnt; i++)
            {
              const char *expected_value;
              char *value;
              int j;

              expected_value = NULL;
              for (j = 0; j < n_data; j++)
                if ((data[j] & KEY_MASK) == (insertions[i] & KEY_MASK))
                  {
                    expected_value = make_value (data[j]);
                    data[j] = data[--n_data];
                    break;
                  }

              value = string_map_find_and_delete (&map,
                                                  make_key (insertions[i]));
              check ((value != NULL) == (expected_value != NULL));
              check (value == NULL || !strcmp (value, expected_value));
              free (value);
            }
          assert (string_map_is_empty (&map));

          string_map_destroy (&map);
        }
    }
}

static void
make_patterned_map (struct string_map *map, unsigned int pattern, int basis,
                    int insertions[], int *np)
{
  int n;
  int i;

  string_map_init (map);

  n = 0;
  for (i = 0; pattern != 0; i++)
    if (pattern & (1u << i))
      {
        pattern &= pattern - 1;
        insertions[n] = i | random_value (i, basis);
        check (string_map_insert (map, make_key (insertions[n]),
                                  make_value (insertions[n])));
        n++;
      }
  check_string_map (map, insertions, n);

  *np = n;
}

static void
for_each_map (void (*cb)(struct string_map *, int data[], int n),
              int basis)
{
  enum { MAX_ELEMS = 5 };
  unsigned int pattern;

  for (pattern = 0; pattern < (1u << MAX_ELEMS); pattern++)
    {
      int data[MAX_ELEMS];
      struct string_map map;
      int n;

      make_patterned_map (&map, pattern, basis, data, &n);
      (*cb) (&map, data, n);
      string_map_destroy (&map);
    }
}

static void
for_each_pair_of_maps (
  void (*cb)(struct string_map *a, int a_data[], int n_a,
             struct string_map *b, int b_data[], int n_b),
  int a_basis, int b_basis)
{
  enum { MAX_ELEMS = 5 };
  unsigned int a_pattern, b_pattern;

  for (a_pattern = 0; a_pattern < (1u << MAX_ELEMS); a_pattern++)
    for (b_pattern = 0; b_pattern < (1u << MAX_ELEMS); b_pattern++)
      {
        int a_data[MAX_ELEMS], b_data[MAX_ELEMS];
        struct string_map a_map, b_map;
        int n_a, n_b;

        make_patterned_map (&a_map, a_pattern, a_basis, a_data, &n_a);
        make_patterned_map (&b_map, b_pattern, b_basis, b_data, &n_b);
        (*cb) (&a_map, a_data, n_a, &b_map, b_data, n_b);
        string_map_destroy (&a_map);
        string_map_destroy (&b_map);
      }
}

static void
clear_cb (struct string_map *map, int data[] UNUSED, int n UNUSED)
{
  string_map_clear (map);
  check_string_map (map, NULL, 0);
}

static void
test_clear (void)
{
  for_each_map (clear_cb, 5);
}

static void
clone_cb (struct string_map *map, int data[], int n)
{
  struct string_map clone;

  string_map_clone (&clone, map);
  check_string_map (&clone, data, n);
  string_map_destroy (&clone);
}

static void
test_clone (void)
{
  for_each_map (clone_cb, 6);
}

static void
node_swap_value_cb (struct string_map *map, int data[], int n)
{
  int i;

  for (i = 0; i < n; i++)
    {
      const char *value = make_value (data[i]);
      struct string_map_node *node;
      char *old_value;

      node = string_map_find_node (map, make_key (data[i]));
      check (node != NULL);
      check (!strcmp (string_map_node_get_value (node), value));
      data[i] = (data[i] & KEY_MASK) | random_value (i, 15);
      old_value = string_map_node_swap_value (node, make_value (data[i]));
      check (old_value != NULL);
      check (!strcmp (value, old_value));
      free (old_value);
    }
}

static void
test_node_swap_value (void)
{
  for_each_map (node_swap_value_cb, 14);
}

static void
swap_cb (struct string_map *a, int a_data[], int n_a,
         struct string_map *b, int b_data[], int n_b)
{
  string_map_swap (a, b);
  check_string_map (a, b_data, n_b);
  check_string_map (b, a_data, n_a);
}

static void
test_swap (void)
{
  for_each_pair_of_maps (swap_cb, 7, 8);
}

static void
insert_map_cb (struct string_map *a, int a_data[], int n_a,
               struct string_map *b, int b_data[], int n_b)
{
  int i, j;

  string_map_insert_map (a, b);

  for (i = 0; i < n_b; i++)
    {
      for (j = 0; j < n_a; j++)
        if ((b_data[i] & KEY_MASK) == (a_data[j] & KEY_MASK))
          goto found;
      a_data[n_a++] = b_data[i];
    found:;
    }
  check_string_map (a, a_data, n_a);
  check_string_map (b, b_data, n_b);
}

static void
test_insert_map (void)
{
  for_each_pair_of_maps (insert_map_cb, 91, 10);
}

static void
replace_map_cb (struct string_map *a, int a_data[], int n_a,
               struct string_map *b, int b_data[], int n_b)
{
  int i, j;

  string_map_replace_map (a, b);

  for (i = 0; i < n_b; i++)
    {
      for (j = 0; j < n_a; j++)
        if ((b_data[i] & KEY_MASK) == (a_data[j] & KEY_MASK))
          {
            a_data[j] = (a_data[j] & KEY_MASK) | (b_data[i] & VALUE_MASK);
            goto found;
          }
      a_data[n_a++] = b_data[i];
    found:;
    }
  check_string_map (a, a_data, n_a);
  check_string_map (b, b_data, n_b);
}

static void
test_replace_map (void)
{
  for_each_pair_of_maps (replace_map_cb, 11, 12);
}

static void
check_set (struct string_set *set, const int *data, int n_data,
           int mask, int shift)
{
  int *unique;
  int n_unique;
  int i;

  n_unique = 0;
  unique = xmalloc (n_data * sizeof *unique);
  for (i = 0; i < n_data; i++)
    {
      int idx = (data[i] & mask) >> shift;
      int j;

      for (j = 0; j < n_unique; j++)
        if (unique[j] == idx)
          goto found;
      unique[n_unique++] = idx;
    found:;
    }

  check (string_set_count (set) == n_unique);
  for (i = 0; i < n_unique; i++)
    check (string_set_contains (set, get_string (unique[i])));
  string_set_destroy (set);
  free (unique);
}

static void
get_keys_and_values_cb (struct string_map *map, int data[], int n)
{
  struct string_set keys, values;

  string_set_init (&keys);
  string_set_init (&values);
  string_map_get_keys (map, &keys);
  string_map_get_values (map, &values);
  check_set (&keys, data, n, KEY_MASK, KEY_SHIFT);
  check_set (&values, data, n, VALUE_MASK, VALUE_SHIFT);
}

static void
test_get_keys_and_values (void)
{
  for_each_map (get_keys_and_values_cb, 13);
}

static void
test_destroy_null (void)
{
  string_map_destroy (NULL);
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
      "replace",
      "insert and replace in random sequence",
      test_replace
    },
    {
      "insert-ordered",
      "insert in ascending order",
      test_insert_ordered
    },
    {
      "clear",
      "clear",
      test_clear
    },
    {
      "clone",
      "clone",
      test_clone
    },
    {
      "swap",
      "swap",
      test_swap
    },
    {
      "node-swap-value",
      "node_swap_value",
      test_node_swap_value
    },
    {
      "insert-map",
      "insert_map",
      test_insert_map
    },
    {
      "replace-map",
      "replace_map",
      test_replace_map
    },
    {
      "get-keys-and-values",
      "get keys and values",
      test_get_keys_and_values
    },
    {
      "destroy-null",
      "destroying null table",
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
      printf ("%s: test string map library\n"
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
            free_strings ();
            return 0;
          }

      fprintf (stderr, "unknown test %s; use --help for help\n", argv[1]);
      return EXIT_FAILURE;
    }
}
