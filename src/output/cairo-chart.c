/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2009, 2010, 2011, 2014 Free Software Foundation, Inc.

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

#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

void
xrchart_geometry_init (cairo_t *cr, struct xrchart_geometry *geom,
                       double width, double length)
{
  /* Set default chart geometry. */
  geom->axis[SCALE_ORDINATE].data_max = 0.900 * length;
  geom->axis[SCALE_ORDINATE].data_min = 0.120 * length;

  geom->axis[SCALE_ABSCISSA].data_min = 0.150 * width;
  geom->axis[SCALE_ABSCISSA].data_max = 0.800 * width;
  geom->abscissa_bottom = 0.070 * length;
  geom->ordinate_left = 0.050 * width;
  geom->title_bottom = 0.920 * length;
  geom->legend_left = 0.810 * width;
  geom->legend_right = width;
  geom->font_size = 15.0;
  geom->in_path = false;
  geom->dataset = NULL;
  geom->n_datasets = 0;

  geom->fill_colour = data_colour[0];

  cairo_set_line_width (cr, 1.0);

  cairo_rectangle (cr, geom->axis[SCALE_ABSCISSA].data_min, geom->axis[SCALE_ORDINATE].data_min,
                   geom->axis[SCALE_ABSCISSA].data_max - geom->axis[SCALE_ABSCISSA].data_min,
                   geom->axis[SCALE_ORDINATE].data_max - geom->axis[SCALE_ORDINATE].data_min);
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

/*  
    These colours come from: 
    http://tango.freedesktop.org/static/cvs/tango-art-tools/palettes/Tango-Palette.gpl */
const struct xrchart_colour data_colour[XRCHART_N_COLOURS] =
  {
    {252, 233,  79},	/* Butter 1 */
    {138, 226,  52},	/* Chameleon 1 */
    {252, 175,  62},	/* Orange 1 */
    {114, 159, 207},	/* Sky Blue 1 */
    {173, 127, 168},	/* Plum 1 */
    {233, 185, 110},	/* Chocolate 1 */
    {239,  41,  41},	/* Scarlet Red 1 */
    {238, 238, 236},	/* Aluminium 1 */

    {237, 212,   0},	/* Butter 2 */
    {115, 210,  22},	/* Chameleon 2 */
    {245, 121,   0},	/* Orange 2 */
    {52,  101, 164},	/* Sky Blue 2 */
    {117,  80, 123},	/* Plum 2 */
    {193, 125,  17},	/* Chocolate 2 */
    {204,   0,   0},	/* Scarlet Red 2 */

    {136, 138, 133},	/* Aluminium 4 */

    {196, 160,   0},	/* Butter 3 */
    {78,  154,   6},	/* Chameleon 3 */
    {206,  92,   0},	/* Orange 3 */
    {32,   74, 135},	/* Sky Blue 3 */
    {92,   53, 102},	/* Plum 3 */
    {143,  89,   2},	/* Chocolate 3 */
    {164,   0,   0},	/* Scarlet Red 3 */
    {85,   87,  83},	/* Aluminium 5 */

    {211, 215, 207},	/* Aluminium 2 */
    {186, 189, 182},	/* Aluminium 3 */
    {46,   52,  54},	/* Aluminium 6 */
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
xrchart_label_rotate (cairo_t *cr, int horz_justify, int vert_justify,
		      double font_size, const char *string, double angle)
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
  cairo_rotate (cr, angle);
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

void
xrchart_label (cairo_t *cr, int horz_justify, int vert_justify,
               double font_size, const char *string)
{
  xrchart_label_rotate (cr, horz_justify, vert_justify, font_size, string, 0);
}


/* Draw a tick mark at position
   If label is non null, then print it at the tick mark
*/
static void
draw_tick_internal (cairo_t *cr, const struct xrchart_geometry *geom,
		    enum tick_orientation orientation,
		    bool rotated,
		    double position,
		    const char *s);

void
draw_tick (cairo_t *cr, const struct xrchart_geometry *geom,
           enum tick_orientation orientation,
	   bool rotated,
           double position,
           const char *label, ...)
{
  va_list ap;
  char *s;
  va_start (ap, label);
  s = xvasprintf (label, ap);

  if (fabs (position) < DBL_EPSILON)
    position = 0;

  draw_tick_internal (cr, geom, orientation, rotated, position, s);
  free (s);
  va_end (ap);
}


static void
draw_tick_internal (cairo_t *cr, const struct xrchart_geometry *geom,
		    enum tick_orientation orientation,
		    bool rotated,
		    double position,
		    const char *s)
{
  const int tickSize = 10;
  double x, y;

  cairo_move_to (cr, geom->axis[SCALE_ABSCISSA].data_min, geom->axis[SCALE_ORDINATE].data_min);

  if (orientation == SCALE_ABSCISSA)
    {
      cairo_rel_move_to (cr, position, 0);
      cairo_rel_line_to (cr, 0, -tickSize);
    }
  else if (orientation == SCALE_ORDINATE)
    {
      cairo_rel_move_to (cr, 0, position);
      cairo_rel_line_to (cr, -tickSize, 0);
    }
  else
    NOT_REACHED ();
  cairo_get_current_point (cr, &x, &y);

  cairo_stroke (cr);

  if (s != NULL)
    {
      cairo_move_to (cr, x, y);

      if (orientation == SCALE_ABSCISSA)
	{
	  if ( rotated) 
	    xrchart_label_rotate (cr, 'l', 'c', geom->font_size, s, -G_PI_4);
	  else
	    xrchart_label (cr, 'c', 't', geom->font_size, s);
	}
      else if (orientation == SCALE_ORDINATE)
        {
          if (fabs (position) < DBL_EPSILON)
	    cairo_rel_move_to (cr, 0, 10);
          xrchart_label (cr, 'r', 'c', geom->font_size, s);
        }
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
  cairo_move_to (cr, geom->axis[SCALE_ABSCISSA].data_min, geom->title_bottom);

  va_start(ap, title);
  s = xvasprintf (title, ap);
  xrchart_label (cr, 'l', 'x', geom->font_size * 1.5, s);
  free (s);
  va_end (ap);

  cairo_restore (cr);
}



static void
xrchart_write_scale (cairo_t *cr, struct xrchart_geometry *geom,
		     double smin, double smax, int ticks, enum tick_orientation orient)
{
  int s;

  const double tick_interval =
    chart_rounded_tick ((smax - smin) / (double) ticks);

  int upper = ceil (smax / tick_interval);
  int lower = floor (smin / tick_interval);

  geom->axis[orient].max = tick_interval * upper;
  geom->axis[orient].min = tick_interval * lower;

  geom->axis[orient].scale = (fabs (geom->axis[orient].data_max - geom->axis[orient].data_min)
     / fabs (geom->axis[orient].max - geom->axis[orient].min));

  for (s = 0 ; s < upper - lower; ++s)
    {
      double pos = (s + lower) * tick_interval;
      draw_tick (cr, geom, orient, false,
		 s * tick_interval * geom->axis[orient].scale, "%.*g",
                 DBL_DIG + 1, pos);
    }
}

/* Set the scale for the ordinate */
void
xrchart_write_yscale (cairo_t *cr, struct xrchart_geometry *geom,
                    double smin, double smax, int ticks)
{
  xrchart_write_scale (cr, geom, smin, smax, ticks, SCALE_ORDINATE);
}

/* Set the scale for the abscissa */
void
xrchart_write_xscale (cairo_t *cr, struct xrchart_geometry *geom,
                    double smin, double smax, int ticks)
{
  xrchart_write_scale (cr, geom, smin, smax, ticks, SCALE_ABSCISSA);
}


/* Write the abscissa label */
void
xrchart_write_xlabel (cairo_t *cr, const struct xrchart_geometry *geom,
                    const char *label)
{
  cairo_move_to (cr, geom->axis[SCALE_ABSCISSA].data_min, geom->abscissa_bottom);
  xrchart_label (cr, 'l', 't', geom->font_size, label);
}

/* Write the ordinate label */
void
xrchart_write_ylabel (cairo_t *cr, const struct xrchart_geometry *geom,
                    const char *label)
{
  cairo_save (cr);
  cairo_translate (cr, geom->ordinate_left,   geom->axis[SCALE_ORDINATE].data_min);
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
  const int legend_top = geom->axis[SCALE_ORDINATE].data_max;
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
  double x_pos = (x - geom->axis[SCALE_ABSCISSA].min) * geom->axis[SCALE_ABSCISSA].scale + geom->axis[SCALE_ABSCISSA].data_min;
  double y_pos = (y - geom->axis[SCALE_ORDINATE].min) * geom->axis[SCALE_ORDINATE].scale + geom->axis[SCALE_ORDINATE].data_min;

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
    (x - geom->axis[SCALE_ABSCISSA].min) * geom->axis[SCALE_ABSCISSA].scale + geom->axis[SCALE_ABSCISSA].data_min ;

  const double y_pos =
    (y - geom->axis[SCALE_ORDINATE].min) * geom->axis[SCALE_ORDINATE].scale + geom->axis[SCALE_ORDINATE].data_min ;

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

  y1 = (y1 - geom->axis[SCALE_ORDINATE].min) * geom->axis[SCALE_ORDINATE].scale + geom->axis[SCALE_ORDINATE].data_min;
  y2 = (y2 - geom->axis[SCALE_ORDINATE].min) * geom->axis[SCALE_ORDINATE].scale + geom->axis[SCALE_ORDINATE].data_min;
  x1 = (x1 - geom->axis[SCALE_ABSCISSA].min) * geom->axis[SCALE_ABSCISSA].scale + geom->axis[SCALE_ABSCISSA].data_min;
  x2 = (x2 - geom->axis[SCALE_ABSCISSA].min) * geom->axis[SCALE_ABSCISSA].scale + geom->axis[SCALE_ABSCISSA].data_min;

  cairo_move_to (cr, x1, y1);
  cairo_line_to (cr, x2, y2);
  cairo_stroke (cr);
}
