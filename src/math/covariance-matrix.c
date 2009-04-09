/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009 Free Software Foundation, Inc.

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
  const union value *val1;
  const union value *val2;
  double dot_product;
  double sum1;
  double sum2;
  double ssize;
};



struct covariance_matrix
{
  struct design_matrix *cov;
  struct design_matrix *ssize;
  struct design_matrix *sums;
  struct hsh_table *ca;
  struct moments1 **m1;
  struct moments **m;
  const struct variable **v_variables;
  size_t n_variables;
  int n_pass;
  int missing_handling;
  enum mv_class missing_value;
  void (*accumulate) (struct covariance_matrix *, const struct ccase *,
		      const struct interaction_variable **, size_t);
  void (*update_moments) (struct covariance_matrix *, size_t, double);
};



static struct hsh_table *covariance_hsh_create (size_t *);
static hsh_hash_func covariance_accumulator_hash;
static unsigned int hash_numeric_alpha (const struct variable *,
					const struct variable *,
					const union value *, size_t);
static hsh_compare_func covariance_accumulator_compare;
static hsh_free_func covariance_accumulator_free;
static void update_moments1 (struct covariance_matrix *, size_t, double);
static void update_moments2 (struct covariance_matrix *, size_t, double);
static struct covariance_accumulator *get_new_covariance_accumulator (const
								      struct
								      variable
								      *,
								      const
								      struct
								      variable
								      *,
								      const
								      union
								      value *,
								      const
								      union
								      value
								      *);
static void covariance_accumulate_listwise (struct covariance_matrix *,
					    const struct ccase *,
					    const struct interaction_variable **,
					    size_t);
static void covariance_accumulate_pairwise (struct covariance_matrix *,
					    const struct ccase *,
					    const struct interaction_variable **,
					    size_t);

struct covariance_matrix *
covariance_matrix_init (size_t n_variables,
			const struct variable *v_variables[], int n_pass,
			int missing_handling, enum mv_class missing_value)
{
  size_t i;
  struct covariance_matrix *result = NULL;

  result = xmalloc (sizeof (*result));
  result->cov = NULL;
  result->n_variables = n_variables;
  result->ca = covariance_hsh_create (&result->n_variables);
  result->m = NULL;
  result->m1 = NULL;
  result->missing_handling = missing_handling;
  result->missing_value = missing_value;
  result->accumulate = (result->missing_handling == LISTWISE) ?
    covariance_accumulate_listwise : covariance_accumulate_pairwise;
  if (n_pass == ONE_PASS)
    {
      result->update_moments = update_moments1;
      result->m1 = xnmalloc (n_variables, sizeof (*result->m1));
      for (i = 0; i < n_variables; i++)
	{
	  result->m1[i] = moments1_create (MOMENT_MEAN);
	}
    }
  else
    {
      result->update_moments = update_moments2;
      result->m = xnmalloc (n_variables, sizeof (*result->m));
      for (i = 0; i < n_variables; i++)
	{
	  result->m[i] = moments_create (MOMENT_MEAN);
	}
    }
  result->v_variables = v_variables;

  result->n_pass = n_pass;

  return result;
}
static size_t 
get_n_rows (size_t n_variables, size_t *v_variables[])
{
  size_t i;
  size_t result = 0;
  for (i = 0; i < n_variables; i++)
    {
      if (var_is_numeric (v_variables[i]))
	{
	  result++;
	}
      else if (var_is_alpha (v_variables[i]))
	{
	  size_t n_categories = cat_get_n_categories (v_variables[i]);
	  result += n_categories - 1;
	}
    }
  return result;
}
/*
  The covariances are stored in a DESIGN_MATRIX structure.
 */
struct design_matrix *
covariance_matrix_create (size_t n_variables,
			  const struct variable *v_variables[])
{
  size_t n_rows = get_n_rows (n_variables, v_variables);
  return design_matrix_create (n_variables, v_variables, n_rows);
}

static void
update_moments1 (struct covariance_matrix *cov, size_t i, double x)
{
  assert (cov->m1 != NULL);
  moments1_add (cov->m1[i], x, 1.0);
}

static void
update_moments2 (struct covariance_matrix *cov, size_t i, double x)
{
  assert (cov->m != NULL);
  moments_pass_one (cov->m[i], x, 1.0);
}

