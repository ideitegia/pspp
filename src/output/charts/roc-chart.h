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

#ifndef OUTPUT_CHARTS_ROC_CHART_H
#define OUTPUT_CHARTS_ROC_CHART_H 1

#include <stdbool.h>

struct casereader;

struct roc_chart *roc_chart_create (bool reference);
void roc_chart_add_var (struct roc_chart *, const char *var_name,
                        const struct casereader *cutpoint_reader);
struct chart *roc_chart_get_chart (struct roc_chart *);

#endif /* output/charts/roc-chart.h */
