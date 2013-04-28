/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009, 2011 Free Software Foundation, Inc.

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

#ifndef __HISTOGRAM_H__
#define __HISTOGRAM_H__

#include <stddef.h>

#include "math/statistic.h"

#include <gsl/gsl_histogram.h>


struct histogram
{
  struct statistic parent;
  gsl_histogram *gsl_hist;
};

/* 
   Prepare a histogram for data which lies in the range [min, max)
   bin_width is a nominal figure only.  It is a hint about what might be
   an good approximate bin width, but the implementation will adjust it
   as it thinks fit.
 */
struct histogram * histogram_create (double bin_width, double min, double max);

void histogram_add (struct histogram *h, double y, double c);


#endif
