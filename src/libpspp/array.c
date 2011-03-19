/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011 Free Software Foundation, Inc.

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

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

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
   License along with the GNU C Library.  If not, see 
   <http://www.gnu.org/licenses/>. */

#include <config.h>

#include "array.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "libpspp/assertion.h"

#include "gl/xalloc.h"
#include "gl/minmax.h"

/* Finds an element in ARRAY, which contains COUNT elements of
   SIZE bytes each, using COMPARE for comparisons.  Returns the
   first element in ARRAY that matches TARGET, or a null pointer
   on failure.  AUX is passed to each comparison as auxiliary
   data. */
void *
find (const void *array, size_t count, size_t size,
      const void *target,
      algo_compare_func *compare, const void *aux)
{
  const char *element = array;

  while (count-- > 0)
    {
      if (compare (target, element, aux) == 0)
        return (void *) element;

      element += size;
    }

  return NULL;
}

/* Counts and return the number of elements in ARRAY, which
   contains COUNT elements of SIZE bytes each, which are equal to
   ELEMENT as compared with COMPARE.  AUX is passed as auxiliary
   data to COMPARE. */
size_t
count_equal (const void *array, size_t count, size_t size,
             const void *element,
             algo_compare_func *compare, const void *aux)
{
  const char *first = array;
  size_t equal_cnt = 0;

  while (count-- > 0)
    {
      if (compare (element, first, aux) == 0)
        equal_cnt++;

      first += size;
    }

  return equal_cnt;
}

/* Counts and return the number of elements in ARRAY, which
   contains COUNT elements of SIZE bytes each, for which
   PREDICATE returns true.  AUX is passed as auxiliary data to
   PREDICATE. */
