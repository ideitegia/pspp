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

#include <config.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_math.h>
#include <stdlib.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <math/coefficient.h>
#include <math/ts/innovations.h>

static void
get_mean (const gsl_matrix *data,
	  struct innovations_estimate **est)

{
  size_t n;
  size_t i;
  double d;
  double tmp;

  for (n = 0; n < data->size2; n++)
    {
      est[n]->n_obs = 0.0;
      est[n]->mean = 0.0;
    }
  for (i = 0; i < data->size1; i++)
    {
      for (n = 0; n < data->size2; n++)
	{
	  tmp = gsl_matrix_get (data, i, n);
	  if (!gsl_isnan (tmp))
	    {
	      est[n]->n_obs += 1.0;
	      d = (tmp - est[n]->mean) / est[n]->n_obs;
	      est[n]->mean += d;
	    }
	}
    }
}
static void
update_cov (struct innovations_estimate **est, gsl_vector_const_view x,
	    gsl_vector_const_view y, size_t lag)
{
  size_t j;
  double xj;
  double yj;

  for (j = 0; j < x.vector.size; j++)
    {
      xj = gsl_vector_get (&x.vector, j);
      yj = gsl_vector_get (&y.vector, j);
      if (!gsl_isnan (xj))
	{
	  if (!gsl_isnan (yj))
	    {
	      xj -= est[j]->mean;
	      yj -= est[j]->mean;
	      *(est[j]->cov + lag) += xj * yj;
	    }
	}
    }
}
static int
get_covariance (const gsl_matrix *data,
		struct innovations_estimate **est, size_t max_lag)
{
  size_t lag;
  size_t j;
  size_t i;
  int rc = 1;

  assert (data != NULL);
  assert (est != NULL);

  for (j = 0; j < data->size2; j++)
    {
      for (lag = 0; lag <= max_lag; lag++)
	{
	  *(est[j]->cov + lag) = 0.0;
	}
    }
  /*
    The rows are in the outer loop because a gsl_matrix is stored in
    row-major order.
   */
  for (i = 0; i < data->size1; i++)
    {
      for (lag = 0; lag <= max_lag && lag < data->size1 - i; lag++)
	{
	  update_cov (est, gsl_matrix_const_row (data, i),
		      gsl_matrix_const_row (data, i + lag), lag);
	}
    }
  for (j = 0; j < data->size2; j++)
    {
      for (lag = 0; lag <= max_lag; lag++)
	{
	  *(est[j]->cov + lag) /= est[j]->n_obs;
	}
    }

  return rc;
}

static double
innovations_convolve (double *x, double *y, struct innovations_estimate *est,
		      int i)
{
  int k;
  double result = 0.0;

  assert (x != NULL && y != NULL);
  assert (est != NULL);
  assert (est->scale != NULL);
  assert (i > 0);
  for (k = 0; k < i; k++)
    {
      result += x[k] * y[k] * est->scale[i-k-1];
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
      result = est->cov[0];
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
      theta[i][i] = est->cov[i+1] / est->scale[0];
      for (j = 1; j <= i; j++)
	{
	  k = i - j;
	  theta[i][k] = (est->cov[k+1] -
			 innovations_convolve (theta[i] + k + 1, theta[j - 1], est, j))
	    / est->scale[j];
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
innovations_struct_init (struct innovations_estimate *est,
			 const struct design_matrix *dm,
			 size_t lag)
{
  size_t j;

  est->mean = 0.0;
  /* COV[0] stores the lag 0 covariance (i.e., the variance), COV[1]
     holds the lag-1 covariance, etc.
   */
  est->cov = xnmalloc (lag + 1, sizeof (*est->cov));
  est->scale = xnmalloc (lag + 1, sizeof (*est->scale));
  est->coeff = xnmalloc (lag, sizeof (*est->coeff)); /* No intercept. */

  /*
    The loop below is an unusual use of PSPP_COEFF_INIT(). In a
    typical model, one column of a DESIGN_MATRIX has one
    coefficient. But in a time-series model, one column has many
    coefficients.
   */
  for (j = 0; j < lag; j++)
    {
      pspp_coeff_init (est->coeff + j, dm);
    }
  est->max_lag = (double) lag;
}
/*
  The mean is subtracted from the original data before computing the
  coefficients. The mean is NOT added back, so if you want to predict
  a new value, you must add the mean to X_hat[m] to get the correct
  value.
 */
static void
subtract_mean (gsl_matrix *m, struct innovations_estimate **est)
{
  size_t i;
  size_t j;
  double tmp;

  for (i = 0; i < m->size1; i++)
    {
      for (j = 0; j < m->size2; j++)
	{
	  tmp = gsl_matrix_get (m, i, j) - est[j]->mean;
	  gsl_matrix_set (m, i, j, tmp);
	}
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
      innovations_struct_init (est[i], dm, lag);
    }

  get_mean (dm->m, est);
  subtract_mean (dm->m, est);
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
