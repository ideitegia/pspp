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

#include <output/charts/np-plot.h>

#include <gsl/gsl_cdf.h>

#include <data/casereader.h>
#include <data/casewriter.h>
#include <libpspp/message.h>
#include <math/np.h>
#include <output/chart-provider.h>
#include <output/charts/cartesian.h>
#include <output/charts/plot-chart.h>

#include "gl/minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* An NP or DNP plot. */
struct np_plot_chart
  {
    struct chart chart;
    char *label;
    struct casereader *data;

    /* Copied directly from struct np. */
    double y_min, y_max;
    double dns_min, dns_max;

    /* Calculated. */
    double slope, intercept;
    double y_first, y_last;
    double x_lower, x_upper;
    double slack;
  };

static const struct chart_class np_plot_chart_class;
static const struct chart_class dnp_plot_chart_class;

static struct chart *
make_np_plot (const struct chart_class *class,
              const struct np *np, const struct casereader *reader,
              const char *label)
{
  struct np_plot_chart *npp;

  if (np->n < 1.0)
    return NULL;

  npp = xmalloc (sizeof *npp);
  chart_init (&npp->chart, class);
  npp->label = xstrdup (label);
  npp->data = casereader_clone (reader);
  npp->y_min = np->y_min;
  npp->y_max = np->y_max;
  npp->dns_min = np->dns_min;
  npp->dns_max = np->dns_max;

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

  return &npp->chart;
}

/* Creates and returns a normal probability plot corresponding to
   the calculations in NP and the data in READER, and label the
   plot with LABEL.  The data in READER must have Y-values in
   value index NP_IDX_Y and NS-values in value index NP_IDX_NS.

   Returns a null pointer if the data set is empty.

   The caller retains ownership of NP and READER. */
struct chart *
np_plot_create (const struct np *np, const struct casereader *reader,
                const char *label)
{
  return make_np_plot (&np_plot_chart_class, np, reader, label);
}

/* Creates and returns a detrended normal probability plot
   corresponding to the calculations in NP and the data in
   READER, and label the plot with LABEL.  The data in READER
   must have Y-values in value index NP_IDX_Y and DNS-values in
   value index NP_IDX_DNS.

   Returns a null pointer if the data set is empty.

   The caller retains ownership of NP and READER. */
struct chart *
dnp_plot_create (const struct np *np, const struct casereader *reader,
                 const char *label)
{
  return make_np_plot (&dnp_plot_chart_class, np, reader, label);
}

static void
np_plot_chart_draw (const struct chart *chart, plPlotter *lp,
                    struct chart_geometry *geom)
{
  const struct np_plot_chart *npp = (struct np_plot_chart *) chart;
  struct casereader *data;
  struct ccase *c;

  chart_write_title (lp, geom, _("Normal Q-Q Plot of %s"), npp->label);
  chart_write_xlabel (lp, geom, _("Observed Value"));
  chart_write_ylabel (lp, geom, _("Expected Normal"));
  chart_write_xscale (lp, geom,
                      npp->x_lower - npp->slack,
                      npp->x_upper + npp->slack, 5);
  chart_write_yscale (lp, geom, npp->y_first, npp->y_last, 5);

  data = casereader_clone (npp->data);
  for (; (c = casereader_read (data)) != NULL; case_unref (c))
    chart_datum (lp, geom, 0,
                 case_data_idx (c, NP_IDX_Y)->f,
                 case_data_idx (c, NP_IDX_NS)->f);
  casereader_destroy (data);

  chart_line (lp, geom, npp->slope, npp->intercept,
              npp->y_first, npp->y_last, CHART_DIM_Y);
}

static void
dnp_plot_chart_draw (const struct chart *chart, plPlotter *lp,
                     struct chart_geometry *geom)
{
  const struct np_plot_chart *dnpp = (struct np_plot_chart *) chart;
  struct casereader *data;
  struct ccase *c;

  chart_write_title (lp, geom, _("Detrended Normal Q-Q Plot of %s"),
                     dnpp->label);
  chart_write_xlabel (lp, geom, _("Observed Value"));
  chart_write_ylabel (lp, geom, _("Dev from Normal"));
  chart_write_xscale (lp, geom, dnpp->y_min, dnpp->y_max, 5);
  chart_write_yscale (lp, geom, dnpp->dns_min, dnpp->dns_max, 5);

  data = casereader_clone (dnpp->data);
  for (; (c = casereader_read (data)) != NULL; case_unref (c))
    chart_datum (lp, geom, 0, case_data_idx (c, NP_IDX_Y)->f,
                 case_data_idx (c, NP_IDX_DNS)->f);
  casereader_destroy (data);

  chart_line (lp, geom, 0, 0, dnpp->y_min, dnpp->y_max, CHART_DIM_X);
}

static void
np_plot_chart_destroy (struct chart *chart)
{
  struct np_plot_chart *npp = (struct np_plot_chart *) chart;

  casereader_destroy (npp->data);
  free (npp->label);
  free (npp);
}

static const struct chart_class np_plot_chart_class =
  {
    np_plot_chart_draw,
    np_plot_chart_destroy
  };

static const struct chart_class dnp_plot_chart_class =
  {
    dnp_plot_chart_draw,
    np_plot_chart_destroy
  };
