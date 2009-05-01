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

#include <config.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_cblas.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_fit.h>
#include <gsl/gsl_multifit.h>
#include <linreg/sweep.h>
#include <math/coefficient.h>
#include <math/linreg.h>
#include <math/coefficient.h>
#include <math/design-matrix.h>
#include <src/data/category.h>
#include <src/data/variable.h>
#include <src/data/value.h>
#include <gl/xalloc.h>

/*
  Find the least-squares estimate of b for the linear model:

  Y = Xb + Z

  where Y is an n-by-1 column vector, X is an n-by-p matrix of
  independent variables, b is a p-by-1 vector of regression coefficients,
  and Z is an n-by-1 normally-distributed random vector with independent
  identically distributed components with mean 0.

  This estimate is found via the sweep operator or singular-value
  decomposition with gsl.


  References:

  1. Matrix Computations, third edition. GH Golub and CF Van Loan.
  The Johns Hopkins University Press. 1996. ISBN 0-8018-5414-8.

  2. Numerical Analysis for Statisticians. K Lange. Springer. 1999.
  ISBN 0-387-94979-8.

  3. Numerical Linear Algebra for Applications in Statistics. JE Gentle.
  Springer. 1998. ISBN 0-387-98542-5.
*/


/*
  Get the mean and standard deviation of a vector
  of doubles via a form of the Kalman filter as
  described on page 32 of [3].
 */
static int
linreg_mean_std (gsl_vector_const_view v, double *mp, double *sp, double *ssp)
{
  size_t i;
  double j = 0.0;
  double d;
  double tmp;
  double mean;
  double variance;

  mean = gsl_vector_get (&v.vector, 0);
  variance = 0;
  for (i = 1; i < v.vector.size; i++)
    {
      j = (double) i + 1.0;
      tmp = gsl_vector_get (&v.vector, i);
      d = (tmp - mean) / j;
      mean += d;
      variance += j * (j - 1.0) * d * d;
    }
  *mp = mean;
  *sp = sqrt (variance / (j - 1.0));
  *ssp = variance;

  return GSL_SUCCESS;
}

/*
  Set V to contain an array of pointers to the variables
  used in the model. V must be at least C->N_COEFFS in length.
  The return value is the number of distinct variables found.
 */
int
pspp_linreg_get_vars (const void *c_, const struct variable **v)
{
  const pspp_linreg_cache *c = c_;
  const struct variable *tmp;
  int i;
  int j;
  int result = 0;

  /*
     Make sure the caller doesn't try to sneak a variable
     into V that is not in the model.
   */
  for (i = 0; i < c->n_coeffs; i++)
    {
      v[i] = NULL;
    }
  for (j = 0; j < c->n_coeffs; j++)
    {
      tmp = pspp_coeff_get_var (c->coeff[j], 0);
      assert (tmp != NULL);
      /* Repeated variables are likely to bunch together, at the end
         of the array. */
      i = result - 1;
      while (i >= 0 && v[i] != tmp)
	{
	  i--;
	}
      if (i < 0 && result < c->n_coeffs)
	{
	  v[result] = tmp;
	  result++;
	}
    }
  return result;
}

/*
  Allocate a pspp_linreg_cache and return a pointer
  to it. n is the number of cases, p is the number of
  independent variables.
 */
