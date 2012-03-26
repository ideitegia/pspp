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


struct histogram *
histogram_create (double bin_width, double min, double max)
{
  int bins;
  struct histogram *h = xmalloc (sizeof *h);
  struct statistic *stat = &h->parent;

  const double half_bin_width = bin_width / 2.0;

  /* The lower and upper limits of the histogram, in units of half
     bin widths */
  int lower_limit, upper_limit;

  /* -1 if the lower end of the range contains more unused space
     than the upper end.
     +1 otherwise.  */
  short sparse_end = 0;

  if (max == min)
    {
      msg (MW, _("Not creating histogram because the data contains less than 2 distinct values"));
      free (h);
      return NULL;
    }

  assert (max > min);

  {
    double ul, ll;
    double lower_tail = modf (min / half_bin_width, &ll);
    double upper_tail = modf (max / half_bin_width, &ul);
    lower_limit = ll - 1;
    upper_limit = ul + 1;
    
    sparse_end = lower_tail < upper_tail ? -1 : +1;
  }

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
     half bin widths, so that the labels are aesthetically pleasing ones. */
  if ( lower_limit % 2 == 0)
    {
      lower_limit += -sparse_end ;
      upper_limit += -sparse_end ;
    }

  bins = (upper_limit - lower_limit) / 2.0;

  /* Force the number of bins to lie in a sensible range */
  if (bins > 25) 
    bins = 25;

  if (bins < 1)
    bins = 1;

  h->gsl_hist = gsl_histogram_alloc (bins);

  gsl_histogram_set_ranges_uniform (h->gsl_hist,
                                    lower_limit * half_bin_width,
                                    upper_limit * half_bin_width);

  stat->accumulate = acc;
  stat->destroy = destroy;

  return h;
}

