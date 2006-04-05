/* lib/linreg/predict.c

 Copyright (C) 2005 Free Software Foundation, Inc.
 Written by Jason H. Stover.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or (at
 your option) any later version.

 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 02111-1307, USA.
*/

#include <math/linreg/linreg.h>
#include <math/linreg/coefficient.h>

/*
  Predict the value of the dependent variable with the
  new set of predictors. PREDICTORS must point to a list
  of variables, each of whose values are stored in VALS,
  in the same order.
 */
double
pspp_linreg_predict (const struct variable *predictors, 
		     const union value *vals, 
		     const pspp_linreg_cache *c,
		     int n_vals)
{
  int i;
  double result;
  double tmp;
  
  assert (predictors != NULL);
  assert (vals != NULL);
  assert (c != NULL);

  result = c->coeff->estimate; /* Intercept. */

  /*
    Stop at the minimum of c->n_coeffs and n_vals in case
    the caller passed us inadequate information, such as too
    few or too many values.
   */
  for (i = 1; i < c->n_coeffs && i < n_vals; i++)
    {
      tmp = pspp_linreg_coeff_get_est (pspp_linreg_get_coeff (c, predictors + i, vals + i));
      if ((predictors + i)->type == NUMERIC)
	{
	  tmp *= (vals + i)->f;
	}
      result += tmp;
    }
  return result;
}
