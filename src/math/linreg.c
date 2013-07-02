/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2010, 2011 Free Software Foundation, Inc. 

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

#include "math/linreg.h"

#include <gsl/gsl_blas.h>
#include <gsl/gsl_cblas.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_fit.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_multifit.h>

#include "data/value.h"
#include "data/variable.h"
#include "linreg/sweep.h"

#include "gl/xalloc.h"

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


const struct variable **
linreg_get_vars (const linreg *c)
{
  return c->indep_vars;
}

/*
  Allocate a linreg and return a pointer to it. n is the number of
  cases, p is the number of independent variables.
 */
linreg *
linreg_alloc (const struct variable *depvar, const struct variable **indep_vars,
	      double n, size_t p)
{
  linreg *c;
  size_t i;

  c = xmalloc (sizeof (*c));
  c->depvar = depvar;
  c->indep_vars = xnmalloc (p, sizeof (*indep_vars));
  c->dependent_column = p;
  for (i = 0; i < p; i++)
    {
      c->indep_vars[i] = indep_vars[i];
    }
  c->indep_means = gsl_vector_alloc (p);
  c->indep_std = gsl_vector_alloc (p);

  c->n_obs = n;
  c->n_indeps = p;
  c->n_coeffs = p;
  c->coeff = xnmalloc (p, sizeof (*c->coeff));
  c->cov = gsl_matrix_calloc (c->n_coeffs + 1, c->n_coeffs + 1);
  c->dft = n - 1;
  c->dfm = p;
  c->dfe = c->dft - c->dfm;
  c->intercept = 0.0;
  c->depvar_mean = 0.0;
  /*
     Default settings.
   */
  c->method = LINREG_SWEEP;

  c->refcnt = 1;

  return c;
}


void
linreg_ref (linreg *c)
{
  c->refcnt++;
}

void
linreg_unref (linreg *c)
{
  if (--c->refcnt == 0)
    {
      gsl_vector_free (c->indep_means);
      gsl_vector_free (c->indep_std);
      gsl_matrix_free (c->cov);
      free (c->indep_vars);
      free (c->coeff);
      free (c);
    }
}

static void
post_sweep_computations (linreg *l, gsl_matrix *sw)
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
  assert (l != NULL);

  l->sse = gsl_matrix_get (sw, l->n_indeps, l->n_indeps);
  l->mse = l->sse / l->dfe;
  /*
    Get the intercept.
  */
  m = l->depvar_mean;
  for (i = 0; i < l->n_indeps; i++)
    {
      tmp = gsl_matrix_get (sw, i, l->n_indeps);
      l->coeff[i] = tmp;
      m -= tmp * linreg_get_indep_variable_mean (l, i);
    }
  /*
    Get the covariance matrix of the parameter estimates.
    Only the upper triangle is necessary.
  */
  
  /*
    The loops below do not compute the entries related
    to the estimated intercept.
  */
  for (i = 0; i < l->n_indeps; i++)
    for (j = i; j < l->n_indeps; j++)
      {
	tmp = -1.0 * l->mse * gsl_matrix_get (sw, i, j);
	gsl_matrix_set (l->cov, i + 1, j + 1, tmp);
      }
  /*
    Get the covariances related to the intercept.
  */
  xtx = gsl_matrix_submatrix (sw, 0, 0, l->n_indeps, l->n_indeps);
  xmxtx = gsl_matrix_submatrix (l->cov, 0, 1, 1, l->n_indeps);
  xm = gsl_matrix_calloc (1, l->n_indeps);
  for (i = 0; i < xm->size2; i++)
    {
      gsl_matrix_set (xm, 0, i, 
		      linreg_get_indep_variable_mean (l, i));
    }
  rc = gsl_blas_dsymm (CblasRight, CblasUpper, l->mse,
		       &xtx.matrix, xm, 0.0, &xmxtx.matrix);
  gsl_matrix_free (xm);
  if (rc == GSL_SUCCESS)
    {
      tmp = l->mse / l->n_obs;
      for (i = 1; i < 1 + l->n_indeps; i++)
	{
	  tmp -= gsl_matrix_get (l->cov, 0, i)
	    * linreg_get_indep_variable_mean (l, i - 1);
	}
      gsl_matrix_set (l->cov, 0, 0, tmp);
      
      l->intercept = m;
    }
  else
    {
      fprintf (stderr, "%s:%d:gsl_blas_dsymm: %s\n",
	       __FILE__, __LINE__, gsl_strerror (rc));
      exit (rc);
    }
}  

