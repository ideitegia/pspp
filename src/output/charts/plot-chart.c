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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <float.h>
#include <assert.h>
#include <math.h>

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



/* Draw a tick mark at position
   If label is non zero, then print it at the tick mark
*/
void
draw_tick (plPlotter *lp, const struct chart_geometry *geom,
           enum tick_orientation orientation,
           double position,
           const char *label, ...)
{
  const int tickSize = 10;

  pl_savestate_r (lp);
  pl_move_r (lp, geom->data_left, geom->data_bottom);

  if (orientation == TICK_ABSCISSA)
    pl_flinerel_r (lp, position, 0, position, -tickSize);
  else if (orientation == TICK_ORDINATE)
    pl_flinerel_r (lp, 0, position, -tickSize, position);
  else
    NOT_REACHED ();

  if (label != NULL)
    {
      va_list ap;
      char *s;

      va_start (ap, label);
      s = xvasprintf (label, ap);
      if (orientation == TICK_ABSCISSA)
        pl_alabel_r (lp, 'c', 't', s);
      else if (orientation == TICK_ORDINATE)
        {
          if (fabs (position) < DBL_EPSILON)
	    pl_moverel_r (lp, 0, 10);
          pl_alabel_r (lp, 'r', 'c', s);
        }
      free (s);
      va_end (ap);
    }

  pl_restorestate_r (lp);
}


/* Write the title on a chart*/
void
chart_write_title (plPlotter *lp, const struct chart_geometry *geom,
                   const char *title, ...)
{
  va_list ap;
  char *s;

  pl_savestate_r (lp);
  pl_ffontsize_r (lp, geom->font_size * 1.5);
  pl_move_r (lp, geom->data_left, geom->title_bottom);

  va_start(ap, title);
  s = xvasprintf (title, ap);
  pl_alabel_r (lp, 0, 0, s);
  free (s);
  va_end (ap);

  pl_restorestate_r (lp);
}


/* Set the scale for the abscissa */
void
chart_write_xscale (plPlotter *lp, struct chart_geometry *geom,
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
    draw_tick (lp, geom, TICK_ABSCISSA,
               (x - geom->x_min) * geom->abscissa_scale, "%g", x);
}


/* Set the scale for the ordinate */
void
chart_write_yscale (plPlotter *lp, struct chart_geometry *geom,
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
    draw_tick (lp, geom, TICK_ORDINATE,
	       (y - geom->y_min) * geom->ordinate_scale, "%g", y);
}

/* Write the abscissa label */
void
chart_write_xlabel (plPlotter *lp, const struct chart_geometry *geom,
                    const char *label)
{
  pl_savestate_r (lp);
  pl_move_r (lp, geom->data_left, geom->abscissa_top);
  pl_alabel_r (lp, 0, 't', label);
  pl_restorestate_r (lp);
}

/* Write the ordinate label */
void
chart_write_ylabel (plPlotter *lp, const struct chart_geometry *geom,
                    const char *label)
{
  pl_savestate_r (lp);
  pl_move_r (lp, geom->data_bottom, geom->ordinate_right);
  pl_textangle_r (lp, 90);
  pl_alabel_r (lp, 0, 0, label);
  pl_restorestate_r(lp);
}
