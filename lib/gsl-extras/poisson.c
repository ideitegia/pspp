/* cdf/poisson.c
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
/*
 * Computes the cumulative distribution function for a Poisson
 * random variable. For a Poisson random variable X with parameter
 * lambda,
 *
 *          Pr( X <= k ) = Pr( Y >= p )
 *
 * where Y is a gamma random variable with parameters k+1 and 1.
 *
 * Reference:
 *
 * W. Feller, "An Introduction to Probability and Its
 * Applications," volume 1. Wiley, 1968. Exercise 46, page 173,
 * chapter 6.
 */
#include <math.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_cdf.h>
#include "gsl-extras.h"

/*
 * Pr (X <= k) for a Poisson random variable X.
 */
double
gslextras_cdf_poisson_P (const long k, const double lambda)
{
  double P;
  double a;

  if ( lambda <= 0.0 )
    {
      GSLEXTRAS_CDF_ERROR ("lambda <= 0", GSL_EDOM);
    }
  if ( k < 0 )
    {
      P = 0.0;
    }
  else
    {
      a = (double) k+1;
      P = gsl_cdf_gamma_Q ( lambda, a, 1.0);
    }
  return P;
}

/*
 * Pr ( X > k ) for a Possion random variable X.
 */
double
gslextras_cdf_poisson_Q (const long k, const double lambda)
{
  double P;
  double a;

  if ( lambda <= 0.0 )
    {
      GSLEXTRAS_CDF_ERROR ("lambda <= 0", GSL_EDOM);
    }
  if ( k < 0 )
    {
      P = 1.0;
    }
  else
    {
      a = (double) k+1;
      P = gsl_cdf_gamma_P ( lambda, a, 1.0);
    }
  return P;
}

