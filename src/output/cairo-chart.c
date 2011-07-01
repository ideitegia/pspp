/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include <assert.h>
#include <cairo/cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/assertion.h"
#include "math/chart-geometry.h"
#include "output/cairo.h"
#include "output/chart-item.h"

#include "gl/error.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

void
xrchart_geometry_init (cairo_t *cr, struct xrchart_geometry *geom,
                       double width, double length)
{
  /* Set default chartetry. */
  geom->data_top = 0.900 * length;
  geom->data_right = 0.800 * width;
  geom->data_bottom = 0.120 * length;
  geom->data_left = 0.150 * width;
  geom->abscissa_top = 0.070 * length;
  geom->ordinate_right = 0.120 * width;
  geom->title_bottom = 0.920 * length;
  geom->legend_left = 0.810 * width;
  geom->legend_right = width;
  geom->font_size = 15.0;
  geom->in_path = false;
  geom->dataset = NULL;
  geom->n_datasets = 0;

  geom->fill_colour.red = 255;
  geom->fill_colour.green = 0;
  geom->fill_colour.blue = 0;

  cairo_set_line_width (cr, 1.0);

  cairo_rectangle (cr, geom->data_left, geom->data_bottom,
                   geom->data_right - geom->data_left,
                   geom->data_top - geom->data_bottom);
  cairo_stroke (cr);
}

void
xrchart_geometry_free (cairo_t *cr UNUSED, struct xrchart_geometry *geom)
{
  int i;

  for (i = 0 ; i < geom->n_datasets; ++i)
    free (geom->dataset[i]);
  free (geom->dataset);
}

#if ! PANGO_VERSION_CHECK (1, 22, 0)
int pango_layout_get_baseline (PangoLayout    *layout);

/* Shamelessly copied from the pango source */
int
pango_layout_get_baseline (PangoLayout    *layout)
{
  int baseline;

  /* XXX this is so inefficient */
  PangoLayoutIter *iter = pango_layout_get_iter (layout);
  baseline = pango_layout_iter_get_baseline (iter);
  pango_layout_iter_free (iter);

  return baseline;
}
#endif



const struct xrchart_colour data_colour[XRCHART_N_COLOURS] =
  {
    { 165, 42, 42 },            /* brown */
    { 255, 0, 0 },              /* red */
    { 255, 165, 0 },            /* orange */
    { 255, 255, 0 },            /* yellow */
    { 0, 255, 0 },              /* green */
    { 0, 0, 255 },              /* blue */
    { 238, 130, 238 },          /* violet */
    { 190, 190, 190 },          /* grey */
    { 255, 192, 203 },          /* pink */
  };

void
xrchart_draw_marker (cairo_t *cr, double x, double y,
                     enum xrmarker_type marker, double size)
{
  cairo_save (cr);
  cairo_translate (cr, x, y);
  cairo_scale (cr, size / 2.0, size / 2.0);
  cairo_set_line_width (cr, cairo_get_line_width (cr) / (size / 2.0));
  switch (marker)
    {
    case XRMARKER_CIRCLE:
      cairo_arc (cr, 0, 0, 1.0, 0, 2 * M_PI);
      cairo_stroke (cr);
      break;

    case XRMARKER_ASTERISK:
      cairo_move_to (cr, 0, -1.0); /* | */
      cairo_line_to (cr, 0, 1.0);
      cairo_move_to (cr, -M_SQRT1_2, -M_SQRT1_2); /* / */
      cairo_line_to (cr, M_SQRT1_2, M_SQRT1_2);
      cairo_move_to (cr, -M_SQRT1_2, M_SQRT1_2); /* \ */
      cairo_line_to (cr, M_SQRT1_2, -M_SQRT1_2);
      cairo_stroke (cr);
      break;

    case XRMARKER_SQUARE:
      cairo_rectangle (cr, -1.0, -1.0, 2.0, 2.0);
      cairo_stroke (cr);
      break;
    }
  cairo_restore (cr);
}

