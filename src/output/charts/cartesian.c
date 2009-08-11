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

#include <output/charts/cartesian.h>

#include <cairo/cairo.h>
#include <math.h>
#include <assert.h>

#include <output/chart.h>
#include <output/chart-provider.h>
#include <output/charts/plot-chart.h>
#include <libpspp/compiler.h>

#include "xalloc.h"

/* Start a new vector called NAME */
void
chart_vector_start (cairo_t *cr, struct chart_geometry *geom, const char *name)
{
  const struct chart_colour *colour;

  cairo_save (cr);

  colour = &data_colour[geom->n_datasets % N_CHART_COLOURS];
  cairo_set_source_rgb (cr,
                        colour->red / 255.0,
                        colour->green / 255.0,
                        colour->blue / 255.0);

  geom->n_datasets++;
  geom->dataset = xrealloc (geom->dataset,
                            geom->n_datasets * sizeof (*geom->dataset));

  geom->dataset[geom->n_datasets - 1] = strdup (name);
}

/* Plot a data point */
void
chart_datum (cairo_t *cr, const struct chart_geometry *geom,
             int dataset UNUSED, double x, double y)
{
  double x_pos = (x - geom->x_min) * geom->abscissa_scale + geom->data_left;
  double y_pos = (y - geom->y_min) * geom->ordinate_scale + geom->data_bottom;

  chart_draw_marker (cr, x_pos, y_pos, MARKER_SQUARE, 15);
}

void
chart_vector_end (cairo_t *cr, struct chart_geometry *geom)
{
  cairo_stroke (cr);
  cairo_restore (cr);
  geom->in_path = false;
}

/* Plot a data point */
void
chart_vector (cairo_t *cr, struct chart_geometry *geom, double x, double y)
{
  const double x_pos =
    (x - geom->x_min) * geom->abscissa_scale + geom->data_left ;

  const double y_pos =
    (y - geom->y_min) * geom->ordinate_scale + geom->data_bottom ;

  if (geom->in_path)
    cairo_line_to (cr, x_pos, y_pos);
  else
    {
      cairo_move_to (cr, x_pos, y_pos);
      geom->in_path = true;
    }
}



/* Draw a line with slope SLOPE and intercept INTERCEPT.
   between the points limit1 and limit2.
   If lim_dim is CHART_DIM_Y then the limit{1,2} are on the
   y axis otherwise the x axis
*/
void
chart_line(cairo_t *cr, const struct chart_geometry *geom,
           double slope, double intercept,
	   double limit1, double limit2, enum CHART_DIM lim_dim)
{
  double x1, y1;
  double x2, y2;

  if ( lim_dim == CHART_DIM_Y )
    {
      x1 = ( limit1 - intercept ) / slope;
      x2 = ( limit2 - intercept ) / slope;
      y1 = limit1;
      y2 = limit2;
    }
  else
    {
      x1 = limit1;
      x2 = limit2;
      y1 = slope * x1 + intercept;
      y2 = slope * x2 + intercept;
    }

  y1 = (y1 - geom->y_min) * geom->ordinate_scale + geom->data_bottom;
  y2 = (y2 - geom->y_min) * geom->ordinate_scale + geom->data_bottom;
  x1 = (x1 - geom->x_min) * geom->abscissa_scale + geom->data_left;
  x2 = (x2 - geom->x_min) * geom->abscissa_scale + geom->data_left;

  cairo_move_to (cr, x1, y1);
  cairo_line_to (cr, x2, y2);
  cairo_stroke (cr);
}
