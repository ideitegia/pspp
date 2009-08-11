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

#ifndef LANGUAGE_STATS_ROC_H
#define LANGUAGE_STATS_ROC_H 1

/* These are case indexes into the cutpoint case readers for ROC
   output, used by roc.c and roc-chart.c. */
#define ROC_CUTPOINT 0
#define ROC_TP 1
#define ROC_FN 2
#define ROC_TN 3
#define ROC_FP 4

#endif /* language/stats/roc.h */