size_t
count_if (const void *array, size_t count, size_t size,
          algo_predicate_func *predicate, const void *aux)
{
  const char *first = array;
  size_t true_cnt = 0;

  while (count-- > 0)
    {
      if (predicate (first, aux) != 0)
        true_cnt++;

      first += size;
    }

  return true_cnt;
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
        algo_compare_func *compare, const void *aux)
{
  char *first = array;
  char *last = first + size * count;
  char *result = array;

  for (;;)
    {
      first += size;
      if (first >= last)
        {
          assert (adjacent_find_equal (array, count,
                                       size, compare, aux) == NULL);
          return count;
        }

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
             algo_compare_func *compare, const void *aux)
{
  sort (array, count, size, compare, aux);
  return unique (array, count, size, compare, aux);
}

/* Reorders ARRAY, which contains COUNT elements of SIZE bytes
   each, so that the elements for which PREDICATE returns true
   precede those for which PREDICATE returns zero.  AUX is
   passed to each predicate as auxiliary data.  Returns the
   number of elements for which PREDICATE returns true.  Not
   stable. */
size_t
partition (void *array, size_t count, size_t size,
           algo_predicate_func *predicate, const void *aux)
{
  size_t true_cnt = count;
  char *first = array;
  char *last = first + true_cnt * size;

  for (;;)
    {
      /* Move FIRST forward to point to first element that fails
         PREDICATE. */
      for (;;)
        {
          if (first == last)
            goto done;
          else if (!predicate (first, aux))
            break;

          first += size;
        }
      true_cnt--;

      /* Move LAST backward to point to last element that passes
         PREDICATE. */
      for (;;)
        {
          last -= size;

          if (first == last)
            goto done;
          else if (predicate (last, aux))
            break;
          else
            true_cnt--;
        }

      /* By swapping FIRST and LAST we extend the starting and
         ending sequences that pass and fail, respectively,
         PREDICATE. */
      SWAP (first, last, size);
      first += size;
    }

 done:
  assert (is_partitioned (array, count, size, true_cnt, predicate, aux));
  return true_cnt;
}

/* Checks whether ARRAY, which contains COUNT elements of SIZE
   bytes each, is partitioned such that PREDICATE returns true
   for the first TRUE_CNT elements and zero for the remaining
   elements.  AUX is passed as auxiliary data to PREDICATE. */
bool
is_partitioned (const void *array, size_t count, size_t size,
                size_t true_cnt,
                algo_predicate_func *predicate, const void *aux)
{
  const char *first = array;
  size_t idx;

  assert (true_cnt <= count);
  for (idx = 0; idx < true_cnt; idx++)
    if (predicate (first + idx * size, aux) == 0)
      return false;
  for (idx = true_cnt; idx < count; idx++)
    if (predicate (first + idx * size, aux) != 0)
      return false;
  return true;
}

/* Copies the COUNT elements of SIZE bytes each from ARRAY to
   RESULT, except that elements for which PREDICATE is false are
   not copied.  Returns the number of elements copied.  AUX is
   passed to PREDICATE as auxiliary data.  */
size_t
copy_if (const void *array, size_t count, size_t size,
         void *result,
         algo_predicate_func *predicate, const void *aux)
{
  const char *input = array;
  const char *last = input + size * count;
  char *output = result;
  size_t nonzero_cnt = 0;

  while (input < last)
    {
      if (predicate (input, aux))
        {
          memcpy (output, input, size);
          output += size;
          nonzero_cnt++;
        }

      input += size;
    }

  assert (nonzero_cnt == count_if (array, count, size, predicate, aux));
  assert (nonzero_cnt == count_if (result, nonzero_cnt, size, predicate, aux));

  return nonzero_cnt;
}

/* Removes N elements starting at IDX from ARRAY, which consists
   of COUNT elements of SIZE bytes each, by shifting the elements
   following them, if any, into its position. */
void
remove_range (void *array_, size_t count, size_t size,
              size_t idx, size_t n)
{
  char *array = array_;

  assert (array != NULL);
  assert (idx <= count);
  assert (idx + n <= count);

  if (idx + n < count)
    memmove (array + idx * size, array + (idx + n) * size,
             size * (count - idx - n));
}

/* Removes element IDX from ARRAY, which consists of COUNT
   elements of SIZE bytes each, by shifting the elements
   following it, if any, into its position. */
void
remove_element (void *array, size_t count, size_t size,
                size_t idx)
{
  remove_range (array, count, size, idx, 1);
}

/* Makes room for N elements starting at IDX in ARRAY, which
   initially consists of COUNT elements of SIZE bytes each, by
   shifting elements IDX...COUNT (exclusive) to the right by N
   positions. */
void
insert_range (void *array_, size_t count, size_t size,
              size_t idx, size_t n)
{
  char *array = array_;

  assert (idx <= count);
  memmove (array + (idx + n) * size, array + idx * size, (count - idx) * size);
}

/* Makes room for a new element at IDX in ARRAY, which initially
   consists of COUNT elements of SIZE bytes each, by shifting
   elements IDX...COUNT (exclusive) to the right by one
   position. */
void
insert_element (void *array, size_t count, size_t size,
                size_t idx)
{
  insert_range (array, count, size, idx, 1);
}

/* Moves an element in ARRAY, which consists of COUNT elements of
   SIZE bytes each, from OLD_IDX to NEW_IDX, shifting around
   other elements as needed.  Runs in O(abs(OLD_IDX - NEW_IDX))
   time. */
void
move_element (void *array_, size_t count, size_t size,
              size_t old_idx, size_t new_idx)
{
  assert (array_ != NULL || count == 0);
  assert (old_idx < count);
  assert (new_idx < count);

  if (old_idx != new_idx)
    {
      char *array = array_;
      char *element = xmalloc (size);
      char *new = array + new_idx * size;
      char *old = array + old_idx * size;

      memcpy (element, old, size);
      if (new < old)
        memmove (new + size, new, (old_idx - new_idx) * size);
      else
        memmove (old, old + size, (new_idx - old_idx) * size);
      memcpy (new, element, size);

      free (element);
    }
}

/* Moves N elements in ARRAY starting at OLD_IDX, which consists
   of COUNT elements of SIZE bytes each, so that they now start
   at NEW_IDX, shifting around other elements as needed. */
void
move_range (void *array_, size_t count, size_t size,
            size_t old_idx, size_t new_idx, size_t n)
{
  assert (array_ != NULL || count == 0);
  assert (n <= count);
  assert (old_idx + n <= count);
  assert (new_idx + n <= count);

  if (old_idx != new_idx && n > 0)
    {
      char *array = array_;
      char *range = xmalloc (size * n);
      char *new = array + new_idx * size;
      char *old = array + old_idx * size;

      memcpy (range, old, size * n);
      if (new < old)
        memmove (new + size * n, new, (old_idx - new_idx) * size);
      else
        memmove (old, old + size * n, (new_idx - old_idx) * size);
      memcpy (new, range, size * n);

      free (range);
    }
}

/* A predicate and its auxiliary data. */
struct pred_aux
  {
    algo_predicate_func *predicate;
    const void *aux;
  };

static bool
not (const void *data, const void *pred_aux_)
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
              algo_compare_func *compare, const void *aux)
{
  char *first = array;
  char *last = first + count * size;
  char *result;

  for (;;)
    {
      if (first >= last)
        goto done;
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
        goto done;

      if (compare (first, element, aux) == 0)
        {
          count--;
          continue;
        }

      memcpy (result, first, size);
      result += size;
    }

 done:
  assert (count_equal (array, count, size, element, compare, aux) == 0);
  return count;
}

/* Copies the COUNT elements of SIZE bytes each from ARRAY to
   RESULT, except that elements for which PREDICATE is true are
   not copied.  Returns the number of elements copied.  AUX is
   passed to PREDICATE as auxiliary data.  */
