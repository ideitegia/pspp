/* PSPP - computes sample statistics.
   Copyright (C) 2005 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */


/* Stubs for plotting routines.
   This module is linked only when charts are not supported */

#include "config.h"
#include "chart.h"


#ifndef NO_CHARTS
#error This file should be used only when compiling without charts.
#endif

struct chart *
chart_create(void)
{
  return 0;
}


void  
chart_write_title(struct chart *chart, const char *title, ...)
{
}


void
chart_submit(struct chart *chart)
{
}


void 
chart_write_xscale(struct chart *ch, double min, double max, int ticks)
{
}


void 
chart_write_yscale(struct chart *ch, double smin, double smax, int ticks)
{
}


void 
chart_write_xlabel(struct chart *ch, const char *label)
{
}

void 
chart_write_ylabel(struct chart *ch, const char *label)
{
}


void
chart_line(struct chart *ch, double slope, double intercept, 
	   double limit1, double limit2, enum CHART_DIM lim_dim)
{
}


void
chart_datum(struct chart *ch, int dataset UNUSED, double x, double y)
{
}


void
histogram_plot(const gsl_histogram *hist,
	       const char *factorname,
	       const struct normal_curve *norm, short show_normal)
{
}

void
boxplot_draw_yscale(struct chart *ch , double y_max, double y_min)
{
}

void 
boxplot_draw_boxplot(struct chart *ch,
		     double box_centre, 
		     double box_width,
		     struct metrics *m,
		     const char *name)
{
}



void
piechart_plot(const char *title, const struct slice *slices, int n_slices)
{
}
