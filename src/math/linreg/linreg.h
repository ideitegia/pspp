/*
  lib/linreg/linreg.h
  
  Copyright (C) 2005 Free Software Foundation, Inc. Written by Jason H. Stover.
  
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

#ifndef LINREG_H
#define LINREG_H

#include <gsl/gsl_math.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>

struct variable;
struct pspp_linreg_coeff;
union value;

enum
{
  PSPP_LINREG_SWEEP,
  PSPP_LINREG_SVD
};



/*
  Options describing what special values should be computed.
 */
struct pspp_linreg_opts_struct
{
  int resid;			/* Should the residuals be returned? */

  int get_depvar_mean_std;
  int *get_indep_mean_std;	/* Array of booleans
				   dictating which
				   independent variables need
				   their means and standard
				   deviations computed within
				   pspp_linreg. This array
				   MUST be of length
				   n_indeps. If element i is
				   1, pspp_linreg will
				   compute the mean and
				   variance of indpendent
				   variable i. If element i
				   is 0, it will not compute
				   the mean and standard
				   deviation, and assume the
				   values are stored.
				   cache->indep_mean[i] is
				   the mean and
				   cache->indep_std[i] is the
				   sample standard deviation. */
};
typedef struct pspp_linreg_opts_struct pspp_linreg_opts;


/*
  Find the least-squares estimate of b for the linear model:

  Y = Xb + Z

  where Y is an n-by-1 column vector, X is an n-by-p matrix of
  independent variables, b is a p-by-1 vector of regression coefficients,
  and Z is an n-by-1 normally-distributed random vector with independent
  identically distributed components with mean 0.

  This estimate is found via the sweep operator or singular-value
  decomposition with gsl.


  References:

  1. Matrix Computations, third edition. GH Golub and CF Van Loan.
  The Johns Hopkins University Press. 1996. ISBN 0-8018-5414-8.

  2. Numerical Analysis for Statisticians. K Lange. Springer. 1999.
  ISBN 0-387-94979-8.

  3. Numerical Linear Algebra for Applications in Statistics. JE Gentle.
  Springer. 1998. ISBN 0-387-98542-5.
*/


struct pspp_linreg_cache_struct
{
  int n_obs;			/* Number of observations. */
  int n_indeps;			/* Number of independent variables. */
  int n_coeffs;

  /*
    The variable struct is ignored during estimation. It is here so
    the calling procedure can find the variable used in the model.
   */
  const struct variable *depvar;

  gsl_vector *residuals;
  struct pspp_linreg_coeff *coeff;
  int method;			/* Method to use to estimate parameters. */
  /*
     Means and standard deviations of the variables.
     If these pointers are null when pspp_linreg() is
     called, pspp_linreg() will compute their values.

     Entry i of indep_means is the mean of independent
     variable i, whose observations are stored in the ith
     column of the design matrix.
   */
  double depvar_mean;
  double depvar_std;
  gsl_vector *indep_means;
  gsl_vector *indep_std;

  /*
     Sums of squares.
   */
  double ssm;			/* Sums of squares for the overall model. */
  gsl_vector *ss_indeps;	/* Sums of squares from each
				   independent variable. */
  double sst;			/* Sum of squares total. */
  double sse;			/* Sum of squares error. */
  double mse;			/* Mean squared error. This is just sse /
				   dfe, but since it is the best unbiased
				   estimate of the population variance, it
				   has its own entry here. */
  gsl_vector *ssx;		/* Centered sums of squares for independent
				   variables, i.e. \sum (x[i] - mean(x))^2. */
  double ssy;			/* Centered sums of squares for dependent
				   variable. 
				*/
  /*
     Covariance matrix of the parameter estimates.
   */
  gsl_matrix *cov;
  /*
     Degrees of freedom.
   */
  double dft;
  double dfe;
  double dfm;

  /*
     'Hat' or Hessian matrix, i.e. (X'X)^{-1}, where X is our
     design matrix.
   */
  gsl_matrix *hat;

  double (*predict) (const struct variable **, const union value **,
		     const void *, int);
  double (*residual) (const struct variable **,
		      const union value **,
		      const union value *,
		      const void *, int);
};

typedef struct pspp_linreg_cache_struct pspp_linreg_cache;



/*
  Allocate a pspp_linreg_cache and return a pointer
  to it. n is the number of cases, p is the number of
  independent variables.
 */
pspp_linreg_cache *pspp_linreg_cache_alloc (size_t n, size_t p);

void pspp_linreg_cache_free (pspp_linreg_cache * c);

/*
  Fit the linear model via least squares. All pointers passed to pspp_linreg
  are assumed to be allocated to the correct size and initialized to the
  values as indicated by opts.
 */
int
pspp_linreg (const gsl_vector * Y, const gsl_matrix * X,
	     const pspp_linreg_opts * opts, pspp_linreg_cache * cache);

double
pspp_linreg_predict (const struct variable **, const union value **,
		     const pspp_linreg_cache *, int);
double
pspp_linreg_residual (const struct variable **, const union value **,
		      const union value *, const pspp_linreg_cache *, int);
#endif
