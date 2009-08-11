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

#include <output/charts/plot-chart.h>

#include <assert.h>
#include <float.h>
#include <math.h>
#include <pango/pango-font.h>
#include <pango/pango-layout.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdio.h>
#include <string.h>

#include <libpspp/assertion.h>
#include <libpspp/str.h>
#include <math/chart-geometry.h>
#include <output/chart-provider.h>
#include <output/manager.h>
#include <output/output.h>

#include "xalloc.h"

const struct chart_colour data_colour[N_CHART_COLOURS] =
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
chart_draw_marker (cairo_t *cr, double x, double y, enum marker_type marker,
                   double size)
{
  cairo_save (cr);
  cairo_translate (cr, x, y);
  cairo_scale (cr, size / 2.0, size / 2.0);
  cairo_set_line_width (cr, cairo_get_line_width (cr) / (size / 2.0));
  switch (marker)
    {
    case MARKER_CIRCLE:
      cairo_arc (cr, 0, 0, 1.0, 0, 2 * M_PI);
      cairo_stroke (cr);
      break;

    case MARKER_ASTERISK:
      cairo_move_to (cr, 0, -1.0); /* | */
      cairo_line_to (cr, 0, 1.0);
      cairo_move_to (cr, -M_SQRT1_2, -M_SQRT1_2); /* / */
      cairo_line_to (cr, M_SQRT1_2, M_SQRT1_2);
      cairo_move_to (cr, -M_SQRT1_2, M_SQRT1_2); /* \ */
      cairo_line_to (cr, M_SQRT1_2, -M_SQRT1_2);
      cairo_stroke (cr);
      break;

    case MARKER_SQUARE:
      cairo_rectangle (cr, -1.0, -1.0, 2.0, 2.0);
      cairo_stroke (cr);
      break;
    }
  cairo_restore (cr);
}

void
chart_label (cairo_t *cr, int horz_justify, int vert_justify, double font_size,
             const char *string)
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
draw_tick (cairo_t *cr, const struct chart_geometry *geom,
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
        chart_label (cr, 'c', 't', geom->font_size, s);
      else if (orientation == TICK_ORDINATE)
        {
          if (fabs (position) < DBL_EPSILON)
	    cairo_rel_move_to (cr, 0, 10);
          chart_label (cr, 'r', 'c', geom->font_size, s);
        }
      free (s);
      va_end (ap);
    }
}


/* Write the title on a chart*/
void
chart_write_title (cairo_t *cr, const struct chart_geometry *geom,
                   const char *title, ...)
{
  va_list ap;
  char *s;

  cairo_save (cr);
  cairo_move_to (cr, geom->data_left, geom->title_bottom);

  va_start(ap, title);
  s = xvasprintf (title, ap);
  chart_label (cr, 'l', 'x', geom->font_size * 1.5, s);
  free (s);
  va_end (ap);

  cairo_restore (cr);
}


/* Set the scale for the abscissa */
void
chart_write_xscale (cairo_t *cr, struct chart_geometry *geom,
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
chart_write_yscale (cairo_t *cr, struct chart_geometry *geom,
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
chart_write_xlabel (cairo_t *cr, const struct chart_geometry *geom,
                    const char *label)
{
  cairo_move_to (cr, geom->data_left, geom->abscissa_top);
  chart_label (cr, 'l', 't', geom->font_size, label);
}

/* Write the ordinate label */
void
chart_write_ylabel (cairo_t *cr, const struct chart_geometry *geom,
                    const char *label)
{
  cairo_save (cr);
  cairo_translate (cr, -geom->data_bottom, -geom->ordinate_right);
  cairo_move_to (cr, 0, 0);
  cairo_rotate (cr, M_PI / 2.0);
  chart_label (cr, 'l', 'x', geom->font_size, label);
  cairo_restore (cr);
}


void
chart_write_legend (cairo_t *cr, const struct chart_geometry *geom)
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
      const struct chart_colour *colour;

      cairo_move_to (cr, xpos, ypos);

      cairo_save (cr);
      colour = &data_colour [ i % N_CHART_COLOURS];
      cairo_set_source_rgb (cr,
                            colour->red / 255.0,
                            colour->green / 255.0,
                            colour->blue / 255.0);
      cairo_rectangle (cr, xpos, ypos, swatch, swatch);
      cairo_fill_preserve (cr);
      cairo_stroke (cr);
      cairo_restore (cr);

      cairo_move_to (cr, xpos + swatch * 1.5, ypos);
      chart_label (cr, 'l', 'x', geom->font_size, geom->dataset[i]);
    }

  cairo_restore (cr);
}
