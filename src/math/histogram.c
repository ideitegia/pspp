/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2008, 2009 Free Software Foundation, Inc.

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
#include "histogram.h"

#include <gl/xalloc.h>
#include <libpspp/assertion.h>
#include <libpspp/cast.h>

#include <gsl/gsl_histogram.h>
#include "chart-geometry.h"
#include <math.h>


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
histogram_create (int bins, double min, double max)
{
  struct histogram *h = xmalloc (sizeof *h);
  struct statistic *stat = &h->parent;
  double upper_limit, lower_limit;

  double bin_width = chart_rounded_tick ((max - min) / (double) bins);
  double bin_width_2 = bin_width / 2.0;

  int n =  ceil (max / (bin_width_2) ) ;

  assert (max > min);

  if ( ! (n % 2 ) ) n++;
  upper_limit = n * bin_width_2;

  n =  floor (min / (bin_width_2) ) ;
  if ( ! (n % 2 ) ) n--;
  lower_limit = n * bin_width_2;

  h->gsl_hist = gsl_histogram_alloc (bins);
  gsl_histogram_set_ranges_uniform (h->gsl_hist, lower_limit, upper_limit);

  stat->accumulate = acc;
  stat->destroy = destroy;

  return h;
}

