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

#ifndef PLOT_HIST_H
#define PLOT_HIST_H

#include <stdbool.h>

struct chart;
struct moments1;
struct histogram;

/* Plot M onto histogram HIST and label it with LABEL */
void histogram_plot (const struct histogram *hist,
		     const char *label,  const struct moments1 *m);


/* A wrapper aroud histogram_plot.
   Don't use this function.  It's legacy only */
void histogram_plot_n (const struct histogram *hist,
		       const char *label,
		       double n, double mean, double var,
		       bool show_normal);


#endif
