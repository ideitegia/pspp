/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2011 Free Software Foundation, Inc. Written by Jason H. Stover.

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

#ifndef LINREG_H
#define LINREG_H

#include <gsl/gsl_math.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <stdbool.h>

enum
{
  LINREG_CONDITIONAL_INVERSE,
  LINREG_QR,
  LINREG_SWEEP,
};



/*
  Options describing what special values should be computed.
 */
struct pspp_linreg_opts_struct
{
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


struct linreg_struct
{
  double n_obs;			/* Number of observations. */
  int n_indeps;			/* Number of independent variables. */
  int n_coeffs;                 /* The intercept is not considered a
				   coefficient here. */

  /*
    Pointers to the variables.
   */
  const struct variable *depvar;
  const struct variable **indep_vars;

  double *coeff;
  double intercept;
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
  gsl_vector *indep_means;
  gsl_vector *indep_std;

  /*
     Sums of squares.
   */
  double ssm;			/* Sums of squares for the overall model. */
  double sst;			/* Sum of squares total. */
  double sse;			/* Sum of squares error. */
  double mse;			/* Mean squared error. This is just sse /
				   dfe, but since it is the best unbiased
				   estimate of the population variance, it
				   has its own entry here. */
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

  int dependent_column; /* Column containing the dependent variable. Defaults to last column. */
  int refcnt;
};

typedef struct linreg_struct linreg;



linreg *linreg_alloc (const struct variable *, const struct variable **, 
		      double, size_t);

void linreg_unref (linreg *);
void linreg_ref (linreg *);

/*
  Fit the linear model via least squares. All pointers passed to pspp_linreg
  are assumed to be allocated to the correct size and initialized to the
  values as indicated by opts.
 */
void linreg_fit (const gsl_matrix *, linreg *);

double linreg_predict (const linreg *, const double *, size_t);
double linreg_residual (const linreg *, double, const double *, size_t);
const struct variable ** linreg_get_vars (const linreg *);

/*
  Mean of the independent variable.
 */
double linreg_get_indep_variable_mean (const linreg *, size_t);
void linreg_set_indep_variable_mean (linreg *, size_t, double);

double linreg_mse (const linreg *);

double linreg_intercept (const linreg *);

const gsl_matrix * linreg_cov (const linreg *);
double linreg_coeff (const linreg *, size_t);
const struct variable * linreg_indep_var (const linreg *, size_t);
size_t linreg_n_coeffs (const linreg *);
double linreg_n_obs (const linreg *);
double linreg_sse (const linreg *);
double linreg_ssreg (const linreg *);
double linreg_dfmodel (const linreg *);
double linreg_sst (const linreg *);
void linreg_set_depvar_mean (linreg *, double);
double linreg_get_depvar_mean (const linreg *);
#endif
