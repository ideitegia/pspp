/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2008, 2009, 2011 Free Software Foundation, Inc.

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

#include "output/charts/np-plot.h"

#include <gsl/gsl_cdf.h>

#include "data/casereader.h"
#include "libpspp/cast.h"
#include "math/np.h"
#include "output/chart-item-provider.h"

#include "gl/minmax.h"

static struct chart_item *
make_np_plot (const struct np *np, const struct casereader *reader,
              const char *label, bool detrended)
{
  struct np_plot_chart *npp;

  if (np->n < 1.0)
    return NULL;

  npp = xzalloc (sizeof *npp);
  chart_item_init (&npp->chart_item, &np_plot_chart_class, label);
  npp->data = casereader_clone (reader);
  npp->y_min = np->y_min;
  npp->y_max = np->y_max;
  npp->dns_min = np->dns_min;
  npp->dns_max = np->dns_max;
  npp->detrended = detrended;

  /* Slope and intercept of the ideal normal probability line. */
  npp->slope = 1.0 / np->stddev;
  npp->intercept = -np->mean / np->stddev;

  npp->y_first = gsl_cdf_ugaussian_Pinv (1 / (np->n + 1));
  npp->y_last = gsl_cdf_ugaussian_Pinv (np->n / (np->n + 1));

  /* Need to make sure that both the scatter plot and the ideal fit into the
     plot. */
  npp->x_lower = MIN (np->y_min, (npp->y_first - npp->intercept) / npp->slope);
  npp->x_upper = MAX (np->y_max, (npp->y_last  - npp->intercept) / npp->slope);
  npp->slack = (npp->x_upper - npp->x_lower) * 0.05;

  return &npp->chart_item;
}

/* Creates and returns a normal probability plot corresponding to
   the calculations in NP and the data in READER, and label the
   plot with LABEL.  The data in READER must have Y-values in
   value index NP_IDX_Y and NS-values in value index NP_IDX_NS.

   Returns a null pointer if the data set is empty.

   The caller retains ownership of NP and READER. */
struct chart_item *
np_plot_create (const struct np *np, const struct casereader *reader,
                const char *label)
{
  return make_np_plot (np, reader, label, false);
}

/* Creates and returns a detrended normal probability plot
   corresponding to the calculations in NP and the data in
   READER, and label the plot with LABEL.  The data in READER
   must have Y-values in value index NP_IDX_Y and DNS-values in
   value index NP_IDX_DNS.

   Returns a null pointer if the data set is empty.

   The caller retains ownership of NP and READER. */
struct chart_item *
dnp_plot_create (const struct np *np, const struct casereader *reader,
                 const char *label)
{
  return make_np_plot (np, reader, label, true);
}

static void
np_plot_chart_destroy (struct chart_item *chart_item)
{
  struct np_plot_chart *npp = to_np_plot_chart (chart_item);
  casereader_destroy (npp->data);
  free (npp);
}

const struct chart_item_class np_plot_chart_class =
  {
    np_plot_chart_destroy
  };
