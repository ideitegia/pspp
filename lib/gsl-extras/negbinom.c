/* cdf/negbinom.c
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#include <math.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_cdf.h>
#include "gsl-extras.h"

/*
 * Pr(X <= n) for a negative binomial random variable X, i.e.,
 * the probability of n or fewer failuers before success k.
 */
double
gslextras_cdf_negative_binomial_P(const long n, const long k, const double p)
{
  double P;
  double a;
  double b;

  if(p > 1.0 || p < 0.0)
    {
      GSLEXTRAS_CDF_ERROR("p < 0 or p > 1",GSL_EDOM);
    }
  if ( k < 0 )
    {
      GSLEXTRAS_CDF_ERROR ("k < 0",GSL_EDOM);
    }
  if ( n < 0 )
    {
      P = 0.0;
    }
  else
    {
      a = (double) k;
      b = (double) n+1;
      P = gsl_cdf_beta_P(p, a, b);
    }

  return P;
}
/*
 * Pr ( X > n ).
 */
double
gslextras_cdf_negative_binomial_Q(const long n, const long k, const double p)
{
  double P;
  double a;
  double b;

  if(p > 1.0 || p < 0.0)
    {
      GSLEXTRAS_CDF_ERROR("p < 0 or p > 1",GSL_EDOM);
    }
  if ( k < 0 )
    {
      GSLEXTRAS_CDF_ERROR ("k < 0",GSL_EDOM);
    }
  if ( n < 0 )
    {
      P = 1.0;
    }
  else
    {
      a = (double) k;
      b = (double) n+1;
      P = gsl_cdf_beta_Q(p, a, b);
    }

  return P;
}

