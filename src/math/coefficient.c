/* PSPP - a program for statistical analysis.
   Copyright (C) 2005 Free Software Foundation, Inc.

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
  Accessor functions for matching coefficients and variables.
 */
#include <config.h>
#include <math/coefficient.h>
#include "src/math/design-matrix.h"

#include <gl/xalloc.h>


struct varinfo
{
  const struct variable *v;	/* Variable associated with this
				   coefficient. Note this variable
				   may not be unique. In other words,
				   a coefficient structure may have
				   other v_info's, each with its own
				   variable. */
  const union value *val;	/* Value of the variable v which this varinfo
				   refers to. This member is relevant only to
				   categorical variables. */
  double mean; /* Mean for this variable */
  double sd; /* Standard deviation for this variable */
};

void
pspp_coeff_free (struct pspp_coeff *c)
{
  free (c->v_info);
  free (c);
}

/*
  Initialize the variable and value pointers inside the
  coefficient structures for the model.
 */
void
pspp_coeff_init (struct pspp_coeff ** coeff, const struct design_matrix *X)
{
  size_t i;
  int n_vals = 1;

  assert (coeff != NULL);
  for (i = 0; i < X->m->size2; i++)
    {
      coeff[i] = xmalloc (sizeof (*coeff[i]));
      coeff[i]->n_vars = n_vals;	/* Currently, no procedures allow
					   interactions.  This line will have to
					   change when procedures that allow
					   interaction terms are written.
					*/
      coeff[i]->v_info = xnmalloc (coeff[i]->n_vars, sizeof (*coeff[i]->v_info));
      assert (coeff[i]->v_info != NULL);
      coeff[i]->v_info->v = design_matrix_col_to_var (X, i);
      
      if (var_is_alpha (coeff[i]->v_info->v))
	{
	  size_t k;
	  k = design_matrix_var_to_column (X, coeff[i]->v_info->v);
	  assert (k <= i);
	  k = i - k;
	  coeff[i]->v_info->val =
	    cat_subscript_to_value (k, coeff[i]->v_info->v);
	}
      coeff[i]->v_info->mean = 0.0;
      coeff[i]->v_info->sd = 0.0;
    }
}
void
pspp_coeff_set_estimate (struct pspp_coeff *coef, double estimate)
{
  coef->estimate = estimate;
}

void
pspp_coeff_set_std_err (struct pspp_coeff *coef, double std_err)
{
  coef->std_err = std_err;
}

/*
  Return the estimated value of the coefficient.
 */
double
pspp_coeff_get_est (const struct pspp_coeff *coef)
{
  if (coef == NULL)
    {
      return 0.0;
    }
  return coef->estimate;
}

/*
  Return the standard error of the estimated coefficient.
*/
double
pspp_coeff_get_std_err (const struct pspp_coeff *coef)
{
  if (coef == NULL)
    {
      return 0.0;
    }
  return coef->std_err;
}

/*
  How many variables are associated with this coefficient?
 */
int
pspp_coeff_get_n_vars (struct pspp_coeff *coef)
{
  if (coef == NULL)
    {
      return 0;
    }
  return coef->n_vars;
}

/*
  Which variable does this coefficient match? I should be
  0 unless the coefficient refers to an interaction term.
 */
const struct variable *
pspp_coeff_get_var (struct pspp_coeff *coef, int i)
{
  if (coef == NULL)
    {
      return NULL;
    }
  assert (i < coef->n_vars);
  return (coef->v_info + i)->v;
}

/*
  Which coefficient does this variable match? If the variable is
  categorical, and has more than one coefficient, use the VAL to find
  its coefficient.
 */
struct pspp_coeff *
pspp_coeff_var_to_coeff (const struct variable *v, struct pspp_coeff **coefs, 
			 size_t n_coef, const union value *val)
{
  size_t i = 0;
  size_t j = 0;
  size_t v_idx;
  int found = 0;
  struct pspp_coeff *result = NULL;

  if (v != NULL)
    {
      v_idx = var_get_dict_index (v);
      while (i < n_coef)
	{
	  if (coefs[i]->v_info != NULL)
	    {
	      if (var_get_dict_index (coefs[i]->v_info->v) == v_idx)
		{
		  break;
		}
	    }
	  i++;
	}
      result = coefs[i];
      if (var_is_alpha (v))
	{
	  /*
	    Use the VAL to find the coefficient.
	   */
	  if (val != NULL)
	    {
	      j = i;
	      while (j < n_coef && compare_values (pspp_coeff_get_value (coefs[j], v),
						   val, var_get_width (v)) != 0)
		{
		  j++;
		}
	      result = ((j < n_coef) ? coefs[j] : NULL);
	    }
	}
    }
  return result;
}

/*
  Which value is associated with this coefficient/variable combination?
 */
const union value *
pspp_coeff_get_value (struct pspp_coeff *coef,
			     const struct variable *v)
{
  int i = 0;
  const struct variable *candidate;

  if (coef == NULL || v == NULL)
    {
      return NULL;
    }
  if (var_is_numeric (v))
    {
      return NULL;
    }
  while (i < coef->n_vars)
    {
      candidate = pspp_coeff_get_var (coef, i);
      if (v == candidate)
	{
	  return (coef->v_info + i)->val;
	}
      i++;
    }
  return NULL;
}

/*
  Get or set the standard deviation of the variable associated with this coefficient.
 */
double pspp_coeff_get_sd (const struct pspp_coeff *coef)
{
  return coef->v_info->sd;
}
void pspp_coeff_set_sd (struct pspp_coeff *coef, double s)
{
  coef->v_info->sd = s;
}

/*
  Get or set the mean for the variable associated with this coefficient.
*/
double pspp_coeff_get_mean (const struct pspp_coeff *coef)
{
  return coef->v_info->mean;
}

void pspp_coeff_set_mean (struct pspp_coeff *coef, double m)
{
  coef->v_info->mean = m;
}