pspp_linreg_cache *
pspp_linreg_cache_alloc (const struct variable *depvar, const struct variable **indep_vars,
			 size_t n, size_t p)
{
  size_t i;
  pspp_linreg_cache *c;

  c = (pspp_linreg_cache *) malloc (sizeof (pspp_linreg_cache));
  c->depvar = depvar;
  c->indep_vars = indep_vars;
  c->indep_means = gsl_vector_alloc (p);
  c->indep_std = gsl_vector_alloc (p);
  c->ssx = gsl_vector_alloc (p);	/* Sums of squares for the
					   independent variables.
					 */
  c->ss_indeps = gsl_vector_alloc (p);	/* Sums of squares for the
					   model parameters.
					 */
  c->n_obs = n;
  c->n_indeps = p;
  c->n_coeffs = 0;
  for (i = 0; i < p; i++)
    {
      if (var_is_numeric (indep_vars[i]))
	{
	  c->n_coeffs++;
	}
      else
	{
	  c->n_coeffs += cat_get_n_categories (indep_vars[i]) - 1;
	}
    }

  c->cov = gsl_matrix_alloc (c->n_coeffs + 1, c->n_coeffs + 1);
  /*
     Default settings.
   */
  c->method = PSPP_LINREG_SWEEP;
  c->predict = pspp_linreg_predict;
  c->residual = pspp_linreg_residual;	/* The procedure to compute my
					   residuals. */
  c->get_vars = pspp_linreg_get_vars;	/* The procedure that returns
					   pointers to model
					   variables. */
  c->resid = NULL;		/* The variable storing my residuals. */
  c->pred = NULL;		/* The variable storing my predicted values. */

  return c;
}

bool
pspp_linreg_cache_free (void *m)
{
  int i;

  pspp_linreg_cache *c = m;
  if (c != NULL)
    {
      gsl_vector_free (c->indep_means);
      gsl_vector_free (c->indep_std);
      gsl_vector_free (c->ss_indeps);
      gsl_matrix_free (c->cov);
      gsl_vector_free (c->ssx);
      for (i = 0; i < c->n_coeffs; i++)
	{
	  pspp_coeff_free (c->coeff[i]);
	}
      free (c->coeff);
      free (c);
    }
  return true;
}
static void
cache_init (pspp_linreg_cache *cache)
{
  assert (cache != NULL);
  cache->dft = cache->n_obs - 1;
  cache->dfm = cache->n_indeps;
  cache->dfe = cache->dft - cache->dfm;
  cache->intercept = 0.0;
}

static void
post_sweep_computations (pspp_linreg_cache *cache, const struct design_matrix *dm,
			 gsl_matrix *sw)
{
  gsl_matrix *xm;
  gsl_matrix_view xtx;
  gsl_matrix_view xmxtx;
  double m;
  double tmp;
  size_t i;
  size_t j;
  int rc;
  
  assert (sw != NULL);
  assert (cache != NULL);

  cache->sse = gsl_matrix_get (sw, cache->n_indeps, cache->n_indeps);
  cache->mse = cache->sse / cache->dfe;
  /*
    Get the intercept.
  */
  m = cache->depvar_mean;
  for (i = 0; i < cache->n_indeps; i++)
    {
      tmp = gsl_matrix_get (sw, i, cache->n_indeps);
      cache->coeff[i]->estimate = tmp;
      m -= tmp * pspp_linreg_get_indep_variable_mean (cache, design_matrix_col_to_var (dm, i));
    }
  /*
    Get the covariance matrix of the parameter estimates.
    Only the upper triangle is necessary.
  */
  
  /*
    The loops below do not compute the entries related
    to the estimated intercept.
  */
  for (i = 0; i < cache->n_indeps; i++)
    for (j = i; j < cache->n_indeps; j++)
      {
	tmp = -1.0 * cache->mse * gsl_matrix_get (sw, i, j);
	gsl_matrix_set (cache->cov, i + 1, j + 1, tmp);
      }
  /*
    Get the covariances related to the intercept.
  */
  xtx = gsl_matrix_submatrix (sw, 0, 0, cache->n_indeps, cache->n_indeps);
  xmxtx = gsl_matrix_submatrix (cache->cov, 0, 1, 1, cache->n_indeps);
  xm = gsl_matrix_calloc (1, cache->n_indeps);
  for (i = 0; i < xm->size2; i++)
    {
      gsl_matrix_set (xm, 0, i, 
		      pspp_linreg_get_indep_variable_mean (cache, design_matrix_col_to_var (dm, i)));
    }
  rc = gsl_blas_dsymm (CblasRight, CblasUpper, cache->mse,
		       &xtx.matrix, xm, 0.0, &xmxtx.matrix);
  gsl_matrix_free (xm);
  if (rc == GSL_SUCCESS)
    {
      tmp = cache->mse / cache->n_obs;
      for (i = 1; i < 1 + cache->n_indeps; i++)
	{
	  tmp -= gsl_matrix_get (cache->cov, 0, i)
	    * pspp_linreg_get_indep_variable_mean (cache, design_matrix_col_to_var (dm, i - 1));
	}
      gsl_matrix_set (cache->cov, 0, 0, tmp);
      
      cache->intercept = m;
    }
  else
    {
      fprintf (stderr, "%s:%d:gsl_blas_dsymm: %s\n",
	       __FILE__, __LINE__, gsl_strerror (rc));
      exit (rc);
    }
}  
  
