/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010 Free Software Foundation, Inc.

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

/* This is a test program for the ll_* routines defined in
   ll.c.  This test program aims to be as comprehensive as
   possible.  "gcov -b" should report 100% coverage of lines and
   branches in the ll_* routines.  "valgrind --leak-check=yes
   --show-reachable=yes" should give a clean report.

   This test program depends only on ll.c and the standard C
   library.

   See llx-test.c for a similar program for the llx_*
   routines. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libpspp/ll.h>
#include <assert.h>
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

/* Allocates and returns N * M bytes of memory. */
static void *
xnmalloc (size_t n, size_t m)
{
  if ((size_t) -1 / m <= n)
    xalloc_die ();
  return xmalloc (n * m);
}

/* List type and support routines. */

/* Test data element. */
struct element
  {
    struct ll ll;               /* Embedded list element. */
    int x;                      /* Primary value. */
    int y;                      /* Secondary value. */
  };

static int aux_data;

/* Returns the `struct element' that LL is embedded within. */
static struct element *
ll_to_element (const struct ll *ll)
{
  return ll_data (ll, struct element, ll);
}

/* Prints the elements in LIST. */
static void UNUSED
print_list (struct ll_list *list)
{
  struct ll *x;

  printf ("list:");
  for (x = ll_head (list); x != ll_null (list); x = ll_next (x))
    {
      struct element *e = ll_to_element (x);
      printf (" %d", e->x);
    }
  printf ("\n");
}

/* Prints the value returned by PREDICATE given auxiliary data
   AUX for each element in LIST. */
static void UNUSED
print_pred (struct ll_list *list,
            ll_predicate_func *predicate, void *aux UNUSED)
{
  struct ll *x;

  printf ("pred:");
  for (x = ll_head (list); x != ll_null (list); x = ll_next (x))
    printf (" %d", predicate (x, aux));
  printf ("\n");
}

/* Prints the CNT numbers in VALUES. */
static void UNUSED
print_array (int values[], size_t cnt)
{
  size_t i;

  printf ("arry:");
  for (i = 0; i < cnt; i++)
    printf (" %d", values[i]);
  printf ("\n");
}

/* Compares the `x' values in A and B and returns a strcmp-type
   return value.  Verifies that AUX points to aux_data. */
static int
compare_elements (const struct ll *a_, const struct ll *b_, void *aux)
{
  const struct element *a = ll_to_element (a_);
  const struct element *b = ll_to_element (b_);

  check (aux == &aux_data);
  return a->x < b->x ? -1 : a->x > b->x;
}

/* Compares the `x' and `y' values in A and B and returns a
   strcmp-type return value.  Verifies that AUX points to
   aux_data. */
static int
compare_elements_x_y (const struct ll *a_, const struct ll *b_, void *aux)
{
  const struct element *a = ll_to_element (a_);
  const struct element *b = ll_to_element (b_);

  check (aux == &aux_data);
  if (a->x != b->x)
    return a->x < b->x ? -1 : 1;
  else if (a->y != b->y)
    return a->y < b->y ? -1 : 1;
  else
    return 0;
}

/* Compares the `y' values in A and B and returns a strcmp-type
   return value.  Verifies that AUX points to aux_data. */
static int
compare_elements_y (const struct ll *a_, const struct ll *b_, void *aux)
{
  const struct element *a = ll_to_element (a_);
  const struct element *b = ll_to_element (b_);

  check (aux == &aux_data);
  return a->y < b->y ? -1 : a->y > b->y;
}

/* Returns true if the bit in *PATTERN indicated by `x in
   *ELEMENT is set, false otherwise. */
static bool
pattern_pred (const struct ll *element_, void *pattern_)
{
  const struct element *element = ll_to_element (element_);
  unsigned int *pattern = pattern_;

  return (*pattern & (1u << element->x)) != 0;
}

/* Allocates N elements in *ELEMS.
   Adds the elements to LIST, if it is nonnull.
   Puts pointers to the elements' list elements in *ELEMP,
   followed by a pointer to the list null element, if ELEMP is
   nonnull.
   Allocates space for N values in *VALUES, if VALUES is
   nonnull. */
static void
allocate_elements (size_t n,
                   struct ll_list *list,
                   struct element ***elems,
                   struct ll ***elemp,
                   int **values)
{
  size_t i;

  if (list != NULL)
    ll_init (list);

  *elems = xnmalloc (n, sizeof **elems);
  for (i = 0; i < n; i++)
    {
      (*elems)[i] = xmalloc (sizeof ***elems);
      if (list != NULL)
        ll_push_tail (list, &(*elems)[i]->ll);
    }

  if (elemp != NULL)
    {
      *elemp = xnmalloc (n + 1, sizeof *elemp);
      for (i = 0; i < n; i++)
        (*elemp)[i] = &(*elems)[i]->ll;
      (*elemp)[n] = ll_null (list);
    }

  if (values != NULL)
    *values = xnmalloc (n, sizeof *values);
}

/* Copies the CNT values of `x' from LIST into VALUES[]. */
static void
extract_values (struct ll_list *list, int values[], size_t cnt)
{
  struct ll *x;

  check (ll_count (list) == cnt);
  for (x = ll_head (list); x != ll_null (list); x = ll_next (x))
    {
      struct element *e = ll_to_element (x);
      *values++ = e->x;
    }
}

/* As allocate_elements, but sets ascending values, starting
   from 0, in `x' values in *ELEMS and in *VALUES (if
   nonnull). */
static void
allocate_ascending (size_t n,
                    struct ll_list *list,
                    struct element ***elems,
                    struct ll ***elemp,
                    int **values)
{
  size_t i;

  allocate_elements (n, list, elems, elemp, values);

  for (i = 0; i < n; i++)
    (*elems)[i]->x = i;
  if (values != NULL)
    extract_values (list, *values, n);
}

/* As allocate_elements, but sets binary values extracted from
   successive bits in PATTERN in `x' values in *ELEMS and in
   *VALUES (if nonnull). */
