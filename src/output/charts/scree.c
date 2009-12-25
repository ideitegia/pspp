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

#include <output/chart-provider.h>
#include <output/charts/cartesian.h>
#include <output/charts/plot-chart.h>

#include <gsl/gsl_vector.h>

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct scree
  {
    struct chart chart;
    const gsl_vector *eval;
    const char *xlabel;
  };

static const struct chart_class scree_class;

struct scree *
scree_create (const gsl_vector *eigenvalues, const char *xlabel)
{
  struct scree *rc = xmalloc (sizeof *rc);
  chart_init (&rc->chart, &scree_class);
  rc->eval = eigenvalues;
  rc->xlabel = xlabel;
  return rc;
}


struct chart *
scree_get_chart (struct scree *rc)
{
  return &rc->chart;
}

static void
scree_draw (const struct chart *chart, cairo_t *cr,
                struct chart_geometry *geom)
{
  const struct scree *rc = UP_CAST (chart, struct scree, chart);
  size_t i;
  double min, max;

  chart_write_title (cr, geom, _("Scree Plot"));
  chart_write_xlabel (cr, geom, rc->xlabel);
  chart_write_ylabel (cr, geom, _("Eigenvalue"));

  gsl_vector_minmax (rc->eval, &min, &max);

  if ( fabs (max) > fabs (min))
    max = fabs (max);
  else
    max = fabs (min);

  chart_write_yscale (cr, geom, 0, max, max);
  chart_write_xscale (cr, geom, 0, rc->eval->size + 1, rc->eval->size + 1);

  chart_vector_start (cr, geom, "");
  for (i = 0 ; i < rc->eval->size; ++i)
    {
      const double x = 1 + i;
      const double y = gsl_vector_get (rc->eval, i);
      chart_vector (cr, geom, x, y);
    }
  chart_vector_end (cr, geom);

  for (i = 0 ; i < rc->eval->size; ++i)
    {
      const double x = 1 + i;
      const double y = gsl_vector_get (rc->eval, i);
      chart_datum (cr, geom, 0, x, y);
    }
}

static void
scree_destroy (struct chart *chart)
{
  struct scree *rc = UP_CAST (chart, struct scree, chart);

  free (rc);
}

static const struct chart_class scree_class =
  {
    scree_draw,
    scree_destroy
  };