void
xrchart_label (cairo_t *cr, int horz_justify, int vert_justify,
               double font_size, const char *string)
{
  PangoFontDescription *desc;
  PangoLayout *layout;
  double x, y;

  desc = pango_font_description_from_string ("sans serif");
  if (desc == NULL)
    {
      cairo_new_path (cr);
      return;
    }
  pango_font_description_set_absolute_size (desc, font_size * PANGO_SCALE);

  cairo_save (cr);
  cairo_get_current_point (cr, &x, &y);
  cairo_translate (cr, x, y);
  cairo_move_to (cr, 0, 0);
  cairo_scale (cr, 1.0, -1.0);

  layout = pango_cairo_create_layout (cr);
  pango_layout_set_font_description (layout, desc);
  pango_layout_set_text (layout, string, -1);
  if (horz_justify != 'l')
    {
      int width_pango;
      double width;

      pango_layout_get_size (layout, &width_pango, NULL);
      width = (double) width_pango / PANGO_SCALE;
      if (horz_justify == 'r')
        cairo_rel_move_to (cr, -width, 0);
      else
        cairo_rel_move_to (cr, -width / 2.0, 0);
    }
  if (vert_justify == 'x')
    {
      int baseline_pango = pango_layout_get_baseline (layout);
      double baseline = (double) baseline_pango / PANGO_SCALE;
      cairo_rel_move_to (cr, 0, -baseline);
    }
  else if (vert_justify != 't')
    {
      int height_pango;
      double height;

      pango_layout_get_size (layout, NULL, &height_pango);
      height = (double) height_pango / PANGO_SCALE;
      if (vert_justify == 'b')
        cairo_rel_move_to (cr, 0, -height);
      else if (vert_justify == 'c')
        cairo_rel_move_to (cr, 0, -height / 2.0);
    }
  pango_cairo_show_layout (cr, layout);
  g_object_unref (layout);

  cairo_restore (cr);

  cairo_new_path (cr);

  pango_font_description_free (desc);
}

/* Draw a tick mark at position
   If label is non zero, then print it at the tick mark
*/
void
draw_tick (cairo_t *cr, const struct xrchart_geometry *geom,
           enum tick_orientation orientation,
           double position,
           const char *label, ...)
{
  const int tickSize = 10;
  double x, y;

  cairo_move_to (cr, geom->data_left, geom->data_bottom);

  if (orientation == TICK_ABSCISSA)
    {
      cairo_rel_move_to (cr, position, 0);
      cairo_rel_line_to (cr, 0, -tickSize);
    }
  else if (orientation == TICK_ORDINATE)
    {
      cairo_rel_move_to (cr, 0, position);
      cairo_rel_line_to (cr, -tickSize, 0);
    }
  else
    NOT_REACHED ();
  cairo_get_current_point (cr, &x, &y);

  cairo_stroke (cr);

  if (label != NULL)
    {
      va_list ap;
      char *s;

      cairo_move_to (cr, x, y);

      va_start (ap, label);
      s = xvasprintf (label, ap);
      if (orientation == TICK_ABSCISSA)
        xrchart_label (cr, 'c', 't', geom->font_size, s);
      else if (orientation == TICK_ORDINATE)
        {
          if (fabs (position) < DBL_EPSILON)
	    cairo_rel_move_to (cr, 0, 10);
          xrchart_label (cr, 'r', 'c', geom->font_size, s);
        }
      free (s);
      va_end (ap);
    }
}


/* Write the title on a chart*/
void
xrchart_write_title (cairo_t *cr, const struct xrchart_geometry *geom,
                   const char *title, ...)
{
  va_list ap;
  char *s;

  cairo_save (cr);
  cairo_move_to (cr, geom->data_left, geom->title_bottom);

  va_start(ap, title);
  s = xvasprintf (title, ap);
  xrchart_label (cr, 'l', 'x', geom->font_size * 1.5, s);
  free (s);
  va_end (ap);

  cairo_restore (cr);
}


/* Set the scale for the abscissa */
void
xrchart_write_xscale (cairo_t *cr, struct xrchart_geometry *geom,
                    double min, double max, int ticks)
{
  double x;

  const double tick_interval =
    chart_rounded_tick ((max - min) / (double) ticks);

  geom->x_max = ceil (max / tick_interval) * tick_interval;
  geom->x_min = floor (min / tick_interval) * tick_interval;
  geom->abscissa_scale = fabs(geom->data_right - geom->data_left) /
    fabs(geom->x_max - geom->x_min);

  for (x = geom->x_min; x <= geom->x_max; x += tick_interval)
    draw_tick (cr, geom, TICK_ABSCISSA,
               (x - geom->x_min) * geom->abscissa_scale, "%g", x);
}


