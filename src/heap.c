/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include "heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#if STANDALONE
#define GLOBAL_DEBUGGING 1
#define _(x) (x)
#endif

/* Creates and returns a heap with an initial capacity of M_ELEM
   elements.  Returns nonzero only if successful. */
struct heap *
heap_create (size_t m_elem)
{
  struct heap *h = malloc (sizeof *h);
  if (h != NULL)
    {
      h->n_elem = 0;
      h->m_elem = m_elem;
      h->elem = malloc (h->m_elem * sizeof *h->elem);
      if (h->elem == NULL)
	{
	  free (h);
	  h = NULL;
	}
    }
  return h;
}

/* Destroys the heap at *H. */
void
heap_destroy (struct heap *h)
{
  assert (h != NULL);
  free (h->elem);
  free (h);
}

/* Inserts into heap *H an element having index INDEX and key KEY.
   Returns nonzero only if successful. */
int
heap_insert (struct heap *h, int index, int key)
{
  int i, j;

  assert (h != NULL);
  if (h->n_elem >= h->m_elem)
    {
      h->elem = realloc (h->elem, 2 * h->m_elem * sizeof *h->elem);
      if (h->elem == NULL)
	return 0;
      h->m_elem *= 2;
    }

  /* Knuth's Algorithm 5.2.3-16.  Step 1. */
  j = h->n_elem + 1;

  for (;;)
    {
      /* Step 2. */
      i = j / 2;

      /* Step 3. */
      if (i == 0 || h->elem[i - 1].key <= key)
	{
	  h->elem[j - 1].index = index;
	  h->elem[j - 1].key = key;
	  h->n_elem++;
	  return 1;
	}

      /* Step 4. */
      h->elem[j - 1] = h->elem[i - 1];
      j = i;
    }
}

/* Deletes the first element in the heap (the one with the greatest
   index) and returns its index, or -1 if the heap is empty.  If KEY
   is non-NULL then *KEY is set to the deleted element's key, if it
   returns non-NULL. */
int
heap_delete (struct heap *h, int *key)
{
  /* Knuth's Algorithm 5.2.3H-19. */
  int first, K, R, l, r, i, j;

  if (h->n_elem == 0)
    return -1;
  first = h->elem[0].index;
  if (key)
    *key = h->elem[0].key;
  K = h->elem[h->n_elem - 1].key;
  R = h->elem[h->n_elem - 1].index;
  l = 1;
  r = h->n_elem - 1;

  /* H3. */
  j = 1;

H4:
  i = j;
  j *= 2;
  if (j == r)
    goto H6;
  else if (j > r)
    goto H8;

  /* H5. */
  if (h->elem[j - 1].key > h->elem[j].key)
    j++;

H6:
  if (K <= h->elem[j - 1].key)
    goto H8;

  /* H7. */
  h->elem[i - 1] = h->elem[j - 1];
  goto H4;

H8:
  h->elem[i - 1].key = K;
  h->elem[i - 1].index = R;

  h->n_elem--;
  return first;
}

/* Returns the number of elements in heap H. */
int
heap_size (struct heap *h)
{
  return h->n_elem;
}

#if GLOBAL_DEBUGGING
/* Checks that a heap is really a heap. */
void
heap_verify (const struct heap *h)
{
  size_t j;

  for (j = 1; j <= h->n_elem; j++)
    {
      if (j / 2 >= 1 && h->elem[j / 2 - 1].key > h->elem[j - 1].key)
	printf (_("bad ordering of keys %d and %d\n"), j / 2 - 1, j - 1);
    }
}

/* Dumps out the heap on stdout. */
void
heap_dump (const struct heap *h)
{
  size_t j;

  printf (_("Heap contents:\n"));
  for (j = 1; j <= h->n_elem; j++)
    {
      int partner;
      if (j / 2 >= 1)
	partner = h->elem[j / 2 - 1].key;
      else
	partner = -1;
      printf ("%6d-%5d", h->elem[j - 1].key, partner);
    }
}
#endif /* GLOBAL_DEBUGGING */

#if STANDALONE
#include <time.h>

/* To perform a fairly thorough test of the heap routines, define
   STANDALONE to nonzero then compile this file by itself. */

/* Compares the second elements of the integer arrays at _A and _B and
   returns a strcmp()-type result. */
int
compare_int2 (const void *pa, const void *pb)
{
  int *a = (int *) pa;
  int *b = (int *) pb;

  return a[1] - b[1];
}

#define N_ELEM 16

/* Arrange the N elements of ARRAY in random order. */
void
shuffle (int (*array)[2], int n)
{
  int i;
  
  for (i = 0; i < n; i++)
    {
      int j = i + rand () % (n - i);
      int t = array[j][0], s = array[j][1];
      array[j][0] = array[i][0], array[j][1] = array[i][1];
      array[i][0] = t, array[i][1] = s;
    }
}

/* Test routine. */
int
main (void)
{
  struct heap *h;
  int i;
  int array[N_ELEM][2];

  srand (time (0));

  h = heap_create (16);
  for (i = 0; i < N_ELEM; i++)
    {
      array[i][0] = i;
      array[i][1] = N_ELEM - i - 1;
    }
  shuffle (array, N_ELEM);

  printf ("Insertion order:\n");
  for (i = 0; i < N_ELEM; i++)
    {
      printf ("(%d,%d) ", array[i][0], array[i][1]);
      heap_insert (h, array[i][0], array[i][1]);
      heap_verify (h);
    }
  putchar ('\n');

  /*heap_dump(&h); */

  printf ("\nDeletion order:\n");
  for (i = 0; i < N_ELEM; i++)
    {
      int index, key;
      index = heap_delete (h, &key);
      assert (index != -1);
      printf ("(%d,%d) ", index, key);
      fflush (stdout);
      assert (index == N_ELEM - i - 1 && key == i);
      heap_verify (h);
    }
  putchar ('\n');
  heap_destroy (h);

  return 0;
}
#endif
