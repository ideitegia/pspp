/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011, 2014 Free Software Foundation, Inc.

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

#include "output/charts/plot-hist.h"

#include <float.h>
#include <gsl/gsl_randist.h>

#include "data/val-type.h"
#include "output/cairo-chart.h"

#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Write the legend of the chart */
static void
histogram_write_legend (cairo_t *cr, const struct xrchart_geometry *geom,
                        double n, double mean, double stddev)
{
  double y = geom->axis[SCALE_ORDINATE].data_min;
  cairo_save (cr);

  if (n != SYSMIS)
    {
      char *buf = xasprintf (_("N = %.2f"), n);
      cairo_move_to (cr, geom->legend_left, y);
      xrchart_label (cr, 'l', 'b', geom->font_size, buf);
      y += geom->font_size * 1.5;
      free (buf);
    }

  if (mean != SYSMIS)
    {
      char *buf = xasprintf (_("Mean = %.1f"), mean);
      cairo_move_to (cr,geom->legend_left, y);
      xrchart_label (cr, 'l', 'b', geom->font_size, buf);
      y += geom->font_size * 1.5;
      free (buf);
    }

  if (stddev != SYSMIS)
    {
      char *buf = xasprintf (_("Std. Dev = %.2f"), stddev);
      cairo_move_to (cr, geom->legend_left, y);
      xrchart_label (cr, 'l', 'b', geom->font_size, buf);
      free (buf);
    }

  cairo_restore (cr);
}

static void
hist_draw_bar (cairo_t *cr, const struct xrchart_geometry *geom,
               const gsl_histogram *h, int bar, bool label)
{
  double upper;
  double lower;
  double height;

  const size_t bins = gsl_histogram_bins (h);

  const double x_pos =
    (geom->axis[SCALE_ABSCISSA].data_max - geom->axis[SCALE_ABSCISSA].data_min) *
    bar / (double) bins ;

  const double width =
    (geom->axis[SCALE_ABSCISSA].data_max - geom->axis[SCALE_ABSCISSA].data_min) / (double) bins ;

  assert ( 0 == gsl_histogram_get_range (h, bar, &lower, &upper));

  assert ( upper >= lower);

  height = geom->axis[SCALE_ORDINATE].scale * gsl_histogram_get (h, bar);

  cairo_rectangle (cr,
		   geom->axis[SCALE_ABSCISSA].data_min + x_pos,
		   geom->axis[SCALE_ORDINATE].data_min,
                   width, height);
  cairo_save (cr);
  cairo_set_source_rgb (cr,
                        geom->fill_colour.red / 255.0,
                        geom->fill_colour.green / 255.0,
                        geom->fill_colour.blue / 255.0);
  cairo_fill_preserve (cr);
  cairo_restore (cr);
  cairo_stroke (cr);

  if (label)
    draw_tick (cr, geom, SCALE_ABSCISSA, bins > 10,
	       x_pos + width / 2.0, "%.*g",
               DBL_DIG + 1, (upper + lower) / 2.0);
}

void
xrchart_draw_histogram (const struct chart_item *chart_item, cairo_t *cr,
                        struct xrchart_geometry *geom)
{
  struct histogram_chart *h = to_histogram_chart (chart_item);
  int i;
  int bins;

  xrchart_write_title (cr, geom, _("HISTOGRAM"));

  xrchart_write_ylabel (cr, geom, _("Frequency"));
  xrchart_write_xlabel (cr, geom, chart_item_get_title (chart_item));

  if (h->gsl_hist == NULL)
    {
      /* Probably all values are SYSMIS. */
      return;
    }

  bins = gsl_histogram_bins (h->gsl_hist);

  xrchart_write_yscale (cr, geom, 0, gsl_histogram_max_val (h->gsl_hist), 5);

  for (i = 0; i < bins; i++)
    {
      hist_draw_bar (cr, geom, h->gsl_hist, i, true);
    }

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

      abscissa_scale = (geom->axis[SCALE_ABSCISSA].data_max - geom->axis[SCALE_ABSCISSA].data_min) / (x_max - x_min);
      ordinate_scale = (geom->axis[SCALE_ORDINATE].data_max - geom->axis[SCALE_ORDINATE].data_min) /
	gsl_histogram_max_val (h->gsl_hist);

      cairo_move_to (cr, geom->axis[SCALE_ABSCISSA].data_min, geom->axis[SCALE_ORDINATE].data_min);
      for (d = geom->axis[SCALE_ABSCISSA].data_min;
	   d <= geom->axis[SCALE_ABSCISSA].data_max;
	   d += (geom->axis[SCALE_ABSCISSA].data_max - geom->axis[SCALE_ABSCISSA].data_min) / 100.0)
	{
	  const double x = (d - geom->axis[SCALE_ABSCISSA].data_min) / abscissa_scale + x_min;
	  const double y = h->n * range *
	    gsl_ran_gaussian_pdf (x - h->mean, h->stddev);

          cairo_line_to (cr, d, geom->axis[SCALE_ORDINATE].data_min  + y * ordinate_scale);

	}
      cairo_stroke (cr);
    }
}
