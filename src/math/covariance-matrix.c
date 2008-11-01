/* PSPP - a program for statistical analysis.
   Copyright (C) 2008 Free Software Foundation, Inc.

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
  Create and update the values in the covariance matrix.
*/
#include <assert.h>
#include <config.h>
#include <data/case.h>
#include <data/category.h>
#include <data/variable.h>
#include <data/value.h>
#include <libpspp/hash.h>
#include <libpspp/hash-functions.h>
#include <math/covariance-matrix.h>
#include <math/moments.h>
#include <string.h>
#include <xalloc.h>

/*
  Structure used to accumulate the covariance matrix in a single data
  pass.  Before passing the data, we do not know how many categories
  there are in each categorical variable. Therefore we do not know the
  size of the covariance matrix. To get around this problem, we
  accumulate the elements of the covariance matrix in pointers to
  COVARIANC_ACCUMULATOR. These values are then used to populate
  the covariance matrix.
 */
struct covariance_accumulator
{
  const struct variable *v1;
  const struct variable *v2;
  double product;
  const union value *val1;
  const union value *val2;
};

static hsh_hash_func covariance_accumulator_hash;
static unsigned int hash_numeric_alpha (const struct variable *, const struct variable *, 
					const union value *, size_t);
static hsh_compare_func covariance_accumulator_compare;
static hsh_free_func covariance_accumulator_free;

/*
  The covariances are stored in a DESIGN_MATRIX structure.
 */
struct design_matrix *
covariance_matrix_create (size_t n_variables, const struct variable *v_variables[])
{
  return design_matrix_create (n_variables, v_variables, (size_t) n_variables);
}

void covariance_matrix_destroy (struct design_matrix *x)
{
  design_matrix_destroy (x);
}

/*
  Update the covariance matrix with the new entries, assuming that ROW
  corresponds to a categorical variable and V2 is numeric.
 */
static void
covariance_update_categorical_numeric (struct design_matrix *cov, double mean,
			  size_t row, 
			  const struct variable *v2, double x, const union value *val2)
{
  size_t col;
  double tmp;
  
  assert (var_is_numeric (v2));

  col = design_matrix_var_to_column (cov, v2);
  assert (val2 != NULL);
  tmp = gsl_matrix_get (cov->m, row, col);
  gsl_matrix_set (cov->m, row, col, (val2->f - mean) * x + tmp);
  gsl_matrix_set (cov->m, col, row, (val2->f - mean) * x + tmp);
}
static void
column_iterate (struct design_matrix *cov, const struct variable *v,
		double ssize, double x, const union value *val1, size_t row)
{
  size_t col;
  size_t i;
  double y;
  double tmp;
  const union value *tmp_val;

  col = design_matrix_var_to_column (cov, v);  
  for (i = 0; i < cat_get_n_categories (v) - 1; i++)
    {
      col += i;
      y = -1.0 * cat_get_category_count (i, v) / ssize;
      tmp_val = cat_subscript_to_value (i, v);
      if (compare_values (tmp_val, val1, v))
	{
	  y += -1.0;
	}
      tmp = gsl_matrix_get (cov->m, row, col);
      gsl_matrix_set (cov->m, row, col, x * y + tmp);
      gsl_matrix_set (cov->m, col, row, x * y + tmp);
    }
}
/*
  Call this function in the second data pass. The central moments are
  MEAN1 and MEAN2. Any categorical variables should already have their
  values summarized in in its OBS_VALS element.
 */
