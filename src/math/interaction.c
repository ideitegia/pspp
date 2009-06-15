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
  size_t n_alpha;
};

struct interaction_value
{
  const struct interaction_variable *intr;
  union value val; /* Concatenation of the string values in this
                      interaction's value, or the product of a bunch
                      of numeric values for a purely numeric
                      interaction.
		    */
  double f; /* Product of the numerical values in this interaction's value. */
};

/*
  An interaction_variable has type alpha if any of members have type
  alpha. Otherwise, its type is numeric.
 */
struct interaction_variable *
interaction_variable_create (const struct variable **vars, int n_vars)
{
  struct interaction_variable *result = NULL;
  size_t i;

  if (n_vars > 0)
    {
      result = xmalloc (sizeof (*result));
      result->n_alpha = 0;
      result->members = xnmalloc (n_vars, sizeof (*result->members));
      result->n_vars = n_vars;
      for (i = 0; i < n_vars; i++)
	{
	  result->members[i] = vars[i];
	  if (var_is_alpha (vars[i]))
	    {
	      result->n_alpha++;
	    }
	}
    }
  result->intr = var_create_internal (0, 0);

  return result;
}
void interaction_variable_destroy (struct interaction_variable *iv)
{
  var_destroy (iv->intr);
  free (iv->members);
  free (iv);
}

/*
  Get one of the member variables.
 */
const struct variable *
interaction_variable_get_member (const struct interaction_variable *iv, size_t i)
{
  return iv->members[i];
}

size_t
interaction_get_n_vars (const struct interaction_variable *iv)
{
  return (iv == NULL) ? 0 : iv->n_vars;
}

size_t
interaction_get_n_alpha (const struct interaction_variable *iv)
{
  return iv->n_alpha;
}

size_t
interaction_get_n_numeric (const struct interaction_variable *iv)
{
  return (interaction_get_n_vars (iv) - interaction_get_n_alpha (iv));
}

/*
  Get the interaction varibale itself.
 */
const struct variable *
interaction_variable_get_var (const struct interaction_variable *iv)
{
  return iv->intr;
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
  const struct variable *member;
  size_t i;
  size_t n_vars;
  
  if (var != NULL)
    {
      int val_width;
      char *val;

      result = xmalloc (sizeof (*result));
      result->intr = var;
      n_vars = interaction_get_n_vars (var);
      val_width = n_vars * MAX_SHORT_STRING + 1;
      value_init (&result->val, val_width);
      val = value_str_rw (&result->val, val_width);
      val[0] = '\0';
      result->f = 1.0;
      for (i = 0; i < n_vars; i++)
	{
	  member = interaction_variable_get_member (var, i);

	  if (var_is_value_missing (member, vals[i], MV_ANY))
	    {
	      value_set_missing (&result->val, MAX_SHORT_STRING);
	      result->f = SYSMIS;
	      break;
	    }
	  else
	    {
	      if (var_is_alpha (var->members[i]))
		{
                  int w = var_get_width (var->members[i]);
		  strncat (val, value_str (vals[i], w), MAX_SHORT_STRING);
		}
	      else if (var_is_numeric (var->members[i]))
		{
		  result->f *= vals[i]->f;
		}
	    }
	}
      if (interaction_get_n_alpha (var) == 0)
	{
	  /*
	    If there are no categorical variables, then the
	    interaction consists of only numeric data. In this case,
	    code that uses this interaction_value will see the union
	    member as the numeric value. If we were to store that
	    numeric value in result->f as well, the calling code may
	    inadvertently square this value by multiplying by
	    result->val->f. Such multiplication would be correct for an
	    interaction consisting of both categorical and numeric
	    data, but a mistake for purely numerical interactions. To
	    avoid the error, we set result->f to 1.0 for numeric
	    interactions.
	   */
	  result->val.f = result->f;
	  result->f = 1.0;
	}
    }
  return result;
}

const union value *
interaction_value_get (const struct interaction_value *val)
{
  return &val->val;
}

/*
  Returns the numeric value of the non-zero entry for the vector
  corresponding to this interaction.  Do not use this function to get
  the numeric value of a purley numeric interaction. Instead, use the
  union value * returned by interaction_value_get.
 */
double 
interaction_value_get_nonzero_entry (const struct interaction_value *val)
{
  if (val != NULL)
    return val->f;
  return 1.0;
}

void 
interaction_value_destroy (struct interaction_value *val)
{
  if (val != NULL)
    {
      size_t n_vars = interaction_get_n_vars (val->intr);
      int val_width = n_vars * MAX_SHORT_STRING + 1;

      value_destroy (&val->val, val_width);
      free (val);
    }
}

/*
  Return a value from a variable that is an interaction. 
 */
struct interaction_value *
interaction_case_data (const struct ccase *ccase, const struct variable *var, 
		       const struct interaction_variable **intr_vars, size_t n_intr)
{
  size_t i;
  size_t n_vars;
  const struct interaction_variable *iv = NULL;
  const struct variable *intr;
  const struct variable *member;
  const union value **vals = NULL;

  for (i = 0; i < n_intr; i++)
    {
      iv = intr_vars[i];
      intr = interaction_variable_get_var (iv);
      if (var_get_dict_index (intr) == var_get_dict_index (var))
	{
	  break;
	}
    }
  n_vars = interaction_get_n_vars (iv);
  vals = xnmalloc (n_vars, sizeof (*vals));
  for (i = 0; i < n_vars; i++)
    {
      member = interaction_variable_get_member (iv, i);
      vals[i] = case_data (ccase, member);
    }
  return interaction_value_create (iv, vals);
}

bool
is_interaction (const struct variable *var, const struct interaction_variable **iv, size_t n_intr)
{
  size_t i;
  const struct variable *intr;
  
  for (i = 0; i < n_intr; i++)
    {
      intr = interaction_variable_get_var (iv[i]);
      if (var_get_dict_index (intr) == var_get_dict_index (var))
	{
	  return true;
	}
    }
  return false;
}
  