/*
  Predict the value of the dependent variable with the new set of
  predictors. VALS are assumed to be in the order corresponding to the
  order of the coefficients in the linreg struct.
 */
double
linreg_predict (const linreg *c, const double *vals, size_t n_vals)
{
  size_t j;
  double result;

  assert (n_vals = c->n_coeffs);
  if (vals == NULL || c == NULL)
    {
      return GSL_NAN;
    }
  if (c->coeff == NULL)
    {
      /* The stupid model: just guess the mean. */
      return c->depvar_mean;
    }
  result = c->intercept;

  for (j = 0; j < n_vals; j++)
    {
      result += linreg_coeff (c, j) * vals[j];
    }

  return result;
}

double
linreg_residual (const linreg *c, double obs, const double *vals, size_t n_vals)
{
  if (vals == NULL || c == NULL)
    {
      return GSL_NAN;
    }
  return (obs - linreg_predict (c, vals, n_vals));
}

/*
  Mean of the independent variable.
 */
double linreg_get_indep_variable_mean (const linreg *c, size_t j)
{
  assert (c != NULL);
  return gsl_vector_get (c->indep_means, j);
}

void linreg_set_indep_variable_mean (linreg *c, size_t j, double m)
{
  assert (c != NULL);
  gsl_vector_set (c->indep_means, j, m);
}

static void
linreg_fit_qr (const gsl_matrix *cov, linreg *l)
{
  double intcpt_coef = 0.0;
  double intercept_variance = 0.0;
  gsl_matrix *xtx;
  gsl_matrix *q;
  gsl_matrix *r;
  gsl_vector *xty;
  gsl_vector *tau;
  gsl_vector *params;
  double tmp = 0.0;
  size_t i;
  size_t j;

  xtx = gsl_matrix_alloc (cov->size1 - 1, cov->size2 - 1);
  xty = gsl_vector_alloc (cov->size1 - 1);
  tau = gsl_vector_alloc (cov->size1 - 1);
  params = gsl_vector_alloc (cov->size1 - 1);

  for (i = 0; i < xtx->size1; i++)
    {
      gsl_vector_set (xty, i, gsl_matrix_get (cov, cov->size2 - 1, i));
      for (j = 0; j < xtx->size2; j++)
	{
	  gsl_matrix_set (xtx, i, j, gsl_matrix_get (cov, i, j));
	}
    }
  gsl_linalg_QR_decomp (xtx, tau);
  q = gsl_matrix_alloc (xtx->size1, xtx->size2);
  r = gsl_matrix_alloc (xtx->size1, xtx->size2);

  gsl_linalg_QR_unpack (xtx, tau, q, r);
  gsl_linalg_QR_solve (xtx, tau, xty, params);
  for (i = 0; i < params->size; i++)
    {
      l->coeff[i] = gsl_vector_get (params, i);
    }
  l->sst = gsl_matrix_get (cov, cov->size1 - 1, cov->size2 - 1);
  l->ssm = 0.0;
  for (i = 0; i < l->n_indeps; i++)
    {
      l->ssm += gsl_vector_get (xty, i) * l->coeff[i];
    }
  l->sse = l->sst - l->ssm;

  gsl_blas_dtrsm (CblasLeft, CblasLower, CblasNoTrans, CblasNonUnit, linreg_mse (l),
		  r, q);
  /* Copy the lower triangle into the upper triangle. */
  for (i = 0; i < q->size1; i++)
    {
      gsl_matrix_set (l->cov, i + 1, i + 1, gsl_matrix_get (q, i, i));
      for (j = i + 1; j < q->size2; j++)
	{
	  intercept_variance -= 2.0 * gsl_matrix_get (q, i, j) *
	    linreg_get_indep_variable_mean (l, i) *
	    linreg_get_indep_variable_mean (l, j);
	  gsl_matrix_set (q, i, j, gsl_matrix_get (q, j, i));
	}
    }
  l->intercept = linreg_get_depvar_mean (l);
  tmp = 0.0;
  for (i = 0; i < l->n_indeps; i++)
    {
      tmp = linreg_get_indep_variable_mean (l, i);
      l->intercept -= l->coeff[i] * tmp;
      intercept_variance += tmp * tmp * gsl_matrix_get (q, i, i);
    }

  /* Covariances related to the intercept. */
  intercept_variance += linreg_mse (l) / linreg_n_obs (l);
  gsl_matrix_set (l->cov, 0, 0, intercept_variance);  
  for (i = 0; i < q->size1; i++)
    {
      for (j = 0; j < q->size2; j++)
	{
	  intcpt_coef -= gsl_matrix_get (q, i, j) 
	    * linreg_get_indep_variable_mean (l, j);
	}
      gsl_matrix_set (l->cov, 0, i + 1, intcpt_coef);
      gsl_matrix_set (l->cov, i + 1, 0, intcpt_coef);
      intcpt_coef = 0.0;
    }
      
  gsl_matrix_free (q);
  gsl_matrix_free (r);
  gsl_vector_free (xty);
  gsl_vector_free (tau);
  gsl_matrix_free (xtx);
  gsl_vector_free (params);
}