/*
  Fit the linear model via least squares. All pointers passed to pspp_linreg
  are assumed to be allocated to the correct size and initialized to the
  values as indicated by opts.
 */
int
pspp_linreg (const gsl_vector * Y, const struct design_matrix *dm,
	     const pspp_linreg_opts * opts, pspp_linreg_cache * cache)
{
  int rc;
  gsl_matrix *design = NULL;
  gsl_matrix_view xtx;
  gsl_vector_view xty;
  gsl_vector_view xi;
  gsl_vector_view xj;
  gsl_vector *param_estimates;
  struct pspp_coeff *coef;
  const struct variable *v;
  const union value *val;

  size_t i;
  size_t j;
  double tmp;
  double m;
  double s;
  double ss;

  if (cache == NULL)
    {
      return GSL_EFAULT;
    }
  if (opts->get_depvar_mean_std)
    {
      linreg_mean_std (gsl_vector_const_subvector (Y, 0, Y->size),
		       &m, &s, &ss);
      cache->depvar_mean = m;
      cache->depvar_std = s;
      cache->sst = ss;
    }
  cache_init (cache);
  cache->n_coeffs = dm->m->size2;
  for (i = 0; i < dm->m->size2; i++)
    {
      if (opts->get_indep_mean_std[i])
	{
	  linreg_mean_std (gsl_matrix_const_column (dm->m, i), &m, &s, &ss);
	  v = design_matrix_col_to_var (dm, i);
	  val = NULL;
	  if (var_is_alpha (v))
	    {
	      j = i - design_matrix_var_to_column (dm, v);
	      val = cat_subscript_to_value (j, v);
	    }
	  coef = pspp_linreg_get_coeff (cache, v, val);
	  pspp_coeff_set_mean (coef, m);
	  pspp_coeff_set_sd (coef, s);
	  gsl_vector_set (cache->ssx, i, ss);

	}
    }

  if (cache->method == PSPP_LINREG_SWEEP)
    {
      gsl_matrix *sw;
      /*
         Subtract the means to improve the condition of the design
         matrix. This requires copying dm->m and Y. We do not divide by the
         standard deviations of the independent variables here since doing
         so would cause a miscalculation of the residual sums of
         squares. Dividing by the standard deviation is done GSL's linear
         regression functions, so if the design matrix has a poor
         condition, use QR decomposition.

         The design matrix here does not include a column for the intercept
         (i.e., a column of 1's). If using PSPP_LINREG_QR, we need that column,
         so design is allocated here when sweeping, or below if using QR.
       */
      design = gsl_matrix_alloc (dm->m->size1, dm->m->size2);
      for (i = 0; i < dm->m->size2; i++)
	{
	  v = design_matrix_col_to_var (dm, i);
	  m = pspp_linreg_get_indep_variable_mean (cache, v);
	  for (j = 0; j < dm->m->size1; j++)
	    {
	      tmp = (gsl_matrix_get (dm->m, j, i) - m);
	      gsl_matrix_set (design, j, i, tmp);
	    }
	}
      sw = gsl_matrix_calloc (cache->n_indeps + 1, cache->n_indeps + 1);
      xtx = gsl_matrix_submatrix (sw, 0, 0, cache->n_indeps, cache->n_indeps);

      for (i = 0; i < xtx.matrix.size1; i++)
	{
	  tmp = gsl_vector_get (cache->ssx, i);
	  gsl_matrix_set (&(xtx.matrix), i, i, tmp);
	  xi = gsl_matrix_column (design, i);
	  for (j = (i + 1); j < xtx.matrix.size2; j++)
	    {
	      xj = gsl_matrix_column (design, j);
	      gsl_blas_ddot (&(xi.vector), &(xj.vector), &tmp);
	      gsl_matrix_set (&(xtx.matrix), i, j, tmp);
	    }
	}

      gsl_matrix_set (sw, cache->n_indeps, cache->n_indeps, cache->sst);
      xty = gsl_matrix_column (sw, cache->n_indeps);
      /*
         This loop starts at 1, with i=0 outside the loop, so we can get
         the model sum of squares due to the first independent variable.
       */
      xi = gsl_matrix_column (design, 0);
      gsl_blas_ddot (&(xi.vector), Y, &tmp);
      gsl_vector_set (&(xty.vector), 0, tmp);
      tmp *= tmp / gsl_vector_get (cache->ssx, 0);
      gsl_vector_set (cache->ss_indeps, 0, tmp);
      for (i = 1; i < cache->n_indeps; i++)
	{
	  xi = gsl_matrix_column (design, i);
	  gsl_blas_ddot (&(xi.vector), Y, &tmp);
	  gsl_vector_set (&(xty.vector), i, tmp);
	}

      /*
         Sweep on the matrix sw, which contains XtX, XtY and YtY.
       */
      reg_sweep (sw);
      post_sweep_computations (cache, dm, sw);
      gsl_matrix_free (sw);
    }
  else if (cache->method == PSPP_LINREG_CONDITIONAL_INVERSE)
    {
      /*
	Use the SVD of X^T X to find a conditional inverse of X^TX. If
	the SVD is X^T X = U D V^T, then set the conditional inverse
	to (X^T X)^c = V D^- U^T. D^- is defined as follows: If entry
	(i, i) has value sigma_i, then entry (i, i) of D^- is 1 /
	sigma_i if sigma_i > 0, and 0 otherwise. Then solve the normal
	equations by setting the estimated parameter vector to 
	(X^TX)^c X^T Y.
       */
    }
  else
    {
      gsl_multifit_linear_workspace *wk;
      /*
         Use QR decomposition via GSL.
       */

      param_estimates = gsl_vector_alloc (1 + dm->m->size2);
      design = gsl_matrix_alloc (dm->m->size1, 1 + dm->m->size2);

      for (j = 0; j < dm->m->size1; j++)
	{
	  gsl_matrix_set (design, j, 0, 1.0);
	  for (i = 0; i < dm->m->size2; i++)
	    {
	      tmp = gsl_matrix_get (dm->m, j, i);
	      gsl_matrix_set (design, j, i + 1, tmp);
	    }
	}

      wk = gsl_multifit_linear_alloc (design->size1, design->size2);
      rc = gsl_multifit_linear (design, Y, param_estimates,
				cache->cov, &(cache->sse), wk);
      for (i = 0; i < cache->n_coeffs; i++)
	{
	  cache->coeff[i]->estimate = gsl_vector_get (param_estimates, i + 1);
	}
      cache->intercept = gsl_vector_get (param_estimates, 0);
      if (rc == GSL_SUCCESS)
	{
	  gsl_multifit_linear_free (wk);
	  gsl_vector_free (param_estimates);
	}
      else
	{
	  fprintf (stderr, "%s:%d: gsl_multifit_linear returned %d\n",
		   __FILE__, __LINE__, rc);
	}
    }


  cache->ssm = cache->sst - cache->sse;
  /*
     Get the remaining sums of squares for the independent
     variables.
   */
  m = 0;
  for (i = 1; i < cache->n_indeps; i++)
    {
      j = i - 1;
      m += gsl_vector_get (cache->ss_indeps, j);
      tmp = cache->ssm - m;
      gsl_vector_set (cache->ss_indeps, i, tmp);
    }

  gsl_matrix_free (design);
  return GSL_SUCCESS;
}

