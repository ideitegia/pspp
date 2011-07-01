/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>

#include "math/correlation.h"

#include <gsl/gsl_matrix.h>
#include <gsl/gsl_cdf.h>
#include <math.h>

#include "libpspp/misc.h"

#include "gl/minmax.h"


double
significance_of_correlation (double rho, double w)
{
  double t = w - 2;

  /* |rho| will mathematically always be in the range [0, 1.0].  Inaccurate
     calculations sometimes cause it to be slightly greater than 1.0, so
     force it into the correct range to avoid NaN from sqrt(). */
  t /= 1 - MIN (1, pow2 (rho));

  t = sqrt (t);
  t *= rho;
  
  if (t > 0)
    return  gsl_cdf_tdist_Q (t, w - 2);
  else
    return  gsl_cdf_tdist_P (t, w - 2);
}

gsl_matrix *
correlation_from_covariance (const gsl_matrix *cv, const gsl_matrix *v)
{
  size_t i, j;
  gsl_matrix *corr = gsl_matrix_calloc (cv->size1, cv->size2);
  
  for (i = 0 ; i < cv->size1; ++i)
    {
      for (j = 0 ; j < cv->size2; ++j)
	{
	  double rho = gsl_matrix_get (cv, i, j);
	  
	  rho /= sqrt (gsl_matrix_get (v, i, j))
	    * 
	    sqrt (gsl_matrix_get (v, j, i));
	  
	  gsl_matrix_set (corr, i, j, rho);
	}
    }
  
  return corr;
}
