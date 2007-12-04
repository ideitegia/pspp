/* cdf/binomial.c
 *
 * Copyright (C) 2004 Free Software Foundation, Inc.
 * 
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

/*
 * Computes the cumulative distribution function for a binomial
 * random variable. For a binomial random variable X with n trials
 * and success probability p,
 *
 *          Pr( X <= k ) = Pr( Y >= p )
 *
 * where Y is a beta random variable with parameters k+1 and n-k.
 *
 * Reference:
 *
 * W. Feller, "An Introduction to Probability and Its
 * Applications," volume 1. Wiley, 1968. Exercise 45, page 173,
 * chapter 6.
 */
#include <math.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_cdf.h>
#include "gsl-extras.h"

double
gslextras_cdf_binomial_P(const long k, const long n, const double p)
{
  double P;
  double a;
  double b;

  if(p > 1.0 || p < 0.0)
    {
      GSLEXTRAS_CDF_ERROR("p < 0 or p > 1",GSL_EDOM);
    }
  if ( k >= n )
    {
      P = 1.0;
    }
  else if (k < 0)
    {
      P = 0.0;
    }
  else
    {
      a = (double) k+1;
      b = (double) n - k;
      P = gsl_cdf_beta_Q( p, a, b);
    }

  return P;
}
double
gslextras_cdf_binomial_Q(const long k, const long n, const double q)
{
  double P;
  double a;
  double b;

  if(q > 1.0 || q < 0.0)
    {
      GSLEXTRAS_CDF_ERROR("p < 0 or p > 1",GSL_EDOM);
    }
  if( k >= n )
    {
      P = 0.0;
    }
  else if ( k < 0 )
    {
      P = 1.0;
    }
  else
    {
      a = (double) k+1;
      b = (double) n - k;
      P = gsl_cdf_beta_P(q, a, b);
    }

  return P;
}