/*
  Is the coefficient COEF contained in the list of coefficients
  COEF_LIST?
 */
static int
has_coefficient (const struct pspp_coeff **coef_list, const struct pspp_coeff *coef,
		 size_t n)
{
  size_t i = 0;

  while (i < n)
    {
      if (coef_list[i] == coef)
	{
	  return 1;
	}
      i++;
    }
  return 0;
}
/*
  Predict the value of the dependent variable with the
  new set of predictors. PREDICTORS must point to a list
  of variables, each of whose values are stored in VALS,
  in the same order.
 */
double
pspp_linreg_predict (const struct variable **predictors,
		     const union value **vals, const void *c_, int n_vals)
{
  const pspp_linreg_cache *c = c_;
  int j;
  size_t next_coef = 0;
  const struct pspp_coeff **coef_list;
  const struct pspp_coeff *coe;
  double result;
  double tmp;

  if (predictors == NULL || vals == NULL || c == NULL)
    {
      return GSL_NAN;
    }
  if (c->coeff == NULL)
    {
      /* The stupid model: just guess the mean. */
      return c->depvar_mean;
    }
  coef_list = xnmalloc (c->n_coeffs, sizeof (*coef_list));
  result = c->intercept;

  /*
     The loops guard against the possibility that the caller passed us
     inadequate information, such as too few or too many values, or
     a redundant list of variable names.
   */
  for (j = 0; j < n_vals; j++)
    {
      coe = pspp_linreg_get_coeff (c, predictors[j], vals[j]);
      if (!has_coefficient (coef_list, coe, next_coef))
	{
	  tmp = pspp_coeff_get_est (coe);
	  if (var_is_numeric (predictors[j]))
	    {
	      tmp *= vals[j]->f;
	    }
	  result += tmp;
	  coef_list[next_coef++] = coe;
	}
    }
  free (coef_list);

  return result;
}

