/* PSPP - Binary encodings for categorical variables.
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

 */

#ifndef CAT_H
#define CAT_H
#define CAT_VALUE_NOT_FOUND -2
#include <stdbool.h>
#include <stddef.h>

union value;
struct variable ; 

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
  Return the number of categories of a categorical variable.
 */
size_t  cat_get_n_categories (const struct variable *v);


#endif
