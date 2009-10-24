/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2009 Free Software Foundation, Inc.

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
#include <gsl/gsl_histogram.h>
#include <gsl/gsl_randist.h>
#include <assert.h>

#include <output/charts/plot-hist.h>
#include <output/charts/plot-chart.h>
#include <output/chart-provider.h>

#include <data/variable.h>
#include <libpspp/cast.h>
#include <libpspp/hash.h>
#include <output/chart.h>
#include <math/histogram.h>
#include <math/moments.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

static const struct chart_class histogram_chart_class;

/* Write the legend of the chart */
static void
histogram_write_legend (cairo_t *cr, const struct chart_geometry *geom,
                        double n, double mean, double stddev)
{
  double y = geom->data_bottom;
  cairo_save (cr);

  if (n != SYSMIS)
    {
      char *buf = xasprintf ("N = %.2f", n);
      cairo_move_to (cr, geom->legend_left, y);
      chart_label (cr, 'l', 'b', geom->font_size, buf);
      y += geom->font_size * 1.5;
      free (buf);
    }

  if (mean != SYSMIS)
    {
      char *buf = xasprintf ("Mean = %.1f", mean);
      cairo_move_to (cr,geom->legend_left, y);
      chart_label (cr, 'l', 'b', geom->font_size, buf);
      y += geom->font_size * 1.5;
      free (buf);
    }

  if (stddev != SYSMIS)
    {
      char *buf = xasprintf ("Std. Dev = %.2f", stddev);
      cairo_move_to (cr, geom->legend_left, y);
      chart_label (cr, 'l', 'b', geom->font_size, buf);
      free (buf);
    }

  cairo_restore (cr);
}

static void
hist_draw_bar (cairo_t *cr, const struct chart_geometry *geom,
               const gsl_histogram *h, int bar)
{
  double upper;
  double lower;
  double height;

  const size_t bins = gsl_histogram_bins (h);
  const double x_pos = (geom->data_right - geom->data_left) * bar / (double) bins ;
  const double width = (geom->data_right - geom->data_left) / (double) bins ;

  assert ( 0 == gsl_histogram_get_range (h, bar, &lower, &upper));

  assert ( upper >= lower);

  height = gsl_histogram_get (h, bar) *
    (geom->data_top - geom->data_bottom) / gsl_histogram_max_val (h);

  cairo_rectangle (cr, geom->data_left + x_pos, geom->data_bottom,
                   width, height);
  cairo_save (cr);
  cairo_set_source_rgb (cr,
                        geom->fill_colour.red / 255.0,
                        geom->fill_colour.green / 255.0,
                        geom->fill_colour.blue / 255.0);
  cairo_fill_preserve (cr);
  cairo_restore (cr);
  cairo_stroke (cr);

  draw_tick (cr, geom, TICK_ABSCISSA,
             x_pos + width / 2.0, "%g", (upper + lower) / 2.0);
}

struct histogram_chart
  {
    struct chart chart;
    gsl_histogram *gsl_hist;
    char *label;
    double n;
    double mean;
    double stddev;
    bool show_normal;
  };

/* Plots a histogram of the data in HIST with the given LABEL.
   Labels the histogram with each of N, MEAN, and STDDEV that is
   not SYSMIS.  If all three are not SYSMIS and SHOW_NORMAL is
   true, also draws a normal curve on the histogram. */
struct chart *
histogram_chart_create (const struct histogram *hist, const char *label,
                        double n, double mean, double stddev,
                        bool show_normal)
{
  struct histogram_chart *h;

  h = xmalloc (sizeof *h);
  chart_init (&h->chart, &histogram_chart_class);
  h->gsl_hist = hist->gsl_hist ? gsl_histogram_clone (hist->gsl_hist) : NULL;
  h->label = xstrdup (label);
  h->n = n;
  h->mean = mean;
  h->stddev = stddev;
  h->show_normal = show_normal;
  return &h->chart;
}

static void
histogram_chart_draw (const struct chart *chart, cairo_t *cr,
                      struct chart_geometry *geom)
{
  struct histogram_chart *h = UP_CAST (chart, struct histogram_chart, chart);
  int i;
  int bins;

  chart_write_title (cr, geom, _("HISTOGRAM"));

  chart_write_ylabel (cr, geom, _("Frequency"));
  chart_write_xlabel (cr, geom, h->label);

  if (h->gsl_hist == NULL)
    {
      /* Probably all values are SYSMIS. */
      return;
    }

  bins = gsl_histogram_bins (h->gsl_hist);

  chart_write_yscale (cr, geom, 0, gsl_histogram_max_val (h->gsl_hist), 5);

  for (i = 0; i < bins; i++)
    hist_draw_bar (cr, geom, h->gsl_hist, i);

  histogram_write_legend (cr, geom, h->n, h->mean, h->stddev);

  if (h->show_normal
      && h->n != SYSMIS && h->mean != SYSMIS && h->stddev != SYSMIS)
    {
      /* Draw the normal curve */
      double d;
      double x_min, x_max, not_used;
      double abscissa_scale;
      double ordinate_scale;
      double range;

      gsl_histogram_get_range (h->gsl_hist, 0, &x_min, &not_used);
      range = not_used - x_min;
      gsl_histogram_get_range (h->gsl_hist, bins - 1, &not_used, &x_max);

      abscissa_scale = (geom->data_right - geom->data_left) / (x_max - x_min);
      ordinate_scale = (geom->data_top - geom->data_bottom) /
	gsl_histogram_max_val (h->gsl_hist);

      cairo_move_to (cr, geom->data_left, geom->data_bottom);
      for (d = geom->data_left;
	   d <= geom->data_right;
	   d += (geom->data_right - geom->data_left) / 100.0)
	{
	  const double x = (d - geom->data_left) / abscissa_scale + x_min;
	  const double y = h->n * range *
	    gsl_ran_gaussian_pdf (x - h->mean, h->stddev);

          cairo_line_to (cr, d, geom->data_bottom  + y * ordinate_scale);

	}
      cairo_stroke (cr);
    }
}


static void
histogram_chart_destroy (struct chart *chart)
{
  struct histogram_chart *h = UP_CAST (chart, struct histogram_chart, chart);
  if (h->gsl_hist != NULL)
    gsl_histogram_free (h->gsl_hist);
  free (h->label);
  free (h);
}

static const struct chart_class histogram_chart_class =
  {
    histogram_chart_draw,
    histogram_chart_destroy
  };