void
covariance_matrix_destroy (struct covariance_matrix *cov)
{
  size_t i;

  assert (cov != NULL);
  design_matrix_destroy (cov->cov);
  design_matrix_destroy (cov->ssize);
  design_matrix_destroy (cov->sums);
  hsh_destroy (cov->ca);
  if (cov->n_pass == ONE_PASS)
    {
      for (i = 0; i < cov->n_variables; i++)
	{
	  moments1_destroy (cov->m1[i]);
	}
      free (cov->m1);
    }
  else
    {
      for (i = 0; i < cov->n_variables; i++)
	{
	  moments_destroy (cov->m[i]);
	}
      free (cov->m);
    }
}

/*
  Update the covariance matrix with the new entries, assuming that ROW
  corresponds to a categorical variable and V2 is numeric.
 */
static void
covariance_update_categorical_numeric (struct design_matrix *cov, double mean,
				       size_t row,
				       const struct variable *v2, double x,
				       const union value *val2)
{
  size_t col;
  double tmp;

  assert (var_is_numeric (v2));

  col = design_matrix_var_to_column (cov, v2);
  assert (val2 != NULL);
  tmp = design_matrix_get_element (cov, row, col);
  design_matrix_set_element (cov, row, col, (val2->f - mean) * x + tmp);
  design_matrix_set_element (cov, col, row, (val2->f - mean) * x + tmp);
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
      if (!compare_values_short (tmp_val, val1, v))
	{
	  y += -1.0;
	}
      tmp = design_matrix_get_element (cov, row, col);
      design_matrix_set_element (cov, row, col, x * y + tmp);
      design_matrix_set_element (cov, col, row, x * y + tmp);
    }
}

/*
  Call this function in the second data pass. The central moments are
  MEAN1 and MEAN2. Any categorical variables should already have their
  values summarized in in its OBS_VALS element.
 */
void
covariance_pass_two (struct design_matrix *cov, double mean1, double mean2,
		     double ssize, const struct variable *v1,
		     const struct variable *v2, const union value *val1,
		     const union value *val2)
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
	  if (!compare_values_short (tmp_val, val1, v1))
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
      x += design_matrix_get_element (cov, col, row);
      design_matrix_set_element (cov, row, col, x);
      design_matrix_set_element (cov, col, row, x);
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
  v_min =
    (var_get_dict_index (ca->v1) <
     var_get_dict_index (ca->v2)) ? ca->v1 : ca->v2;
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
      unsigned hash = hash_bytes (val_max, var_get_width (v_max), 0);
      hash = hash_bytes (val_min, var_get_width (v_min), hash);
      return hash_int (*n_vars * (*n_vars + 1 + idx_max) + idx_min, hash);
    }
  return -1u;
}

/*
  Make a hash table consisting of struct covariance_accumulators.
  This allows the accumulation of the elements of a covariance matrix
  in a single data pass. Call covariance_accumulate () for each case 
  in the data.
 */
static struct hsh_table *
covariance_hsh_create (size_t *n_vars)
{
  return hsh_create (*n_vars * *n_vars, covariance_accumulator_compare,
		     covariance_accumulator_hash, covariance_accumulator_free,
		     n_vars);
}

static void
covariance_accumulator_free (void *c_, const void *aux UNUSED)
{
  struct covariance_accumulator *c = c_;
  assert (c != NULL);
  free (c);
}

/*
  Hash comparison. Returns 0 for a match, or a non-zero int
  otherwise. The sign of a non-zero return value *should* indicate the
  position of C relative to the covariance_accumulator described by
  the other arguments. But for now, it just returns 1 for any
  non-match.  This should be changed when someone figures out how to
  compute a sensible sign for the return value.
 */
static int
match_nodes (const struct covariance_accumulator *c,
	     const struct variable *v1, const struct variable *v2,
	     const union value *val1, const union value *val2)
{
  if (var_get_dict_index (v1) == var_get_dict_index (c->v1))
    if (var_get_dict_index (v2) == var_get_dict_index (c->v2))
      {
	if (var_is_numeric (v1) && var_is_numeric (v2))
	  {
	    return 0;
	  }
	if (var_is_numeric (v1) && var_is_alpha (v2))
	  {
	    if (!compare_values_short (val2, c->val2, v2))
	      {
		return 0;
	      }
	  }
	if (var_is_alpha (v1) && var_is_numeric (v2))
	  {
	    if (!compare_values_short (val1, c->val1, v1))
	      {
		return 0;
	      }
	  }
	if (var_is_alpha (v1) && var_is_alpha (v2))
	  {
	    if (!compare_values_short (val1, c->val1, v1))
	      {
		if (!compare_values_short (val2, c->val2, v2))
		  {
		    return 0;
		  }
	      }
	  }
      }
  return 1;
}

