/* PSPP - Binary encodings for categorical variables.
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

 */

#ifndef CATEGORY_H
#define CATEGORY_H

#include <stddef.h>

struct cat_vals;
struct variable ; 
union value;

void cat_stored_values_create (const struct variable *);
void cat_stored_values_destroy (struct cat_vals *);

size_t cat_value_find (const struct variable *, const union value *);

const union value *cat_subscript_to_value (const size_t,
					   const struct variable *);


void cat_value_update (const struct variable *, const union value *);


/*
  Return the number of categories of a categorical variable.
 */
size_t  cat_get_n_categories (const struct variable *v);


#endif
