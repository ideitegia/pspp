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
#include <data/category.h>
#include <data/value.h>
#include <data/variable.h>
#include <gl/xalloc.h>
#include <libpspp/message.h>
#include <stdlib.h>
#include <string.h>

#define CAT_VALUE_NOT_FOUND -1

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
  size_t *value_counts; /* Element i stores the number of cases for which
			   the categorical variable has that corresponding 
			   value. This is necessary for computing covariance
			   matrices.
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
      obs_vals->value_counts = xnmalloc (N_INITIAL_CATEGORIES, sizeof *obs_vals->value_counts);
      var_set_obs_vals (v, obs_vals);
    }
}

void
cat_stored_values_destroy (struct cat_vals *obs_vals)
{
  if (obs_vals != NULL)
    {
      if (obs_vals->n_allocated_categories > 0)
	{
	  free (obs_vals->vals);
	  free (obs_vals->value_counts);
	}
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
      if (!compare_values_short (candidate, val, v))
	{
	  return i;
	}
    }
  return CAT_VALUE_NOT_FOUND;
}

/*
   Add the new value unless it is already present. Increment the count.
 */
void
cat_value_update (const struct variable *v, const union value *val)
{
  if (var_is_alpha (v))
    {
      size_t i;
      struct cat_vals *cv = var_get_obs_vals (v);
      i = cat_value_find (v, val);
      if (i == CAT_VALUE_NOT_FOUND)
	{
	  if (cv->n_categories >= cv->n_allocated_categories)
	    {
	      cv->n_allocated_categories *= 2;
	      cv->vals = xnrealloc (cv->vals,
				    cv->n_allocated_categories,
				    sizeof *cv->vals);
	      cv->value_counts = xnrealloc (cv->value_counts, cv->n_allocated_categories,
					    sizeof *cv->value_counts);
	    }
	  cv->vals[cv->n_categories] = *val;
	  cv->value_counts[cv->n_categories] = 1;
	  cv->n_categories++;
	}
      else
	{
	  cv->value_counts[i]++;
	}
    }
}
/*
  Return the count for the sth category.
 */
size_t
cat_get_category_count (const size_t s, const struct variable *v)
{
  struct cat_vals *tmp;
  size_t n_categories;

  tmp = var_get_obs_vals (v);
  n_categories = cat_get_n_categories (v);
  if (s < n_categories)
    {
      return tmp->value_counts[s];
    }
  return CAT_VALUE_NOT_FOUND;
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

/*
  If VAR is categorical with d categories, its first category should
  correspond to the origin in d-dimensional Euclidean space.
 */
bool
cat_is_origin (const struct variable *var, const union value *val)
{
  if (var_is_numeric (var))
    {
      return false;
    }
  if (cat_value_find (var, val) == 0)
    {
      return true;
    }
  return false;
}
