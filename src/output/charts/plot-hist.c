/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2009, 2011 Free Software Foundation, Inc.

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

#include <stdio.h>
#include <math.h>
#include <gsl/gsl_randist.h>
#include <assert.h>

#include "libpspp/cast.h"
#include "math/histogram.h"
#include "math/moments.h"
#include "output/chart-item-provider.h"
#include "output/charts/plot-hist.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Plots a histogram of the data in HIST with the given LABEL.
   Labels the histogram with each of N, MEAN, and STDDEV that is
   not SYSMIS.  If all three are not SYSMIS and SHOW_NORMAL is
   true, also draws a normal curve on the histogram. */
struct chart_item *
histogram_chart_create (const gsl_histogram *hist, const char *label,
                        double n, double mean, double stddev,
                        bool show_normal)
{
  struct histogram_chart *h;

  h = xmalloc (sizeof *h);
  chart_item_init (&h->chart_item, &histogram_chart_class, label);
  h->gsl_hist = hist != NULL ? gsl_histogram_clone (hist) : NULL;
  h->n = n;
  h->mean = mean;
  h->stddev = stddev;
  h->show_normal = show_normal;
  return &h->chart_item;
}

static void
histogram_chart_destroy (struct chart_item *chart_item)
{
  struct histogram_chart *h = UP_CAST (chart_item, struct histogram_chart,
                                       chart_item);
  if (h->gsl_hist != NULL)
    gsl_histogram_free (h->gsl_hist);
  free (h);
}

const struct chart_item_class histogram_chart_class =
  {
    histogram_chart_destroy
  };