void covariance_pass_two (struct design_matrix *cov, double mean1, double mean2,
			  double ssize, const struct variable *v1, 
			  const struct variable *v2, const union value *val1, const union value *val2)
{
  size_t row;
  size_t col;
  size_t i;
  double x;
  const union value *tmp_val;

  if (var_is_alpha (v1))
    {
      row = design_matrix_var_to_column (cov, v1);
      for (i = 0; i < cat_get_n_categories (v1) - 1; i++)
	{
	  row += i;
	  x = -1.0 * cat_get_category_count (i, v1) / ssize;
	  tmp_val = cat_subscript_to_value (i, v1);
	  if (compare_values (tmp_val, val1, v1))
	    {
	      x += 1.0;
	    }
	  if (var_is_numeric (v2))
	    {
	      covariance_update_categorical_numeric (cov, mean2, row, 
						     v2, x, val2);
	    }
	  else
	    {
	      column_iterate (cov, v1, ssize, x, val1, row);
	      column_iterate (cov, v2, ssize, x, val2, row);
	    }
	}
    }
  else if (var_is_alpha (v2))
    {
      /*
	Reverse the orders of V1, V2, etc. and put ourselves back
	in the previous IF scope.
       */
      covariance_pass_two (cov, mean2, mean1, ssize, v2, v1, val2, val1);
    }
  else
    {
      /*
	Both variables are numeric.
      */
      row = design_matrix_var_to_column (cov, v1);  
      col = design_matrix_var_to_column (cov, v2);
      x = (val1->f - mean1) * (val2->f - mean2);
      x += gsl_matrix_get (cov->m, col, row);
      gsl_matrix_set (cov->m, row, col, x);
      gsl_matrix_set (cov->m, col, row, x);
    }
}

static unsigned int
covariance_accumulator_hash (const void *h, const void *aux)
{
  struct covariance_accumulator *ca = (struct covariance_accumulator *) h;
  size_t *n_vars = (size_t *) aux;
  size_t idx_max;
  size_t idx_min;
  const struct variable *v_min;
  const struct variable *v_max;
  const union value *val_min;
  const union value *val_max;

  /*
    Order everything by the variables' indices. This ensures we get the
    same key regardless of the order in which the variables are stored
    and passed around.
   */
  v_min = (var_get_dict_index (ca->v1) < var_get_dict_index (ca->v2)) ? ca->v1 : ca->v2;
  v_max = (ca->v1 == v_min) ? ca->v2 : ca->v1;

  val_min = (v_min == ca->v1) ? ca->val1 : ca->val2;
  val_max = (ca->val1 == val_min) ? ca->val2 : ca->val1;

  idx_min = var_get_dict_index (v_min);
  idx_max = var_get_dict_index (v_max);

  if (var_is_numeric (v_max) && var_is_numeric (v_min))
    {
      return (*n_vars * idx_max + idx_min);
    }
  if (var_is_numeric (v_max) && var_is_alpha (v_min))
    {
      return hash_numeric_alpha (v_max, v_min, val_min, *n_vars);
    }
  if (var_is_alpha (v_max) && var_is_numeric (v_min))
    {
      return (hash_numeric_alpha (v_min, v_max, val_max, *n_vars));
    }
  if (var_is_alpha (v_max) && var_is_alpha (v_min))
    {
      unsigned int tmp;
      char *x = xnmalloc (1 + var_get_width (v_max) + var_get_width (v_min), sizeof (*x));
      strncpy (x, val_max->s, var_get_width (v_max));
      strncat (x, val_min->s, var_get_width (v_min));
      tmp = *n_vars * (*n_vars + 1 + idx_max)
	+ idx_min
	+ hsh_hash_string (x);
      free (x);
      return tmp;
    }
  return -1u;
}

/*
  Make a hash table consisting of struct covariance_accumulators.
  This allows the accumulation of the elements of a covariance matrix
  in a single data pass. Call covariance_accumulate () for each case 
  in the data.
 */
struct hsh_table *
covariance_hsh_create (size_t n_vars)
{
  return hsh_create (n_vars * (n_vars + 1) / 2, covariance_accumulator_compare, 
		     covariance_accumulator_hash, covariance_accumulator_free, &n_vars);
}

