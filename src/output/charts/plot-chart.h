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

#ifndef PLOT_CHART_H
#define PLOT_CHART_H

#include <cairo/cairo.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <float.h>
#include <assert.h>
#include <math.h>


#include <math/chart-geometry.h>
#include <output/chart.h>
#include <output/chart-provider.h>

#include <libpspp/compiler.h>
#include <libpspp/str.h>
#include <output/manager.h>
#include <output/output.h>

#define N_CHART_COLOURS 9
extern const struct chart_colour data_colour[];

enum tick_orientation
  {
    TICK_ABSCISSA=0,
    TICK_ORDINATE
  };

struct chart_geometry;


enum marker_type
  {
    MARKER_CIRCLE,              /* Hollow circle. */
    MARKER_ASTERISK,            /* Asterisk (*). */
    MARKER_SQUARE               /* Hollow square. */
  };

void chart_draw_marker (cairo_t *, double x, double y, enum marker_type,
                        double size);

void chart_label (cairo_t *, int horz_justify, int vert_justify,
                  const char *);

/* Draw a tick mark at position
   If label is non zero, then print it at the tick mark
*/
void draw_tick(cairo_t *, const struct chart_geometry *,
	  enum tick_orientation orientation,
	  double position,
	       const char *label, ...)
  PRINTF_FORMAT (5, 6);


/* Write the title on a chart*/
void   chart_write_title(cairo_t *, const struct chart_geometry *,
                         const char *title, ...)
  PRINTF_FORMAT (3, 4);


/* Set the scale for the abscissa */
void  chart_write_xscale(cairo_t *, struct chart_geometry *,
                         double min, double max, int ticks);


/* Set the scale for the ordinate */
void  chart_write_yscale(cairo_t *, struct chart_geometry *,
                         double smin, double smax, int ticks);

void chart_write_xlabel(cairo_t *, const struct chart_geometry *,
                        const char *label) ;

/* Write the ordinate label */
void  chart_write_ylabel(cairo_t *, const struct chart_geometry *,
                         const char *label);

#endif
