/* PSPP - a program for statistical analysis.
   Copyright (C) 2004 Free Software Foundation, Inc.

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
#include <math.h>
#include <gsl/gsl_histogram.h>
#include <assert.h>
#include "histogram.h"
#include "chart-geometry.h"


gsl_histogram *
histogram_create(double bins, double x_min, double x_max)
{
  int n;
  double bin_width ;
  double bin_width_2 ;
  double upper_limit, lower_limit;

  gsl_histogram *hist = gsl_histogram_alloc(bins);

  bin_width = chart_rounded_tick((x_max - x_min)/ bins);
  bin_width_2 = bin_width / 2.0;

  n =  ceil( x_max / (bin_width_2) ) ;
  if ( ! (n % 2 ) ) n++;
  upper_limit = n * bin_width_2;

  n =  floor( x_min / (bin_width_2) ) ;
  if ( ! (n % 2 ) ) n--;
  lower_limit = n * bin_width_2;

  gsl_histogram_set_ranges_uniform(hist, lower_limit, upper_limit);

  return hist;
}

