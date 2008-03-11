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

#include <config.h>
#include <math/linreg/linreg.h>
#include <math/coefficient.h>
#include <gl/xalloc.h>

/*
  Is the coefficient COEF contained in the list of coefficients
  COEF_LIST?
 */
static int
has_coefficient (const struct pspp_coeff **coef_list, const struct pspp_coeff *coef,
		 size_t n)
{
  size_t i = 0;

  while (i < n)
    {
      if (coef_list[i] == coef)
	{
	  return 1;
	}
      i++;
    }
  return 0;
}
/*
  Predict the value of the dependent variable with the
  new set of predictors. PREDICTORS must point to a list
  of variables, each of whose values are stored in VALS,
  in the same order.
 */
double
pspp_linreg_predict (const struct variable **predictors,
		     const union value **vals, const void *c_, int n_vals)
{
  const pspp_linreg_cache *c = c_;
  int j;
  size_t next_coef = 0;
  const struct pspp_coeff **coef_list;
  const struct pspp_coeff *coe;
  double result;
  double tmp;

  if (predictors == NULL || vals == NULL || c == NULL)
    {
      return GSL_NAN;
    }
  if (c->coeff == NULL)
    {
      /* The stupid model: just guess the mean. */
      return c->depvar_mean;
    }
  coef_list = xnmalloc (c->n_coeffs, sizeof (*coef_list));
  result = c->intercept;

  /*
     The loops guard against the possibility that the caller passed us
     inadequate information, such as too few or too many values, or
     a redundant list of variable names.
   */
  for (j = 0; j < n_vals; j++)
    {
      coe = pspp_linreg_get_coeff (c, predictors[j], vals[j]);
      if (!has_coefficient (coef_list, coe, next_coef))
	{
	  tmp = pspp_coeff_get_est (coe);
	  if (var_is_numeric (predictors[j]))
	    {
	      tmp *= vals[j]->f;
	    }
	  result += tmp;
	  coef_list[next_coef++] = coe;
	}
    }
  free (coef_list);

  return result;
}

double
pspp_linreg_residual (const struct variable **predictors,
		      const union value **vals,
		      const union value *obs, const void *c, int n_vals)
{
  double pred;
  double result;

  if (predictors == NULL || vals == NULL || c == NULL || obs == NULL)
    {
      return GSL_NAN;
    }
  pred = pspp_linreg_predict (predictors, vals, c, n_vals);

  result = gsl_isnan (pred) ? GSL_NAN : (obs->f - pred);
  return result;
}
