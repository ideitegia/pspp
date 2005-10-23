/* lib/linreg/linreg.c

 Copyright (C) 2005 Free Software Foundation, Inc.
 Written by Jason H. Stover.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or (at
 your option) any later version.

 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 02111-1307, USA.
*/

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

#include "pspp_linreg.h"
#include <gsl/gsl_errno.h>
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
  Allocate a pspp_linreg_cache and return a pointer
  to it. n is the number of cases, p is the number of 
  independent variables.
 */
pspp_linreg_cache *
pspp_linreg_cache_alloc (size_t n, size_t p)
{
  pspp_linreg_cache *cache;

  cache = (pspp_linreg_cache *) malloc (sizeof (pspp_linreg_cache));
  cache->param_estimates = gsl_vector_alloc (p + 1);
  cache->indep_means = gsl_vector_alloc (p);
  cache->indep_std = gsl_vector_alloc (p);
  cache->ssx = gsl_vector_alloc (p);	/* Sums of squares for the independent
					   variables.
					 */
  cache->ss_indeps = gsl_vector_alloc (p);	/* Sums of squares for the model 
						   parameters. 
						 */
  cache->cov = gsl_matrix_alloc (p + 1, p + 1);	/* Covariance matrix. */
  cache->n_obs = n;
  cache->n_indeps = p;
  /*
     Default settings.
   */
  cache->method = PSPP_LINREG_SWEEP;

  return cache;
}

void
pspp_linreg_cache_free (pspp_linreg_cache * cache)
{
  gsl_vector_free (cache->param_estimates);
  gsl_vector_free (cache->indep_means);
  gsl_vector_free (cache->indep_std);
  gsl_vector_free (cache->ss_indeps);
  gsl_matrix_free (cache->cov);
  free (cache);
}

/*
  Fit the linear model via least squares. All pointers passed to pspp_linreg
  are assumed to be allocated to the correct size and initialized to the
  values as indicated by opts. 
 */
int
pspp_linreg (const gsl_vector * Y, const gsl_matrix * X,
	     const pspp_linreg_opts * opts, pspp_linreg_cache * cache)
{
  int rc;
  gsl_matrix *design;
  gsl_matrix_view xtx;
  gsl_matrix_view xm;
  gsl_matrix_view xmxtx;
  gsl_vector_view xty;
  gsl_vector_view xi;
  gsl_vector_view xj;

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
  for (i = 0; i < cache->n_indeps; i++)
    {
      if (opts->get_indep_mean_std[i])
	{
	  linreg_mean_std (gsl_matrix_const_column (X, i), &m, &s, &ss);
	  gsl_vector_set (cache->indep_means, i, m);
	  gsl_vector_set (cache->indep_std, i, s);
	  gsl_vector_set (cache->ssx, i, ss);
	}
    }
  cache->dft = cache->n_obs - 1;
  cache->dfm = cache->n_indeps;
  cache->dfe = cache->dft - cache->dfm;
  if (cache->method == PSPP_LINREG_SWEEP)
    {
      gsl_matrix *sw;
      /*
         Subtract the means to improve the condition of the design
         matrix. This requires copying X and Y. We do not divide by the
         standard deviations of the independent variables here since doing
         so would cause a miscalculation of the residual sums of
         squares. Dividing by the standard deviation is done GSL's linear
         regression functions, so if the design matrix has a very poor
         condition, use QR decomposition.
         *
         The design matrix here does not include a column for the intercept
         (i.e., a column of 1's). If using PSPP_LINREG_QR, we need that column,
         so design is allocated here when sweeping, or below if using QR.
       */
      design = gsl_matrix_alloc (X->size1, X->size2);
      for (i = 0; i < X->size2; i++)
	{
	  m = gsl_vector_get (cache->indep_means, i);
	  for (j = 0; j < X->size1; j++)
	    {
	      tmp = (gsl_matrix_get (X, j, i) - m);
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
      pspp_reg_sweep (sw);
      cache->sse = gsl_matrix_get (sw, cache->n_indeps, cache->n_indeps);
      cache->mse = cache->sse / cache->dfe;
      /*
         Get the intercept.
       */
      m = cache->depvar_mean;
      for (i = 0; i < cache->n_indeps; i++)
	{
	  tmp = gsl_matrix_get (sw, i, cache->n_indeps);
	  gsl_vector_set (cache->param_estimates, i + 1, tmp);
	  m -= tmp * gsl_vector_get (cache->indep_means, i);
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
      xm = gsl_matrix_view_vector (cache->indep_means, 1, cache->n_indeps);
      rc = gsl_blas_dsymm (CblasRight, CblasUpper, cache->mse,
			   &xtx.matrix, &xm.matrix, 0.0, &xmxtx.matrix);
      if (rc == GSL_SUCCESS)
	{
	  tmp = cache->mse / cache->n_obs;
	  for (i = 1; i < 1 + cache->n_indeps; i++)
	    {
	      tmp -= gsl_matrix_get (cache->cov, 0, i)
		* gsl_vector_get (cache->indep_means, i - 1);
	    }
	  gsl_matrix_set (cache->cov, 0, 0, tmp);

	  gsl_vector_set (cache->param_estimates, 0, m);
	}
      else
	{
	  fprintf (stderr, "%s:%d:gsl_blas_dsymm: %s\n",
		   __FILE__, __LINE__, gsl_strerror (rc));
	  exit (rc);
	}
      gsl_matrix_free (sw);
    }
  else
    {
      /*
         Use QR decomposition via GSL.
       */
      design = gsl_matrix_alloc (X->size1, 1 + X->size2);

      for (j = 0; j < X->size1; j++)
	{
	  gsl_matrix_set (design, j, 0, 1.0);
	  for (i = 0; i < X->size2; i++)
	    {
	      tmp = gsl_matrix_get (X, j, i);
	      gsl_matrix_set (design, j, i + 1, tmp);
	    }
	}
      gsl_multifit_linear_workspace *wk =
	gsl_multifit_linear_alloc (design->size1, design->size2);
      rc = gsl_multifit_linear (design, Y, cache->param_estimates,
				cache->cov, &(cache->sse), wk);
      if (rc == GSL_SUCCESS)
	{
	  gsl_multifit_linear_free (wk);
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
