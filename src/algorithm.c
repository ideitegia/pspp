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

/* Copyright (C) 2001 Free Software Foundation, Inc.
  
   This file is part of the GNU ISO C++ Library.  This library is free
   software; you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option)
   any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this library; see the file COPYING.  If not, write to the Free
   Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
   USA.

   As a special exception, you may use this file as part of a free software
   library without restriction.  Specifically, if other files instantiate
   templates or use macros or inline functions from this file, or you compile
   this file and link it with other files to produce an executable, this
   file does not by itself cause the resulting executable to be covered by
   the GNU General Public License.  This exception does not however
   invalidate any other reasons why the executable file might be covered by
   the GNU General Public License. */

/*
 *
 * Copyright (c) 1994
 * Hewlett-Packard Company
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Hewlett-Packard Company makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 *
 * Copyright (c) 1996
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

/* Copyright (C) 1991, 1992, 1996, 1997, 1999 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Written by Douglas C. Schmidt (schmidt@ics.uci.edu).

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#include <config.h>
#include "algorithm.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "alloc.h"
#include "random.h"

/* Finds an element in ARRAY, which contains COUNT elements of
   SIZE bytes each, using COMPARE for comparisons.  Returns the
   first element in ARRAY that matches TARGET, or a null pointer
   on failure.  AUX is passed to each comparison as auxiliary
   data. */
void *find (const void *array, size_t count, size_t size,
            const void *target,
            algo_compare_func *compare, void *aux) 
{
  const unsigned char *element = array;

  while (count-- > 0) 
    {
      if (compare (target, element, aux) == 0)
        return (void *) element;

      element += size;
    }

  return NULL;
}

/* Byte-wise swap two items of size SIZE. */
#define SWAP(a, b, size)                        \
  do                                            \
    {                                           \
      register size_t __size = (size);          \
      register char *__a = (a), *__b = (b);     \
      do                                        \
	{                                       \
	  char __tmp = *__a;                    \
	  *__a++ = *__b;                        \
	  *__b++ = __tmp;                       \
	} while (--__size > 0);                 \
    } while (0)

/* Makes the elements in ARRAY unique, by moving up duplicates,
   and returns the new number of elements in the array.  Sorted
   arrays only.  Arguments same as for sort() above. */
size_t
unique (void *array, size_t count, size_t size,
        algo_compare_func *compare, void *aux) 
{
  char *first = array;
  char *last = first + size * count;
  char *result = array;

  for (;;) 
    {
      first += size;
      if (first >= last)
        return count;

      if (compare (result, first, aux)) 
        {
          result += size;
          if (result != first)
            memcpy (result, first, size);
        }
      else 
        count--;
    }
}

/* Helper function that calls sort(), then unique(). */
size_t
sort_unique (void *array, size_t count, size_t size,
             algo_compare_func *compare, void *aux) 
{
  sort (array, count, size, compare, aux);
  return unique (array, count, size, compare, aux);
}

/* Reorders ARRAY, which contains COUNT elements of SIZE bytes
   each, so that the elements for which PREDICATE returns nonzero
   precede those for which PREDICATE returns zero.  AUX is
   passed to each predicate as auxiliary data.  Returns the
   number of elements for which PREDICATE returns nonzero.  Not
   stable. */
size_t 
partition (void *array, size_t count, size_t size,
           algo_predicate_func *predicate, void *aux) 
{
  char *first = array;
  char *last = first + count * size;

  for (;;)
    {
      /* Move FIRST forward to point to first element that fails
         PREDICATE. */
      for (;;) 
        {
          if (first == last)
            return count;
          else if (!predicate (first, aux)) 
            break;

          first += size; 
        }
      count--;

      /* Move LAST backward to point to last element that passes
         PREDICATE. */
      for (;;) 
        {
          last -= size;

          if (first == last)
            return count;
          else if (predicate (last, aux)) 
            break;
          else
            count--;
        }
      
      /* By swapping FIRST and LAST we extend the starting and
         ending sequences that pass and fail, respectively,
         PREDICATE. */
      SWAP (first, last, size);
      first += size;
    }
}

/* A algo_random_func that uses random.h. */
unsigned
algo_default_random (unsigned max, void *aux unused) 
{
  return rng_get_unsigned (pspp_rng ()) % max;
}

/* Randomly reorders ARRAY, which contains COUNT elements of SIZE
   bytes each.  Uses RANDOM as a source of random data, passing
   AUX as the auxiliary data.  RANDOM may be null to use a
   default random source. */
