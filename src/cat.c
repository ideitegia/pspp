/* PSPP - binary encodings for categorical variables.
   Copyright (C) 2005 Free Software Foundation, Inc.
   Written by Jason H Stover <jason@sakla.net>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

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
#include <stdlib.h>
#include <error.h>
#include "alloc.h"
#include "error.h"
#include "cat.h"
#include "cat-routines.h"
#include <string.h>

#define N_INITIAL_CATEGORIES 1

void
cat_stored_values_create (struct variable *v)
{
  if (v->obs_vals == NULL)
    {
      v->obs_vals = xmalloc (sizeof (*v->obs_vals));
      v->obs_vals->n_categories = 0;
      v->obs_vals->n_allocated_categories = N_INITIAL_CATEGORIES;
      v->obs_vals->vals =
	xnmalloc (N_INITIAL_CATEGORIES, sizeof *v->obs_vals->vals);
    }
}

void
cat_stored_values_destroy (struct variable *v)
{
  assert (v != NULL);
  if (v->obs_vals != NULL)
    {
      free (v->obs_vals);
    }
}

/*
  Which subscript corresponds to val?
 */
size_t
cat_value_find (const struct variable *v, const union value *val)
{
  size_t i;
  const union value *candidate;

  assert (val != NULL);
  assert (v != NULL);
  assert (v->obs_vals != NULL);
  for (i = 0; i < v->obs_vals->n_categories; i++)
    {
      candidate = v->obs_vals->vals + i;
      assert (candidate != NULL);
      if (!compare_values (candidate, val, v->width))
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
cat_value_update (struct variable *v, const union value *val)
{
  struct cat_vals *cv;

  if (v->type == ALPHA)
    {
      assert (val != NULL);
      assert (v != NULL);
      cv = v->obs_vals;
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

union value *
cat_subscript_to_value (const size_t s, struct variable *v)
{
  assert (v->obs_vals != NULL);
  if (s < v->obs_vals->n_categories)
    {
      return (v->obs_vals->vals + s);
    }
  else
    {
      return NULL;
    }
}