size_t
remove_copy_if (const void *array, size_t count, size_t size,
                void *result,
                algo_predicate_func *predicate, const void *aux)
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
               algo_compare_func *compare, const void *aux)
{
  assert (array != NULL || count == 0);
  assert (count <= INT_MAX);
  assert (compare != NULL);

  if (count != 0)
    {
      const char *first = array;
      int low = 0;
      int high = count - 1;

      while (low <= high)
        {
          int middle = (low + high) / 2;
          const char *element = first + middle * size;
          int cmp = compare (value, element, aux);

          if (cmp > 0)
            low = middle + 1;
          else if (cmp < 0)
            high = middle - 1;
          else
            return (void *) element;
        }
    }

  expensive_assert (find (array, count, size, value, compare, aux) == NULL);
  return NULL;
}

/* Lexicographically compares ARRAY1, which contains COUNT1
   elements of SIZE bytes each, to ARRAY2, which contains COUNT2
   elements of SIZE bytes, according to COMPARE.  Returns a
   strcmp()-type result.  AUX is passed to COMPARE as auxiliary
   data. */
int
lexicographical_compare_3way (const void *array1, size_t count1,
                              const void *array2, size_t count2,
                              size_t size,
                              algo_compare_func *compare, const void *aux)
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

/* If you consider tuning this algorithm, you should consult first:
   Engineering a sort function; Jon Bentley and M. Douglas McIlroy;
   Software - Practice and Experience; Vol. 23 (11), 1249-1265, 1993.  */

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
sort (void *array, size_t count, size_t size,
      algo_compare_func *compare, const void *aux)
{
  char *const first = array;
  const size_t max_thresh = MAX_THRESH * size;

  if (count == 0)
    /* Avoid lossage with unsigned arithmetic below.  */
    return;

  if (count > MAX_THRESH)
    {
      char *lo = first;
      char *hi = &lo[size * (count - 1)];
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

	  if (compare (mid, lo, aux) < 0)
	    SWAP (mid, lo, size);
	  if (compare (hi, mid, aux) < 0)
	    SWAP (mid, hi, size);
	  else
	    goto jump_over;
	  if (compare (mid, lo, aux) < 0)
	    SWAP (mid, lo, size);
	jump_over:;

	  left_ptr  = lo + size;
	  right_ptr = hi - size;

	  /* Here's the famous ``collapse the walls'' section of quicksort.
	     Gotta like those tight inner loops!  They are the main reason
	     that this algorithm runs much faster than others. */
	  do
	    {
	      while (compare (left_ptr, mid, aux) < 0)
		left_ptr += size;

	      while (compare (mid, right_ptr, aux) < 0)
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

  /* Once the FIRST array is partially sorted by quicksort the rest
     is completely sorted using insertion sort, since this is efficient
     for partitions below MAX_THRESH size. FIRST points to the beginning
     of the array to sort, and END_PTR points at the very last element in
     the array (*not* one beyond it!). */

  {
    char *const end_ptr = &first[size * (count - 1)];
    char *tmp_ptr = first;
    char *thresh = MIN (end_ptr, first + max_thresh);
    register char *run_ptr;

    /* Find smallest element in first threshold and place it at the
       array's beginning.  This is the smallest array element,
       and the operation speeds up insertion sort's inner loop. */

    for (run_ptr = tmp_ptr + size; run_ptr <= thresh; run_ptr += size)
      if (compare (run_ptr, tmp_ptr, aux) < 0)
        tmp_ptr = run_ptr;

    if (tmp_ptr != first)
      SWAP (tmp_ptr, first, size);

    /* Insertion sort, running from left-hand-side up to right-hand-side.  */

    run_ptr = first + size;
    while ((run_ptr += size) <= end_ptr)
      {
	tmp_ptr = run_ptr - size;
	while (compare (run_ptr, tmp_ptr, aux) < 0)
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

  assert (is_sorted (array, count, size, compare, aux));
}

/* Tests whether ARRAY, which contains COUNT elements of SIZE
   bytes each, is sorted in order according to COMPARE.  AUX is
   passed to COMPARE as auxiliary data. */
bool
is_sorted (const void *array, size_t count, size_t size,
           algo_compare_func *compare, const void *aux)
{
  const char *first = array;
  size_t idx;

  for (idx = 0; idx + 1 < count; idx++)
    if (compare (first + idx * size, first + (idx + 1) * size, aux) > 0)
      return false;

  return true;
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
                       algo_compare_func *compare, const void *aux)
{
  const char *first1 = array1;
  const char *last1 = first1 + count1 * size;
  const char *first2 = array2;
  const char *last2 = first2 + count2 * size;
  char *result = result_;
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
                     algo_compare_func *compare, const void *aux)
{
  const char *first = array;
  const char *last = first + count * size;

  while (first < last && first + size < last)
    {
      if (compare (first, first + size, aux) == 0)
        return (void *) first;
      first += size;
    }

  return NULL;
}

/* ARRAY contains COUNT elements of SIZE bytes each.  Initially
   the first COUNT - 1 elements of these form a heap, followed by
   a single element not part of the heap.  This function adds the
   final element, forming a heap of COUNT elements in ARRAY.
   Uses COMPARE to compare elements, passing AUX as auxiliary
   data. */
void
push_heap (void *array, size_t count, size_t size,
           algo_compare_func *compare, const void *aux)
{
  char *first = array;
  size_t i;

  expensive_assert (count < 1 || is_heap (array, count - 1,
                                          size, compare, aux));
  for (i = count; i > 1; i /= 2)
    {
      char *parent = first + (i / 2 - 1) * size;
      char *element = first + (i - 1) * size;
      if (compare (parent, element, aux) < 0)
        SWAP (parent, element, size);
      else
        break;
    }
  expensive_assert (is_heap (array, count, size, compare, aux));
}

/* ARRAY contains COUNT elements of SIZE bytes each.  Initially
   the children of ARRAY[idx - 1] are heaps, but ARRAY[idx - 1]
   may be smaller than its children.  This function fixes that,
   so that ARRAY[idx - 1] itself is a heap.  Uses COMPARE to
   compare elements, passing AUX as auxiliary data. */
static void
heapify (void *array, size_t count, size_t size,
         size_t idx,
         algo_compare_func *compare, const void *aux)
{
  char *first = array;

  for (;;)
    {
      size_t left = 2 * idx;
      size_t right = left + 1;
      size_t largest = idx;

      if (left <= count
          && compare (first + size * (left - 1),
                      first + size * (idx - 1), aux) > 0)
        largest = left;

      if (right <= count
          && compare (first + size * (right - 1),
                      first + size * (largest - 1), aux) > 0)
        largest = right;

      if (largest == idx)
        break;

      SWAP (first + size * (idx - 1), first + size * (largest - 1), size);
      idx = largest;
    }
}

/* ARRAY contains COUNT elements of SIZE bytes each.  Initially
   all COUNT elements form a heap.  This function moves the
   largest element in the heap to the final position in ARRAY and
   reforms a heap of the remaining COUNT - 1 elements at the
   beginning of ARRAY.  Uses COMPARE to compare elements, passing
   AUX as auxiliary data. */
void
pop_heap (void *array, size_t count, size_t size,
          algo_compare_func *compare, const void *aux)
{
  char *first = array;

  expensive_assert (is_heap (array, count, size, compare, aux));
  SWAP (first, first + (count - 1) * size, size);
  heapify (first, count - 1, size, 1, compare, aux);
  expensive_assert (count < 1 || is_heap (array, count - 1,
                                          size, compare, aux));
}

/* Turns ARRAY, which contains COUNT elements of SIZE bytes, into
   a heap.  Uses COMPARE to compare elements, passing AUX as
   auxiliary data. */
void
make_heap (void *array, size_t count, size_t size,
           algo_compare_func *compare, const void *aux)
{
  size_t idx;

  for (idx = count / 2; idx >= 1; idx--)
    heapify (array, count, size, idx, compare, aux);
  expensive_assert (count < 1 || is_heap (array, count, size, compare, aux));
}

/* ARRAY contains COUNT elements of SIZE bytes each.  Initially
   all COUNT elements form a heap.  This function turns the heap
   into a fully sorted array.  Uses COMPARE to compare elements,
   passing AUX as auxiliary data. */
void
sort_heap (void *array, size_t count, size_t size,
           algo_compare_func *compare, const void *aux)
{
  char *first = array;
  size_t idx;

  expensive_assert (is_heap (array, count, size, compare, aux));
  for (idx = count; idx >= 2; idx--)
    {
      SWAP (first, first + (idx - 1) * size, size);
      heapify (array, idx - 1, size, 1, compare, aux);
    }
  expensive_assert (is_sorted (array, count, size, compare, aux));
}

/* ARRAY contains COUNT elements of SIZE bytes each.  This
   function tests whether ARRAY is a heap and returns true if so,
   false otherwise.  Uses COMPARE to compare elements, passing
   AUX as auxiliary data. */
bool
is_heap (const void *array, size_t count, size_t size,
         algo_compare_func *compare, const void *aux)
{
  const char *first = array;
  size_t child;

  for (child = 2; child <= count; child++)
    {
      size_t parent = child / 2;
      if (compare (first + (parent - 1) * size,
                   first + (child - 1) * size, aux) < 0)
        return false;
    }

  return true;
}

