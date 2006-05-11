/*
  lib/linreg/coefficient.c
  
  Copyright (C) 2005 Free Software Foundation, Inc. Written by Jason H Stover.
  
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

/*
  Accessor functions for matching coefficients and variables.
 */
#include <math/linreg/coefficient.h>
#include <math/linreg/linreg.h>
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
pspp_linreg_coeff_free (struct pspp_linreg_coeff *c)
{
  free (c->v_info);
  free (c);
}

/*
  Initialize the variable and value pointers inside the
  coefficient structures for the linear model.
 */
void
pspp_linreg_coeff_init (pspp_linreg_cache * c, struct design_matrix *X)
{
  size_t i;
  size_t j;
  int n_vals = 1;
  struct pspp_linreg_coeff *coeff;

  c->coeff = xnmalloc (X->m->size2 + 1, sizeof (*c->coeff));
  c->coeff[0] = xmalloc (sizeof (*c->coeff[0]));
  c->coeff[0]->v_info = NULL;	/* Intercept has no associated variable. */
  for (i = 0; i < X->m->size2; i++)
    {
      j = i + 1;		/* The first coefficient is the intercept. */
      c->coeff[j] = xmalloc (sizeof (*c->coeff[j]));
      coeff = c->coeff[j];
      coeff->n_vars = n_vals;	/* Currently, no procedures allow
				   interactions.  This line will have to
				   change when procedures that allow
				   interaction terms are written. 
				 */
      coeff->v_info = xnmalloc (coeff->n_vars, sizeof (*coeff->v_info));
      assert (coeff->v_info != NULL);
      coeff->v_info->v =
	(const struct variable *) design_matrix_col_to_var (X, i);

      if (coeff->v_info->v->type == ALPHA)
	{
	  size_t k;
	  k = design_matrix_var_to_column (X, coeff->v_info->v);
	  assert (k <= i);
	  k = i - k;
	  coeff->v_info->val =
	    cat_subscript_to_value (k, (struct variable *) coeff->v_info->v);
	}
    }
}
void
pspp_linreg_coeff_set_estimate (struct pspp_linreg_coeff *c, double estimate)
{
  c->estimate = estimate;
}

void
pspp_linreg_coeff_set_std_err (struct pspp_linreg_coeff *c, double std_err)
{
  c->std_err = std_err;
}

/*
  Return the estimated value of the coefficient.
 */
double
pspp_linreg_coeff_get_est (const struct pspp_linreg_coeff *c)
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
pspp_linreg_coeff_get_std_err (const struct pspp_linreg_coeff *c)
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
pspp_linreg_coeff_get_n_vars (struct pspp_linreg_coeff *c)
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
pspp_linreg_coeff_get_var (struct pspp_linreg_coeff *c, int i)
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
pspp_linreg_coeff_get_value (struct pspp_linreg_coeff *c,
			     const struct variable *v)
{
  int i = 0;
  const struct variable *candidate;

  if (c == NULL || v == NULL)
    {
      return NULL;
    }
  if (v->type == NUMERIC)
    {
      return NULL;
    }
  while (i < c->n_vars)
    {
      candidate = pspp_linreg_coeff_get_var (c, i);
      if (v->index == candidate->index)
	{
	  return (c->v_info + i)->val;
	}
      i++;
    }
  return NULL;
}

/*
  Which coefficient is associated with V? The VAL argument is relevant
  only to categorical variables.
 */
const struct pspp_linreg_coeff *
pspp_linreg_get_coeff (const pspp_linreg_cache * c,
		       const struct variable *v, const union value *val)
{
  int i = 1;
  struct pspp_linreg_coeff *result = NULL;
  const struct variable *tmp = NULL;

  if (c == NULL)
    {
      return NULL;
    }
  if (c->coeff == NULL || c->n_indeps == 0 || v == NULL)
    {
      return NULL;
    }

  result = c->coeff[i];
  tmp = pspp_linreg_coeff_get_var (result, 0);
  while (tmp->index != v->index && i < c->n_coeffs)
    {
      result = c->coeff[i];
      tmp = pspp_linreg_coeff_get_var (result, 0);
      i++;
    }
  if (i > c->n_coeffs)
    {
      return NULL;
    }
  if (v->type == NUMERIC)
    {
      return result;
    }
  else if (val != NULL)
    {
      /*
         If v is categorical, we need to ensure the coefficient
         matches the VAL.
       */
      while (tmp->index != v->index && i < c->n_coeffs
	     && compare_values (pspp_linreg_coeff_get_value (result, tmp),
				val, v->width))
	{			/* FIX THIS */
	  i++;
	  result = c->coeff[i];
	  tmp = pspp_linreg_coeff_get_var (result, 0);
	}
      if (i == c->n_coeffs)
	{
	  return NULL;
	}
      return result;
    }
  return NULL;
}
