/*
  src/math/ts/innovations.c
  
  Copyright (C) 2006 Free Software Foundation, Inc. Written by Jason H. Stover.
  
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
  Find preliminary ARMA coefficients via the innovations algorithm.
  Also compute the sample mean and covariance matrix for each series.

  Reference:

  P. J. Brockwell and R. A. Davis. Time Series: Theory and
  Methods. Second edition. Springer. New York. 1991. ISBN
  0-387-97429-6. Sections 5.2, 8.3 and 8.4.
 */

#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_math.h>
#include <stdlib.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <math/coefficient.h>
#include <math/ts/innovations.h>

static void
get_mean_variance (const gsl_matrix *data,
		   struct innovations_estimate **est)
		   
{
  size_t n;
  size_t i;
  double d;
  double tmp;

  for (n = 0; n < data->size2; n++)
    {
      est[n]->n_obs = 2.0;
      est[n]->mean = 0.0;
      est[n]->variance = 0.0;
    }
  for (i = 0; i < data->size1; i++)
    {
      for (n = 0; n < data->size2; n++)
	{
	  tmp = gsl_matrix_get (data, i, n);
	  if (!gsl_isnan (tmp))
	    {
	      d = (tmp - est[n]->mean) / est[n]->n_obs;
	      est[n]->mean += d;
	      est[n]->variance += est[n]->n_obs * est[n]->n_obs * d * d;
	      est[n]->n_obs += 1.0;
	    }
	}
    }
  for (n = 0; n < data->size2; n++)
    {
      /* Maximum likelihood estimate of the variance. */
      est[n]->variance /= est[n]->n_obs;
    }
}

static int
get_covariance (const gsl_matrix *data, 
		struct innovations_estimate **est, size_t max_lag)
{
  size_t lag;
  size_t j;
  size_t i;
  double x;
  double y;
  int rc = 1;

  assert (data != NULL);
  assert (est != NULL);
  
  for (i = 0; i < data->size1; i++)
    {
      for (j = 0; j < data->size2; j++)
	{
	  x = gsl_matrix_get (data, i, j);

	  if (!gsl_isnan (x))
	    {
	      x -= est[j]->mean;
	      for (lag = 1; lag <= max_lag && lag < (data->size1 - i); lag++)
		{
		  y = gsl_matrix_get (data, i + lag, j);
		  if (!gsl_isnan (y))
		    {
		      y -= est[j]->mean;
		      *(est[j]->cov + lag - 1) += y * x;
		      est[j]->n_obs += 1.0;
		    }
		}
	    }
	}
    }
  for (j = 0; j < data->size2; j++)
    {
      *(est[j]->cov + lag - 1) /= est[j]->n_obs;
    }

  return rc;
}
static double
innovations_convolve (double **theta, struct innovations_estimate *est,
		      int i, int j)
{
  int k;
  double result = 0.0;

  for (k = 0; k < j; k++)
    {
      result += theta[i-1][i-k-1] * theta[j][j-k-1] * est->scale[k];
    }
  return result;
}
static void
innovations_update_scale (struct innovations_estimate *est, double *theta,
			  size_t i)
{
  double result = 0.0;
  size_t j;
  size_t k;

  if (i < (size_t) est->max_lag)
    {
      result = est->variance;
      for (j = 0; j < i; j++)
	{
	  k = i - j - 1;
	  result -= theta[k] * theta[k] * est->scale[j];
	}
      est->scale[i] = result;
    }
}
static void
init_theta (double **theta, size_t max_lag)
{
  size_t i;
  size_t j;

  for (i = 0; i < max_lag; i++)
    {
      for (j = 0; j <= i; j++)
	{
	  theta[i][j] = 0.0;
	}
    }
}
static void
innovations_update_coeff (double **theta, struct innovations_estimate *est,
			  size_t max_lag)
{
  size_t i;
  size_t j;
  size_t k;

  for (i = 0; i < max_lag; i++)
    {
      for (j = 0; j <= i; j++)
	{
	  k = i - j;
	  theta[i][k] = (est->cov[k] - 
	    innovations_convolve (theta, est, i, j))
	    / est->scale[k];
	}
      innovations_update_scale (est, theta[i], i + 1);
    }  
}
static void
get_coef (const gsl_matrix *data,
	  struct innovations_estimate **est, size_t max_lag)
{
  size_t i;
  size_t n;
  double **theta;

