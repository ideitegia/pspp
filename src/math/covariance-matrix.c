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
  Create and update the values in the covariance matrix.
*/
#include <assert.h>
#include <config.h>
#include <data/variable.h>
#include <data/value.h>
#include "covariance-matrix.h"
#include "moments.h"

/*
  The covariances are stored in a DESIGN_MATRIX structure.
 */
struct design_matrix *
covariance_matrix_create (int n_variables, const struct variable *v_variables[])
{
  return design_matrix_create (n_variables, v_variables, (size_t) n_variables);
}

void covariance_matrix_destroy (struct design_matrix *x)
{
  design_matrix_destroy (x);
}

/*
  Update the covariance matrix with the new entries, assuming that ROW
  corresponds to a categorical variable and V2 is numeric.
 */
static void
covariance_update_categorical_numeric (struct design_matrix *cov, double mean,
			  size_t row, 
			  const struct variable *v2, double x, const union value *val2)
{
  size_t col;
  double tmp;
  
  assert (var_is_numeric (v2));

  col = design_matrix_var_to_column (cov, v2);
  assert (val2 != NULL);
  tmp = gsl_matrix_get (cov->m, row, col);
  gsl_matrix_set (cov->m, row, col, (val2->f - mean) * x + tmp);
  gsl_matrix_set (cov->m, col, row, (val2->f - mean) * x + tmp);
}
static void
column_iterate (struct design_matrix *cov, const struct variable *v,
		double ssize, double x, const union value *val1, size_t row)
{
  size_t col;
  size_t i;
  double y;
  double tmp;
  const union value *tmp_val;

  col = design_matrix_var_to_column (cov, v);  
  for (i = 0; i < cat_get_n_categories (v) - 1; i++)
    {
      col += i;
      y = -1.0 * cat_get_category_count (i, v) / ssize;
      tmp_val = cat_subscript_to_value (i, v);
      if (compare_values (tmp_val, val1, v))
	{
	  y += -1.0;
	}
      tmp = gsl_matrix_get (cov->m, row, col);
      gsl_matrix_set (cov->m, row, col, x * y + tmp);
      gsl_matrix_set (cov->m, col, row, x * y + tmp);
    }
}
/*
  Call this function in the second data pass. The central moments are
  MEAN1 and MEAN2. Any categorical variables should already have their
  values summarized in in its OBS_VALS element.
 */
void covariance_pass_two (struct design_matrix *cov, double mean1, double mean2,
			  double ssize, const struct variable *v1, 
			  const struct variable *v2, const union value *val1, const union value *val2)
{
  size_t row;
  size_t col;
  size_t i;
  double x;
  const union value *tmp_val;

  if (var_is_alpha (v1))
    {
      row = design_matrix_var_to_column (cov, v1);
      for (i = 0; i < cat_get_n_categories (v1) - 1; i++)
	{
	  row += i;
	  x = -1.0 * cat_get_category_count (i, v1) / ssize;
	  tmp_val = cat_subscript_to_value (i, v1);
	  if (compare_values (tmp_val, val1, v1))
	    {
	      x += 1.0;
	    }
	  if (var_is_numeric (v2))
	    {
	      covariance_update_categorical_numeric (cov, mean2, row, 
						     v2, x, val2);
	    }
	  else
	    {
	      column_iterate (cov, v1, ssize, x, val1, row);
	      column_iterate (cov, v2, ssize, x, val2, row);
	    }
	}
    }
  else if (var_is_alpha (v2))
    {
      /*
	Reverse the orders of V1, V2, etc. and put ourselves back
	in the previous IF scope.
       */
      covariance_pass_two (cov, mean2, mean1, ssize, v2, v1, val2, val1);
    }
  else
    {
      /*
	Both variables are numeric.
      */
      row = design_matrix_var_to_column (cov, v1);  
      col = design_matrix_var_to_column (cov, v2);
      x = (val1->f - mean1) * (val2->f - mean2);
      x += gsl_matrix_get (cov->m, col, row);
      gsl_matrix_set (cov->m, row, col, x);
      gsl_matrix_set (cov->m, col, row, x);
    }
}

