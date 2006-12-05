/* PSPP - Creates design-matrices.
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
  Create design matrices for procedures that need them.
*/
#include <config.h>

#include "design-matrix.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <libpspp/alloc.h>
#include <libpspp/message.h>
#include <data/variable.h>
#include <data/category.h>

#include <gsl/gsl_machine.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>

#define DM_COLUMN_NOT_FOUND -1
#define DM_INDEX_NOT_FOUND -3

/*
  Which element of a vector is equal to the value x?
 */
static size_t
cat_which_element_eq (const gsl_vector * vec, double x)
{
  size_t i;

  for (i = 0; i < vec->size; i++)
    {
      if (fabs (gsl_vector_get (vec, i) - x) < GSL_DBL_EPSILON)
	{
	  return i;
	}
    }
  return CAT_VALUE_NOT_FOUND;
}
static int
cat_is_zero_vector (const gsl_vector * vec)
{
  size_t i;

  for (i = 0; i < vec->size; i++)
    {
      if (gsl_vector_get (vec, i) != 0.0)
	{
	  return 0;
	}
    }
  return 1;
}

/*
  Return the value of v corresponding to the vector vec.
 */
union value *
cat_vector_to_value (const gsl_vector * vec, struct variable *v)
{
  size_t i;

  i = cat_which_element_eq (vec, 1.0);
  if (i != CAT_VALUE_NOT_FOUND)
    {
      return cat_subscript_to_value (i + 1, v);
    }
  if (cat_is_zero_vector (vec))
    {
      return cat_subscript_to_value (0, v);
    }
  return NULL;
}

struct design_matrix *
design_matrix_create (int n_variables,
		      const struct variable *v_variables[],
		      const size_t n_data)
{
  struct design_matrix *dm;
  const struct variable *v;
  size_t i;
  size_t n_cols = 0;
  size_t col;

  dm = xmalloc (sizeof *dm);
  dm->vars = xnmalloc (n_variables, sizeof *dm->vars);
  dm->n_vars = n_variables;

  for (i = 0; i < n_variables; i++)
    {
      v = v_variables[i];
      assert ((dm->vars + i) != NULL);
      (dm->vars + i)->v = v;	/* Allows us to look up the variable from
				   the design matrix. */
      (dm->vars + i)->first_column = n_cols;
      if (var_is_numeric (v))
	{
	  (dm->vars + i)->last_column = n_cols;
	  n_cols++;
	}
      else if (var_is_alpha (v))
	{
	  assert (v->obs_vals != NULL);
	  (dm->vars + i)->last_column =
	    (dm->vars + i)->first_column + v->obs_vals->n_categories - 2;
	  n_cols += v->obs_vals->n_categories - 1;
	}
    }
  dm->m = gsl_matrix_calloc (n_data, n_cols);
  col = 0;

  return dm;
}

void
design_matrix_destroy (struct design_matrix *dm)
{
  free (dm->vars);
  gsl_matrix_free (dm->m);
  free (dm);
}

/*
  Return the index of the variable for the
  given column.
 */
static size_t
design_matrix_col_to_var_index (const struct design_matrix *dm, size_t col)
{
  size_t i;
  struct design_matrix_var v;

  for (i = 0; i < dm->n_vars; i++)
    {
      v = dm->vars[i];
      if (v.first_column <= col && col <= v.last_column)
	return (v.v)->index;
    }
  return DM_INDEX_NOT_FOUND;
}

/*
  Return a pointer to the variable whose values
  are stored in column col.
 */
struct variable *
design_matrix_col_to_var (const struct design_matrix *dm, size_t col)
{
  size_t index;
  size_t i;
  struct design_matrix_var dmv;

  index = design_matrix_col_to_var_index (dm, col);
  for (i = 0; i < dm->n_vars; i++)
    {
      dmv = dm->vars[i];
      if ((dmv.v)->index == index)
	{
	  return (struct variable *) dmv.v;
	}
    }
  return NULL;
}

static size_t
cmp_dm_var_index (const struct design_matrix_var *dmv, size_t index)
{
  if (dmv->v->index == index)
    return 1;
  return 0;
}

/*
  Return the number of the first column which holds the
  values for variable v.
 */
size_t
design_matrix_var_to_column (const struct design_matrix * dm,
			     const struct variable * v)
{
  size_t i;
  struct design_matrix_var tmp;

  for (i = 0; i < dm->n_vars; i++)
    {
      tmp = dm->vars[i];
      if (cmp_dm_var_index (&tmp, v->index))
	{
	  return tmp.first_column;
	}
    }
  return DM_COLUMN_NOT_FOUND;
}

/* Last column. */
static size_t
dm_var_to_last_column (const struct design_matrix *dm,
		       const struct variable *v)
{
  size_t i;
  struct design_matrix_var tmp;

  for (i = 0; i < dm->n_vars; i++)
    {
      tmp = dm->vars[i];
      if (cmp_dm_var_index (&tmp, v->index))
	{
	  return tmp.last_column;
	}
    }
  return DM_COLUMN_NOT_FOUND;
}

/*
  Set the appropriate value in the design matrix, 
  whether that value is from a categorical or numeric
  variable. For a categorical variable, only the usual
  binary encoding is allowed.
 */
void
design_matrix_set_categorical (struct design_matrix *dm, size_t row,
			       const struct variable *var,
			       const union value *val)
{
  size_t col;
  size_t is_one;
  size_t fc;
  size_t lc;
  double entry;

  assert (var_is_alpha (var));
  fc = design_matrix_var_to_column (dm, var);
  lc = dm_var_to_last_column (dm, var);
  assert (lc != DM_COLUMN_NOT_FOUND);
  assert (fc != DM_COLUMN_NOT_FOUND);
  is_one = fc + cat_value_find (var, val);
  for (col = fc; col <= lc; col++)
    {
      entry = (col == is_one) ? 1.0 : 0.0;
      gsl_matrix_set (dm->m, row, col, entry);
    }
}
void
design_matrix_set_numeric (struct design_matrix *dm, size_t row,
			   const struct variable *var, const union value *val)
{
  size_t col;

  assert (var_is_numeric (var));
  col = design_matrix_var_to_column ((const struct design_matrix *) dm, var);
  assert (col != DM_COLUMN_NOT_FOUND);
  gsl_matrix_set (dm->m, row, col, val->f);
}
