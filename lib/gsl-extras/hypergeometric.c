/* cdf/hypergeometric.c
 *
 * Copyright (C) 2004 Jason H. Stover.
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
 * Computes the cumulative distribution function for a hypergeometric
 * random variable. A hypergeometric random variable X is the number
 * of elements of type 0 in a sample of size t, drawn from a population
 * of size n1 + n0, in which n1 are of type 1 and n0 are of type 0.
 *
 * This algorithm computes Pr( X <= k ) by summing the terms from
 * the mass function, Pr( X = k ).
 *
 * References:
 *
 * T. Wu. An accurate computation of the hypergeometric distribution 
 * function. ACM Transactions on Mathematical Software. Volume 19, number 1,
 * March 1993.
 *  This algorithm is not used, since it requires factoring the
 *  numerator and denominator, then cancelling. It is more accurate
 *  than the algorithm used here, but the cancellation requires more
 *  time than the algorithm used here.
 *
 * W. Feller. An Introduction to Probability Theory and Its Applications,
 * third edition. 1968. Chapter 2, section 6. 
 */
#include <config.h>
#include <math.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_randist.h>
#include "gsl-extras.h"

/*
 * Pr (X <= k)
 */
double
gslextras_cdf_hypergeometric_P (const unsigned int k, 
                                const unsigned int n0,
                                const unsigned int n1,
                                const unsigned int t)
{
  unsigned int i;
  unsigned int mode;
  double P;
  double tmp;
  double relerr;

  if( t > (n0+n1))
    {
      GSLEXTRAS_CDF_ERROR("t larger than population size",GSL_EDOM);
    }
  else if( k >= n0 || k >= t)
    {
      P = 1.0;
    }
  else if (k < 0.0)
    {
      P = 0.0;
    }
  else
    {
      P = 0.0;
      mode = (int) t*n0 / (n0+n1);
      relerr = 1.0;
      if( k < mode )
	{
	  i = k;
	  relerr = 1.0;
	  while(i != UINT_MAX && relerr > GSL_DBL_EPSILON && P < 1.0)
	    {
	      tmp = gsl_ran_hypergeometric_pdf(i, n0, n1, t);
	      P += tmp;
	      relerr = tmp / P;
	      i--;
	    }
	}
      else
	{
	  i = mode;
	  relerr = 1.0;
	  while(i <= k && relerr > GSL_DBL_EPSILON && P < 1.0)
	    {
	      tmp = gsl_ran_hypergeometric_pdf(i, n0, n1, t);
	      P += tmp;
	      relerr = tmp / P;
	      i++;
	    }
	  i = mode - 1;
	  relerr = 1.0;
	  while( i != UINT_MAX && relerr > GSL_DBL_EPSILON && P < 1.0)
	    {
	      tmp = gsl_ran_hypergeometric_pdf(i, n0, n1, t);
	      P += tmp;
	      relerr = tmp / P;
	      i--;
	    }
	}
      /*
       * Hack to get rid of a pesky error when the sum
       * gets slightly above 1.0.
       */
      P = GSL_MIN_DBL (P, 1.0);
    }
  return P;
}

/*
 * Pr (X > k)
 */
double
gslextras_cdf_hypergeometric_Q (const unsigned int k, 
                                const unsigned int n0,
                                const unsigned int n1,
                                const unsigned int t)
{
  unsigned int i;
  unsigned int mode;
  double P;
  double relerr;
  double tmp;

  if( t > (n0+n1))
    {
      GSLEXTRAS_CDF_ERROR("t larger than population size",GSL_EDOM);
    }
  else if( k >= n0 || k >= t)
    {
      P = 0.0;
    }
  else if (k < 0.0)
    {
      P = 1.0;
    }
  else
    {
      P = 0.0;
      mode = (int) t*n0 / (n0+n1);
      relerr = 1.0;
      
      if(k < mode)
	{
	  i = mode;
	  while( i <= t && relerr > GSL_DBL_EPSILON && P < 1.0)
	    {
	      tmp = gsl_ran_hypergeometric_pdf(i, n0, n1, t);
	      P += tmp;
	      relerr = tmp / P;
	      i++;
	    }
	  i = mode - 1;
	  relerr = 1.0;
	  while ( i > k && relerr > GSL_DBL_EPSILON && P < 1.0)
	    {
	      tmp = gsl_ran_hypergeometric_pdf(i, n0, n1, t);
	      P += tmp;
	      relerr = tmp / P;
	      i--;
	    }
	}
      else
	{
	  i = k+1;
	  while(i <= t && relerr > GSL_DBL_EPSILON && P < 1.0)
	    {
	      tmp = gsl_ran_hypergeometric_pdf(i, n0, n1, t);
	      P += tmp;
	      relerr = tmp / P;
	      i++;
	    }
	}
      /*
       * Hack to get rid of a pesky error when the sum
       * gets slightly above 1.0.
       */
      P = GSL_MIN_DBL(P, 1.0);
    }
  return P;
}