double
pspp_linreg_residual (const struct variable **predictors,
		      const union value **vals,
		      const union value *obs, const void *c, int n_vals)
{
  double pred;
  double result;

  if (predictors == NULL || vals == NULL || c == NULL || obs == NULL)
    {
      return GSL_NAN;
    }
  pred = pspp_linreg_predict (predictors, vals, c, n_vals);

  result = isnan (pred) ? GSL_NAN : (obs->f - pred);
  return result;
}

/*
  Which coefficient is associated with V? The VAL argument is relevant
  only to categorical variables.
 */
struct pspp_coeff *
pspp_linreg_get_coeff (const pspp_linreg_cache * c,
		       const struct variable *v, const union value *val)
{
  if (c == NULL)
    {
      return NULL;
    }
  if (c->coeff == NULL || c->n_indeps == 0 || v == NULL)
    {
      return NULL;
    }
  return pspp_coeff_var_to_coeff (v, c->coeff, c->n_coeffs, val);
}
/*
  Return the standard deviation of the independent variable.
 */
double pspp_linreg_get_indep_variable_sd (pspp_linreg_cache *c, const struct variable *v)
{
  if (var_is_numeric (v))
    {
      const struct pspp_coeff *coef;
      coef = pspp_linreg_get_coeff (c, v, NULL);
      return pspp_coeff_get_sd (coef);
    }
  return GSL_NAN;
}