static void
allocate_pattern (size_t n,
                  int pattern,
                  struct ll_list *list,
                  struct element ***elems,
                  struct ll ***elemp,
                  int **values)
{
  size_t i;

  allocate_elements (n, list, elems, elemp, values);

  for (i = 0; i < n; i++)
    (*elems)[i]->x = (pattern & (1 << i)) != 0;
  if (values != NULL)
    extract_values (list, *values, n);
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

/* As allocate_ascending, but orders the values randomly. */
static void
allocate_random (size_t n,
                 struct ll_list *list,
                 struct element ***elems,
                 struct ll ***elemp,
                 int **values)
{
  size_t i;

  allocate_elements (n, list, elems, elemp, values);

  for (i = 0; i < n; i++)
    (*elems)[i]->x = i;
  random_shuffle (*elems, n, sizeof **elems);
  if (values != NULL)
    extract_values (list, *values, n);
}

/* Frees the N elements of ELEMS, ELEMP, and VALUES. */
static void
free_elements (size_t n,
               struct element **elems,
               struct ll **elemp,
               int *values)
{
  size_t i;

  for (i = 0; i < n; i++)
    free (elems[i]);
  free (elems);
  free (elemp);
  free (values);
}

/* Compares A and B and returns a strcmp-type return value. */
static int
compare_ints (const void *a_, const void *b_, void *aux UNUSED)
{
  const int *a = a_;
  const int *b = b_;

  return *a < *b ? -1 : *a > *b;
}

/* Compares A and B and returns a strcmp-type return value. */
static int
compare_ints_noaux (const void *a_, const void *b_)
{
  const int *a = a_;
  const int *b = b_;

  return *a < *b ? -1 : *a > *b;
}

/* Checks that LIST contains the CNT values in ELEMENTS. */
static void
check_list_contents (struct ll_list *list, int elements[], size_t cnt)
{
  struct ll *ll;
  size_t i;

  check ((cnt == 0) == ll_is_empty (list));

  /* Iterate in forward order. */
  for (ll = ll_head (list), i = 0; i < cnt; ll = ll_next (ll), i++)
    {
      struct element *e = ll_to_element (ll);
      check (elements[i] == e->x);
      check (ll != ll_null (list));
    }
  check (ll == ll_null (list));

  /* Iterate in reverse order. */
  for (ll = ll_tail (list), i = 0; i < cnt; ll = ll_prev (ll), i++)
    {
      struct element *e = ll_to_element (ll);
      check (elements[cnt - i - 1] == e->x);
      check (ll != ll_null (list));
    }
  check (ll == ll_null (list));

  check (ll_count (list) == cnt);
}

/* Lexicographically compares ARRAY1, which contains COUNT1
   elements of SIZE bytes each, to ARRAY2, which contains COUNT2
   elements of SIZE bytes, according to COMPARE.  Returns a
   strcmp-type result.  AUX is passed to COMPARE as auxiliary
   data. */
static int
lexicographical_compare_3way (const void *array1, size_t count1,
                              const void *array2, size_t count2,
                              size_t size,
                              int (*compare) (const void *, const void *,
                                              void *aux),
                              void *aux)
{
  const char *first1 = array1;
  const char *first2 = array2;
  size_t min_count = count1 < count2 ? count1 : count2;

  while (min_count > 0)
    {
      int cmp = compare (first1, first2, aux);
      if (cmp != 0)
        return cmp;

      first1 += size;
      first2 += size;
      min_count--;
    }

  return count1 < count2 ? -1 : count1 > count2;
}

/* Tests. */

/* Tests list push and pop operations. */
static void
test_push_pop (void)
{
  const int max_elems = 1024;

  struct ll_list list;
  struct element **elems;
  int *values;

  int i;

  allocate_elements (max_elems, NULL, &elems, NULL, &values);

  /* Push on tail. */
  ll_init (&list);
  check_list_contents (&list, NULL, 0);
  for (i = 0; i < max_elems; i++)
    {
      values[i] = elems[i]->x = i;
      ll_push_tail (&list, &elems[i]->ll);
      check_list_contents (&list, values, i + 1);
    }

  /* Remove from tail. */
  for (i = 0; i < max_elems; i++)
    {
      struct element *e = ll_to_element (ll_pop_tail (&list));
      check (e->x == max_elems - i - 1);
      check_list_contents (&list, values, max_elems - i - 1);
    }

  /* Push at start. */
  check_list_contents (&list, NULL, 0);
  for (i = 0; i < max_elems; i++)
    {
      values[max_elems - i - 1] = elems[i]->x = max_elems - i - 1;
      ll_push_head (&list, &elems[i]->ll);
      check_list_contents (&list, &values[max_elems - i - 1], i + 1);
    }

  /* Remove from start. */
  for (i = 0; i < max_elems; i++)
    {
      struct element *e = ll_to_element (ll_pop_head (&list));
      check (e->x == (int) i);
      check_list_contents (&list, &values[i + 1], max_elems - i - 1);
    }

  free_elements (max_elems, elems, NULL, values);
}

/* Tests insertion and removal at arbitrary positions. */
static void
test_insert_remove (void)
{
  const int max_elems = 16;
  int cnt;

  for (cnt = 0; cnt < max_elems; cnt++)
    {
      struct element **elems;
      struct ll **elemp;
      int *values = xnmalloc (cnt + 1, sizeof *values);

      struct ll_list list;
      struct element extra;
      int pos;

      allocate_ascending (cnt, &list, &elems, &elemp, NULL);
      extra.x = -1;
      for (pos = 0; pos <= cnt; pos++)
        {
          int i, j;

          ll_insert (elemp[pos], &extra.ll);

          j = 0;
          for (i = 0; i < pos; i++)
            values[j++] = i;
          values[j++] = -1;
          for (; i < cnt; i++)
            values[j++] = i;
          check_list_contents (&list, values, cnt + 1);

          ll_remove (&extra.ll);
        }
      check_list_contents (&list, values, cnt);

      free_elements (cnt, elems, elemp, values);
    }
}

/* Tests swapping individual elements. */
static void
test_swap (void)
{
  const int max_elems = 8;
  int cnt;

  for (cnt = 0; cnt <= max_elems; cnt++)
    {
      struct ll_list list;
      struct element **elems;
      int *values;

      int i, j, k;

      allocate_ascending (cnt, &list, &elems, NULL, &values);
      check_list_contents (&list, values, cnt);

      for (i = 0; i < cnt; i++)
        for (j = 0; j < cnt; j++)
          for (k = 0; k < 2; k++)
            {
              int t;

              ll_swap (&elems[i]->ll, &elems[j]->ll);
              t = values[i];
              values[i] = values[j];
              values[j] = t;
              check_list_contents (&list, values, cnt);
            }

      free_elements (cnt, elems, NULL, values);
    }
}

/* Tests swapping ranges of list elements. */
static void
test_swap_range (void)
{
  const int max_elems = 8;
  int cnt, a0, a1, b0, b1, r;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (a0 = 0; a0 <= cnt; a0++)
      for (a1 = a0; a1 <= cnt; a1++)
        for (b0 = a1; b0 <= cnt; b0++)
          for (b1 = b0; b1 <= cnt; b1++)
            for (r = 0; r < 2; r++)
              {
                struct ll_list list;
                struct element **elems;
                struct ll **elemp;
                int *values;

                int i, j;

                allocate_ascending (cnt, &list, &elems, &elemp, &values);
                check_list_contents (&list, values, cnt);

                j = 0;
                for (i = 0; i < a0; i++)
                  values[j++] = i;
                for (i = b0; i < b1; i++)
                  values[j++] = i;
                for (i = a1; i < b0; i++)
                  values[j++] = i;
                for (i = a0; i < a1; i++)
                  values[j++] = i;
                for (i = b1; i < cnt; i++)
                  values[j++] = i;
                check (j == cnt);

                if (r == 0)
                  ll_swap_range (elemp[a0], elemp[a1], elemp[b0], elemp[b1]);
                else
                  ll_swap_range (elemp[b0], elemp[b1], elemp[a0], elemp[a1]);
                check_list_contents (&list, values, cnt);

                free_elements (cnt, elems, elemp, values);
              }
}

/* Tests removing ranges of list elements. */
static void
test_remove_range (void)
{
  const int max_elems = 8;

  int cnt, r0, r1;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (r0 = 0; r0 <= cnt; r0++)
      for (r1 = r0; r1 <= cnt; r1++)
        {
          struct ll_list list;
          struct element **elems;
          struct ll **elemp;
          int *values;

          int i, j;

          allocate_ascending (cnt, &list, &elems, &elemp, &values);
          check_list_contents (&list, values, cnt);

          j = 0;
          for (i = 0; i < r0; i++)
            values[j++] = i;
          for (i = r1; i < cnt; i++)
            values[j++] = i;

          ll_remove_range (elemp[r0], elemp[r1]);
          check_list_contents (&list, values, j);

          free_elements (cnt, elems, elemp, values);
        }
}

/* Tests ll_remove_equal. */
static void
test_remove_equal (void)
{
  const int max_elems = 8;

  int cnt, r0, r1, eq_pat;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (r0 = 0; r0 <= cnt; r0++)
      for (r1 = r0; r1 <= cnt; r1++)
        for (eq_pat = 0; eq_pat <= 1 << cnt; eq_pat++)
          {
            struct ll_list list;
            struct element **elems;
            struct ll **elemp;
            int *values;

            struct element to_remove;
            int remaining;
            int i;

            allocate_elements (cnt, &list, &elems, &elemp, &values);

            remaining = 0;
            for (i = 0; i < cnt; i++)
              {
                int x = eq_pat & (1 << i) ? -1 : i;
                bool delete = x == -1 && r0 <= i && i < r1;
                elems[i]->x = x;
                if (!delete)
                  values[remaining++] = x;
              }

            to_remove.x = -1;
            check ((int) ll_remove_equal (elemp[r0], elemp[r1], &to_remove.ll,
                                          compare_elements, &aux_data)
                   == cnt - remaining);
            check_list_contents (&list, values, remaining);

            free_elements (cnt, elems, elemp, values);
          }
}

/* Tests ll_remove_if. */
static void
test_remove_if (void)
{
  const int max_elems = 8;

  int cnt, r0, r1, pattern;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (r0 = 0; r0 <= cnt; r0++)
      for (r1 = r0; r1 <= cnt; r1++)
        for (pattern = 0; pattern <= 1 << cnt; pattern++)
          {
            struct ll_list list;
            struct element **elems;
            struct ll **elemp;
            int *values;

            int remaining;
            int i;

            allocate_elements (cnt, &list, &elems, &elemp, &values);

            remaining = 0;
            for (i = 0; i < cnt; i++)
              {
                bool delete = (pattern & (1 << i)) && r0 <= i && i < r1;
                elems[i]->x = i;
                if (!delete)
                  values[remaining++] = i;
              }

            check ((int) ll_remove_if (elemp[r0], elemp[r1],
                                       pattern_pred, &pattern)
                   == cnt - remaining);
            check_list_contents (&list, values, remaining);

            free_elements (cnt, elems, elemp, values);
          }
}

/* Tests ll_moved. */
static void
test_moved (void)
{
  const int max_elems = 8;

  int cnt;

  for (cnt = 0; cnt <= max_elems; cnt++)
    {
      struct ll_list list;
      struct element **elems;
      struct element **new_elems;
      int *values;

      int i;

      allocate_ascending (cnt, &list, &elems, NULL, &values);
      allocate_elements (cnt, NULL, &new_elems, NULL, NULL);
      check_list_contents (&list, values, cnt);

      for (i = 0; i < cnt; i++)
        {
          *new_elems[i] = *elems[i];
          ll_moved (&new_elems[i]->ll);
          check_list_contents (&list, values, cnt);
        }

      free_elements (cnt, elems, NULL, values);
      free_elements (cnt, new_elems, NULL, NULL);
    }
}

/* Tests, via HELPER, a function that looks at list elements
   equal to some specified element. */
static void
test_examine_equal_range (void (*helper) (int r0, int r1, int eq_pat,
                                          struct ll *to_find,
                                          struct ll **elemp))
{
  const int max_elems = 8;

  int cnt, r0, r1, eq_pat;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (eq_pat = 0; eq_pat <= 1 << cnt; eq_pat++)
      {
        struct ll_list list;
        struct element **elems;
        struct ll **elemp;
        int *values;

        struct element to_find;

        int i;

        allocate_ascending (cnt, &list, &elems, &elemp, &values);

        for (i = 0; i < cnt; i++)
          if (eq_pat & (1 << i))
            values[i] = elems[i]->x = -1;

        to_find.x = -1;
        for (r0 = 0; r0 <= cnt; r0++)
          for (r1 = r0; r1 <= cnt; r1++)
            helper (r0, r1, eq_pat, &to_find.ll, elemp);

        check_list_contents (&list, values, cnt);

        free_elements (cnt, elems, elemp, values);
      }
}

/* Tests, via HELPER, a function that looks at list elements for
   which a given predicate returns true. */
static void
test_examine_if_range (void (*helper) (int r0, int r1, int eq_pat,
                                       struct ll **elemp))
{
  const int max_elems = 8;

  int cnt, r0, r1, eq_pat;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (eq_pat = 0; eq_pat <= 1 << cnt; eq_pat++)
      {
        struct ll_list list;
        struct element **elems;
        struct ll **elemp;
        int *values;

        allocate_ascending (cnt, &list, &elems, &elemp, &values);

        for (r0 = 0; r0 <= cnt; r0++)
          for (r1 = r0; r1 <= cnt; r1++)
            helper (r0, r1, eq_pat, elemp);

        check_list_contents (&list, values, cnt);

        free_elements (cnt, elems, elemp, values);
      }
}

/* Helper function for testing ll_find_equal. */
static void
test_find_equal_helper (int r0, int r1, int eq_pat,
                        struct ll *to_find, struct ll **elemp)
{
  struct ll *match;
  int i;

  match = ll_find_equal (elemp[r0], elemp[r1], to_find,
                         compare_elements, &aux_data);
  for (i = r0; i < r1; i++)
    if (eq_pat & (1 << i))
      break;

  check (match == elemp[i]);
}

/* Tests ll_find_equal. */
static void
test_find_equal (void)
{
  test_examine_equal_range (test_find_equal_helper);
}

/* Helper function for testing ll_find_if. */
static void
test_find_if_helper (int r0, int r1, int eq_pat, struct ll **elemp)
{
  struct ll *match = ll_find_if (elemp[r0], elemp[r1],
                                 pattern_pred, &eq_pat);
  int i;

  for (i = r0; i < r1; i++)
    if (eq_pat & (1 << i))
      break;

  check (match == elemp[i]);
}

/* Tests ll_find_if. */
static void
test_find_if (void)
{
  test_examine_if_range (test_find_if_helper);
}

/* Tests ll_find_adjacent_equal. */
static void
test_find_adjacent_equal (void)
{
  const int max_elems = 8;

  int cnt, eq_pat;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (eq_pat = 0; eq_pat <= 1 << cnt; eq_pat++)
      {
        struct ll_list list;
        struct element **elems;
        struct ll **elemp;
        int *values;
        int match;

        int i;

        allocate_ascending (cnt, &list, &elems, &elemp, &values);

        match = -1;
        for (i = 0; i < cnt - 1; i++)
          {
            elems[i]->y = i;
            if (eq_pat & (1 << i))
              {
                values[i] = elems[i]->x = match;
                values[i + 1] = elems[i + 1]->x = match;
              }
            else
              match--;
          }

        for (i = 0; i <= cnt; i++)
          {
            struct ll *ll1 = ll_find_adjacent_equal (elemp[i], ll_null (&list),
                                                     compare_elements,
                                                     &aux_data);
            struct ll *ll2;
            int j;

            ll2 = ll_null (&list);
            for (j = i; j < cnt - 1; j++)
              if (eq_pat & (1 << j))
                {
                  ll2 = elemp[j];
                  break;
                }
            check (ll1 == ll2);
          }
        check_list_contents (&list, values, cnt);

        free_elements (cnt, elems, elemp, values);
      }
}

/* Helper function for testing ll_count_range. */
static void
test_count_range_helper (int r0, int r1, int eq_pat UNUSED, struct ll **elemp)
{
  check ((int) ll_count_range (elemp[r0], elemp[r1]) == r1 - r0);
}

/* Tests ll_count_range. */
static void
test_count_range (void)
{
  test_examine_if_range (test_count_range_helper);
}

/* Helper function for testing ll_count_equal. */
static void
test_count_equal_helper (int r0, int r1, int eq_pat,
                         struct ll *to_find, struct ll **elemp)
{
  int count1, count2;
  int i;

  count1 = ll_count_equal (elemp[r0], elemp[r1], to_find,
                           compare_elements, &aux_data);
  count2 = 0;
  for (i = r0; i < r1; i++)
    if (eq_pat & (1 << i))
      count2++;

  check (count1 == count2);
}

/* Tests ll_count_equal. */
static void
test_count_equal (void)
{
  test_examine_equal_range (test_count_equal_helper);
}

/* Helper function for testing ll_count_if. */
static void
test_count_if_helper (int r0, int r1, int eq_pat, struct ll **elemp)
{
  int count1;
  int count2;
  int i;

  count1 = ll_count_if (elemp[r0], elemp[r1], pattern_pred, &eq_pat);

  count2 = 0;
  for (i = r0; i < r1; i++)
    if (eq_pat & (1 << i))
      count2++;

  check (count1 == count2);
}

/* Tests ll_count_if. */
static void
test_count_if (void)
{
  test_examine_if_range (test_count_if_helper);
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

/* Returns the number of permutations of the CNT values in
   VALUES.  If VALUES contains duplicates, they must be
   adjacent. */
static unsigned int
expected_perms (int *values, size_t cnt)
{
  size_t i, j;
  unsigned int perm_cnt;

  perm_cnt = factorial (cnt);
  for (i = 0; i < cnt; i = j)
    {
      for (j = i + 1; j < cnt; j++)
        if (values[i] != values[j])
          break;
      perm_cnt /= factorial (j - i);
    }
  return perm_cnt;
}

/* Tests ll_min and ll_max. */
static void
test_min_max (void)
{
  const int max_elems = 6;
  int cnt;

  for (cnt = 0; cnt <= max_elems; cnt++)
    {
      struct ll_list list;
      struct element **elems;
      struct ll **elemp;
      int *values;
      int *new_values = xnmalloc (cnt, sizeof *values);

      size_t perm_cnt;

      allocate_ascending (cnt, &list, &elems, &elemp, &values);

      perm_cnt = 1;
      while (ll_next_permutation (ll_head (&list), ll_null (&list),
                                  compare_elements, &aux_data))
        {
          int r0, r1;
          struct ll *x;
          int i;

          for (i = 0, x = ll_head (&list); x != ll_null (&list);
               x = ll_next (x), i++)
            {
              struct element *e = ll_to_element (x);
              elemp[i] = x;
              new_values[i] = e->x;
            }
          for (r0 = 0; r0 <= cnt; r0++)
            for (r1 = r0; r1 <= cnt; r1++)
              {
                struct ll *min = ll_min (elemp[r0], elemp[r1],
                                         compare_elements, &aux_data);
                struct ll *max = ll_max (elemp[r0], elemp[r1],
                                         compare_elements, &aux_data);
                if (r0 == r1)
                  {
                    check (min == elemp[r1]);
                    check (max == elemp[r1]);
                  }
                else
                  {
                    int min_int, max_int;
                    int i;

                    min_int = max_int = new_values[r0];
                    for (i = r0; i < r1; i++)
                      {
                        int value = new_values[i];
                        if (value < min_int)
                          min_int = value;
                        if (value > max_int)
                          max_int = value;
                      }
                    check (min != elemp[r1]
                           && ll_to_element (min)->x == min_int);
                    check (max != elemp[r1]
                           && ll_to_element (max)->x == max_int);
                  }
              }
          perm_cnt++;
        }
      check (perm_cnt == factorial (cnt));
      check_list_contents (&list, values, cnt);

      free_elements (cnt, elems, elemp, values);
      free (new_values);
    }
}

/* Tests ll_lexicographical_compare_3way. */
static void
test_lexicographical_compare_3way (void)
{
  const int max_elems = 4;

  int cnt_a, pat_a, cnt_b, pat_b;

  for (cnt_a = 0; cnt_a <= max_elems; cnt_a++)
    for (pat_a = 0; pat_a <= 1 << cnt_a; pat_a++)
      for (cnt_b = 0; cnt_b <= max_elems; cnt_b++)
        for (pat_b = 0; pat_b <= 1 << cnt_b; pat_b++)
          {
            struct ll_list list_a, list_b;
            struct element **elems_a, **elems_b;
            struct ll **elemp_a, **elemp_b;
            int *values_a, *values_b;

            int a0, a1, b0, b1;

            allocate_pattern (cnt_a, pat_a,
                              &list_a, &elems_a, &elemp_a, &values_a);
            allocate_pattern (cnt_b, pat_b,
                              &list_b, &elems_b, &elemp_b, &values_b);

            for (a0 = 0; a0 <= cnt_a; a0++)
              for (a1 = a0; a1 <= cnt_a; a1++)
                for (b0 = 0; b0 <= cnt_b; b0++)
                  for (b1 = b0; b1 <= cnt_b; b1++)
                    {
                      int a_ordering = lexicographical_compare_3way (
                        values_a + a0, a1 - a0,
                        values_b + b0, b1 - b0,
                        sizeof *values_a,
                        compare_ints, NULL);

                      int b_ordering = ll_lexicographical_compare_3way (
                        elemp_a[a0], elemp_a[a1],
                        elemp_b[b0], elemp_b[b1],
                        compare_elements, &aux_data);

                      check (a_ordering == b_ordering);
                    }

            free_elements (cnt_a, elems_a, elemp_a, values_a);
            free_elements (cnt_b, elems_b, elemp_b, values_b);
          }
}

/* Appends the `x' value in element E to the array pointed to by
   NEXT_OUTPUT, and advances NEXT_OUTPUT to the next position. */
static void
apply_func (struct ll *e_, void *next_output_)
{
  struct element *e = ll_to_element (e_);
  int **next_output = next_output_;

  *(*next_output)++ = e->x;
}

/* Tests ll_apply. */
static void
test_apply (void)
{
  const int max_elems = 8;

  int cnt, r0, r1;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (r0 = 0; r0 <= cnt; r0++)
      for (r1 = r0; r1 <= cnt; r1++)
        {
          struct ll_list list;
          struct element **elems;
          struct ll **elemp;
          int *values;

          int *output;
          int *next_output;

          int i;

          allocate_ascending (cnt, &list, &elems, &elemp, &values);
          check_list_contents (&list, values, cnt);

          output = next_output = xnmalloc (cnt, sizeof *output);
          ll_apply (elemp[r0], elemp[r1], apply_func, &next_output);
          check_list_contents (&list, values, cnt);

          check (r1 - r0 == next_output - output);
          for (i = 0; i < r1 - r0; i++)
            check (output[i] == r0 + i);

          free_elements (cnt, elems, elemp, values);
          free (output);
        }
}

/* Tests ll_reverse. */
static void
test_reverse (void)
{
  const int max_elems = 8;

  int cnt, r0, r1;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (r0 = 0; r0 <= cnt; r0++)
      for (r1 = r0; r1 <= cnt; r1++)
        {
          struct ll_list list;
          struct element **elems;
          struct ll **elemp;
          int *values;

          int i, j;

          allocate_ascending (cnt, &list, &elems, &elemp, &values);
          check_list_contents (&list, values, cnt);

          j = 0;
          for (i = 0; i < r0; i++)
            values[j++] = i;
          for (i = r1 - 1; i >= r0; i--)
            values[j++] = i;
          for (i = r1; i < cnt; i++)
            values[j++] = i;

          ll_reverse (elemp[r0], elemp[r1]);
          check_list_contents (&list, values, cnt);

          free_elements (cnt, elems, elemp, values);
        }
}

/* Tests ll_next_permutation and ll_prev_permutation when the
   permuted values have no duplicates. */
static void
test_permutations_no_dups (void)
{
  const int max_elems = 8;
  int cnt;

  for (cnt = 0; cnt <= max_elems; cnt++)
    {
      struct ll_list list;
      struct element **elems;
      int *values;
      int *old_values = xnmalloc (cnt, sizeof *values);
      int *new_values = xnmalloc (cnt, sizeof *values);

      size_t perm_cnt;

      allocate_ascending (cnt, &list, &elems, NULL, &values);

      perm_cnt = 1;
      extract_values (&list, old_values, cnt);
      while (ll_next_permutation (ll_head (&list), ll_null (&list),
                                  compare_elements, &aux_data))
        {
          extract_values (&list, new_values, cnt);
          check (lexicographical_compare_3way (new_values, cnt,
                                               old_values, cnt,
                                               sizeof *new_values,
                                               compare_ints, NULL) > 0);
          memcpy (old_values, new_values, (cnt) * sizeof *old_values);
          perm_cnt++;
        }
      check (perm_cnt == factorial (cnt));
      check_list_contents (&list, values, cnt);

      perm_cnt = 1;
      ll_reverse (ll_head (&list), ll_null (&list));
      extract_values (&list, old_values, cnt);
      while (ll_prev_permutation (ll_head (&list), ll_null (&list),
                                  compare_elements, &aux_data))
        {
          extract_values (&list, new_values, cnt);
          check (lexicographical_compare_3way (new_values, cnt,
                                               old_values, cnt,
                                               sizeof *new_values,
                                               compare_ints, NULL) < 0);
          memcpy (old_values, new_values, (cnt) * sizeof *old_values);
          perm_cnt++;
        }
      check (perm_cnt == factorial (cnt));
      ll_reverse (ll_head (&list), ll_null (&list));
      check_list_contents (&list, values, cnt);

      free_elements (cnt, elems, NULL, values);
      free (old_values);
      free (new_values);
    }
}

/* Tests ll_next_permutation and ll_prev_permutation when the
   permuted values contain duplicates. */
static void
test_permutations_with_dups (void)
{
  const int max_elems = 8;
  const int max_dup = 3;
  const int repetitions = 1024;

  int cnt, repeat;

  for (repeat = 0; repeat < repetitions; repeat++)
    for (cnt = 0; cnt < max_elems; cnt++)
      {
        struct ll_list list;
        struct element **elems;
        int *values;
        int *old_values = xnmalloc (max_elems, sizeof *values);
        int *new_values = xnmalloc (max_elems, sizeof *values);

        unsigned int permutation_cnt;
        int left = cnt;
        int value = 0;

        allocate_elements (cnt, &list, &elems, NULL, &values);

        value = 0;
        while (left > 0)
          {
            int max = left < max_dup ? left : max_dup;
            int n = rand () % max + 1;
            while (n-- > 0)
              {
                int idx = cnt - left--;
                values[idx] = elems[idx]->x = value;
              }
            value++;
          }

        permutation_cnt = 1;
        extract_values (&list, old_values, cnt);
        while (ll_next_permutation (ll_head (&list), ll_null (&list),
                                    compare_elements, &aux_data))
          {
            extract_values (&list, new_values, cnt);
            check (lexicographical_compare_3way (new_values, cnt,
                                                 old_values, cnt,
                                                 sizeof *new_values,
                                                 compare_ints, NULL) > 0);
            memcpy (old_values, new_values, cnt * sizeof *old_values);
            permutation_cnt++;
          }
        check (permutation_cnt == expected_perms (values, cnt));
        check_list_contents (&list, values, cnt);

        permutation_cnt = 1;
        ll_reverse (ll_head (&list), ll_null (&list));
        extract_values (&list, old_values, cnt);
        while (ll_prev_permutation (ll_head (&list), ll_null (&list),
                                    compare_elements, &aux_data))
          {
            extract_values (&list, new_values, cnt);
            check (lexicographical_compare_3way (new_values, cnt,
                                                 old_values, cnt,
                                                 sizeof *new_values,
                                                 compare_ints, NULL) < 0);
            permutation_cnt++;
          }
        ll_reverse (ll_head (&list), ll_null (&list));
        check (permutation_cnt == expected_perms (values, cnt));
        check_list_contents (&list, values, cnt);

        free_elements (cnt, elems, NULL, values);
        free (old_values);
        free (new_values);
      }
}

/* Tests ll_merge when no equal values are to be merged. */
static void
test_merge_no_dups (void)
{
  const int max_elems = 8;
  const int max_filler = 3;

  int merge_cnt, pattern, pfx, gap, sfx, order;

  for (merge_cnt = 0; merge_cnt < max_elems; merge_cnt++)
    for (pattern = 0; pattern <= (1 << merge_cnt); pattern++)
      for (pfx = 0; pfx < max_filler; pfx++)
        for (gap = 0; gap < max_filler; gap++)
          for (sfx = 0; sfx < max_filler; sfx++)
            for (order = 0; order < 2; order++)
              {
                struct ll_list list;
                struct element **elems;
                struct ll **elemp;
                int *values;

                int list_cnt = pfx + merge_cnt + gap + sfx;
                int a0, a1, b0, b1;
                int i, j;

                allocate_elements (list_cnt, &list,
                                   &elems, &elemp, &values);

                j = 0;
                for (i = 0; i < pfx; i++)
                  elems[j++]->x = 100 + i;
                a0 = j;
                for (i = 0; i < merge_cnt; i++)
                  if (pattern & (1u << i))
                    elems[j++]->x = i;
                a1 = j;
                for (i = 0; i < gap; i++)
                  elems[j++]->x = 200 + i;
                b0 = j;
                for (i = 0; i < merge_cnt; i++)
                  if (!(pattern & (1u << i)))
                    elems[j++]->x = i;
                b1 = j;
                for (i = 0; i < sfx; i++)
                  elems[j++]->x = 300 + i;
                check (list_cnt == j);

                j = 0;
                for (i = 0; i < pfx; i++)
                  values[j++] = 100 + i;
                if (order == 0)
                  for (i = 0; i < merge_cnt; i++)
                    values[j++] = i;
                for (i = 0; i < gap; i++)
                  values[j++] = 200 + i;
                if (order == 1)
                  for (i = 0; i < merge_cnt; i++)
                    values[j++] = i;
                for (i = 0; i < sfx; i++)
                  values[j++] = 300 + i;
                check (list_cnt == j);

                if (order == 0)
                  ll_merge (elemp[a0], elemp[a1], elemp[b0], elemp[b1],
                            compare_elements, &aux_data);
                else
                  ll_merge (elemp[b0], elemp[b1], elemp[a0], elemp[a1],
                            compare_elements, &aux_data);

                check_list_contents (&list, values, list_cnt);

                free_elements (list_cnt, elems, elemp, values);
              }
}

/* Tests ll_merge when equal values are to be merged. */
static void
test_merge_with_dups (void)
{
  const int max_elems = 8;

  int cnt, merge_pat, inc_pat, order;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (merge_pat = 0; merge_pat <= (1 << cnt); merge_pat++)
      for (inc_pat = 0; inc_pat <= (1 << cnt); inc_pat++)
        for (order = 0; order < 2; order++)
          {
            struct ll_list list;
            struct element **elems;
            struct ll **elemp;
            int *values;

            int mid;
            int i, j, k;

            allocate_elements (cnt, &list, &elems, &elemp, &values);

            j = 0;
            for (i = k = 0; i < cnt; i++)
              {
                if (merge_pat & (1u << i))
                  elems[j++]->x = k;
                if (inc_pat & (1u << i))
                  k++;
              }
            mid = j;
            for (i = k = 0; i < cnt; i++)
              {
                if (!(merge_pat & (1u << i)))
                  elems[j++]->x = k;
                if (inc_pat & (1u << i))
                  k++;
              }
            check (cnt == j);

            if (order == 0)
              {
                for (i = 0; i < cnt; i++)
                  elems[i]->y = i;
              }
            else
              {
                for (i = 0; i < mid; i++)
                  elems[i]->y = 100 + i;
                for (i = mid; i < cnt; i++)
                  elems[i]->y = i;
              }

            j = 0;
            for (i = k = 0; i < cnt; i++)
              {
                values[j++] = k;
                if (inc_pat & (1u << i))
                  k++;
              }
            check (cnt == j);

            if (order == 0)
              ll_merge (elemp[0], elemp[mid], elemp[mid], elemp[cnt],
                        compare_elements, &aux_data);
            else
              ll_merge (elemp[mid], elemp[cnt], elemp[0], elemp[mid],
                        compare_elements, &aux_data);

            check_list_contents (&list, values, cnt);
            check (ll_is_sorted (ll_head (&list), ll_null (&list),
                                 compare_elements_x_y, &aux_data));

            free_elements (cnt, elems, elemp, values);
          }
}

/* Tests ll_sort on all permutations up to a maximum number of
   elements. */
static void
test_sort_exhaustive (void)
{
  const int max_elems = 8;
  int cnt;

  for (cnt = 0; cnt <= max_elems; cnt++)
    {
      struct ll_list list;
      struct element **elems;
      int *values;

      struct element **perm_elems;
      int *perm_values;

      size_t perm_cnt;

      allocate_ascending (cnt, &list, &elems, NULL, &values);
      allocate_elements (cnt, NULL, &perm_elems, NULL, &perm_values);

      perm_cnt = 1;
      while (ll_next_permutation (ll_head (&list), ll_null (&list),
                                  compare_elements, &aux_data))
        {
          struct ll_list perm_list;
          int j;

          extract_values (&list, perm_values, cnt);
          ll_init (&perm_list);
          for (j = 0; j < cnt; j++)
            {
              perm_elems[j]->x = perm_values[j];
              ll_push_tail (&perm_list, &perm_elems[j]->ll);
            }
          ll_sort (ll_head (&perm_list), ll_null (&perm_list),
                   compare_elements, &aux_data);
          check_list_contents (&perm_list, values, cnt);
          check (ll_is_sorted (ll_head (&perm_list), ll_null (&perm_list),
                               compare_elements, &aux_data));
          perm_cnt++;
        }
      check (perm_cnt == factorial (cnt));

      free_elements (cnt, elems, NULL, values);
      free_elements (cnt, perm_elems, NULL, perm_values);
    }
}

/* Tests that ll_sort is stable in the presence of equal
   values. */
static void
test_sort_stable (void)
{
  const int max_elems = 6;
  int cnt, inc_pat;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (inc_pat = 0; inc_pat <= 1 << cnt; inc_pat++)
      {
        struct ll_list list;
        struct element **elems;
        int *values;

        struct element **perm_elems;
        int *perm_values;

        size_t perm_cnt;
        int i, j;

        allocate_elements (cnt, &list, &elems, NULL, &values);
        allocate_elements (cnt, NULL, &perm_elems, NULL, &perm_values);

        j = 0;
        for (i = 0; i < cnt; i++)
          {
            elems[i]->x = values[i] = j;
            if (inc_pat & (1 << i))
              j++;
            elems[i]->y = i;
          }

        perm_cnt = 1;
        while (ll_next_permutation (ll_head (&list), ll_null (&list),
                                    compare_elements_y, &aux_data))
          {
            struct ll_list perm_list;

            extract_values (&list, perm_values, cnt);
            ll_init (&perm_list);
            for (i = 0; i < cnt; i++)
              {
                perm_elems[i]->x = perm_values[i];
                perm_elems[i]->y = i;
                ll_push_tail (&perm_list, &perm_elems[i]->ll);
              }
            ll_sort (ll_head (&perm_list), ll_null (&perm_list),
                     compare_elements, &aux_data);
            check_list_contents (&perm_list, values, cnt);
            check (ll_is_sorted (ll_head (&perm_list), ll_null (&perm_list),
                                 compare_elements_x_y, &aux_data));
            perm_cnt++;
          }
        check (perm_cnt == factorial (cnt));

        free_elements (cnt, elems, NULL, values);
        free_elements (cnt, perm_elems, NULL, perm_values);
      }
}

/* Tests that ll_sort does not disturb elements outside the
   range sorted. */
static void
test_sort_subset (void)
{
  const int max_elems = 8;

  int cnt, r0, r1, repeat;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (repeat = 0; repeat < 100; repeat++)
      for (r0 = 0; r0 <= cnt; r0++)
        for (r1 = r0; r1 <= cnt; r1++)
          {
            struct ll_list list;
            struct element **elems;
            struct ll **elemp;
            int *values;

            allocate_random (cnt, &list, &elems, &elemp, &values);

            qsort (&values[r0], r1 - r0, sizeof *values, compare_ints_noaux);
            ll_sort (elemp[r0], elemp[r1], compare_elements, &aux_data);
            check_list_contents (&list, values, cnt);

            free_elements (cnt, elems, elemp, values);
          }
}

/* Tests that ll_sort works with large lists. */
static void
test_sort_big (void)
{
  const int max_elems = 1024;

  int cnt;

  for (cnt = 0; cnt < max_elems; cnt++)
    {
      struct ll_list list;
      struct element **elems;
      int *values;

      allocate_random (cnt, &list, &elems, NULL, &values);

      qsort (values, cnt, sizeof *values, compare_ints_noaux);
      ll_sort (ll_head (&list), ll_null (&list), compare_elements, &aux_data);
      check_list_contents (&list, values, cnt);

      free_elements (cnt, elems, NULL, values);
    }
}

/* Tests ll_unique. */
static void
test_unique (void)
{
  const int max_elems = 10;

  int *ascending = xnmalloc (max_elems, sizeof *ascending);

  int cnt, inc_pat, i, j, unique_values;

  for (i = 0; i < max_elems; i++)
    ascending[i] = i;

  for (cnt = 0; cnt < max_elems; cnt++)
    for (inc_pat = 0; inc_pat < (1 << cnt); inc_pat++)
      {
        struct ll_list list, dups;
        struct element **elems;
        int *values;

        allocate_elements (cnt, &list, &elems, NULL, &values);

        j = unique_values = 0;
        for (i = 0; i < cnt; i++)
          {
            unique_values = j + 1;
            elems[i]->x = values[i] = j;
            if (inc_pat & (1 << i))
              j++;
          }
        check_list_contents (&list, values, cnt);

        ll_init (&dups);
        check (ll_unique (ll_head (&list), ll_null (&list), ll_null (&dups),
                          compare_elements, &aux_data)
               == (size_t) unique_values);
        check_list_contents (&list, ascending, unique_values);

        ll_splice (ll_null (&list), ll_head (&dups), ll_null (&dups));
        ll_sort (ll_head (&list), ll_null (&list), compare_elements, &aux_data);
        check_list_contents (&list, values, cnt);

        free_elements (cnt, elems, NULL, values);
      }

  free (ascending);
}

/* Tests ll_sort_unique. */
static void
test_sort_unique (void)
{
  const int max_elems = 7;
  int cnt, inc_pat;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (inc_pat = 0; inc_pat <= 1 << cnt; inc_pat++)
      {
        struct ll_list list;
        struct element **elems;
        int *values;

        struct element **perm_elems;
        int *perm_values;

        int unique_cnt;
        int *unique_values;

        size_t perm_cnt;
        int i, j;

        allocate_elements (cnt, &list, &elems, NULL, &values);
        allocate_elements (cnt, NULL, &perm_elems, NULL, &perm_values);

        j = unique_cnt = 0;
        for (i = 0; i < cnt; i++)
          {
            elems[i]->x = values[i] = j;
            unique_cnt = j + 1;
            if (inc_pat & (1 << i))
              j++;
          }

        unique_values = xnmalloc (unique_cnt, sizeof *unique_values);
        for (i = 0; i < unique_cnt; i++)
          unique_values[i] = i;

        perm_cnt = 1;
        while (ll_next_permutation (ll_head (&list), ll_null (&list),
                                    compare_elements, &aux_data))
          {
            struct ll_list perm_list;

            extract_values (&list, perm_values, cnt);
            ll_init (&perm_list);
            for (i = 0; i < cnt; i++)
              {
                perm_elems[i]->x = perm_values[i];
                perm_elems[i]->y = i;
                ll_push_tail (&perm_list, &perm_elems[i]->ll);
              }
            ll_sort_unique (ll_head (&perm_list), ll_null (&perm_list), NULL,
                            compare_elements, &aux_data);
            check_list_contents (&perm_list, unique_values, unique_cnt);
            check (ll_is_sorted (ll_head (&perm_list), ll_null (&perm_list),
                                 compare_elements_x_y, &aux_data));
            perm_cnt++;
          }
        check (perm_cnt == expected_perms (values, cnt));

        free_elements (cnt, elems, NULL, values);
        free_elements (cnt, perm_elems, NULL, perm_values);
        free (unique_values);
      }
}

/* Tests ll_insert_ordered. */
static void
test_insert_ordered (void)
{
  const int max_elems = 6;
  int cnt, inc_pat;

  for (cnt = 0; cnt <= max_elems; cnt++)
    for (inc_pat = 0; inc_pat <= 1 << cnt; inc_pat++)
      {
        struct ll_list list;
        struct element **elems;
        int *values;

        struct element **perm_elems;
        int *perm_values;

        size_t perm_cnt;
        int i, j;

        allocate_elements (cnt, &list, &elems, NULL, &values);
        allocate_elements (cnt, NULL, &perm_elems, NULL, &perm_values);

        j = 0;
        for (i = 0; i < cnt; i++)
          {
            elems[i]->x = values[i] = j;
            if (inc_pat & (1 << i))
              j++;
            elems[i]->y = i;
          }

        perm_cnt = 1;
        while (ll_next_permutation (ll_head (&list), ll_null (&list),
                                    compare_elements_y, &aux_data))
          {
            struct ll_list perm_list;

            extract_values (&list, perm_values, cnt);
            ll_init (&perm_list);
            for (i = 0; i < cnt; i++)
              {
                perm_elems[i]->x = perm_values[i];
                perm_elems[i]->y = i;
                ll_insert_ordered (ll_head (&perm_list), ll_null (&perm_list),
                                   &perm_elems[i]->ll,
                                   compare_elements, &aux_data);
              }
            check (ll_is_sorted (ll_head (&perm_list), ll_null (&perm_list),
                                 compare_elements_x_y, &aux_data));
            perm_cnt++;
          }
        check (perm_cnt == factorial (cnt));

        free_elements (cnt, elems, NULL, values);
        free_elements (cnt, perm_elems, NULL, perm_values);
      }
}

/* Tests ll_partition. */
static void
test_partition (void)
{
  const int max_elems = 10;

  int cnt;
  unsigned int pbase;
  int r0, r1;

  for (cnt = 0; cnt < max_elems; cnt++)
    for (r0 = 0; r0 <= cnt; r0++)
      for (r1 = r0; r1 <= cnt; r1++)
        for (pbase = 0; pbase <= (1u << (r1 - r0)); pbase++)
          {
            struct ll_list list;
            struct element **elems;
            struct ll **elemp;
            int *values;

            unsigned int pattern = pbase << r0;
            int i, j;
            int first_false;
            struct ll *part_ll;

            allocate_ascending (cnt, &list, &elems, &elemp, &values);

            /* Check that ll_find_partition works okay in every
               case.  We use it after partitioning, too, but that
               only tests cases where it returns non-null. */
            for (i = r0; i < r1; i++)
              if (!(pattern & (1u << i)))
                break;
            j = i;
            for (; i < r1; i++)
              if (pattern & (1u << i))
                break;
            part_ll = ll_find_partition (elemp[r0], elemp[r1],
                                         pattern_pred,
                                         &pattern);
            if (i == r1)
              check (part_ll == elemp[j]);
            else
              check (part_ll == NULL);

            /* Figure out expected results. */
            j = 0;
            first_false = -1;
            for (i = 0; i < r0; i++)
              values[j++] = i;
            for (i = r0; i < r1; i++)
              if (pattern & (1u << i))
                values[j++] = i;
            for (i = r0; i < r1; i++)
              if (!(pattern & (1u << i)))
                {
                  if (first_false == -1)
                    first_false = i;
                  values[j++] = i;
                }
            if (first_false == -1)
              first_false = r1;
            for (i = r1; i < cnt; i++)
              values[j++] = i;
            check (j == cnt);

            /* Partition and check for expected results. */
            check (ll_partition (elemp[r0], elemp[r1],
                                 pattern_pred, &pattern)
                   == elemp[first_false]);
            check (ll_find_partition (elemp[r0], elemp[r1],
                                      pattern_pred, &pattern)
                   == elemp[first_false]);
            check_list_contents (&list, values, cnt);
            check ((int) ll_count (&list) == cnt);

            free_elements (cnt, elems, elemp, values);
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
      "push-pop",
      "push/pop",
      test_push_pop
    },
    {
      "insert-remove",
      "insert/remove",
      test_insert_remove
    },
    {
      "swap",
      "swap",
      test_swap
    },
    {
      "swap-range",
      "swap_range",
      test_swap_range
    },
    {
      "remove-range",
      "remove_range",
      test_remove_range
    },
    {
      "remove-equal",
      "remove_equal",
      test_remove_equal
    },
    {
      "remove-if",
      "remove_if",
      test_remove_if
    },
    {
      "moved",
      "moved",
      test_moved
    },
    {
      "find-equal",
      "find_equal",
      test_find_equal
    },
    {
      "find-if",
      "find_if",
      test_find_if
    },
    {
      "find-adjacent-equal",
      "find_adjacent_equal",
      test_find_adjacent_equal
    },
    {
      "count-range",
      "count_range",
      test_count_range
    },
    {
      "count-equal",
      "count_equal",
      test_count_equal
    },
    {
      "count-if",
      "count_if",
      test_count_if
    },
    {
      "min-max",
      "min/max",
      test_min_max
    },
    {
      "lexicographical-compare-3way",
      "lexicographical_compare_3way",
      test_lexicographical_compare_3way
    },
    {
      "apply",
      "apply",
      test_apply
    },
    {
      "reverse",
      "reverse",
      test_reverse
    },
    {
      "permutations-no-dups",
      "permutations (no dups)",
      test_permutations_no_dups
    },
    {
      "permutations-with-dups",
      "permutations (with dups)",
      test_permutations_with_dups
    },
    {
      "merge-no-dups",
      "merge (no dups)",
      test_merge_no_dups
    },
    {
      "merge-with-dups",
      "merge (with dups)",
      test_merge_with_dups
    },
    {
      "sort-exhaustive",
      "sort (exhaustive)",
      test_sort_exhaustive
    },
    {
      "sort-stable",
      "sort (stability)",
      test_sort_stable
    },
    {
      "sort-subset",
      "sort (subset)",
      test_sort_subset
    },
    {
      "sort-big",
      "sort (big)",
      test_sort_big
    },
    {
      "unique",
      "unique",
      test_unique
    },
    {
      "sort-unique",
      "sort_unique",
      test_sort_unique
    },
    {
      "insert-ordered",
      "insert_ordered",
      test_insert_ordered
    },
    {
      "partition",
      "partition",
      test_partition
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
      printf ("%s: test doubly linked list (ll) library\n"
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
