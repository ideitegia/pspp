/* PSPP - a program for statistical analysis.
   Copyright (C) 2009 Free Software Foundation, Inc.

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

#include <output/charts/scree.h>

#include <math.h>

#include <output/cairo-chart.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

void
xrchart_draw_scree (const struct chart_item *chart_item, cairo_t *cr,
                    struct xrchart_geometry *geom)
{
  const struct scree *rc = to_scree (chart_item);
  size_t i;
  double min, max;

  xrchart_write_title (cr, geom, _("Scree Plot"));
  xrchart_write_xlabel (cr, geom, rc->xlabel);
  xrchart_write_ylabel (cr, geom, _("Eigenvalue"));

  gsl_vector_minmax (rc->eval, &min, &max);

  if ( fabs (max) > fabs (min))
    max = fabs (max);
  else
    max = fabs (min);

  xrchart_write_yscale (cr, geom, 0, max, max);
  xrchart_write_xscale (cr, geom, 0, rc->eval->size + 1, rc->eval->size + 1);

  xrchart_vector_start (cr, geom, "");
  for (i = 0 ; i < rc->eval->size; ++i)
    {
      const double x = 1 + i;
      const double y = gsl_vector_get (rc->eval, i);
      xrchart_vector (cr, geom, x, y);
    }
  xrchart_vector_end (cr, geom);

  for (i = 0 ; i < rc->eval->size; ++i)
    {
      const double x = 1 + i;
      const double y = gsl_vector_get (rc->eval, i);
      xrchart_datum (cr, geom, 0, x, y);
    }
}
