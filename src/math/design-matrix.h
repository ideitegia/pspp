/* PSPP - Creates design matrices.
   Copyright (C) 2005 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

/*
  Create design matrices for procedures that need them.
 */

#ifndef DESIGN_MATRIX_H
#define DESIGN_MATRIX_H

#include <gsl/gsl_matrix.h>
#include <stdbool.h>
#include <data/category.h>
#include <data/cat-routines.h>
struct design_matrix_var
{
  size_t first_column;		/* First column for this variable in
				   the design_matix. If this variable
				   is categorical, its values are
				   stored in multiple, contiguous
				   columns, as dictated by its vector
				   encoding in the variable's struct
				   cat_vals.
				 */
  size_t last_column;
  const struct variable *v;
};
struct design_matrix
{
  gsl_matrix *m;
  struct design_matrix_var *vars;	/* Element i corresponds to
					   the variable whose values
					   are stored in at least one
					   column of m. If that
					   variable is categorical
					   with more than two
					   categories, its values are
					   stored in multiple,
					   contiguous columns. The
					   variable's values are then
					   stored in the columns
					   first_column through
					   last_column of the
					   design_matrix_var
					   structure.
					 */
  size_t n_vars;
};
union value *cat_vector_to_value (const gsl_vector *, struct variable *);

struct design_matrix *design_matrix_create (int, const struct variable *[],
					    const size_t);

void design_matrix_destroy (struct design_matrix *);

void design_matrix_set_categorical (struct design_matrix *, size_t,
				    const struct variable *,
				    const union value *);

void design_matrix_set_numeric (struct design_matrix *, size_t,
				const struct variable *, const union value *);

size_t design_matrix_var_to_column (const struct design_matrix *,
				    const struct variable *);

struct variable *design_matrix_col_to_var (const struct design_matrix *,
					   size_t);

#endif
