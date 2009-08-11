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

#include <output/charts/roc-chart.h>

#include <output/chart-provider.h>
#include <output/charts/cartesian.h>
#include <output/charts/plot-chart.h>
#include <data/casereader.h>
#include <language/stats/roc.h>

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct roc_var
  {
    char *name;
    struct casereader *cutpoint_reader;
  };

struct roc_chart
  {
    struct chart chart;
    bool reference;
    struct roc_var *vars;
    size_t n_vars;
    size_t allocated_vars;
  };

static const struct chart_class roc_chart_class;

struct roc_chart *
roc_chart_create (bool reference)
{
  struct roc_chart *rc = xmalloc (sizeof *rc);
  chart_init (&rc->chart, &roc_chart_class);
  rc->reference = reference;
  rc->vars = NULL;
  rc->n_vars = 0;
  rc->allocated_vars = 0;
  return rc;
}

void
roc_chart_add_var (struct roc_chart *rc, const char *var_name,
                   const struct casereader *cutpoint_reader)
{
  struct roc_var *rv;

  if (rc->n_vars >= rc->allocated_vars)
    rc->vars = x2nrealloc (rc->vars, &rc->allocated_vars, sizeof *rc->vars);

  rv = &rc->vars[rc->n_vars++];
  rv->name = xstrdup (var_name);
  rv->cutpoint_reader = casereader_clone (cutpoint_reader);
}

struct chart *
roc_chart_get_chart (struct roc_chart *rc)
{
  return &rc->chart;
}

static void
roc_chart_draw (const struct chart *chart, cairo_t *cr,
                struct chart_geometry *geom)
{
  const struct roc_chart *rc = UP_CAST (chart, struct roc_chart, chart);
  size_t i;

  chart_write_title (cr, geom, _("ROC Curve"));
  chart_write_xlabel (cr, geom, _("1 - Specificity"));
  chart_write_ylabel (cr, geom, _("Sensitivity"));

  chart_write_xscale (cr, geom, 0, 1, 5);
  chart_write_yscale (cr, geom, 0, 1, 5);

  if ( rc->reference )
    {
      chart_line (cr, geom, 1.0, 0,
		  0.0, 1.0,
		  CHART_DIM_X);
    }

  for (i = 0; i < rc->n_vars; ++i)
    {
      const struct roc_var *rv = &rc->vars[i];
      struct casereader *r = casereader_clone (rv->cutpoint_reader);
      struct ccase *cc;

      chart_vector_start (cr, geom, rv->name);
      for (; (cc = casereader_read (r)) != NULL; case_unref (cc))
	{
	  double se = case_data_idx (cc, ROC_TP)->f;
	  double sp = case_data_idx (cc, ROC_TN)->f;

	  se /= case_data_idx (cc, ROC_FN)->f + case_data_idx (cc, ROC_TP)->f ;
	  sp /= case_data_idx (cc, ROC_TN)->f + case_data_idx (cc, ROC_FP)->f ;

	  chart_vector (cr, geom, 1 - sp, se);
	}
      chart_vector_end (cr, geom);
      casereader_destroy (r);
    }

  chart_write_legend (cr, geom);
}

static void
roc_chart_destroy (struct chart *chart)
{
  struct roc_chart *rc = UP_CAST (chart, struct roc_chart, chart);
  size_t i;

  for (i = 0; i < rc->n_vars; i++)
    {
      struct roc_var *rv = &rc->vars[i];
      free (rv->name);
      casereader_destroy (rv->cutpoint_reader);
    }
  free (rc->vars);
  free (rc);
}

static const struct chart_class roc_chart_class =
  {
    roc_chart_draw,
    roc_chart_destroy
  };