/*
  This function is meant to be used as a comparison function for
  a struct hsh_table in src/libpspp/hash.c.
*/
static int
covariance_accumulator_compare (const void *a1_, const void *a2_,
				const void *aux UNUSED)
{
  const struct covariance_accumulator *a1 = a1_;
  const struct covariance_accumulator *a2 = a2_;

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
	+ var_get_dict_index (v2) + hash_string (val->s, 0);
    }
  else if (var_is_alpha (v1) && var_is_numeric (v2))
    {
      result = hash_numeric_alpha (v2, v1, val, n_vars);
    }
  return result;
}


static double
update_product (const struct variable *v1, const struct variable *v2,
		const union value *val1, const union value *val2)
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
static double
update_sum (const struct variable *var, const union value *val, double weight)
{
  assert (var != NULL);
  assert (val != NULL);
  if (var_is_alpha (var))
    {
      return weight;
    }
  return val->f;
}
static struct covariance_accumulator *
get_new_covariance_accumulator (const struct variable *v1,
				const struct variable *v2,
				const union value *val1,
				const union value *val2)
{
  if ((v1 != NULL) && (v2 != NULL) && (val1 != NULL) && (val2 != NULL))
    {
      struct covariance_accumulator *ca;
      ca = xmalloc (sizeof (*ca));
      ca->v1 = v1;
      ca->v2 = v2;
      ca->val1 = val1;
      ca->val2 = val2;
      return ca;
    }
  return NULL;
}

static const struct variable **
get_covariance_variables (const struct covariance_matrix *cov)
{
  return cov->v_variables;
}

static void
update_hash_entry (struct hsh_table *c,
		   const struct variable *v1,
		   const struct variable *v2,
		   const union value *val1, const union value *val2, 
		   const struct interaction_value *i_val1,
		   const struct interaction_value *i_val2)
{
  struct covariance_accumulator *ca;
  struct covariance_accumulator *new_entry;
  double iv_f1;
  double iv_f2;

  iv_f1 = interaction_value_get_nonzero_entry (i_val1);
  iv_f2 = interaction_value_get_nonzero_entry (i_val2);
  ca = get_new_covariance_accumulator (v1, v2, val1, val2);
  ca->dot_product = update_product (ca->v1, ca->v2, ca->val1, ca->val2);
  ca->dot_product *= iv_f1 * iv_f2;
  ca->sum1 = update_sum (ca->v1, ca->val1, iv_f1);
  ca->sum2 = update_sum (ca->v2, ca->val2, iv_f2);
  ca->ssize = 1.0;
  new_entry = hsh_insert (c, ca);
  
  if (new_entry != NULL)
    {
      new_entry->dot_product += ca->dot_product;
      new_entry->ssize += 1.0;
      new_entry->sum1 += ca->sum1;
      new_entry->sum2 += ca->sum2;
      /*
	If DOT_PRODUCT is null, CA was not already in the hash
	hable, so we don't free it because it was just inserted.
	If DOT_PRODUCT was not null, CA is already in the hash table.
	Unnecessary now, it must be freed here.
      */
      free (ca);
    }
}

/*
  Compute the covariance matrix in a single data-pass. Cases with
  missing values are dropped pairwise, in other words, only if one of
  the two values necessary to accumulate the inner product is missing.

  Do not call this function directly. Call it through the struct
  covariance_matrix ACCUMULATE member function, for example,
  cov->accumulate (cov, ccase).
 */
static void
covariance_accumulate_pairwise (struct covariance_matrix *cov,
				const struct ccase *ccase, 
				const struct interaction_variable **i_var,
				size_t n_intr)
{
  size_t i;
  size_t j;
  const union value *val1;
  const union value *val2;
  const struct variable **v_variables;
  struct interaction_value *i_val1 = NULL;
  struct interaction_value *i_val2 = NULL;

  assert (cov != NULL);
  assert (ccase != NULL);

  v_variables = get_covariance_variables (cov);
  assert (v_variables != NULL);

  for (i = 0; i < cov->n_variables; ++i)
    {
      if (is_interaction (v_variables[i], i_var, n_intr))
	{
	  i_val1 = interaction_case_data (ccase, v_variables[i], i_var, n_intr);
	  val1 = interaction_value_get (i_val1);
	}
      else
	{
	  val1 = case_data (ccase, v_variables[i]);
	}
      if (!var_is_value_missing (v_variables[i], val1, cov->missing_value))
	{
	  cat_value_update (v_variables[i], val1);
	  if (var_is_numeric (v_variables[i]))
	    cov->update_moments (cov, i, val1->f);

	  for (j = i; j < cov->n_variables; j++)
	    {
	      if (is_interaction (v_variables[j], i_var, n_intr))
		{
		  i_val2 = interaction_case_data (ccase, v_variables[j], i_var, n_intr);
		  val2 = interaction_value_get (i_val2);
		}
	      else
		{
		  val2 = case_data (ccase, v_variables[j]);
		}
	      if (!var_is_value_missing
		  (v_variables[j], val2, cov->missing_value))
		{
		  update_hash_entry (cov->ca, v_variables[i], v_variables[j],
				     val1, val2, i_val1, i_val2);
		  if (j != i)
		    update_hash_entry (cov->ca, v_variables[j],
				       v_variables[i], val2, val1, i_val2, i_val1);
		}
	    }
	}
    }
}

