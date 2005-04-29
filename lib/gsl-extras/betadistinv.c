/* cdf/betadistinv.c
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
 * Invert the Beta distribution. 
 * 
 * References:
 *
 * Roger W. Abernathy and Robert P. Smith. "Applying Series Expansion 
 * to the Inverse Beta Distribution to Find Percentiles of the F-Distribution,"
 * ACM Transactions on Mathematical Software, volume 19, number 4, December 1993,
 * pages 474-480.
 *
 * G.W. Hill and A.W. Davis. "Generalized asymptotic expansions of a 
 * Cornish-Fisher type," Annals of Mathematical Statistics, volume 39, number 8,
 * August 1968, pages 1264-1273.
 */

#include <config.h>
#include <math.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_sf_gamma.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_randist.h>
#include "gsl-extras.h"

#define BETAINV_INIT_ERR .001
#define BETADISTINV_N_TERMS 3
#define BETADISTINV_MAXITER 20

static double 
s_bisect (double x, double y)
{
  double result = GSL_MIN(x,y) + fabs(x - y) / 2.0;
  return result;
}
static double
new_guess_P ( double old_guess, double x, double y, 
	      double prob, double a, double b)
{
  double result;
  double p_hat;
  double end_point;
  
  p_hat = gsl_cdf_beta_P(old_guess, a, b);
  if (p_hat < prob)
    {
      end_point = GSL_MAX(x,y);
    }
  else if ( p_hat > prob )
    {
      end_point = GSL_MIN(x,y);
    }
  else
    {
      end_point = old_guess;
    }
  result = s_bisect(old_guess, end_point);
  
  return result;
}

static double
new_guess_Q ( double old_guess, double x, double y, 
	      double prob, double a, double b)
{
  double result;
  double q_hat;
  double end_point;
  
  q_hat = gsl_cdf_beta_Q(old_guess, a, b);
  if (q_hat >= prob)
    {
      end_point = GSL_MAX(x,y);
    }
  else if ( q_hat < prob )
    {
      end_point = GSL_MIN(x,y);
    }
  else
    {
      end_point = old_guess;
    }
  result = s_bisect(old_guess, end_point);
  
  return result;
}

/*
 * The get_corn_fish_* functions below return the first
 * three terms of the Cornish-Fisher expansion
 * without recursion. The recursive functions
 * make the code more legible when higher order coefficients
 * are used, but terms beyond the cubic do not 
 * improve accuracy.
 */
  /*
   * Linear coefficient for the 
   * Cornish-Fisher expansion.
   */
static double 
get_corn_fish_lin (const double x, const double a, const double b)
{
  double result;
  
  result = gsl_ran_beta_pdf (x, a, b);
  if(result > 0)
    {
      result = 1.0 / result;
    }
  else
    {
      result = GSL_DBL_MAX;
    }

  return result;
}
  /*
   * Quadratic coefficient for the 
   * Cornish-Fisher expansion.
   */
static double
get_corn_fish_quad (const double x, const double a, const double b)
{
  double result;
  double gam_ab;
  double gam_a;
  double gam_b;
  double num;
  double den;
  
  gam_ab =  gsl_sf_lngamma(a + b);
  gam_a = gsl_sf_lngamma (a);
  gam_b = gsl_sf_lngamma (b);
  num = exp(2 * (gam_a + gam_b - gam_ab)) * (1 - a + x * (b + a - 2));
  den = 2.0 * pow ( x, 2*a - 1 ) * pow ( 1 - x, 2 * b - 1 );
  if ( fabs(den) > 0.0)
    {
      result = num / den;
    }
  else
    {
      result = 0.0;
    }

  return result;
}
/*
 * The cubic term for the Cornish-Fisher expansion.
 * Theoretically, use of this term should give a better approximation, 
 * but in practice inclusion of the cubic term worsens the 
 * iterative procedure in gsl_cdf_beta_Pinv and gsl_cdf_beta_Qinv
 * for extreme values of p, a or b.
 */				    
#if 0
static double 
get_corn_fish_cube (const double x, const double a, const double b)
{
  double result;
  double am1 = a - 1.0;
  double am2 = a - 2.0;
  double apbm2 = a+b-2.0;
  double apbm3 = a+b-3.0;
  double apbm4 = a+b-4.0;
  double ab1ab2 = am1 * am2;
  double tmp;
  double num;
  double den;

  num =  (am1 - x * apbm2) * (am1 - x * apbm2);
  tmp = ab1ab2 - x * (apbm2 * ( ab1ab2 * apbm4 + 1) + x * apbm2 * apbm3);
  num += tmp;
  tmp = gsl_ran_beta_pdf(x,a,b);
  den = 2.0 * x * x * (1 - x) * (1 - x) * pow(tmp,3.0);
  if ( fabs(den) > 0.0)
    {
      result = num / den;
    }
  else
    {
      result = 0.0;
    }

  return result;
}
#endif
/*
 * The Cornish-Fisher coefficients can be defined recursivley,
 * starting with the nth derivative of s_psi = -f'(x)/f(x),
 * where f is the beta density.
 *
 * The section below was commented out since 
 * the recursive generation of the coeficients did
 * not improve the accuracy of the directly coded 
 * the first three coefficients.
 */
