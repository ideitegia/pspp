/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2009 Free Software Foundation, Inc.

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

#ifndef OUTPUT_PLOT_HIST_H
#define OUTPUT_PLOT_HIST_H

#include <stdbool.h>

struct chart;
struct histogram;

/* Creates and returns a new chart that depicts a histogram of
   the data in HIST with the given LABEL.  Labels the histogram
   with each of N, MEAN, and STDDEV that is not SYSMIS.  If all
   three are not SYSMIS and SHOW_NORMAL is true, also draws a
   normal curve on the histogram. */
struct chart *histogram_chart_create (const struct histogram *hist,
                                      const char *label,
                                      double n, double mean, double stddev,
                                      bool show_normal);

#endif /* output/plot-hist.h */
