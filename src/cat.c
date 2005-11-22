/* PSPP - linear regression.
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
  Functions and data structures to recode categorical variables into
  vectors and sub-rows of matrices.

  For some statistical models, it is necessary to change each value
  of a categorical variable to a vector with binary entries. These
  vectors are then stored as sub-rows within a matrix during
  model-fitting. E.g., we need functions and data strucutres to map a
  value, say 'a', of a variable named 'cat_var', to a vector, say (0
  1 0 0 0), and vice versa.  We also need to be able to map the
  vector back to the value 'a', and if the vector is a sub-row of a
  matrix, we need to know which sub-row corresponds to the variable
  'cat_var'.

  The data structures defined here will be placed in the variable 
  structure in the future. When that happens, the useful code
  in this file will be that which refers to design matrices.
*/
#include <config.h>
#include <stdlib.h>
#include <error.h>
#include "alloc.h"
#include "error.h"
#include "var.h"
#include "cat.h"
#include <string.h>
#include <math.h>
#include <gsl/gsl_machine.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>

#define N_INITIAL_CATEGORIES 1
#define CAT_COLUMN_NOT_FOUND -1
#define CAT_VALUE_NOT_FOUND -2
#define CAT_INDEX_NOT_FOUND -3

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

#if 0
struct recoded_categorical_array *
cr_recoded_cat_ar_create (int n_variables, struct variable *v_variables[])
{
  size_t n_categoricals = 0;
  size_t i;
  struct recoded_categorical_array *ca;
  struct variable *v;

  ca = xmalloc (sizeof *ca);
  for (i = 0; i < n_variables; i++)
    {
      v = v_variables[i];
      if (v->type == ALPHA)
	{
	  n_categoricals++;
	}
    }
  ca->n_vars = n_categoricals;
  ca->a = xnmalloc (n_categoricals, sizeof *ca->a);
  for (i = 0; i < n_categoricals; i++)
    {
      *(ca->a + i) = cr_recoded_categorical_create (v_variables[i]);
    }

  return ca;
}

int
cr_free_recoded_array (struct recoded_categorical_array *r)
{
  int rc = 0;
  size_t i;

  for (i = 0; i < r->n_vars; i++)
    {
      cr_recoded_categorical_destroy (*(r->a + i));
    }
  return rc;
}
#endif
static size_t
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

/*
   Create a gsl_matrix, whose rows correspond to values of a
   categorical variable. Since n categories have n-1 degrees of
   freedom, the gsl_matrix is n-by-(n-1), with the first category
   encoded as the zero vector.
 */
#if  0
void
cat_create_value_matrix (struct variable *v)
{
  size_t i;
  size_t row;
  size_t col;
  size_t n_rows;
  size_t n_cols;

  assert (v != NULL);
  if (v->type == ALPHA)
    {
      assert (v->rc != NULL);
      n_rows = v->rc->n_categories;
      n_cols = v->rc->n_categories - 1;
      v->rc->m = gsl_matrix_calloc (n_rows, n_cols);
      for (row = 1; row < n_rows; row++)
	{
	  col = row - 1;
	  gsl_matrix_set (v->rc->m, row, col, 1.0);
	}
    }
}
#endif
/*
  Return the subscript of the binary vector corresponding
  to this value.
 */
size_t
cat_value_to_subscript (const union value *val, struct variable *v)
{
  const union value *val2;
  size_t subscript;
  int different;

  assert (v != NULL);
  assert (val != NULL);
  assert (v->obs_vals != NULL);
  subscript = v->obs_vals->n_categories - 1;
  while (subscript > 0)
    {
      val2 = v->obs_vals->vals + subscript;
      assert (val2 != NULL);
      different = compare_values (val, val2, v->width);
      if (!different)
	{
	  return subscript;
	}
      subscript--;
    }
  return subscript;
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

#if 0
/*
  Return the row of the matrix corresponding
  to the value v.
 */
static gsl_vector_view
cr_value_to_vector (const union value *v, struct recoded_categorical *cr)
{
  size_t row;
  row = cr_value_to_subscript (v, cr);
  return gsl_matrix_row (cr->m, row);
}
#endif
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
  Return the value corresponding to the vector.
  To avoid searching the matrix, this routine takes
  advantage of the fact that element (i,i+1) is 1
  when i is between 1 and cr->n_categories - 1 and
  i is 0 otherwise.
 */
union value *
cr_vector_to_value (const gsl_vector * vec, struct variable *v)
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

#if 0
/*
  Given a variable, return a pointer to its recoded
  structure. BUSTED IN HERE.
 */
struct recoded_categorical *
cr_var_to_recoded_categorical (const struct variable *v,
			       struct recoded_categorical_array *ca)
{
  struct recoded_categorical *rc;
  size_t i;

  for (i = 0; i < ca->n_vars; i++)
    {
      rc = *(ca->a + i);
      if (rc->v->index == v->index)
	{
	  return rc;
	}
    }
  return NULL;
}
#endif
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
      if (v->type == NUMERIC)
	{
	  n_cols++;
	  (dm->vars + i)->last_column = n_cols;
	}
      else if (v->type == ALPHA)
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
  return CAT_INDEX_NOT_FOUND;
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
  return CAT_COLUMN_NOT_FOUND;
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
  return CAT_COLUMN_NOT_FOUND;
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

  assert (var->type == ALPHA);
  fc = design_matrix_var_to_column (dm, var);
  lc = dm_var_to_last_column (dm, var);
  assert (lc != CAT_COLUMN_NOT_FOUND);
  assert (fc != CAT_COLUMN_NOT_FOUND);
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

  assert (var->type == NUMERIC);
  col = design_matrix_var_to_column ((const struct design_matrix *) dm, var);
  assert (col != CAT_COLUMN_NOT_FOUND);
  gsl_matrix_set (dm->m, row, col, val->f);
}
