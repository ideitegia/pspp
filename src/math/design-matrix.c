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
#include <data/value.h>

#include <gsl/gsl_machine.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>

#define DM_COLUMN_NOT_FOUND -1
#define DM_INDEX_NOT_FOUND -3


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
	  size_t n_categories = cat_get_n_categories (v);
	  (dm->vars + i)->last_column =
	    (dm->vars + i)->first_column + n_categories - 2;
	  n_cols += n_categories - 1;
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
const struct variable *
design_matrix_col_to_var (const struct design_matrix *dm, size_t col)
{
  size_t i;
  struct design_matrix_var v;

  for (i = 0; i < dm->n_vars; i++)
    {
      v = dm->vars[i];
      if (v.first_column <= col && col <= v.last_column)
	return v.v;
    }
  return NULL;
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
      if (tmp.v == v)
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
      if (tmp.v == v)
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
