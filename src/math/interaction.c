/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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
  An interaction is a gsl_vector containing a "product" of other
  variables. The variables can be either categorical or numeric.
  If the variables are all numeric, the interaction is just the
  scalar product. If any of the variables are categorical, their
  product is a vector containing 0's in all but one entry. This entry
  is found by combining the vectors corresponding to the variables'
  OBS_VALS member. If there are K categorical variables, each with
  N_1, N_2, ..., N_K categories, then the interaction will have
  N_1 * N_2 * N_3 *...* N_K - 1 entries.
 */

#include <config.h>
#include <assert.h>
#include <libpspp/alloc.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_vector.h>
#include <data/category.h>
#include <data/variable.h>
#include "interaction.h"

/*
  Convert a list of values to a binary vector. The order of VALS must
  correspond to the order of V.
 */
gsl_vector *
get_interaction (union value **vals, const struct variable **v, size_t n_vars)
{
  gsl_vector *result = NULL;
  size_t *subs = NULL;
  size_t length = 1;
  size_t i;
  size_t j;
  double tmp = 1.0;

  assert (n_vars > 0);
  for (i = 0; i < n_vars; i++)
    {
      if (var_is_alpha (v[i]))
	{
	  length *= cat_get_n_categories (v[i]);
	}
      else
	{
	  length = (length > 0) ? length : 1;
	}
    }
  if (length > 0)
    {
      length--;
    }

  result = gsl_vector_calloc (length);
  subs = xnmalloc (n_vars, sizeof (*subs));
  for (j = 0; j < n_vars; j++)
    {
      if (var_is_alpha (v[j]))
	{
	  subs[j] = cat_value_find (v[j], vals[j]);
	}
    }
  j = subs[0];
  for (i = 1; i < n_vars; i++)
    {
      j = j * cat_get_n_categories (v[i]) + subs[i];
    }
  gsl_vector_set (result, j, 1.0);
  /*
     If any of the variables are numeric, the interaction of that
     variable with another is just a scalar product.
   */
  for (i = 1; i < n_vars; i++)
    {
      if (var_is_numeric (v[i]))
	{
	  tmp *= vals[i]->f;
	}
    }
  if (fabs (tmp - 1.0) > GSL_DBL_EPSILON)
    {
      gsl_vector_set (result, j, tmp);
    }
  free (subs);

  return result;
}