/*
  Compute the covariance matrix in a single data-pass. Cases with
  missing values are dropped listwise. In other words, if one of the
  values for any variable in a case is missing, the entire case is
  skipped. 

  The caller must use a casefilter to remove the cases with missing
  values before calling covariance_accumulate_listwise. This function
  assumes that CCASE has already passed through this filter, and
  contains no missing values.

  Do not call this function directly. Call it through the struct
  covariance_matrix ACCUMULATE member function, for example,
  cov->accumulate (cov, ccase).
 */
static void
covariance_accumulate_listwise (struct covariance_matrix *cov,
				const struct ccase *ccase,
				const struct interaction_variable **i_var,
				size_t n_intr)
{
  size_t i;
  size_t j;
  const union value *val1;
  const union value *val2;
  const struct variable **v_variables;
  struct interaction_value *i_val1 = NULL;
  struct interaction_value *i_val2 = NULL;

  assert (cov != NULL);
  assert (ccase != NULL);

  v_variables = get_covariance_variables (cov);
  assert (v_variables != NULL);

  for (i = 0; i < cov->n_variables; ++i)
    {
      if (is_interaction (v_variables[i], i_var, n_intr))
	{
	  i_val1 = interaction_case_data (ccase, v_variables[i], i_var, n_intr);
	  val1 = interaction_value_get (i_val1);
	}
      else
	{
	  val1 = case_data (ccase, v_variables[i]);
	}
      cat_value_update (v_variables[i], val1);
      if (var_is_numeric (v_variables[i]))
	cov->update_moments (cov, i, val1->f);

      for (j = i; j < cov->n_variables; j++)
	{
	  if (is_interaction (v_variables[j], i_var, n_intr))
	    {
	      i_val2 = interaction_case_data (ccase, v_variables[j], i_var, n_intr);
	      val2 = interaction_value_get (i_val2);
	    }
	  else
	    {
	      val2 = case_data (ccase, v_variables[j]);
	    }
	  update_hash_entry (cov->ca, v_variables[i], v_variables[j],
			     val1, val2, i_val1, i_val2);
	  if (j != i)
	    update_hash_entry (cov->ca, v_variables[j], v_variables[i],
			       val2, val1, i_val2, i_val1);
	}
    }
}

/*
  Call this function during the data pass. Each case will be added to
  a hash containing all values of the covariance matrix. After the
  data have been passed, call covariance_matrix_compute to put the
  values in the struct covariance_matrix. 
 */
void
covariance_matrix_accumulate (struct covariance_matrix *cov,
			      const struct ccase *ccase, void **aux, size_t n_intr)
{
  cov->accumulate (cov, ccase, (const struct interaction_variable **) aux, n_intr);
}
/*
  If VAR is categorical with d categories, its first category should
  correspond to the origin in d-dimensional Euclidean space.
 */
static bool
is_origin (const struct variable *var, const union value *val)
{
  if (cat_value_find (var, val) == 0)
    {
      return true;
    }
  return false;
}

/*
  Return the subscript of the column of the design matrix
  corresponding to VAL. If VAR is categorical with d categories, its
  first category should correspond to the origin in d-dimensional
  Euclidean space, so there is no subscript for this value.
 */
static size_t
get_exact_subscript (const struct design_matrix *dm, const struct variable *var,
		     const union value *val)
{
  size_t result;

  if (is_origin (var, val))
    {
      return -1u;
    }

  result = design_matrix_var_to_column (dm, var);
  if (var_is_alpha (var))
    {
      result += cat_value_find (var, val) - 1;
    }
  return result;
}

static void
covariance_matrix_insert (struct design_matrix *cov,
			  const struct variable *v1,
			  const struct variable *v2, const union value *val1,
			  const union value *val2, double product)
{
  size_t row;
  size_t col;

  assert (cov != NULL);

  row = get_exact_subscript (cov, v1, val1);
  col = get_exact_subscript (cov, v2, val2);
  if (row != -1u && col != -1u)
    {
      design_matrix_set_element (cov, row, col, product);
    }
}


