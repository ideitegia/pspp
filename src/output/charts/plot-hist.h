/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
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

#ifndef PLOT_HIST_H
#define PLOT_HIST_H

#include <config.h>
#include <gsl/gsl_histogram.h>


struct normal_curve
{
  double N ;
  double mean ;
  double stddev ;
};
struct chart;

/* Write the legend of the chart */
void histogram_write_legend(struct chart *ch, const struct normal_curve *norm);

void histogram_plot(const gsl_histogram *hist,
	       const char *factorname,
	       const struct normal_curve *norm, short show_normal);


#endif
