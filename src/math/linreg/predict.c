/*
   lib/linreg/predict.c
  
   Copyright (C) 2005 Free Software Foundation, Inc. Written by Jason H. Stover.

   This program is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your option)
   any later version.
   
   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.
   
   You should have received a copy of the GNU General Public License along with
   this program; if not, write to the Free Software Foundation, Inc., 51
   Franklin Street, Fifth Floor, Boston, MA 02111-1307, USA.
 */

#include <math/linreg/linreg.h>
#include <math/coefficient.h>
#include <gl/xalloc.h>

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
  int i;
  int j;
  const struct pspp_linreg_coeff **found;
  const struct pspp_linreg_coeff *coe;
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
  found = xnmalloc (c->n_coeffs, sizeof (*found));
  *found = c->coeff[0];
  result = c->coeff[0]->estimate;	/* Intercept. */

  /*
     The loops guard against the possibility that the caller passed us
     inadequate information, such as too few or too many values, or
     a redundant list of variable names.
   */
  for (j = 0; j < n_vals; j++)
    {
      coe = pspp_linreg_get_coeff (c, predictors[j], vals[j]);
      i = 1;
      while (found[i] == coe && i < c->n_coeffs)
	{
	  i++;
	}
      if (i < c->n_coeffs)
	{
	  found[i] = coe;
	  tmp = pspp_linreg_coeff_get_est (coe);
	  if (predictors[j]->type == NUMERIC)
	    {
	      tmp *= vals[j]->f;
	    }
	  result += tmp;
	}
    }
  free (found);

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