void
random_shuffle (void *array_, size_t count, size_t size,
                algo_random_func *random, void *aux)
{
  unsigned char *array = array_;
  int i;

  if (random == NULL)
    random = algo_default_random;

  for (i = 1; i < count; i++)
    SWAP (array + i * size, array + random (i + 1, aux) * size, size);
}

/* Copies the COUNT elements of SIZE bytes each from ARRAY to
   RESULT, except that elements for which PREDICATE is false are
   not copied.  Returns the number of elements copied.  AUX is
   passed to PREDICATE as auxiliary data.  */
size_t 
copy_if (const void *array, size_t count, size_t size,
         void *result,
         algo_predicate_func *predicate, void *aux) 
{
  const unsigned char *input = array;
  const unsigned char *last = input + size * count;
  unsigned char *output = result;
  
  while (input < last)
    {
      if (predicate (input, aux)) 
        {
          memcpy (output, input, size);
          output += size;
        }
      else
        count--;

      input += size;
    }

  return count;
}

/* A predicate and its auxiliary data. */
struct pred_aux 
  {
    algo_predicate_func *predicate;
    void *aux;
  };

static int
not (const void *data, void *pred_aux_) 
{
  const struct pred_aux *pred_aux = pred_aux_;

  return !pred_aux->predicate (data, pred_aux->aux);
}

/* Removes elements equal to ELEMENT from ARRAY, which consists
   of COUNT elements of SIZE bytes each.  Returns the number of
   remaining elements.  AUX is passed to COMPARE as auxiliary
   data. */
size_t
remove_equal (void *array, size_t count, size_t size,
              void *element,
              algo_compare_func *compare, void *aux) 
{
  unsigned char *first = array;
  unsigned char *last = first + count * size;
  unsigned char *result;

  for (;;)
    {
      if (first >= last)
        return count;
      if (compare (first, element, aux) == 0)
        break;

      first += size;
    }

  result = first;
  count--;
  for (;;) 
    {
      first += size;
      if (first >= last)
        return count;

      if (compare (first, element, aux) == 0) 
        {
          count--; 
          continue;
        }
      
      memcpy (result, first, size);
      result += size;
    }

  return count;
}

/* Copies the COUNT elements of SIZE bytes each from ARRAY to
   RESULT, except that elements for which PREDICATE is true are
   not copied.  Returns the number of elements copied.  AUX is
   passed to PREDICATE as auxiliary data.  */
size_t 
remove_copy_if (const void *array, size_t count, size_t size,
                void *result,
                algo_predicate_func *predicate, void *aux) 
{
  struct pred_aux pred_aux;
  pred_aux.predicate = predicate;
  pred_aux.aux = aux;
  return copy_if (array, count, size, result, not, &pred_aux);
}

/* Searches ARRAY, which contains COUNT of SIZE bytes each, using
   a binary search.  Returns any element that equals VALUE, if
   one exists, or a null pointer otherwise.  ARRAY must ordered
   according to COMPARE.  AUX is passed to COMPARE as auxiliary
   data. */
void *
binary_search (const void *array, size_t count, size_t size,
               void *value,
               algo_compare_func *compare, void *aux) 
{
  assert (array != NULL);
  assert (count <= INT_MAX);
  assert (compare != NULL);

  if (count != 0) 
    {
      const unsigned char *first = array;
      int low = 0;
      int high = count - 1;

      while (low <= high) 
        {
          int middle = (low + high) / 2;
          const unsigned char *element = first + middle * size;
          int cmp = compare (value, element, aux);

          if (cmp > 0) 
            low = middle + 1;
          else if (cmp < 0)
            high = middle - 1;
          else
            return (void *) element;
        }
    }
  return NULL;
}

/* Lexicographically compares ARRAY1, which contains COUNT1
   elements of SIZE bytes each, to ARRAY2, which contains COUNT2
   elements of SIZE bytes, according to COMPARE.  Returns a
   strcmp()-type result.  AUX is passed to COMPARE as auxiliary
   data. */
