/* cdf/geometric.c
 *
 * Copyright (C) 2004 Free Software Foundation, Inc.
 * Written by Jason H. Stover.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 */

/*
 * Pr(X <= n) for a negative binomial random variable X, i.e.,
 * the probability of n or fewer failuers before success k.
 */

#include <config.h>
#include <math.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_sf.h>
#include <gsl/gsl_cdf.h>
#include "gsl-extras.h"

/*
 * Pr (X <= n), i.e., the probability of n or fewer
 * failures until the first success.
 */
double
gslextras_cdf_geometric_P (const long n, const double p)
{
  double P;
  double a;
  int i;
  int m;
  double sign = 1.0;
  double term;
  double q;

  if(p > 1.0 || p < 0.0)
    {
      GSLEXTRAS_CDF_ERROR("p < 0 or p > 1",GSL_EDOM);
    }
  if ( n < 0 )
    {
      return 0.0;
    }
  q = 1.0 - p;
  a = (double) n+1;
  if( p < GSL_DBL_EPSILON )
    {
      /*
       * 1.0 - pow(q,a) will overflow, so use
       * a Taylor series.
       */
      i = 2;
      m = n+1;
      term = exp(log(a) + log(p));
      P = term;
      while ( term > GSL_DBL_MIN && i < m)
	{
	  term = exp (sign * gsl_sf_lnchoose(m,i) + i * log(p));
	  P += term;
	  i++;
	  sign = -sign;
	}
    }
  else
    {
      P = 1.0 - pow ( q, a);
    }
  return P;
}
double
gslextras_cdf_geometric_Q ( const long n, const double p)
{
  double P;
  double q;
  double a;

  if(p > 1.0 || p < 0.0)
    {
      GSLEXTRAS_CDF_ERROR("p < 0 or p > 1",GSL_EDOM);
    }
  if ( n < 0 )
    {
      P = 1.0;
    }
  else
    {
      a = (double) n+1;
      q = 1.0 - p;
      P = pow(q, a);
    }

  return P;
}
