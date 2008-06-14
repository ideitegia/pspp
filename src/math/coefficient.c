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
pspp_coeff_init (struct pspp_coeff ** c, const struct design_matrix *X)
{
  size_t i;
  int n_vals = 1;

  assert (c != NULL);
  for (i = 0; i < X->m->size2; i++)
    {
      c[i] = xmalloc (sizeof (*c[i]));
      c[i]->n_vars = n_vals;	/* Currently, no procedures allow
				   interactions.  This line will have to
				   change when procedures that allow
				   interaction terms are written.
				 */
      c[i]->v_info = xnmalloc (c[i]->n_vars, sizeof (*c[i]->v_info));
      assert (c[i]->v_info != NULL);
      c[i]->v_info->v = design_matrix_col_to_var (X, i);

      if (var_is_alpha (c[i]->v_info->v))
	{
	  size_t k;
	  k = design_matrix_var_to_column (X, c[i]->v_info->v);
	  assert (k <= i);
	  k = i - k;
	  c[i]->v_info->val =
	    cat_subscript_to_value (k, c[i]->v_info->v);
	}
    }
}
void
pspp_coeff_set_estimate (struct pspp_coeff *c, double estimate)
{
  c->estimate = estimate;
}

void
pspp_coeff_set_std_err (struct pspp_coeff *c, double std_err)
{
  c->std_err = std_err;
}

/*
  Return the estimated value of the coefficient.
 */
double
pspp_coeff_get_est (const struct pspp_coeff *c)
{
  if (c == NULL)
    {
      return 0.0;
    }
  return c->estimate;
}

/*
  Return the standard error of the estimated coefficient.
*/
double
pspp_coeff_get_std_err (const struct pspp_coeff *c)
{
  if (c == NULL)
    {
      return 0.0;
    }
  return c->std_err;
}

/*
  How many variables are associated with this coefficient?
 */
int
pspp_coeff_get_n_vars (struct pspp_coeff *c)
{
  if (c == NULL)
    {
      return 0;
    }
  return c->n_vars;
}

/*
  Which variable does this coefficient match? I should be
  0 unless the coefficient refers to an interaction term.
 */
const struct variable *
pspp_coeff_get_var (struct pspp_coeff *c, int i)
{
  if (c == NULL)
    {
      return NULL;
    }
  assert (i < c->n_vars);
  return (c->v_info + i)->v;
}

/*
  Which value is associated with this coefficient/variable combination?
 */
const union value *
pspp_coeff_get_value (struct pspp_coeff *c,
			     const struct variable *v)
{
  int i = 0;
  const struct variable *candidate;

  if (c == NULL || v == NULL)
    {
      return NULL;
    }
  if (var_is_numeric (v))
    {
      return NULL;
    }
  while (i < c->n_vars)
    {
      candidate = pspp_coeff_get_var (c, i);
      if (v == candidate)
	{
	  return (c->v_info + i)->val;
	}
      i++;
    }
  return NULL;
}

