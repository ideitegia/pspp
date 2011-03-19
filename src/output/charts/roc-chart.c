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

#include <config.h>

#include "output/charts/roc-chart.h"

#include "data/casereader.h"
#include "language/stats/roc.h"
#include "output/chart-item-provider.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct roc_chart *
roc_chart_create (bool reference)
{
  struct roc_chart *rc = xmalloc (sizeof *rc);
  chart_item_init (&rc->chart_item, &roc_chart_class, NULL);
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

static void
roc_chart_destroy (struct chart_item *chart_item)
{
  struct roc_chart *rc = UP_CAST (chart_item, struct roc_chart, chart_item);
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

const struct chart_item_class roc_chart_class =
  {
    roc_chart_destroy
  };
