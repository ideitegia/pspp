/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011 Free Software Foundation, Inc.

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

#include "output/cairo-chart.h"

#include <math.h>

#include "math/box-whisker.h"
#include "math/chart-geometry.h"
#include "output/charts/boxplot.h"

/* Draw an OUTLIER on the plot CH
 * at CENTRELINE
 */
static void
draw_case (cairo_t *cr, const struct xrchart_geometry *geom, double centreline,
	   const struct outlier *outlier)
{
  double y = geom->data_bottom + (outlier->value - geom->y_min) * geom->ordinate_scale;
  xrchart_draw_marker (cr, centreline, y,
                     outlier->extreme ? XRMARKER_ASTERISK : XRMARKER_CIRCLE,
                     20);

  cairo_move_to (cr, centreline + 10, y);
  xrchart_label (cr, 'l', 'c', geom->font_size, ds_cstr (&outlier->label));
}

static void
boxplot_draw_box (cairo_t *cr, const struct xrchart_geometry *geom,
                  double box_centre,
                  double box_width,
                  const struct box_whisker *bw,
                  const char *name)
{
  double whisker[2];
  double hinge[3];
  struct ll *ll;

  const struct ll_list *outliers;

  const double box_left = box_centre - box_width / 2.0;

  const double box_right = box_centre + box_width / 2.0;

  double box_bottom ;
  double box_top ;
  double bottom_whisker ;
  double top_whisker ;

  box_whisker_whiskers (bw, whisker);
  box_whisker_hinges (bw, hinge);

  box_bottom = geom->data_bottom + (hinge[0] - geom->y_min ) * geom->ordinate_scale;

  box_top = geom->data_bottom + (hinge[2] - geom->y_min ) * geom->ordinate_scale;

  bottom_whisker = geom->data_bottom + (whisker[0] - geom->y_min) *
    geom->ordinate_scale;

  top_whisker = geom->data_bottom + (whisker[1] - geom->y_min) * geom->ordinate_scale;

  /* Draw the box */
  cairo_rectangle (cr,
                   box_left,
                   box_bottom,
                   box_right - box_left,
                   box_top - box_bottom);
  cairo_save (cr);
  cairo_set_source_rgb (cr,
                        geom->fill_colour.red / 255.0,
                        geom->fill_colour.green / 255.0,
                        geom->fill_colour.blue / 255.0);
  cairo_fill (cr);
  cairo_restore (cr);
  cairo_stroke (cr);

  /* Draw the median */
  cairo_save (cr);
  cairo_set_line_width (cr, cairo_get_line_width (cr) * 5);
  cairo_move_to (cr,
                 box_left,
                 geom->data_bottom + (hinge[1] - geom->y_min) * geom->ordinate_scale);
  cairo_line_to (cr,
                 box_right,
                 geom->data_bottom + (hinge[1] - geom->y_min) * geom->ordinate_scale);
  cairo_stroke (cr);
  cairo_restore (cr);

  /* Draw the bottom whisker */
  cairo_move_to (cr, box_left, bottom_whisker);
  cairo_line_to (cr, box_right, bottom_whisker);
  cairo_stroke (cr);

  /* Draw top whisker */
  cairo_move_to (cr, box_left, top_whisker);
  cairo_line_to (cr, box_right, top_whisker);
  cairo_stroke (cr);

  /* Draw centre line.
     (bottom half) */
  cairo_move_to (cr, box_centre, bottom_whisker);
  cairo_line_to (cr, box_centre, box_bottom);
  cairo_stroke (cr);

  /* (top half) */
  cairo_move_to (cr, box_centre, top_whisker);
  cairo_line_to (cr, box_centre, box_top);
  cairo_stroke (cr);

  outliers = box_whisker_outliers (bw);
  for (ll = ll_head (outliers);
       ll != ll_null (outliers); ll = ll_next (ll))
    {
      const struct outlier *outlier = ll_data (ll, struct outlier, ll);
      draw_case (cr, geom, box_centre, outlier);
    }

  /* Draw  tick  mark on x axis */
  draw_tick(cr, geom, TICK_ABSCISSA, box_centre - geom->data_left, "%s", name);
}

static void
boxplot_draw_yscale (cairo_t *cr, struct xrchart_geometry *geom,
                     double y_max, double y_min)
{
  double y_tick;
  double d;

  geom->y_max = y_max;
  geom->y_min = y_min;

  y_tick = chart_rounded_tick (fabs (geom->y_max - geom->y_min) / 5.0);

  geom->y_min = (ceil (geom->y_min / y_tick) - 1.0) * y_tick;

  geom->y_max = (floor (geom->y_max / y_tick) + 1.0) * y_tick;

  geom->ordinate_scale = (fabs (geom->data_top - geom->data_bottom)
                          / fabs (geom->y_max - geom->y_min));

  for (d = geom->y_min; d <= geom->y_max; d += y_tick)
    draw_tick (cr, geom, TICK_ORDINATE,
               (d - geom->y_min) * geom->ordinate_scale, "%g", d);
}

void
xrchart_draw_boxplot (const struct chart_item *chart_item, cairo_t *cr,
                      struct xrchart_geometry *geom)
{
  const struct boxplot *boxplot = to_boxplot (chart_item);
  double box_width;
  size_t i;

  boxplot_draw_yscale (cr, geom, boxplot->y_max, boxplot->y_min);
  xrchart_write_title (cr, geom, "%s", chart_item->title);

  box_width = (geom->data_right - geom->data_left) / boxplot->n_boxes / 2.0;
  for (i = 0; i < boxplot->n_boxes; i++)
    {
      const struct boxplot_box *box = &boxplot->boxes[i];
      const double box_centre = (i * 2 + 1) * box_width + geom->data_left;
      boxplot_draw_box (cr, geom, box_centre, box_width, box->bw, box->label);
    }
}
