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

#ifndef OUTPUT_CHARTS_SCREE_H
#define OUTPUT_CHARTS_SCREE_H 1

#include <gsl/gsl_vector.h>

struct scree;
struct chart;

/* Create a "Scree Plot" of EIGENVALUES with LABEL on the X Axis */
struct scree *scree_create (const gsl_vector *eigenvalues, const char *label);

/* Return the chart underlying SCREE */
struct chart *scree_get_chart (struct scree *scree);

#endif 
