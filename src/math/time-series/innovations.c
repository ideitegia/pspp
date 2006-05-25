/*
  src/math/time-series/arma/innovations.c
  
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
#include <gettext.h>
#define _(msgid) gettext (msgid)

static void
get_mean_variance (size_t n_vars, const struct casefile *cf,
		   struct innovations_estimate **est)
		   
{
  struct casereader *r;
  struct ccase *c;
  size_t n;
  double *x;
  double d;
  double tmp;
  double variance;

  x = xnmalloc (n_vars, sizeof *j);
  
  for (n = 0; n < n_vars; n++)
    {
      x[n] = 2.0;
      est[n]->mean = 0.0;
      est[n]->variance = 0.0;
    }
  for (r = casefile_get_reader (cf); casereader_read (r, &c);
       case_destroy (&c))
    {
      for (n = 0; n < n_vars; n++)
	{
	  if (!mv_is_value_missing (&v->miss, val))
	    {
	      tmp = case_data (&c, est[n]->variable->fv);
	      d = (tmp - est[n]->mean) / x[n];
	      est[n]->mean += d;
	      est[n]->variance += x[n] * x[n] * d * d;
	      x[n] += 1.0;
	    }
	}
    }
  for (n = 0; n < n_vars; n++)
    {
      est[n]->variance /= x[n];
    }
  free (x);
}

struct innovations_estimate ** pspp_innovations (const struct variable **vars, size_t *n_vars,
						 size_t max_lag, const struct casefile *cf)
{
  struct innovations_estimate **est;
  struct casereader *r;
  struct ccase *c;
  size_t i;

  est = xnmalloc (*n_vars, sizeof *est);
  for (i = 0; i < *n_vars; i++)
    {
      if (vars[i]->type == NUMERIC)
	{
	  est[i] = xmalloc (sizeof **est);
	  est[i]->variable = vars[i];
	  est[i]->mean = 0.0;
	  est[i]->variance = 0.0;
	  est[i]->cov = gsl_matrix_calloc (max_lag, max_lag);
	  est[i]->coeff = xnmalloc (max_lag, sizeof (*est[i]->coeff));
	  for (j = 0; j < max_lag; j++)
	    {
	      est[i]->coeff + j = xmalloc (sizeof (*(est[i]->coeff + j)));
	    }
	}
      else
	{
	  *n_vars--;
	  msg (MW, _("Cannot compute autocovariance for a non-numeric variable %s"),
		     var_to_string (vars[i]));
	}
    }

  /*
    First data pass to get the mean and variance.
   */
  get_mean_variance (*n_vars, cf, est);
}