int
lexicographical_compare (const void *array1, size_t count1,
                         const void *array2, size_t count2,
                         size_t size,
                         algo_compare_func *compare, void *aux) 
{
  const unsigned char *first1 = array1;
  const unsigned char *first2 = array2;
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

/* If you consider tuning this algorithm, you should consult first:
   Engineering a sort function; Jon Bentley and M. Douglas McIlroy;
   Software - Practice and Experience; Vol. 23 (11), 1249-1265, 1993.  */

#include <alloca.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Discontinue quicksort algorithm when partition gets below this size.
   This particular magic number was chosen to work best on a Sun 4/260. */
#define MAX_THRESH 4

/* Stack node declarations used to store unfulfilled partition obligations. */
typedef struct
  {
    char *lo;
    char *hi;
  } stack_node;

/* The next 4 #defines implement a very fast in-line stack abstraction. */
/* The stack needs log (total_elements) entries (we could even subtract
   log(MAX_THRESH)).  Since total_elements has type size_t, we get as
   upper bound for log (total_elements):
   bits per byte (CHAR_BIT) * sizeof(size_t).  */
#define STACK_SIZE	(CHAR_BIT * sizeof(size_t))
#define PUSH(low, high)	((void) ((top->lo = (low)), (top->hi = (high)), ++top))
#define	POP(low, high)	((void) (--top, (low = top->lo), (high = top->hi)))
#define	STACK_NOT_EMPTY	(stack < top)


/* Order size using quicksort.  This implementation incorporates
   four optimizations discussed in Sedgewick:

   1. Non-recursive, using an explicit stack of pointer that store the
      next array partition to sort.  To save time, this maximum amount
      of space required to store an array of SIZE_MAX is allocated on the
      stack.  Assuming a 32-bit (64 bit) integer for size_t, this needs
      only 32 * sizeof(stack_node) == 256 bytes (for 64 bit: 1024 bytes).
      Pretty cheap, actually.

   2. Chose the pivot element using a median-of-three decision tree.
      This reduces the probability of selecting a bad pivot value and
      eliminates certain extraneous comparisons.

   3. Only quicksorts TOTAL_ELEMS / MAX_THRESH partitions, leaving
      insertion sort to order the MAX_THRESH items within each partition.
      This is a big win, since insertion sort is faster for small, mostly
      sorted array segments.

   4. The larger of the two sub-partitions is always pushed onto the
      stack first, with the algorithm then concentrating on the
      smaller partition.  This *guarantees* no more than log (total_elems)
      stack size is needed (actually O(1) in this case)!  */

void
sort (void *const pbase, size_t total_elems, size_t size,
      algo_compare_func *cmp, void *aux)
{
  register char *base_ptr = (char *) pbase;

  const size_t max_thresh = MAX_THRESH * size;

  if (total_elems == 0)
    /* Avoid lossage with unsigned arithmetic below.  */
    return;

  if (total_elems > MAX_THRESH)
    {
      char *lo = base_ptr;
      char *hi = &lo[size * (total_elems - 1)];
      stack_node stack[STACK_SIZE];
      stack_node *top = stack + 1;

      while (STACK_NOT_EMPTY)
        {
          char *left_ptr;
          char *right_ptr;

	  /* Select median value from among LO, MID, and HI. Rearrange
	     LO and HI so the three values are sorted. This lowers the
	     probability of picking a pathological pivot value and
	     skips a comparison for both the LEFT_PTR and RIGHT_PTR in
	     the while loops. */

	  char *mid = lo + size * ((hi - lo) / size >> 1);

	  if ((*cmp) ((void *) mid, (void *) lo, aux) < 0)
	    SWAP (mid, lo, size);
	  if ((*cmp) ((void *) hi, (void *) mid, aux) < 0)
	    SWAP (mid, hi, size);
	  else
	    goto jump_over;
	  if ((*cmp) ((void *) mid, (void *) lo, aux) < 0)
	    SWAP (mid, lo, size);
	jump_over:;

	  left_ptr  = lo + size;
	  right_ptr = hi - size;

	  /* Here's the famous ``collapse the walls'' section of quicksort.
	     Gotta like those tight inner loops!  They are the main reason
	     that this algorithm runs much faster than others. */
	  do
	    {
	      while ((*cmp) ((void *) left_ptr, (void *) mid, aux) < 0)
		left_ptr += size;

	      while ((*cmp) ((void *) mid, (void *) right_ptr, aux) < 0)
		right_ptr -= size;

	      if (left_ptr < right_ptr)
		{
		  SWAP (left_ptr, right_ptr, size);
		  if (mid == left_ptr)
		    mid = right_ptr;
		  else if (mid == right_ptr)
		    mid = left_ptr;
		  left_ptr += size;
		  right_ptr -= size;
		}
	      else if (left_ptr == right_ptr)
		{
		  left_ptr += size;
		  right_ptr -= size;
		  break;
		}
	    }
	  while (left_ptr <= right_ptr);

          /* Set up pointers for next iteration.  First determine whether
             left and right partitions are below the threshold size.  If so,
             ignore one or both.  Otherwise, push the larger partition's
             bounds on the stack and continue sorting the smaller one. */

          if ((size_t) (right_ptr - lo) <= max_thresh)
            {
              if ((size_t) (hi - left_ptr) <= max_thresh)
		/* Ignore both small partitions. */
                POP (lo, hi);
              else
		/* Ignore small left partition. */
                lo = left_ptr;
            }
          else if ((size_t) (hi - left_ptr) <= max_thresh)
	    /* Ignore small right partition. */
            hi = right_ptr;
          else if ((right_ptr - lo) > (hi - left_ptr))
            {
	      /* Push larger left partition indices. */
              PUSH (lo, right_ptr);
              lo = left_ptr;
            }
          else
            {
	      /* Push larger right partition indices. */
              PUSH (left_ptr, hi);
              hi = right_ptr;
            }
        }
    }

  /* Once the BASE_PTR array is partially sorted by quicksort the rest
     is completely sorted using insertion sort, since this is efficient
     for partitions below MAX_THRESH size. BASE_PTR points to the beginning
     of the array to sort, and END_PTR points at the very last element in
     the array (*not* one beyond it!). */

#define min(x, y) ((x) < (y) ? (x) : (y))

  {
    char *const end_ptr = &base_ptr[size * (total_elems - 1)];
    char *tmp_ptr = base_ptr;
    char *thresh = min(end_ptr, base_ptr + max_thresh);
    register char *run_ptr;

    /* Find smallest element in first threshold and place it at the
       array's beginning.  This is the smallest array element,
       and the operation speeds up insertion sort's inner loop. */

    for (run_ptr = tmp_ptr + size; run_ptr <= thresh; run_ptr += size)
      if ((*cmp) ((void *) run_ptr, (void *) tmp_ptr, aux) < 0)
        tmp_ptr = run_ptr;

    if (tmp_ptr != base_ptr)
      SWAP (tmp_ptr, base_ptr, size);

    /* Insertion sort, running from left-hand-side up to right-hand-side.  */

    run_ptr = base_ptr + size;
    while ((run_ptr += size) <= end_ptr)
      {
	tmp_ptr = run_ptr - size;
	while ((*cmp) ((void *) run_ptr, (void *) tmp_ptr, aux) < 0)
	  tmp_ptr -= size;

	tmp_ptr += size;
        if (tmp_ptr != run_ptr)
          {
            char *trav;

	    trav = run_ptr + size;
	    while (--trav >= run_ptr)
              {
                char c = *trav;
                char *hi, *lo;

                for (hi = lo = trav; (lo -= size) >= tmp_ptr; hi = lo)
                  *hi = *lo;
                *hi = c;
              }
          }
      }
  }
}

/* Computes the generalized set difference, ARRAY1 minus ARRAY2,
   into RESULT, and returns the number of elements written to
   RESULT.  If a value appears M times in ARRAY1 and N times in
   ARRAY2, then it will appear max(M - N, 0) in RESULT.  ARRAY1
   and ARRAY2 must be sorted, and RESULT is sorted and stable.
   ARRAY1 consists of COUNT1 elements, ARRAY2 of COUNT2 elements,
   each SIZE bytes.  AUX is passed to COMPARE as auxiliary
   data. */
size_t set_difference (const void *array1, size_t count1,
                       const void *array2, size_t count2,
                       size_t size,
                       void *result_,
                       algo_compare_func *compare, void *aux) 
{
  const unsigned char *first1 = array1;
  const unsigned char *last1 = first1 + count1 * size;
  const unsigned char *first2 = array2;
  const unsigned char *last2 = first2 + count2 * size;
  unsigned char *result = result_;
  size_t result_count = 0;
  
  while (first1 != last1 && first2 != last2) 
    {
      int cmp = compare (first1, first2, aux);
      if (cmp < 0)
        {
          memcpy (result, first1, size);
          first1 += size;
          result += size;
          result_count++;
        }
      else if (cmp > 0)
        first2 += size;
      else
        {
          first1 += size;
          first2 += size;
        }
    }

  while (first1 != last1) 
    {
      memcpy (result, first1, size);
      first1 += size;
      result += size;
      result_count++;
    }

  return result_count;
}

/* Finds the first pair of adjacent equal elements in ARRAY,
   which has COUNT elements of SIZE bytes.  Returns the first
   element in ARRAY such that COMPARE returns zero when it and
   its successor element are compared, or a null pointer if no
   such element exists.  AUX is passed to COMPARE as auxiliary
   data. */
void *
adjacent_find_equal (const void *array, size_t count, size_t size,
                     algo_compare_func *compare, void *aux) 
{
  const unsigned char *first = array;
  const unsigned char *last = first + count * size;

  while (first < last && first + size < last) 
    {
      if (compare (first, first + size, aux) == 0)
        return (void *) first;
      first += size;
    }

  return NULL;
}

