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
  Functions and data structures to store values of a categorical
  variable, and to recode those values into binary vectors.

  For some statistical models, it is necessary to change each value
  of a categorical variable to a vector with binary entries. These
  vectors are then stored as sub-rows within a matrix during
  model-fitting. For example, we need functions and data strucutres to map a
  value, say 'a', of a variable named 'cat_var', to a vector, say (0
  1 0 0 0), and vice versa.  We also need to be able to map the
  vector back to the value 'a', and if the vector is a sub-row of a
  matrix, we need to know which sub-row corresponds to the variable
  'cat_var'.
*/
#include <config.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <libpspp/message.h>
#include "category.h"
#include "value.h"
#include "variable.h"

#include "xalloc.h"

#define CAT_VALUE_NOT_FOUND -2

#define N_INITIAL_CATEGORIES 1

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

void
cat_stored_values_create (const struct variable *v)
{
  if (!var_has_obs_vals (v))
    {
      struct cat_vals *obs_vals = xmalloc (sizeof *obs_vals);

      obs_vals->n_categories = 0;
      obs_vals->n_allocated_categories = N_INITIAL_CATEGORIES;
      obs_vals->vals = xnmalloc (N_INITIAL_CATEGORIES, sizeof *obs_vals->vals);
      var_set_obs_vals (v, obs_vals);
    }
}

void
cat_stored_values_destroy (struct cat_vals *obs_vals)
{
  if (obs_vals != NULL)
    {
      if (obs_vals->n_allocated_categories > 0)
        free (obs_vals->vals);
      free (obs_vals);
    }
}

/*
  Which subscript corresponds to val?
 */
size_t
cat_value_find (const struct variable *v, const union value *val)
{
  struct cat_vals *obs_vals = var_get_obs_vals (v);
  size_t i;
  const union value *candidate;

  for (i = 0; i < obs_vals->n_categories; i++)
    {
      candidate = obs_vals->vals + i;
      assert (candidate != NULL);
      if (!compare_values (candidate, val, var_get_width (v)))
	{
	  return i;
	}
    }
  return CAT_VALUE_NOT_FOUND;
}

/*
   Add the new value unless it is already present.
 */
void
cat_value_update (const struct variable *v, const union value *val)
{
  if (var_is_alpha (v))
    {
      struct cat_vals *cv = var_get_obs_vals (v);
      if (cat_value_find (v, val) == CAT_VALUE_NOT_FOUND)
	{
	  if (cv->n_categories >= cv->n_allocated_categories)
	    {
	      cv->n_allocated_categories *= 2;
	      cv->vals = xnrealloc (cv->vals,
				    cv->n_allocated_categories,
				    sizeof *cv->vals);
	    }
	  cv->vals[cv->n_categories] = *val;
	  cv->n_categories++;
	}
    }
}

const union value *
cat_subscript_to_value (const size_t s, const struct variable *v)
{
  struct cat_vals *obs_vals = var_get_obs_vals (v);
  return s < obs_vals->n_categories ? obs_vals->vals + s : NULL;
}

/*
  Return the number of categories of a categorical variable.
 */
size_t
cat_get_n_categories (const struct variable *v)
{
  return var_get_obs_vals (v)->n_categories;
}

