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

#include <output/charts/piechart.h>

#include <assert.h>
#include <float.h>
#include <gsl/gsl_math.h>
#include <math.h>
#include <stdio.h>

#include <data/value-labels.h>
#include <libpspp/str.h>
#include <output/charts/plot-chart.h>
#include <output/chart-provider.h>

#include "minmax.h"

struct piechart
  {
    struct chart chart;
    char *title;
    struct slice *slices;
    int n_slices;
  };

static const struct chart_class piechart_class;

/* Draw a single slice of the pie */
static void
draw_segment(cairo_t *,
	     double centre_x, double centre_y,
	     double radius,
	     double start_angle, double segment_angle,
	     const struct chart_colour *) ;



/* Creates and returns a chart that will render a piechart with
   the given TITLE and the N_SLICES described in SLICES. */
struct chart *
piechart_create (const char *title, const struct slice *slices, int n_slices)
{
  struct piechart *pie;
  int i;

  pie = xmalloc (sizeof *pie);
  chart_init (&pie->chart, &piechart_class);
  pie->title = xstrdup (title);
  pie->slices = xnmalloc (n_slices, sizeof *pie->slices);
  for (i = 0; i < n_slices; i++)
    {
      const struct slice *src = &slices[i];
      struct slice *dst = &pie->slices[i];

      ds_init_string (&dst->label, &src->label);
      dst->magnitude = src->magnitude;
    }
  pie->n_slices = n_slices;
  return &pie->chart;
}

static void
piechart_draw (const struct chart *chart, cairo_t *cr,
               struct chart_geometry *geom)
{
  struct piechart *pie = (struct piechart *) chart;
  double total_magnitude;
  double left_label, right_label;
  double centre_x, centre_y;
  double radius;
  double angle;
  int i;

  centre_x = (geom->data_right + geom->data_left) / 2.0 ;
  centre_y = (geom->data_top + geom->data_bottom) / 2.0 ;

  left_label = geom->data_left + (geom->data_right - geom->data_left)/10.0;
  right_label = geom->data_right - (geom->data_right - geom->data_left)/10.0;

  radius = MIN (5.0 / 12.0 * (geom->data_top - geom->data_bottom),
                1.0 / 4.0 * (geom->data_right - geom->data_left));

  radius = MIN (5.0 / 12.0 * (geom->data_top - geom->data_bottom),
                1.0 / 4.0 * (geom->data_right - geom->data_left));

  chart_write_title (cr, geom, "%s", pie->title);

  total_magnitude = 0.0;
  for (i = 0; i < pie->n_slices; i++)
    total_magnitude += pie->slices[i].magnitude;

  angle = 0.0;
  for (i = 0; i < pie->n_slices ; ++i )
    {
      const double segment_angle =
	pie->slices[i].magnitude / total_magnitude * 2 * M_PI ;

      const double label_x = centre_x -
	radius * sin(angle + segment_angle/2.0);

      const double label_y = centre_y +
	radius * cos(angle + segment_angle/2.0);

      /* Fill the segment */
      draw_segment (cr,
                    centre_x, centre_y, radius,
                    angle, segment_angle,
                    &data_colour[i % N_CHART_COLOURS]);

      /* Now add the labels */
      if ( label_x < centre_x )
	{
          cairo_move_to (cr, label_x, label_y);
          cairo_line_to (cr, left_label, label_y);
          cairo_stroke (cr);
	  cairo_move_to (cr, left_label, label_y + 5);
	  chart_label (cr, 'l', 'x', geom->font_size,
                       ds_cstr (&pie->slices[i].label));
	}
      else
	{
	  cairo_move_to (cr, label_x, label_y);
          cairo_line_to (cr, right_label, label_y);
          cairo_stroke (cr);
	  cairo_move_to (cr, right_label, label_y + 5);
	  chart_label (cr, 'r', 'x', geom->font_size,
                       ds_cstr (&pie->slices[i].label));
	}

      angle += segment_angle;
    }

  /* Draw an outline to the pie */
  cairo_arc (cr, centre_x, centre_y, radius, 0, 2 * M_PI);
  cairo_stroke (cr);
}

/* Draw a single slice of the pie */
static void
draw_segment(cairo_t *cr,
	     double x0, double y0,
	     double radius,
	     double start_angle, double segment_angle,
	     const struct chart_colour *colour)
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

static void
piechart_destroy (struct chart *chart)
{
  struct piechart *pie = (struct piechart *) chart;
  int i;

  free (pie->title);
  for (i = 0; i < pie->n_slices; i++)
    {
      struct slice *slice = &pie->slices[i];
      ds_destroy (&slice->label);
    }
  free (pie->slices);
  free (pie);
}

static const struct chart_class piechart_class =
  {
    piechart_draw,
    piechart_destroy
  };