  theta = xnmalloc (max_lag, sizeof (*theta));
  for (i = 0; i < max_lag; i++)
    {
      theta[i] = xnmalloc (max_lag, sizeof (**(theta + i)));
    }

  for (n = 0; n < data->size2; n++)
    {
      init_theta (theta, max_lag);
      innovations_update_scale (est[n], theta[0], 0);
      innovations_update_coeff (theta, est[n], max_lag);
      /* Copy the final row of coefficients into EST->COEFF.*/
      for (i = 0; i < max_lag; i++)
	{
	  /*
	    The order of storage here means that the best predicted value
	    for the time series is computed as follows:

	    Let X[m], X[m-1],... denote the original series.
	    Let X_hat[0] denote the best predicted value of X[0],
	    X_hat[1] denote the projection of X[1] onto the subspace
	    spanned by {X[0] - X_hat[0]}. Let X_hat[m] denote the 
	    projection of X[m] onto the subspace spanned by {X[m-1] - X_hat[m-1],
	    X[m-2] - X_hat[m-2],...,X[0] - X_hat[0]}.

	    Then X_hat[m] = est->coeff[m-1] * (X[m-1] - X_hat[m-1])
	                  + est->coeff[m-1] * (X[m-2] - X_hat[m-2])
			  ...
			  + est->coeff[m-max_lag] * (X[m - max_lag] - X_hat[m - max_lag])

	    (That is what X_hat[m] SHOULD be, anyway. These routines need
	    to be tested.)
	   */
	  pspp_coeff_set_estimate (est[n]->coeff[i], theta[max_lag - 1][i]);
	}
    }

  for (i = 0; i < max_lag; i++)
    {
      free (theta[i]);
    }
  free (theta);
}

static void
innovations_struct_init (struct innovations_estimate *est, size_t lag)
{
  size_t j;

  est->mean = 0.0;
  est->variance = 0.0;
  est->cov = xnmalloc (lag, sizeof (*est->cov));
  est->scale = xnmalloc (lag + 1, sizeof (*est->scale));
  est->coeff = xnmalloc (lag, sizeof (*est->coeff));
  est->max_lag = (double) lag;
  /* COV does not the variance (i.e., the lag 0 covariance). So COV[0]
     holds the lag 1 covariance, COV[i] holds the lag i+1 covariance. */
  for (j = 0; j < lag; j++)
    {
      est->coeff[j] = xmalloc (sizeof (*(est->coeff[j])));
    }
}
      
struct innovations_estimate ** 
pspp_innovations (const struct design_matrix *dm, size_t lag)
{
  struct innovations_estimate **est;
  size_t i;

  est = xnmalloc (dm->m->size2, sizeof *est);
  for (i = 0; i < dm->m->size2; i++)
    {
      est[i] = xmalloc (sizeof *est[i]);
/*       est[i]->variable = vars[i]; */
      innovations_struct_init (est[i], lag);
    }

  get_mean_variance (dm->m, est);
  get_covariance (dm->m, est, lag);
  get_coef (dm->m, est, lag);
  
  return est;
}

static void 
pspp_innovations_free_one (struct innovations_estimate *est)
{
  size_t i;

  assert (est != NULL);
  for (i = 0; i < (size_t) est->max_lag; i++)
    {
      pspp_coeff_free (est->coeff[i]);
    }
  free (est->scale);
  free (est->cov);
  free (est);
}

void pspp_innovations_free (struct innovations_estimate **est, size_t n)
{
  size_t i;

  assert (est != NULL);
  for (i = 0; i < n; i++)
    {
      pspp_innovations_free_one (est[i]);
    }
  free (est);
}
