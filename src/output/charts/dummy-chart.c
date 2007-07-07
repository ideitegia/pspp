/* PSPP - a program for statistical analysis.
   Copyright (C) 2005 Free Software Foundation, Inc.

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


/* Stubs for plotting routines.
   This module is linked only when charts are not supported */

#include "config.h"
#include <output/chart.h>
#include <output/charts/box-whisker.h>
#include <output/charts/piechart.h>
#include <output/charts/plot-chart.h>
#include <output/charts/plot-hist.h>
#include <output/charts/cartesian.h>
#include <gsl/gsl_histogram.h>
#include <libpspp/compiler.h>

#ifndef NO_CHARTS
#error This file should be used only when compiling without charts.
#endif

struct chart *
chart_create(void)
{
  return 0;
}


void
chart_write_title(struct chart *chart UNUSED, const char *title UNUSED, ...)
{
}


void
chart_submit(struct chart *chart UNUSED)
{
}


void
chart_write_xscale(struct chart *ch UNUSED,
                   double min UNUSED, double max UNUSED, int ticks UNUSED)
{
}


void
chart_write_yscale(struct chart *ch UNUSED UNUSED,
                   double smin UNUSED, double smax UNUSED, int ticks UNUSED)
{
}


void
chart_write_xlabel(struct chart *ch UNUSED, const char *label UNUSED)
{
}

void
chart_write_ylabel(struct chart *ch UNUSED, const char *label UNUSED)
{
}


void
chart_line(struct chart *ch UNUSED,
           double slope UNUSED, double intercept UNUSED,
	   double limit1 UNUSED, double limit2 UNUSED,
           enum CHART_DIM lim_dim UNUSED)
{
}


void
chart_datum(struct chart *ch UNUSED, int dataset UNUSED UNUSED,
            double x UNUSED, double y UNUSED)
{
}

struct normal_curve;

void
histogram_plot(const gsl_histogram *hist UNUSED,
	       const char *factorname UNUSED,
	       const struct normal_curve *norm UNUSED,
               short show_normal UNUSED)
{
}

void
boxplot_draw_yscale(struct chart *ch UNUSED,
                    double y_max UNUSED, double y_min UNUSED)
{
}

void
boxplot_draw_boxplot(struct chart *ch UNUSED,
		     double box_centre UNUSED,
		     double box_width UNUSED,
		     struct metrics *m UNUSED,
		     const char *name UNUSED)
{
}



void
piechart_plot(const char *title UNUSED,
              const struct slice *slices UNUSED, int n_slices UNUSED)
{
}