#if 0
static double
s_d_psi (double x, double a, double b, int n)
{
  double result;
  double np1 = (double) n + 1;
  double asgn;
  double bsgn;
  double bm1 = b - 1.0;
  double am1 = a - 1.0;
  double mx = 1.0 - x;
  
  asgn = (n % 2) ? 1.0:-1.0;
  bsgn = (n % 2) ? -1.0:1.0;
  result = gsl_sf_gamma(np1) * ((bsgn * bm1 / (pow(mx, np1)))
				+ (asgn * am1 / (pow(x,np1))));
  return result;
}
/*
 * nth derivative of a coefficient with respect 
 * to x.
 */
static double 
get_d_coeff ( double x, double a, 
	      double b, double n, double k)
{
  double result;
  double d_psi;
  double k_fac;
  double i_fac;
  double kmi_fac;
  double i;
  
  if (n == 2)
    {
      result = s_d_psi(x, a, b, k);
    }
  else
    {
      result = 0.0;
      for (i = 0.0; i < (k+1); i++)
	{
	  k_fac = gsl_sf_lngamma(k+1.0);
	  i_fac = gsl_sf_lngamma(i+1.0);
	  kmi_fac = gsl_sf_lngamma(k-i+1.0);
	  
	  result += exp(k_fac - i_fac - kmi_fac)
	    * get_d_coeff( x, a, b, 2.0, i) 
	    * get_d_coeff( x, a, b, (n - 1.0), (k - i));
	}
      result += get_d_coeff ( x, a, b, (n-1.0), (k+1.0));
    }

  return result;
}
/*
 * Cornish-Fisher coefficient.
 */
static double
get_corn_fish (double c, double x, 
	       double a, double b, double n)
{
  double result;
  double dc;
  double c_prev;
  
  if(n == 1.0)
    {
      result = 1;
    }
  else if (n==2.0)
    {
      result = s_d_psi(x, a, b, 0);
    }
  else
    {
      dc = get_d_coeff(x, a, b, (n-1.0), 1.0);
      c_prev = get_corn_fish(c, x, a, b, (n-1.0));
      result = (n-1) * s_d_psi(x,a,b,0) * c_prev + dc;
    }
  return result;
}
#endif

