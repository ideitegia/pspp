/* PSPP - a program for statistical analysis.
   Copyright (C) 2012 Free Software Foundation, Inc.

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

#include "output/charts/spreadlevel-plot.h"

#include <math.h>

#include "output/cairo-chart.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

void
xrchart_draw_spreadlevel (const struct chart_item *chart_item, cairo_t *cr,
                    struct xrchart_geometry *geom)
{
  const struct spreadlevel_plot_chart *sl = to_spreadlevel_plot_chart (chart_item);
  size_t i;

  const char *name = chart_item_get_title (chart_item);

  xrchart_write_title (cr, geom, _("Spread vs. Level Plot of %s"), name);
  xrchart_write_xlabel (cr, geom, _("Level"));
  xrchart_write_ylabel (cr, geom, _("Spread"));
  

  xrchart_write_xscale (cr, geom, sl->x_lower, sl->x_upper, 5);
  xrchart_write_yscale (cr, geom, sl->y_lower, sl->y_upper, 5);

  for (i = 0 ; i < sl->n_data; ++i)
    {
      xrchart_datum (cr, geom, 0, sl->data[i].x, sl->data[i].y);
    }
}