/* Set the scale for the ordinate */
void
xrchart_write_yscale (cairo_t *cr, struct xrchart_geometry *geom,
                    double smin, double smax, int ticks)
{
  double y;

  const double tick_interval =
    chart_rounded_tick ((smax - smin) / (double) ticks);

  geom->y_max = ceil (smax / tick_interval) * tick_interval;
  geom->y_min = floor (smin / tick_interval) * tick_interval;

  geom->ordinate_scale =
    (fabs (geom->data_top - geom->data_bottom)
     / fabs (geom->y_max - geom->y_min));

  for (y = geom->y_min; y <= geom->y_max; y += tick_interval)
    draw_tick (cr, geom, TICK_ORDINATE,
	       (y - geom->y_min) * geom->ordinate_scale, "%g", y);
}

/* Write the abscissa label */
void
xrchart_write_xlabel (cairo_t *cr, const struct xrchart_geometry *geom,
                    const char *label)
{
  cairo_move_to (cr, geom->data_left, geom->abscissa_top);
  xrchart_label (cr, 'l', 't', geom->font_size, label);
}

/* Write the ordinate label */
void
xrchart_write_ylabel (cairo_t *cr, const struct xrchart_geometry *geom,
                    const char *label)
{
  cairo_save (cr);
  cairo_translate (cr, -geom->data_bottom, -geom->ordinate_right);
  cairo_move_to (cr, 0, 0);
  cairo_rotate (cr, M_PI / 2.0);
  xrchart_label (cr, 'l', 'x', geom->font_size, label);
  cairo_restore (cr);
}


void
xrchart_write_legend (cairo_t *cr, const struct xrchart_geometry *geom)
{
  int i;
  const int vstep = geom->font_size * 2;
  const int xpad = 10;
  const int ypad = 10;
  const int swatch = 20;
  const int legend_top = geom->data_top;
  const int legend_bottom = legend_top -
    (vstep * geom->n_datasets + 2 * ypad );

  cairo_save (cr);

  cairo_rectangle (cr, geom->legend_left, legend_top,
                   geom->legend_right - xpad - geom->legend_left,
                   legend_bottom - legend_top);
  cairo_stroke (cr);

  for (i = 0 ; i < geom->n_datasets ; ++i )
    {
      const int ypos = legend_top - vstep * (i + 1);
      const int xpos = geom->legend_left + xpad;
      const struct xrchart_colour *colour;

      cairo_move_to (cr, xpos, ypos);

      cairo_save (cr);
      colour = &data_colour [ i % XRCHART_N_COLOURS];
      cairo_set_source_rgb (cr,
                            colour->red / 255.0,
                            colour->green / 255.0,
                            colour->blue / 255.0);
      cairo_rectangle (cr, xpos, ypos, swatch, swatch);
      cairo_fill_preserve (cr);
      cairo_stroke (cr);
      cairo_restore (cr);

      cairo_move_to (cr, xpos + swatch * 1.5, ypos);
      xrchart_label (cr, 'l', 'x', geom->font_size, geom->dataset[i]);
    }

  cairo_restore (cr);
}

/* Start a new vector called NAME */
void
xrchart_vector_start (cairo_t *cr, struct xrchart_geometry *geom, const char *name)
{
  const struct xrchart_colour *colour;

  cairo_save (cr);

  colour = &data_colour[geom->n_datasets % XRCHART_N_COLOURS];
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
xrchart_datum (cairo_t *cr, const struct xrchart_geometry *geom,
             int dataset UNUSED, double x, double y)
{
  double x_pos = (x - geom->x_min) * geom->abscissa_scale + geom->data_left;
  double y_pos = (y - geom->y_min) * geom->ordinate_scale + geom->data_bottom;

  xrchart_draw_marker (cr, x_pos, y_pos, XRMARKER_SQUARE, 15);
}

void
xrchart_vector_end (cairo_t *cr, struct xrchart_geometry *geom)
{
  cairo_stroke (cr);
  cairo_restore (cr);
  geom->in_path = false;
}

/* Plot a data point */
void
xrchart_vector (cairo_t *cr, struct xrchart_geometry *geom, double x, double y)
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
   If lim_dim is XRCHART_DIM_Y then the limit{1,2} are on the
   y axis otherwise the x axis
*/
void
xrchart_line(cairo_t *cr, const struct xrchart_geometry *geom,
           double slope, double intercept,
	   double limit1, double limit2, enum xrchart_dim lim_dim)
{
  double x1, y1;
  double x2, y2;

  if ( lim_dim == XRCHART_DIM_Y )
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