/*
  Estimate the model parameters from the covariance matrix. This
  function assumes the covariance entries corresponding to the
  dependent variable are in the final row and column of the covariance
  matrix.
*/
void
linreg_fit (const gsl_matrix *cov, linreg *l)
{
  assert (l != NULL);
  assert (cov != NULL);

  l->sst = gsl_matrix_get (cov, cov->size1 - 1, cov->size2 - 1);
  if (l->method == LINREG_SWEEP)
    {
      gsl_matrix *params;
      params = gsl_matrix_calloc (cov->size1, cov->size2);
      gsl_matrix_memcpy (params, cov);
      reg_sweep (params, l->dependent_column);
      post_sweep_computations (l, params);  
      gsl_matrix_free (params);
    }
  else if (l->method == LINREG_QR)
    {
      linreg_fit_qr (cov, l);
    }
}

double linreg_mse (const linreg *c)
{
  assert (c != NULL);
  return (c->sse / c->dfe);
}

double linreg_intercept (const linreg *c)
{
  return c->intercept;
}

gsl_matrix *
linreg_cov (const linreg *c)
{
  return c->cov;
}

double 
linreg_coeff (const linreg *c, size_t i)
{
  return (c->coeff[i]);
}

const struct variable *
linreg_indep_var (const linreg *c, size_t i)
{
  return (c->indep_vars[i]);
}

size_t 
linreg_n_coeffs (const linreg *c)
{
  return c->n_coeffs;
}

double
linreg_n_obs (const linreg *c)
{
  return c->n_obs;
}

double
linreg_sse (const linreg *c)
{
  return c->sse;
}

double
linreg_ssreg (const linreg *c)
{
  return (c->sst - c->sse);
}

double linreg_sst (const linreg *c)
{
  return c->sst;
}

double 
linreg_dfmodel ( const linreg *c)
{
  return c->dfm;
}

void
linreg_set_depvar_mean (linreg *c, double x)
{
  c->depvar_mean = x;
}

double 
linreg_get_depvar_mean (const linreg *c)
{
  return c->depvar_mean;
}
