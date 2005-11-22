/* PSPP - linear regression.
   Copyright (C) 2005 Free Software Foundation, Inc.
   Written by Jason H Stover <jason@sakla.net>.

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
  Functions and data structures to recode categorical variables into
  vectors and sub-rows of matrices.
  
  To fit many types of statistical models, it is necessary
  to change each value of a categorical variable to a vector with binary
  entries. These vectors are then stored as sub-rows within a matrix
  during model-fitting. We need functions and data strucutres to,
  e.g., map a value, say 'a', of a variable named 'cat_var', to a
  vector, say (0 1 0 0 0), and vice versa.  We also need to be able
  to map the vector back to the value 'a', and if the vector is a
  sub-row of a matrix, we need to know which sub-row corresponds to
  the variable 'cat_var'.

  The data structures defined here will be placed in the variable 
  structure in the future. When that happens, the useful code
  in this file will be that which refers to design matrices.
 */

#ifndef CAT_H
#define CAT_H 1

#include <gsl/gsl_matrix.h>
#include <stdbool.h>
/*
  This structure contains the observed values of a 
  categorical variable.
 */
struct cat_vals
{
  union value *vals;
  size_t n_categories;
  size_t n_allocated_categories;	/* This is used only during
					   initialization to keep
					   track of the number of
					   values stored.
					 */
};

/*
  There are usually multiple categorical variables to recode.  Get rid
  of this structure immediately when the variable structure has been
  modified to contain the binary encoding.
 */
struct recoded_categorical_array
{
  struct recoded_categorical **a;
  size_t n_vars;
};
/*
  The design matrix structure holds the design
  matrix and an array to tell us which columns
  correspond to which variables. This structure 
  is not restricted to categorical variables, and
  perhaps should be moved to its own module.
*/

struct design_matrix_var
{
  int first_column;		/* First column for this variable in
				   the design_matix. If this variable
				   is categorical, its values are
				   stored in multiple, contiguous
				   columns, as dictated by its vector
				   encoding in the variable's struct
				   recoded_categorical.
				 */
  int last_column;
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
union value *cr_vector_to_value (const gsl_vector *, struct variable *);

void cat_stored_values_create (struct variable *);

void cat_value_update (struct variable *, const union value *);

int cat_free_recoded_array (struct recoded_categorical_array *);

struct recoded_categorical_array *cr_recoded_cat_ar_create (int,
							    struct variable
							    *[]);

void cat_recoded_categorical_create (struct variable *);

void cat_create_value_matrix (struct variable *);

struct recoded_categorical *cat_var_to_recoded_categorical (const struct
							    variable *,
							    struct
							    recoded_categorical_array
							    *);

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

void
design_matrix_set (struct design_matrix *, size_t,
		   const struct variable *, const union value *,
		   struct recoded_categorical *);

void cat_stored_values_destroy (struct variable *);

#endif
