/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2008, 2009, 2011, 2012 Free Software Foundation, Inc.

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

#include <config.h>

#include "math/histogram.h"

#include <gsl/gsl_histogram.h>
#include <math.h>

#include "libpspp/message.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "math/chart-geometry.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


#include "gl/xalloc.h"

void
histogram_add (struct histogram *h, double y, double c)
{
  struct statistic *stat = &h->parent;
  stat->accumulate (stat, NULL, c, 0, y);
}

static void
acc (struct statistic *s, const struct ccase *cx UNUSED, double c, double cc UNUSED, double y)
{
  struct histogram *hist = UP_CAST (s, struct histogram, parent);

  gsl_histogram_accumulate (hist->gsl_hist, y, c);
}

static void
destroy (struct statistic *s)
{
  struct histogram *h = UP_CAST (s, struct histogram, parent);
  gsl_histogram_free (h->gsl_hist);
  free (s);
}


/* This functions adjusts the upper and lower range of the histogram to make them fit BIN_WIDTH
   MIN and MAX are the lowest and highest data to be plotted in the histogram.
   ADJ_MIN and ADJ_MAX are locations of the adjusted values of MIN and MAX (the range will always be
   equal or slightly larger).
   Returns the number of bins.
 */
static int
adjust_bin_ranges (double bin_width, double min, double max, double *adj_min, double *adj_max)
{
  const double half_bin_width = bin_width / 2.0;

  /* The lower and upper limits of the histogram, in units of half
     bin widths */
  int lower_limit, upper_limit;

  /* -1 if the lower end of the range contains more unused space
     than the upper end.
     +1 otherwise.  */
  short sparse_end = 0;

  double ul, ll;
  double lower_remainder = fabs (modf (min / half_bin_width, &ll));
  double upper_remainder = fabs (modf (max / half_bin_width, &ul));


  assert (max > min);

  lower_limit = ll;

  /* If the minimum value is not aligned on a half bin width,
     then the lower bound must be extended so that the histogram range includes it. */
  if (lower_remainder > 0)
    lower_limit--;

  /* However, the upper bound must be extended regardless, because histogram bins
     span the range [lower, upper) */
  upper_limit = ul + 1;

  /* So, in the case of the maximum value coinciding with a half bin width,
     the upper end will be the sparse end (because is got extended by a complete
     half bin width).   In other cases, it depends which got the bigger extension. */
  if (upper_remainder == 0)
    sparse_end = +1;
  else
    sparse_end = lower_remainder < upper_remainder ? -1 : +1;

  /* The range must be an EVEN number of half bin_widths */
  if ( (upper_limit - lower_limit) % 2)
    {
      /* Extend the range at the end which gives the least unused space */
      if (sparse_end == +1)
	lower_limit--;
      else
        upper_limit++;
      
      /* Now the other end has more space */
      sparse_end *= -1;
    }

  /* But the range should be aligned to an ODD number of
     half bin widths, so that the labels are aesthetically pleasing ones.
     Otherwise we are likely to get labels such as -3 -1 1 3 instead of -2 0 2 4
  */
  if ( lower_limit % 2 == 0)
    {
      /* Shift the range away from the sparse end, EXCEPT if that is the upper end,
         and it was extended to prevent the maximum value from getting lost */
      if (sparse_end == +1 && upper_remainder > 0)
        {
          lower_limit --;
          upper_limit --;
        }
      else
        {
          lower_limit ++;
          upper_limit ++;
        }
    }

  *adj_min = lower_limit * half_bin_width;
  *adj_max = upper_limit * half_bin_width;

  assert (*adj_max >= max);
  assert (*adj_min <= min);

  return (upper_limit - lower_limit) / 2.0;
}



struct histogram *
histogram_create (double bin_width, double min, double max)
{
  const int MAX_BINS = 25;
  struct histogram *h;
  struct statistic *stat;
  int bins;
  double adjusted_min, adjusted_max;

  assert (bin_width > 0);

  if (max == min)
    {
      msg (MW, _("Not creating histogram because the data contains less than 2 distinct values"));
      return NULL;
    }

  bins = adjust_bin_ranges (bin_width, min, max, &adjusted_min, &adjusted_max);

  /* Force the number of bins to lie in a sensible range. */
  if (bins > MAX_BINS) 
    {
      bins = adjust_bin_ranges ((max - min) / (double) (MAX_BINS - 1),
                                min, max, &adjusted_min, &adjusted_max);
    }

  /* Can this ever happen? */
  if (bins < 1)
    bins = 1;

  h = xmalloc (sizeof *h);

  h->gsl_hist = gsl_histogram_alloc (bins);

  gsl_histogram_set_ranges_uniform (h->gsl_hist, adjusted_min, adjusted_max);

  stat = &h->parent;
  stat->accumulate = acc;
  stat->destroy = destroy;

  return h;
}

