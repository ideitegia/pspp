/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include <math.h>
#include "stats.h"

/* Returns the fourth power of its argument. */
double
hypercube (double x)
{
  x *= x;
  return x * x;
}

/* Returns the cube of its argument. */
double
cube (double x)
{
  return x * x * x;
}

/* Returns the square of its argument. */
double
sqr (double x)
{
  return x * x;
}

/*
 * kurtosis = [(n+1){n*sum(X**4) - 4*sum(X)*sum(X**3)
 *                   + 6*sum(X)**2*sum(X**2)/n - 3*sum(X)**4/n**2}]
 *           /[(n-1)(n-2)(n-3)*(variance)**2]
 *             -[3*{(n-1)**2}]
 *             /[(n-2)(n-3)]
 *
 * This and other formulas from _Biometry_, Sokal and Rohlf,
 * W. H. Freeman and Company, 1969.  See pages 117 and 136 especially.
 */
double
calc_kurt (const double d[4], double n, double variance)
{
  return
    (((n + 1) * (n * d[3]
		 - 4.0 * d[0] * d[2]
		 + 6.0 * sqr (d[0]) * d[1] / n
		 - 3.0 * hypercube (d[0]) / sqr (n)))
     / ((n - 1.0) * (n - 2.0) * (n - 3.0) * sqr (variance))
     - (3.0 * sqr (n - 1.0))
     / ((n - 2.0) * (n - 3.)));
}

/*
 * standard error of kurtosis = sqrt([24n((n-1)**2)]/[(n-3)(n-2)(n+3)(n+5)])
 */
double
calc_sekurt (double n)
{
  return sqrt ((24.0 * n * sqr (n - 1.0))
	       / ((n - 3.0) * (n - 2.0) * (n + 3.0) * (n + 5.0)));
}

/*
 * skewness = [n*sum(X**3) - 3*sum(X)*sum(X**2) + 2*sum(X)**3/n]/
 *           /[(n-1)(n-2)*(variance)**3]
 */
double
calc_skew (const double d[3], double n, double stddev)
{
  return
    ((n * d[2] - 3.0 * d[0] * d[1] + 2.0 * cube (d[0]) / n)
     / ((n - 1.0) * (n - 2.0) * cube (stddev)));
}

/*
 * standard error of skewness = sqrt([6n(n-1)] / [(n-2)(n+1)(n+3)])
 */
double
calc_seskew (double n)
{
  return
    sqrt ((6.0 * n * (n - 1.0))
	  / ((n - 2.0) * (n + 1.0) * (n + 3.0)));
}

/* Returns one-sided significance level corresponding to standard
   normal deviate X.  Algorithm from _SPSS Statistical Algorithms_,
   Appendix 1. */
#if 0
double
normal_sig (double x)
{
  const double a1 = .070523078;
  const double a2 = .0422820123;
  const double a3 = .0092705272;
  const double a4 = .0001520143;
  const double a5 = .0002765672;
  const double a6 = .0000430638;

  const double z = fabs (x) <= 14.14 ? 0.7071067812 * fabs (x) : 10.;
  double r;

  r = 1. + z * (a1 + z * (a2 + z * (a3 + z * (a4 + z * (a5 + z * a6)))));
  r *= r;	/* r ** 2 */
  r *= r;	/* r ** 4 */
  r *= r;	/* r ** 16 */

  return .5 / r;
}
#else /* 1 */
/* Taken from _BASIC Statistics: An Introduction to Problem Solving
   with Your Personal Computer_, Jerry W. O'Dell, TAB 1984, page 314-5. */
double
normal_sig (double z)
{
  double h;

  h = 1 + 0.0498673470 * z;
  z *= z;
  h += 0.0211410061 * z;
  z *= z;
  h += 0.0032776263 * z;
  z *= z;
  h += 0.0000380036 * z;
  z *= z;
  h += 0.0000488906 * z;
  z *= z;
  h += 0.0000053830 * z;
  return pow (h, -16.) / 2.;
}
#endif /* 1 */

/* Algorithm from _Turbo Pascal Programmer's Toolkit_, Rugg and
   Feldman, Que 1989.  Returns the significance level of chi-square
   value CHISQ with DF degrees of freedom, correct to at least 7
   decimal places.  */
double
chisq_sig (double x, int k)
{
  if (x <= 0. || k < 1)
    return 1.0;
  else if (k == 1)
    return 2. * normal_sig (sqrt (x));
  else if (k <= 30)
    {
      double z, z_partial, term, denom, numerator, value;

      z = 1.;
      z_partial = 1.;
      term = k;
      do
	{
	  term += 2;
	  z_partial *= x / term;
	  if (z_partial >= 10000000.)
	    return 0.;
	  z += z_partial;
	}
      while (z_partial >= 1.e-7);
      denom = term = 2 - k % 2;
      while (term < k)
	{
	  term += 2;
	  denom *= term;
	}
      if (k % 2)
	{
	  value = ((k + 1) / 2) * log (x) - x / 2.;
	  numerator = exp (value) * sqrt (2. / x / PI);
	}
      else
	{
	  value = k / 2. * log (x) - x / 2.;
	  numerator = exp (value);
	}
      return 1. - numerator * z / denom;
    }
  else
    {
      double term, numer, norm_x;

      term = 2. / 9. / k;
      numer = pow (x / k, 1. / 3.);
      norm_x = numer / sqrt (term);
      return 1.0 - normal_sig (norm_x);
    }
}
