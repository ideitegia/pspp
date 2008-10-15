/* PSPP - a program for statistical analysis.
   Copyright (C) 2008 Free Software Foundation, Inc.

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

/*
  Create covariance matrices for procedures that need them.
 */

#ifndef COVARIANCE_MATRIX_H
#define COVARIANCE_MATRIX_H

#include <math/design-matrix.h>

struct moments1;
struct ccase;
struct hsh_table;

struct design_matrix *
covariance_matrix_create (size_t, const struct variable *[]);

void covariance_matrix_destroy (struct design_matrix *);

void covariance_pass_two (struct design_matrix *, double,
			  double, double, const struct variable *, 
			  const struct variable *, const union value *, const union value *);
void covariance_accumulate (struct hsh_table *, struct moments1 **,
			    const struct ccase *, const struct variable **, size_t);
struct hsh_table * covariance_hsh_create (size_t);
struct design_matrix * covariance_accumulator_to_matrix (struct hsh_table *, const struct moments1 **,
							 const struct variable **, size_t, size_t);
#endif
