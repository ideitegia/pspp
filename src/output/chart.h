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

#ifndef OUTPUT_CHART_H
#define OUTPUT_CHART_H 1

/* <plot.h> requires <stdio.h> but doesn't #include it. */
#include <stdio.h>

#ifdef HAVE_CHARTS
#include <plot.h>
#else
#include <output/charts/libplot-dummy.h>
#endif

struct chart;

struct chart *chart_ref (const struct chart *);
void chart_unref (struct chart *);

void chart_submit (struct chart *);

#endif /* output/chart.h */
