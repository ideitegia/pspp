/* PSPP - a program for statistical analysis.
   Copyright (C) 2014 Free Software Foundation, Inc.

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

#include "output/charts/scatterplot.h"

#include <gsl/gsl_cdf.h>

#include "data/casereader.h"
#include "libpspp/cast.h"
#include "output/chart-item-provider.h"

#include "gl/minmax.h"


/* Creates a scatterplot 

   The caller retains ownership of READER. */
struct scatterplot_chart *
scatterplot_create (const struct casereader *reader, 
		    const struct variable *xvar, 
		    const struct variable *yvar,
		    const struct variable *byvar,
		    bool *byvar_overflow,
		    const char *label,
		    double xmin, double xmax, double ymin, double ymax)
{
  struct scatterplot_chart *spc;

  spc = xzalloc (sizeof *spc);
  chart_item_init (&spc->chart_item, &scatterplot_chart_class, label);
  spc->data = casereader_clone (reader);

  spc->y_min = ymin;
  spc->y_max = ymax;

  spc->x_min = xmin;
  spc->x_max = xmax;

  spc->xvar = xvar;
  spc->yvar = yvar;
  spc->byvar = byvar;
  spc->byvar_overflow = byvar_overflow;

  return spc;
}

static void
scatterplot_chart_destroy (struct chart_item *chart_item)
{
  struct scatterplot_chart *spc = to_scatterplot_chart (chart_item);
  casereader_destroy (spc->data);
  free (spc);
}

const struct chart_item_class scatterplot_chart_class =
  {
    scatterplot_chart_destroy
  };