void pspp_linreg_set_indep_variable_sd (pspp_linreg_cache *c, const struct variable *v, 
					double s)
{
  if (var_is_numeric (v))
    {
      struct pspp_coeff *coef;
      coef = pspp_linreg_get_coeff (c, v, NULL);
      pspp_coeff_set_sd (coef, s);
    }
}

/*
  Mean of the independent variable.
 */
double pspp_linreg_get_indep_variable_mean (pspp_linreg_cache *c, const struct variable *v)
{
  if (var_is_numeric (v))
    {
      struct pspp_coeff *coef;
      coef = pspp_linreg_get_coeff (c, v, NULL);
      return pspp_coeff_get_mean (coef);
    }
  return GSL_NAN;
}

void pspp_linreg_set_indep_variable_mean (pspp_linreg_cache *c, const struct variable *v, 
					  double m)
{
  if (var_is_numeric (v))
    {
      struct pspp_coeff *coef;
      coef = pspp_linreg_get_coeff (c, v, NULL);
      pspp_coeff_set_mean (coef, m);
    }
}

/*
  Make sure the dependent variable is at the last column, and that
  only variables in the model are in the covariance matrix. 
 */
static struct design_matrix *
rearrange_covariance_matrix (const struct covariance_matrix *cm, pspp_linreg_cache *c)
{
  const struct variable **model_vars;
  struct design_matrix *cov;
  struct design_matrix *result;
  size_t *permutation;
  size_t i;
  size_t j;
  size_t k;
  size_t n_coeffs = 0;

  assert (cm != NULL);
  cov = covariance_to_design (cm);
  assert (cov != NULL);
  assert (c != NULL);
  assert (cov->m->size1 > 0);
  assert (cov->m->size2 == cov->m->size1);
  model_vars = xnmalloc (1 + c->n_indeps, sizeof (*model_vars));

  /*
    Put the model variables in the right order in MODEL_VARS.
    Count the number of coefficients.
   */
  for (i = 0; i < c->n_indeps; i++)
    {
      model_vars[i] = c->indep_vars[i];
    }
  model_vars[i] = c->depvar;
  result = covariance_matrix_create (1 + c->n_indeps, model_vars);
  permutation = xnmalloc (design_matrix_get_n_cols (result), sizeof (*permutation));

  for (j = 0; j < cov->m->size2; j++)
    {
      k = 0;
      while (k < result->m->size2)
	{
	  if (design_matrix_col_to_var (cov, j) == design_matrix_col_to_var (result, k)) 
	    {
	      permutation[k] = j;
	    }
	  k++;
	}
    }
  for (i = 0; i < result->m->size1; i++)
    for (j = 0; j < result->m->size2; j++)
      {
	gsl_matrix_set (result->m, i, j, gsl_matrix_get (cov->m, permutation[i], permutation[j]));
      }
  free (permutation);
  free (model_vars);
  return result;
}
/*
  Estimate the model parameters from the covariance matrix only. This
  method uses less memory than PSPP_LINREG, which requires the entire
  data set to be stored in memory.

  The function assumes FULL_COV may contain columns corresponding to
  variables that are not in the model. It fixes this in
  REARRANG_COVARIANCE_MATRIX. This allows the caller to compute a
  large covariance matrix once before, then pass it to this without
  having to alter it. The problem is that this means the caller must
  set CACHE->N_COEFFS.
*/
void
pspp_linreg_with_cov (const struct covariance_matrix *full_cov, 
		      pspp_linreg_cache * cache)
{
  struct design_matrix *cov;

  assert (full_cov != NULL);
  assert (cache != NULL);

  cov = rearrange_covariance_matrix (full_cov, cache);
  cache_init (cache);
  reg_sweep (cov->m);
  post_sweep_computations (cache, cov, cov->m);  
  design_matrix_destroy (cov);
}

double pspp_linreg_mse (const pspp_linreg_cache *c)
{
  assert (c != NULL);
  return (c->sse / c->dfe);
}