static bool
is_covariance_contributor (const struct covariance_accumulator *ca, const struct design_matrix *dm,
			   size_t i, size_t j)
{
  size_t k;
  const struct variable *v1;
  const struct variable *v2;

  assert (dm != NULL);
  v1 = design_matrix_col_to_var (dm, i);
  if (var_get_dict_index (v1) == var_get_dict_index(ca->v1))
    {
      v2 = design_matrix_col_to_var (dm, j);
      if (var_get_dict_index (v2) == var_get_dict_index (ca->v2))
	{
	  k = get_exact_subscript (dm, v1, ca->val1);
	  if (k == i)
	    {
	      k = get_exact_subscript (dm, v2, ca->val2);
	      if (k == j)
		{
		  return true;
		}
	    }
	}
    }
  return false;
}
static double
get_sum (const struct covariance_matrix *cov, size_t i)
{
  size_t k;
  const struct variable *var;
  const union value *val = NULL;
  struct covariance_accumulator ca;
  struct covariance_accumulator *c;

  assert ( cov != NULL);
  var = design_matrix_col_to_var (cov->cov, i);
  if (var != NULL)
    {
      if (var_is_alpha (var))
	{
	  k = design_matrix_var_to_column (cov->cov, var);
	  i -= k;
	  val = cat_subscript_to_value (i, var);
	}
      ca.v1 = var;
      ca.v2 = var;
      ca.val1 = val;
      ca.val2 = val;
      c = (struct covariance_accumulator *) hsh_find (cov->ca, &ca);
      if (c != NULL)
	{
	  return c->sum1;
	}
    }
  return 0.0;
}
static void
update_ssize (struct design_matrix *dm, size_t i, size_t j, struct covariance_accumulator *ca)
{
  struct variable *var;
  double tmp;
  var = design_matrix_col_to_var (dm, i);
  if (var_get_dict_index (ca->v1) == var_get_dict_index (var))
    {
      var = design_matrix_col_to_var (dm, j);
      if (var_get_dict_index (ca->v2) == var_get_dict_index (var))
	{
	  tmp = design_matrix_get_element (dm, i, j);
	  tmp += ca->ssize;
	  design_matrix_set_element (dm, i, j, tmp);
	}
    }
}
static void
covariance_accumulator_to_matrix (struct covariance_matrix *cov)
{
  size_t i;
  size_t j;
  double sum_i = 0.0;
  double sum_j = 0.0;
  double tmp = 0.0;
  struct covariance_accumulator *entry;
  struct hsh_iterator iter;

  cov->cov = covariance_matrix_create (cov->n_variables, cov->v_variables);
  cov->ssize = covariance_matrix_create (cov->n_variables, cov->v_variables);
  cov->sums = covariance_matrix_create (cov->n_variables, cov->v_variables);
  for (i = 0; i < design_matrix_get_n_cols (cov->cov); i++)
    {
      sum_i = get_sum (cov, i);
      for (j = i; j < design_matrix_get_n_cols (cov->cov); j++)
	{
	  sum_j = get_sum (cov, j);
	  entry = hsh_first (cov->ca, &iter);
	  design_matrix_set_element (cov->sums, i, j, sum_i);	  
	  while (entry != NULL)
	    {
	      update_ssize (cov->ssize, i, j, entry);
	      /*
		We compute the centered, un-normalized covariance matrix.
	      */
	      if (is_covariance_contributor (entry, cov->cov, i, j))
		{
		  covariance_matrix_insert (cov->cov, entry->v1, entry->v2, entry->val1,
					    entry->val2, entry->dot_product);
		}
	      entry = hsh_next (cov->ca, &iter);
	    }
	  tmp = design_matrix_get_element (cov->cov, i, j);
	  tmp -= sum_i * sum_j / design_matrix_get_element (cov->ssize, i, j);
	  design_matrix_set_element (cov->cov, i, j, tmp);
	} 
    }
}


/*
  Call this function after passing the data.
 */
void
covariance_matrix_compute (struct covariance_matrix *cov)
{
  if (cov->n_pass == ONE_PASS)
    {
      covariance_accumulator_to_matrix (cov);
    }
}

struct design_matrix *
covariance_to_design (const struct covariance_matrix *c)
{
  if (c != NULL)
    {
      return c->cov;
    }
  return NULL;
}

double 
covariance_matrix_get_element (const struct covariance_matrix *c, size_t row, size_t col)
{
  return (design_matrix_get_element (c->cov, row, col));
}

