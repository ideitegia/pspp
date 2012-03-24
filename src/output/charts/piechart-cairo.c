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

#include "output/charts/piechart.h"

#include <math.h>

#include "output/cairo-chart.h"

#include "gl/minmax.h"

/* Draw a single slice of the pie */
static void
draw_segment(cairo_t *cr,
	     double x0, double y0,
	     double radius,
	     double start_angle, double segment_angle,
	     const struct xrchart_colour *colour)
{
  cairo_move_to (cr, x0, y0);
  cairo_arc (cr, x0, y0, radius, start_angle, start_angle + segment_angle);
  cairo_line_to (cr, x0, y0);
  cairo_save (cr);
  cairo_set_source_rgb (cr,
                        colour->red / 255.0,
                        colour->green / 255.0,
                        colour->blue / 255.0);
  cairo_fill_preserve (cr);
  cairo_restore (cr);
  cairo_stroke (cr);
}

void
xrchart_draw_piechart (const struct chart_item *chart_item, cairo_t *cr,
                       struct xrchart_geometry *geom)
{
  const struct piechart *pie = to_piechart (chart_item);
  double total_magnitude;
  double left_label, right_label;
  double centre_x, centre_y;
  double radius;
  double angle;
  int i;

  centre_x = (geom->axis[SCALE_ABSCISSA].data_max + geom->axis[SCALE_ORDINATE].data_min) / 2.0 ;
  centre_y = (geom->axis[SCALE_ORDINATE].data_max + geom->axis[SCALE_ORDINATE].data_min) / 2.0 ;

  left_label = geom->axis[SCALE_ORDINATE].data_min + (geom->axis[SCALE_ABSCISSA].data_max - geom->axis[SCALE_ORDINATE].data_min)/10.0;
  right_label = geom->axis[SCALE_ABSCISSA].data_max - (geom->axis[SCALE_ABSCISSA].data_max - geom->axis[SCALE_ORDINATE].data_min)/10.0;

  radius = MIN (5.0 / 12.0 * (geom->axis[SCALE_ORDINATE].data_max - geom->axis[SCALE_ORDINATE].data_min),
                1.0 / 4.0 * (geom->axis[SCALE_ABSCISSA].data_max - geom->axis[SCALE_ORDINATE].data_min));

  radius = MIN (5.0 / 12.0 * (geom->axis[SCALE_ORDINATE].data_max - geom->axis[SCALE_ORDINATE].data_min),
                1.0 / 4.0 * (geom->axis[SCALE_ABSCISSA].data_max - geom->axis[SCALE_ORDINATE].data_min));

  xrchart_write_title (cr, geom, "%s", chart_item_get_title (chart_item));

  total_magnitude = 0.0;
  for (i = 0; i < pie->n_slices; i++)
    total_magnitude += pie->slices[i].magnitude;

  angle = 0.0;
  for (i = 0; i < pie->n_slices ; ++i )
    {
      const double segment_angle =
	pie->slices[i].magnitude / total_magnitude * 2 * M_PI ;

      const double label_x = centre_x +
	radius * cos (angle + segment_angle/2.0);

      const double label_y = centre_y +
	radius * sin (angle + segment_angle/2.0);

      /* Fill the segment */
      draw_segment (cr,
                    centre_x, centre_y, radius,
                    angle, segment_angle,
                    &data_colour[i % XRCHART_N_COLOURS]);

      /* Now add the labels */
      if ( label_x < centre_x )
	{
          cairo_move_to (cr, label_x, label_y);
          cairo_line_to (cr, left_label, label_y);
          cairo_stroke (cr);
	  cairo_move_to (cr, left_label, label_y + 5);
	  xrchart_label (cr, 'l', 'x', geom->font_size,
                         ds_cstr (&pie->slices[i].label));
	}
      else
	{
	  cairo_move_to (cr, label_x, label_y);
          cairo_line_to (cr, right_label, label_y);
          cairo_stroke (cr);
	  cairo_move_to (cr, right_label, label_y + 5);
	  xrchart_label (cr, 'r', 'x', geom->font_size,
                         ds_cstr (&pie->slices[i].label));
	}

      angle += segment_angle;
    }

  /* Draw an outline to the pie */
  cairo_arc (cr, centre_x, centre_y, radius, 0, 2 * M_PI);
  cairo_stroke (cr);
}

