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

#ifndef OUTPUT_CAIRO_CHART_H
#define OUTPUT_CAIRO_CHART_H 1

#include <cairo/cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include "libpspp/compiler.h"

struct chart_item;

struct xrchart_colour
  {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
  };

struct xrchart_axis
{
  int data_max;
  int data_min;

  double scale;
  double min;
  double max;
};

/* The geometry of a chart. */
struct xrchart_geometry
  {
    /* Bottom of the abscissa segment */
    int abscissa_bottom;

    /* Left of the ordinate segment */
    int ordinate_left;

    int title_bottom;

    /* Legend. */
    int legend_left;
    int legend_right;
    char **dataset;
    int n_datasets;

    /* Default font size for the plot. */
    double font_size;

    struct xrchart_colour fill_colour;

    /* Stuff particular to cartesians and boxplots. */
    struct xrchart_axis axis[2];

    /* True iff a path is currently being drawn */
    bool in_path;
  };

void xrchart_geometry_init (cairo_t *, struct xrchart_geometry *,
                            double width, double length);
void xrchart_geometry_free (cairo_t *, struct xrchart_geometry *);

#define XRCHART_N_COLOURS 27
extern const struct xrchart_colour data_colour[];

enum tick_orientation
  {
    SCALE_ABSCISSA=0,
    SCALE_ORDINATE
  };

enum xrmarker_type
  {
    XRMARKER_CIRCLE,              /* Hollow circle. */
    XRMARKER_ASTERISK,            /* Asterisk (*). */
    XRMARKER_SQUARE               /* Hollow square. */
  };

void xrchart_draw_marker (cairo_t *, double x, double y, enum xrmarker_type,
                          double size);

void xrchart_label (cairo_t *, int horz_justify, int vert_justify,
                    double font_size, const char *);

void xrchart_label_rotate (cairo_t *cr, int horz_justify, int vert_justify,
			   double font_size, const char *string, double angle);


/* Draw a tick mark at position
   If label is non zero, then print it at the tick mark
*/
void draw_tick (cairo_t *, const struct xrchart_geometry *,
                enum tick_orientation orientation,
		bool rotated,
		double position,
                const char *label, ...)
  PRINTF_FORMAT (6, 7);


/* Write the title on a chart*/
void xrchart_write_title (cairo_t *, const struct xrchart_geometry *,
                          const char *title, ...)
  PRINTF_FORMAT (3, 4);

/* Set the scale for the abscissa */
void xrchart_write_xscale (cairo_t *, struct xrchart_geometry *,
                           double min, double max, int ticks);


/* Set the scale for the ordinate */
void xrchart_write_yscale (cairo_t *, struct xrchart_geometry *,
                           double smin, double smax, int ticks);

void xrchart_write_xlabel (cairo_t *, const struct xrchart_geometry *,
                           const char *label) ;

/* Write the ordinate label */
void xrchart_write_ylabel (cairo_t *, const struct xrchart_geometry *,
                           const char *label);

void xrchart_write_legend (cairo_t *, const struct xrchart_geometry *);

enum xrchart_dim
  {
    XRCHART_DIM_X,
    XRCHART_DIM_Y
  };

void xrchart_vector_start (cairo_t *, struct xrchart_geometry *,
                           const char *name);
void xrchart_vector_end (cairo_t *, struct xrchart_geometry *);
void xrchart_vector (cairo_t *, struct xrchart_geometry *, double x, double y);

/* Plot a data point */
void xrchart_datum (cairo_t *, const struct xrchart_geometry *,
                    int dataset UNUSED, double x, double y);

/* Draw a line with slope SLOPE and intercept INTERCEPT.
   between the points limit1 and limit2.
   If lim_dim is XRCHART_DIM_Y then the limit{1,2} are on the
   y axis otherwise the x axis
*/
void xrchart_line (cairo_t *, const struct xrchart_geometry *,
                   double slope, double intercept,
                   double limit1, double limit2, enum xrchart_dim lim_dim);

/* Drawing various kinds of charts. */
void xrchart_draw_boxplot (const struct chart_item *, cairo_t *,
                           struct xrchart_geometry *);
void xrchart_draw_roc (const struct chart_item *, cairo_t *,
                       struct xrchart_geometry *);
void xrchart_draw_piechart (const struct chart_item *, cairo_t *,
                            struct xrchart_geometry *);
void xrchart_draw_histogram (const struct chart_item *, cairo_t *,
                             struct xrchart_geometry *);
void xrchart_draw_np_plot (const struct chart_item *, cairo_t *,
                           struct xrchart_geometry *);
void xrchart_draw_scree (const struct chart_item *, cairo_t *,
                         struct xrchart_geometry *);
void xrchart_draw_spreadlevel (const struct chart_item *, cairo_t *,
                         struct xrchart_geometry *);
void xrchart_draw_scatterplot (const struct chart_item *, cairo_t *,
                         struct xrchart_geometry *);


#endif /* output/cairo-chart.h */
