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
  Update the covariance matrix with the new entries, assuming that V1 
  is categorical and V2 is numeric.
 */
static void
covariance_update_categorical_numeric (struct design_matrix *cov, double mean,
			  double weight, double ssize, const struct variable *v1, 
			  const struct variable *v2, const union value *val1, const union value *val2)
{
  double x;
  size_t i;
  size_t col;
  size_t row;
  
  assert (var_is_alpha (v1));
  assert (var_is_numeric (v2));

  row = design_matrix_var_to_column (cov, v1);  
  col = design_matrix_var_to_column (cov, v2);
  for (i = 0; i < cat_get_n_categories (v1); i++)
    {
      row += i;
      x = -1.0 * cat_get_n_categories (v1) / ssize;
      if (i == cat_value_find (v1, val1))
	{
	  x += 1.0;
	}
      assert (val2 != NULL);
      gsl_matrix_set (cov->m, row, col, (val2->f - mean) * x * weight);
    }
}
static void
column_iterate (struct design_matrix *cov, const struct variable *v, double weight,
		double ssize, double x, const union value *val1, size_t row)
{
  size_t col;
  size_t i;
  double y;
  union value *tmp_val;

  col = design_matrix_var_to_column (cov, v);  
  for (i = 0; i < cat_get_n_categories (v) - 1; i++)
    {
      col += i;
      y = -1.0 * cat_get_category_count (i, v) / ssize;
      tmp_val = cat_subscript_to_value (i, v);
      if (compare_values (tmp_val, val1, var_get_width (v)))
	{
	  y += -1.0;
	}
      gsl_matrix_set (cov->m, row, col, x * y * weight);
      gsl_matrix_set (cov->m, col, row, x * y * weight);
    }
}
/*
  Call this function in the second data pass. The central moments are
  MEAN1 and MEAN2. Any categorical variables should already have their
  values summarized in in its OBS_VALS element.
 */
void covariance_pass_two (struct design_matrix *cov, double mean1, double mean2,
			  double weight, double ssize, const struct variable *v1, 
			  const struct variable *v2, const union value *val1, const union value *val2)
{
  size_t row;
  size_t col;
  size_t i;
  double x;
  union value *tmp_val;

  if (var_is_alpha (v1))
    {
      if (var_is_numeric (v2))
	{
	  covariance_update_categorical_numeric (cov, mean2, weight, ssize, v1, 
						 v2, val1, val2);
	}
      else
	{
	  row = design_matrix_var_to_column (cov, v1);
	  for (i = 0; i < cat_get_n_categories (v1) - 1; i++)
	    {
	      row += i;
	      x = -1.0 * cat_get_category_count (i, v1) / ssize;
	      tmp_val = cat_subscript_to_value (i, v1);
	      if (compare_values (tmp_val, val1, var_get_width (v1)))
		{
		  x += 1.0;
		}
	      column_iterate (cov, v1, weight, ssize, x, val1, row);
	      column_iterate (cov, v2, weight, ssize, x, val2, row);
	    }
	}
    }
  else if (var_is_alpha (v2))
    {
      covariance_update_categorical_numeric (cov, mean1, weight, ssize, v2, 
					     v1, val2, val1);
    }
  else
    {
      /*
	Both variables are numeric.
      */
      row = design_matrix_var_to_column (cov, v1);  
      col = design_matrix_var_to_column (cov, v2);
      x = (val1->f - mean1) * (val2->f - mean2) * weight;
      gsl_matrix_set (cov->m, row, col, x);
      gsl_matrix_set (cov->m, col, row, x);
    }
}