double 
gslextras_cdf_beta_Pinv ( const double p, const double a, const double b)
{
  double result;
  double state;
  double beta_result;
  double lower = 0.0;
  double upper = 1.0;
  double c1;
  double c2;
#if 0
  double c3;
#endif
  double frac1;
  double frac2;
  double frac3;
  double frac4;
  double p0;
  double p1;
  double p2;
  double tmp;
  double err;
  double abserr;
  double relerr;
  double min_err;
  int n_iter = 0;

  if ( p < 0.0 )
    {
      GSLEXTRAS_CDF_ERROR("p < 0", GSL_EDOM);
    }
  if ( p > 1.0 )
    {
      GSLEXTRAS_CDF_ERROR("p > 1",GSL_EDOM);
    }
  if ( a < 0.0 )
    {
      GSLEXTRAS_CDF_ERROR ("a < 0", GSL_EDOM );
    }
  if ( b < 0.0 )
    {
      GSLEXTRAS_CDF_ERROR ( "b < 0", GSL_EDOM );
    }
  if ( p == 0.0 )
    {
      return 0.0;
    }
  if ( p == 1.0 )
    {
      return 1.0;
    }

  if (p > (1.0 - GSL_DBL_EPSILON))
    {
      /*
       * When p is close to 1.0, the bisection
       * works better with gsl_cdf_Q.
       */
      state = gslextras_cdf_beta_Qinv ( p, a, b);
      result = 1.0 - state;
      return result;
    }
  if (p < GSL_DBL_EPSILON )
    {
      /*
       * Start at a small value and rise until
       * we are above the correct result. This 
       * avoids overflow. When p is very close to 
       * 0, an initial state value of a/(a+b) will
       * cause the interpolating polynomial
       * to overflow.
       */
      upper = GSL_DBL_MIN;
      beta_result = gsl_cdf_beta_P (upper, a, b);
      while (beta_result < p)
	{
	  lower = upper;
	  upper *= 4.0;
	  beta_result = gsl_cdf_beta_P (upper, a, b);
	}
      state = (lower + upper) / 2.0;
    }
  else
    {
      /*
       * First guess is the expected value.
       */
      lower = 0.0;
      upper = 1.0;
      state = a/(a+b);
      beta_result = gsl_cdf_beta_P (state, a, b);
    }
  err = beta_result - p;
  abserr = fabs(err);
  relerr = abserr / p;
  while ( relerr > BETAINV_INIT_ERR)
    {
      tmp = new_guess_P ( state, lower, upper, 
			  p, a, b);
      lower = ( tmp < state ) ? lower:state;
      upper = ( tmp < state ) ? state:upper;
      state = tmp;
      beta_result = gsl_cdf_beta_P (state, a, b);
      err = p - beta_result;
      abserr = fabs(err);
      relerr = abserr / p;
    }

  result = state;
  min_err = relerr;
  /*
   * Use a second order Lagrange interpolating
   * polynomial to get closer before switching to
   * the iterative method.
   */
  p0 = gsl_cdf_beta_P (lower, a, b);
  p1 = gsl_cdf_beta_P (state, a, b);
  p2 = gsl_cdf_beta_P (upper, a, b);
  if( p0 < p1 && p1 < p2)
    {
      frac1 = (p - p2) / (p0 - p1);
      frac2 = (p - p1) / (p0 - p2);
      frac3 = (p - p0) / (p1 - p2);
      frac4 = (p - p0) * (p - p1) / ((p2 - p0) * (p2 - p1));
      state = frac1 * (frac2 * lower - frac3 * state)
	+ frac4 * upper;

      beta_result = gsl_cdf_beta_P( state, a, b);
      err = p - beta_result;
      abserr = fabs(err);
      relerr = abserr / p;
      if (relerr < min_err)
	{
	  result = state;
	  min_err = relerr;
	}
      else
	{
	  /*
	   * Lagrange polynomial failed to reduce the error.
	   * This will happen with a very skewed beta density. 
	   * Undo previous steps.
	   */
	  state = result;
	  beta_result = gsl_cdf_beta_P(state,a,b);
	  err = p - beta_result;
	  abserr = fabs(err);
	  relerr = abserr / p;
	}
    }

  n_iter = 0;

  /*
   * Newton-type iteration using the terms from the
   * Cornish-Fisher expansion. If only the first term
   * of the expansion is used, this is Newton's method.
   */
  while ( relerr > GSL_DBL_EPSILON && n_iter < BETADISTINV_MAXITER)
    {
      n_iter++;
      c1 = get_corn_fish_lin (state, a, b);
      c2 = get_corn_fish_quad (state, a, b);
      /*
       * The cubic term does not help, and can can
       * harm the approximation for extreme values of
       * p, a, or b.       
       */      
#if 0
      c3 = get_corn_fish_cube (state, a, b);
      state += err * (c1 + (err / 2.0 ) * (c2 + c3 * err / 3.0));
#endif
      state += err * (c1 + (c2 * err / 2.0 ));
      /*
       * The section below which is commented out uses
       * a recursive function to get the coefficients. 
       * The recursion makes coding higher-order terms
       * easier, but did not improve the result beyond
       * the use of three terms. Since explicitly coding
       * those three terms in the get_corn_fish_* functions
       * was not difficult, the recursion was abandoned.
       */
#if 0 
      coeff = 1.0;
      for(i = 1.0; i < BETADISTINV_N_TERMS; i += 1.0)
	{
	  i_fac *= i;
	  coeff = get_corn_fish (coeff, prior_state, a, b, i);
	  state += coeff * pow(err, i) / 
	    (i_fac * pow (gsl_ran_beta_pdf(prior_state,a,b), i));
	}
#endif
      beta_result = gsl_cdf_beta_P ( state, a, b );
      err = p - beta_result;
      abserr = fabs(err);
      relerr = abserr / p;
      if (relerr < min_err)
	{
	  result = state;
	  min_err = relerr;
	}
    }

  return result;
}

