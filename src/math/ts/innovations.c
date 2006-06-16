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
#include <math.h>
#include <stdlib.h>
#include <data/case.h>
#include <data/casefile.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <math/coefficient.h>
#include <math/ts/innovations.h>

static void
get_mean_variance (size_t n_vars, const struct casefile *cf,
		   struct innovations_estimate **est)
		   
{
  struct casereader *r;
  struct ccase c;
  size_t n;
  double d;
  const union value *tmp;

  for (n = 0; n < n_vars; n++)
    {
      est[n]->n_obs = 2.0;
      est[n]->mean = 0.0;
      est[n]->variance = 0.0;
    }
  for (r = casefile_get_reader (cf); casereader_read (r, &c);
       case_destroy (&c))
    {
      for (n = 0; n < n_vars; n++)
	{
	  tmp = case_data (&c, est[n]->variable->fv);
	  if (!mv_is_value_missing (&(est[n]->variable->miss), tmp))
	    {
	      d = (tmp->f - est[n]->mean) / est[n]->n_obs;
	      est[n]->mean += d;
	      est[n]->variance += est[n]->n_obs * est[n]->n_obs * d * d;
	      est[n]->n_obs += 1.0;
	    }
	}
    }
  for (n = 0; n < n_vars; n++)
    {
      /* Maximum likelihood estimate of the variance. */
      est[n]->variance /= est[n]->n_obs;
    }
}

/*
  Read the first MAX_LAG cases.
 */
static bool
innovations_init_cases (struct casereader *r, struct ccase **c, size_t max_lag)
{
  bool value = true;
  size_t lag = 0;

  while (value && lag < max_lag)
    {
      lag++;
      value = casereader_read (r, c[lag]);
    }
  return value;
}

/*
  Read one case and update C, which contains the last MAX_LAG cases.
 */
static bool
innovations_update_cases (struct casereader *r, struct ccase **c, size_t max_lag)
{
  size_t lag;
  bool value = false;
  
  for (lag = 0; lag < max_lag - 1; lag++)
    {
      c[lag] = c[lag+1];
    }
  value = casereader_read (r, c[lag]);
  return value;
}
static void
get_covariance (size_t n_vars, const struct casefile *cf, 
		struct innovations_estimate **est, size_t max_lag)
{
  struct casereader *r;
  struct ccase **c;
  size_t lag;
  size_t n;
  bool read_case = false;
  double d;
  double x;
  const union value *tmp;
  const union value *tmp2;

  c = xnmalloc (max_lag, sizeof (*c));
  
  for (lag = 0; lag < max_lag; lag++)
    {
      c[lag] = xmalloc (sizeof *c[lag]);
    }

  r = casefile_get_reader (cf);
  read_case = innovations_init_cases (r, c, max_lag);

  while (read_case)
    {
      for (n = 0; n < n_vars; n++)
	{
	  tmp2 = case_data (c[0], est[n]->variable->fv);
	  if (!mv_is_value_missing (&est[n]->variable->miss, tmp2))
	    {
	      x = tmp2->f - est[n]->mean;
	      for (lag = 1; lag <= max_lag; lag++)
		{
		  tmp = case_data (c[lag], est[n]->variable->fv);
		  if (!mv_is_value_missing (&est[n]->variable->miss, tmp))
		    {
		      d = (tmp->f - est[n]->mean);
		      *(est[n]->cov + lag) += d * x;
		    }
		}
	    }
	}
      read_case = innovations_update_cases (r, c, max_lag);
    }
  for (lag = 0; lag <= max_lag; lag++)
    {
      for (n = 0; n < n_vars; n++)
	{
	  *(est[n]->cov + lag) /= (est[n]->n_obs - lag);
	}
    }
  for (lag = 0; lag < max_lag; lag++)
    {
      free (c[lag]);
    }
  free (c);
}
static double
innovations_convolve (double **theta, struct innovations_estimate *est,
		      int i, int j)
{
  int k;
  double result = 0.0;

  for (k = 0; k < i; k++)
    {
      result += theta[i][i-k] * theta[j][i-j] * est->cov[k];
    }
  return result;
}
static void
get_coef (size_t n_vars, const struct casefile *cf, 
		struct innovations_estimate **est, size_t max_lag)
{
  int j;
  int i;
  int k;
  size_t n;
  double v;
  double **theta;

  for (n = 0; n < n_vars; n++)
    {
      for (i = 0; i < max_lag; i++)
	{
	  v = est[n]->cov[i];
	  for (j = 0; j < i; j++)
	    {
	      k = i - j;
	      theta[i][k] = est[n]->cov[k] - 
		innovations_convolve (theta, est, i, j);
	    }
	}
    }
}

struct innovations_estimate ** 
pspp_innovations (const struct variable **vars, 
		  size_t *n_vars,
		  size_t lag, 
		  const struct casefile *cf)
{
  struct innovations_estimate **est;
  size_t i;
  size_t j;

  est = xnmalloc (*n_vars, sizeof *est);
  for (i = 0; i < *n_vars; i++)
    {
      if (vars[i]->type == NUMERIC)
	{
	  est[i] = xmalloc (sizeof **est);
	  est[i]->variable = vars[i];
	  est[i]->mean = 0.0;
	  est[i]->variance = 0.0;
	  est[i]->cov = xnmalloc (lag, sizeof (est[i]->cov));
	  est[i]->coeff = xnmalloc (lag, sizeof (*est[i]->coeff));
	  for (j = 0; j < lag; j++)
	    {
	      est[i]->coeff[j] = xmalloc (sizeof (*(est[i]->coeff + j)));
	    }
	}
      else
	{
	  *n_vars--;
/* 	  msg (MW, _("Cannot compute autocovariance for a non-numeric variable %s"), */
/* 		     var_to_string (vars[i])); */
	}
    }

  get_mean_variance (*n_vars, cf, est);
  get_covariance (*n_vars, cf, est, lag);
  get_coef (*n_vars, cf, est, lag);
  
  return est;
}
