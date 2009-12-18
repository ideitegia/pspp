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


#ifndef SRC_MATH_CORRELATION_H
#define SRC_MATH_CORRELATION_H

#include <gsl/gsl_matrix.h>

gsl_matrix * correlation_from_covariance (const gsl_matrix *cv, const gsl_matrix *v);

double significance_of_correlation (double rho, double w);

#endif