static void 
covariance_accumulator_free (void *c_, const void *aux UNUSED)
{
  struct covariance_accumulator *c = c_;
  assert (c != NULL);
  free (c);
}
static int
match_nodes (const struct covariance_accumulator *c, const struct variable *v1,
	     const struct variable *v2, const union value *val1,
	     const union value *val2)
{
  if (var_get_dict_index (v1) == var_get_dict_index (c->v1) && 
      var_get_dict_index (v2) == var_get_dict_index (c->v2))
    {
      if (var_is_numeric (v1) && var_is_numeric (v2))
	{
	  return 0;
	}
      if (var_is_numeric (v1) && var_is_alpha (v2))
	{
	  if (compare_values (val2, c->val2, v2))
	    {
	      return 0;
	    }
	}
      if (var_is_alpha (v1) && var_is_numeric (v2))
	{
	  if (compare_values (val1, c->val1, v1))
	    {
	      return 0;
	    }
	}
      if (var_is_alpha (v1) && var_is_alpha (v2))
	{
	  if (compare_values (val1, c->val1, v1))
	    {
	      if (compare_values (val2, c->val2, v2))
		{
		  return 0;
		}
	    }
	}
    }
  else if (v2 == c->v1 && v1 == c->v2)
    {
      return -match_nodes (c, v2, v1, val2, val1);
    }
  return 1;
}

/*
  This function is meant to be used as a comparison function for
  a struct hsh_table in src/libpspp/hash.c.
*/
static int
covariance_accumulator_compare (const void *a1_, const void *a2_, const void *aux UNUSED)
{
  const struct covariance_accumulator *a1 =  a1_;
  const struct covariance_accumulator *a2 =  a2_;

  if (a1 == NULL && a2 == NULL)
    return 0;

  if (a1 == NULL || a2 == NULL)
    return 1;

  return match_nodes (a1, a2->v1, a2->v2, a2->val1, a2->val2);
}

static unsigned int
hash_numeric_alpha (const struct variable *v1, const struct variable *v2, 
		    const union value *val, size_t n_vars)
{
  unsigned int result = -1u;
  if (var_is_numeric (v1) && var_is_alpha (v2))
    {
      result = n_vars * ((n_vars + 1) + var_get_dict_index (v1))
	+ var_get_dict_index (v2) + hsh_hash_string (val->s);
    }
  else if (var_is_alpha (v1) && var_is_numeric (v2))
    {
      result = hash_numeric_alpha (v2, v1, val, n_vars);
    }
  return result;
}


static double
update_product (const struct variable *v1, const struct variable *v2, const union value *val1,
		const union value *val2)
{
  assert (v1 != NULL);
  assert (v2 != NULL);
  assert (val1 != NULL);
  assert (val2 != NULL);
  if (var_is_alpha (v1) && var_is_alpha (v2))
    {
      return 1.0;
    }
  if (var_is_numeric (v1) && var_is_numeric (v2))
    {
      return (val1->f * val2->f);
    }
  if (var_is_numeric (v1) && var_is_alpha (v2))
    {
      return (val1->f);
    }
  if (var_is_numeric (v2) && var_is_alpha (v1))
    {
      update_product (v2, v1, val2, val1);
    }
  return 0.0;
}
/*
  Compute the covariance matrix in a single data-pass.
 */
void 
covariance_accumulate (struct hsh_table *cov, struct moments1 **m,
		       const struct ccase *ccase, const struct variable **vars,
		       size_t n_vars)
{
  size_t i;
  size_t j;
  const union value *val;
  struct covariance_accumulator *ca;
  struct covariance_accumulator *entry;

  assert (m != NULL);

  for (i = 0; i < n_vars; ++i)
    {
      val = case_data (ccase, vars[i]);
      if (var_is_alpha (vars[i]))
	{
	  cat_value_update (vars[i], val);
	}
      else
	{
	  moments1_add (m[i], val->f, 1.0);
	}
      for (j = i; j < n_vars; j++)
	{
	  ca = xmalloc (sizeof (*ca));
	  ca->v1 = vars[i];
	  ca->v2 = vars[j];
	  ca->val1 = val;
	  ca->val2 = case_data (ccase, ca->v2);
	  ca->product = update_product (ca->v1, ca->v2, ca->val1, ca->val2);
	  entry = hsh_insert (cov, ca);
	  if (entry != NULL)
	    {
	      entry->product += ca->product;
	      /*
		If ENTRY is null, CA was not already in the hash
		hable, so we don't free it because it was just inserted.
		If ENTRY was not null, CA is already in the hash table.
		Unnecessary now, it must be freed here.
	      */
	      free (ca);
	    }
	}
    }
}

