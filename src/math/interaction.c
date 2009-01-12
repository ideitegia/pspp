/* PSPP - a program for statistical analysis.
   Copyright (C) 2009 Free Software Foundation, Inc.

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

  When using these functions, make sure the orders of variables and
  values match when appropriate.
 */

#include <config.h>
#include <assert.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_vector.h>
#include <data/value.h>
#include <data/variable.h>
#include <math/interaction.h>
#include <string.h>
#include <xalloc.h>

struct interaction_variable
{
  int n_vars;
  const struct variable **members;
  struct variable *intr;
};

struct interaction_value
{
  const struct interaction_variable *intr;
  union value *strings; /* Concatenation of the string values in this interaction's value. */
  double f; /* Product of the numerical values in this interaction's value. */
};

struct interaction_variable *
interaction_variable_create (const struct variable **vars, int n_vars)
{
  struct interaction_variable *result = NULL;
  size_t i;

  if (n_vars > 0)
    {
      result = xmalloc (sizeof (*result));
      result->members = xnmalloc (n_vars, sizeof (*result->members));
      result->intr = var_create_internal (0);
      result->n_vars = n_vars;
      for (i = 0; i < n_vars; i++)
	{
	  result->members[i] = vars[i];
	}
    }
  return result;
}

void interaction_variable_destroy (struct interaction_variable *iv)
{
  var_destroy (iv->intr);
  free (iv->members);
  free (iv);
}

size_t
interaction_variable_get_n_vars (const struct interaction_variable *iv)
{
  return (iv == NULL) ? 0 : iv->n_vars;
}

/*
  Given list of values, compute the value of the corresponding
  interaction.  This "value" is not stored as the typical vector of
  0's and one double, but rather the string values are concatenated to
  make one big string value, and the numerical values are multiplied
  together to give the non-zero entry of the corresponding vector.
 */
struct interaction_value *
interaction_value_create (const struct interaction_variable *var, const union value **vals)
{
  struct interaction_value *result = NULL;
  size_t i;
  size_t n_vars;
  
  if (var != NULL)
    {
      result = xmalloc (sizeof (*result));
      result->intr = var;
      n_vars = interaction_variable_get_n_vars (var);
      result->strings = value_create (n_vars * MAX_SHORT_STRING + 1);
      result->f = 1.0;
      for (i = 0; i < n_vars; i++)
	{
	  if (var_is_alpha (var->members[i]))
	    {
	      strncat (result->strings->s, vals[i]->s, MAX_SHORT_STRING);
	    }
	  else if (var_is_numeric (var->members[i]))
	    {
	      result->f *= vals[i]->f;
	    }
	}
    }
  return result;
}

void 
interaction_value_destroy (struct interaction_value *val)
{
  if (val != NULL)
    {
      free (val->strings);
      free (val);
    }
}

