/* PSPP - a program for statistical analysis.
   Copyright (C) 2009 Free Software Foundation, Inc.

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

/* Thanks to Rob van Son for writing the original version of this
   file.  This version has been completely rewritten; only the
   name of the function has been retained.  In the process of
   rewriting, it was sped up from O(2**N) to O(N**3). */

#include <config.h>
#include "wilcoxon-sig.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include "xalloc.h"

/* For integers N and W, with 0 <= N < CHAR_BIT*sizeof(long),
   calculates and returns the value of the function S(N,W),
   defined as the number of subsets of 1, 2, 3, ..., N that sum
   to at least W.  There are 2**N subsets of N items, so S(N,W)
   is in the range 0...2**N.

   There are a few trivial cases:

           * For W <= 0, S(N,W) = 2**N.

           * For W > N*(N+1)/2, S(N,W) = 0.

           * S(1,1) = 1.

   Notably, these trivial cases include all values of W for N = 1.

   Now consider the remaining, nontrivial cases, that is, N > 1 and
   1 <= W <= N*(N+1)/2.  In this case, apply the following identity:

           S(N,W) = S(N-1, W) + S(N-1, W-N).

   The first term on the right hand is the number of subsets that do
   not include N that sum to at least W; the second term is the
   number of subsets that do include N that sum to at least W.

   Then we repeatedly apply the identity to the result, reducing the
   value of N by 1 each time until we reach N=1.  Some expansions
   yield trivial cases, e.g. if W - N <= 0 (in which case we add a
   2**N term to the final result) or if W is greater than the new N.

   Here is an example:

   S(7,7) = S(6,7) + S(6,0)
          = S(6,7) + 64

          = (S(5,7) + S(5,1)) + 64

          = (S(4,7) + S(4,2)) + (S(4,1) + S(4,0)) + 64
          = S(4,7) + S(4,2) + S(4,1) + 80

          = (S(3,7) + S(3,3)) + (S(3,2) + S(3,2)) + (S(3,1) + S(3,0)) + 80
          = S(3,3) + 2*S(3,2) + S(3,1) + 88

          = (S(2,3) + S(2,0)) + 2*(S(2,2) + S(2,0)) + (S(2,1) + S(2,0)) + 88
          = S(2,3) + 2*S(2,2) + S(2,1) + 104

          = (S(1,3) + S(1,1)) + 2*(S(1,2) + S(1,0)) + (S(1,1) + S(2,0)) + 104
          = 2*S(1,1) + 112

          = 114

   This function runs in O(N*W) = O(N**3) time.  It seems very
   likely that it can be made to run in O(N**2) time or perhaps
   even better, but N is, practically speaking, quite small.
   Plus, the return value may be as large as N**2, so N must not
   be larger than 31 on 32-bit systems anyhow, and even 63**3 is
   only 250,047.
*/
static unsigned long int
count_sums_to_W (unsigned long int n, unsigned long int w)
{
  /* The array contain ints even though everything else is long,
     but no element in the array can have a value bigger than N,
     and using int will save some memory on 64-bit systems. */
  unsigned long int total;
  unsigned long int max;
  int *array;

  assert (n < CHAR_BIT * sizeof (unsigned long int));
  if (n == 0)
    return 0;
  else if (w <= 0)
    return 1 << n;
  else if (w > n * (n + 1) / 2)
    return 0;
  else if (n == 1)
    return 1;

  array = xcalloc (w + 1, sizeof *array);
  array[w] = 1;

  max = w;
  total = 0;
  for (; n > 1; n--)
    {
      unsigned long int max_sum = n * (n + 1) / 2;
      int i;

      if (max_sum < max)
        max = max_sum;

      for (i = 1; i <= max; i++)
        if (array[i] != 0)
          {
            int new_w = i - n;
            if (new_w <= 0)
               total += array[i] * (1 << (n - 1));
            else
              array[new_w] += array[i];
          }
    }
  total += array[1];
  free (array);
  return total;
}

/* Returns the exact, two-tailed level of significance for the
   Wilcoxon Matched-Pairs Signed-Ranks test, given sum of ranks
   of positive (or negative samples) W and sample size N.

   Returns -1 if the exact significance level cannot be
   calculated because W is out of the supported range. */
double
LevelOfSignificanceWXMPSR (double w, long int n)
{
  unsigned long int max_w;

  /* Limit N to valid range that won't cause integer overflow. */
  if (n < 0 || n >= CHAR_BIT * sizeof (unsigned long int))
    return -1;

  max_w = n * (n + 1) / 2;
  if (w < max_w / 2)
    w = max_w - w;

  return count_sums_to_W (n, ceil (w)) / (double) (1 << n) * 2;
}