static void 
covariance_matrix_insert (struct design_matrix *cov, const struct variable *v1,
			  const struct variable *v2, const union value *val1, 
			  const union value *val2, double product)
{
  size_t row;
  size_t col;
  size_t i;
  const union value *tmp_val;

  assert (cov != NULL);

  row = design_matrix_var_to_column (cov, v1);
  if (var_is_alpha (v1))
    {
      i = 0;
      tmp_val = cat_subscript_to_value (i, v1);
      while (!compare_values (tmp_val, val1, v1))
	{
	  i++;
	  tmp_val = cat_subscript_to_value (i, v1);
	}
      row += i;
      if (var_is_numeric (v2))
	{
	  col = design_matrix_var_to_column (cov, v2);
	}
      else
	{
	  col = design_matrix_var_to_column (cov, v2);
	  i = 0;
	  tmp_val = cat_subscript_to_value (i, v1);
	  while (!compare_values (tmp_val, val1, v1))
	    {
	      i++;
	      tmp_val = cat_subscript_to_value (i, v1);
	    } 
	  col += i;
	}
    }    
  else
    {
      if (var_is_numeric (v2))
	{
	  col = design_matrix_var_to_column (cov, v2);
	}
      else
	{
	  covariance_matrix_insert (cov, v2, v1, val2, val1, product);
	}
    }
  gsl_matrix_set (cov->m, row, col, product);
  gsl_matrix_set (cov->m, col, row, product);
}

static double
get_center (const struct variable *v, const union value *val, 
	    const struct variable **vars, const struct moments1 **m, size_t n_vars,
	    size_t ssize)
{
  size_t i = 0;

  while ((var_get_dict_index (vars[i]) != var_get_dict_index(v)) && (i < n_vars))
    {
      i++;
    }  
  if (var_is_numeric (v))
    {
      double mean;
      moments1_calculate (m[i], NULL, &mean, NULL, NULL, NULL);
      return mean;
    }
  else 
    {
      i = cat_value_find (v, val);
      return (cat_get_category_count (i, v) / ssize);
    }
  return 0.0;
}

/*
  Subtract the product of the means.
 */
static double
center_entry (const struct covariance_accumulator *ca, const struct variable **vars,
	      const struct moments1 **m, size_t n_vars, size_t ssize)
{
  double m1;
  double m2;
  double result = 0.0;
  
  m1 = get_center (ca->v1, ca->val1, vars, m, n_vars, ssize);
  m2 = get_center (ca->v2, ca->val2, vars, m, n_vars, ssize);
  result = ca->product - ssize * m1 * m2;
  return result;
}

/*
  The first moments in M should be stored in the order corresponding
  to the order of VARS. So, for example, VARS[0] has its moments in
  M[0], VARS[1] has its moments in M[1], etc.
 */
struct design_matrix *
covariance_accumulator_to_matrix (struct hsh_table *cov, const struct moments1 **m,
				  const struct variable **vars, size_t n_vars, size_t ssize)
{
  double tmp;
  struct covariance_accumulator *entry;
  struct design_matrix *result = NULL;
  struct hsh_iterator iter;
  
  result = covariance_matrix_create (n_vars, vars);

  entry = hsh_first (cov, &iter);
  
  while (entry != NULL)
    {
      /*
	We compute the centered, un-normalized covariance matrix.
       */
      tmp = center_entry (entry, vars, m, n_vars, ssize);
      covariance_matrix_insert (result, entry->v1, entry->v2, entry->val1,
				entry->val2, tmp);
      entry = hsh_next (cov, &iter);
    }

  return result;
}