double
gslextras_cdf_beta_Qinv (double q, double a, double b)
{
  double result;
  double state;
  double beta_result;
  double lower = 0.0;
  double upper = 1.0;
  double c1;
  double c2;
#if 0
  double c3;
#endif
  double p0;
  double p1;
  double p2;
  double frac1;
  double frac2;
  double frac3;
  double frac4;
  double tmp;
  double err;
  double abserr;
  double relerr;
  double min_err;
  int n_iter = 0;

  if ( q < 0.0 )
    {
      GSLEXTRAS_CDF_ERROR("q < 0", GSL_EDOM);
    }
  if ( q > 1.0 )
    {
      GSLEXTRAS_CDF_ERROR("q > 1",GSL_EDOM);
    }
  if ( a < 0.0 )
    {
      GSLEXTRAS_CDF_ERROR ("a < 0", GSL_EDOM );
    }
  if ( b < 0.0 )
    {
      GSLEXTRAS_CDF_ERROR ( "b < 0", GSL_EDOM );
    }
  if ( q == 0.0 )
    {
      return 1.0;
    }
  if ( q == 1.0 )
    {
      return 0.0;
    }

  if ( q < GSL_DBL_EPSILON )
    {
      /*
       * When q is close to 0, the bisection
       * and interpolation done in the rest of
       * this routine will not give the correct
       * value within double precision, so 
       * gsl_cdf_beta_Qinv is called instead.
       */
      state = gslextras_cdf_beta_Pinv ( q, a, b);
      result = 1.0 - state;
      return result;
    }
  if ( q > 1.0 - GSL_DBL_EPSILON )
    {
      /*
       * Make the initial guess close to 0.0.
       */
      upper = GSL_DBL_MIN;
      beta_result = gsl_cdf_beta_Q ( upper, a, b);
      while (beta_result > q )
	{
	  lower = upper;
	  upper *= 4.0;
	  beta_result = gsl_cdf_beta_Q ( upper, a, b);
	}
      state = (upper + lower) / 2.0;
    }
  else
    {
      /* Bisection to get an initial approximation.
       * First guess is the expected value.
       */
      state = a/(a+b);
      lower = 0.0;
      upper = 1.0;
    }
  beta_result = gsl_cdf_beta_Q (state, a, b);
  err = beta_result - q;
  abserr = fabs(err);
  relerr = abserr / q;
  while ( relerr > BETAINV_INIT_ERR)
    {
      n_iter++;
      tmp = new_guess_Q ( state, lower, upper, 
			  q, a, b);
      lower = ( tmp < state ) ? lower:state;
      upper = ( tmp < state ) ? state:upper;
      state = tmp;
      beta_result = gsl_cdf_beta_Q (state, a, b);
      err = q - beta_result;
      abserr = fabs(err);
      relerr = abserr / q;
    }
  result = state;
  min_err = relerr;

  /*
   * Use a second order Lagrange interpolating
   * polynomial to get closer before switching to
   * the iterative method.
   */
  p0 = gsl_cdf_beta_Q (lower, a, b);
  p1 = gsl_cdf_beta_Q (state, a, b);
  p2 = gsl_cdf_beta_Q (upper, a, b);
  if(p0 > p1 && p1 > p2)
    {
      frac1 = (q - p2) / (p0 - p1);
      frac2 = (q - p1) / (p0 - p2);
      frac3 = (q - p0) / (p1 - p2);
      frac4 = (q - p0) * (q - p1) / ((p2 - p0) * (p2 - p1));
      state = frac1 * (frac2 * lower - frac3 * state)
	+ frac4 * upper;
      beta_result = gsl_cdf_beta_Q( state, a, b);
      err = beta_result - q;
      abserr = fabs(err);
      relerr = abserr / q;
      if (relerr < min_err)
	{
	  result = state;
	  min_err = relerr;
	}
      else
	{
	  /*
	   * Lagrange polynomial failed to reduce the error.
	   * This will happen with a very skewed beta density. 
	   * Undo previous steps.
	   */
	  state = result;
	  beta_result = gsl_cdf_beta_P(state,a,b);
	  err = q - beta_result;
	  abserr = fabs(err);
	  relerr = abserr / q;
	}
    }

  /*
   * Iteration using the terms from the
   * Cornish-Fisher expansion. If only the first term
   * of the expansion is used, this is Newton's method.
   */

  n_iter = 0;
  while ( relerr > GSL_DBL_EPSILON && n_iter < BETADISTINV_MAXITER)
    {
      n_iter++;
      c1 = get_corn_fish_lin (state, a, b);
      c2 = get_corn_fish_quad (state, a, b);
      /*
       * The cubic term does not help, and can harm
       * the approximation for extreme values of p, a and b.
       */
#if 0
      c3 = get_corn_fish_cube (state, a, b);
      state += err * (c1 + (err / 2.0 ) * (c2 + c3 * err / 3.0));
#endif
      state += err * (c1 + (c2 * err / 2.0 ));
      beta_result = gsl_cdf_beta_Q ( state, a, b );
      err = beta_result - q;
      abserr = fabs(err);
      relerr = abserr / q;
      if (relerr < min_err)
	{
	  result = state;
	  min_err = relerr;
	}
    }

  return result;
}
