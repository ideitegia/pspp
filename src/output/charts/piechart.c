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
draw_segment(plPlotter *,
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
piechart_draw (const struct chart *chart, plPlotter *lp)
{
  struct piechart *pie = (struct piechart *) chart;
  struct chart_geometry geom;
  double total_magnitude;
  double left_label, right_label;
  double centre_x, centre_y;
  double radius;
  double angle;
  int i;

  chart_geometry_init (lp, &geom);

  left_label = geom.data_left + (geom.data_right - geom.data_left)/10.0;
  right_label = geom.data_right - (geom.data_right - geom.data_left)/10.0;

  centre_x = (geom.data_right + geom.data_left) / 2.0 ;
  centre_y = (geom.data_top + geom.data_bottom) / 2.0 ;

  radius = MIN (5.0 / 12.0 * (geom.data_top - geom.data_bottom),
                1.0 / 4.0 * (geom.data_right - geom.data_left));

  chart_write_title (lp, &geom, "%s", pie->title);

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
      draw_segment (lp,
                    centre_x, centre_y, radius,
                    angle, segment_angle,
                    &data_colour[i % N_CHART_COLOURS]);

      /* Now add the labels */
      if ( label_x < centre_x )
	{
	  pl_line_r (lp, label_x, label_y, left_label, label_y );
	  pl_moverel_r (lp, 0, 5);
	  pl_alabel_r (lp, 0, 0, ds_cstr (&pie->slices[i].label));
	}
      else
	{
	  pl_line_r (lp, label_x, label_y, right_label, label_y);
	  pl_moverel_r (lp, 0, 5);
	  pl_alabel_r (lp, 'r', 0, ds_cstr (&pie->slices[i].label));
	}

      angle += segment_angle;
    }

  /* Draw an outline to the pie */
  pl_filltype_r (lp,0);
  pl_fcircle_r (lp, centre_x, centre_y, radius);

  chart_geometry_free (lp);
}

/* Fill a segment with the current fill colour */
static void
fill_segment(plPlotter *lp,
	     double x0, double y0,
	     double radius,
	     double start_angle, double segment_angle)
{

  const double start_x  = x0 - radius * sin(start_angle);
  const double start_y  = y0 + radius * cos(start_angle);

  const double stop_x   =
    x0 - radius * sin(start_angle + segment_angle);

  const double stop_y   =
    y0 + radius * cos(start_angle + segment_angle);

  assert(segment_angle <= 2 * M_PI);
  assert(segment_angle >= 0);

  if ( segment_angle > M_PI )
    {
      /* Then we must draw it in two halves */
      fill_segment(lp, x0, y0, radius, start_angle, segment_angle / 2.0 );
      fill_segment(lp, x0, y0, radius, start_angle + segment_angle / 2.0,
		   segment_angle / 2.0 );
    }
  else
    {
      pl_move_r(lp, x0, y0);

      pl_cont_r(lp, stop_x, stop_y);
      pl_cont_r(lp, start_x, start_y);

      pl_arc_r(lp,
	       x0, y0,
	       stop_x, stop_y,
	       start_x, start_y
	       );

      pl_endpath_r(lp);
    }
}

/* Draw a single slice of the pie */
static void
draw_segment(plPlotter *lp,
	     double x0, double y0,
	     double radius,
	     double start_angle, double segment_angle,
	     const struct chart_colour *colour)
{
  const double start_x  = x0 - radius * sin(start_angle);
  const double start_y  = y0 + radius * cos(start_angle);

  pl_savestate_r(lp);

  pl_savestate_r(lp);
  pl_color_r(lp, colour->red * 257, colour->green * 257, colour->blue * 257);

  pl_pentype_r(lp,1);
  pl_filltype_r(lp,1);

  fill_segment(lp, x0, y0, radius, start_angle, segment_angle);
  pl_restorestate_r(lp);

  /* Draw line dividing segments */
  pl_pentype_r(lp, 1);
  pl_fline_r(lp, x0, y0, start_x, start_y);


  pl_restorestate_r(lp);
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
