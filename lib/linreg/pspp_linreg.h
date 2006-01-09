/* lib/linreg/pspp_linreg.h

   Copyright (C) 2005 Free Software Foundation, Inc.
   Written by Jason H Stover.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02111-1307, USA.
 */

/*
  Find the least-squares estimate of b for the linear model:

  Y = Xb + Z

  where Y is an n-by-1 column vector, X is an n-by-p matrix of 
  independent variables, b is a p-by-1 vector of regression coefficients,
  and Z is an n-by-1 normally-distributed random vector with independent
  identically distributed components with mean 0.

  This estimate is found via the sweep operator or singular-value
  decomposition.


  References:

  Matrix Computations, third edition. GH Golub and CF Van Loan.
  The Johns Hopkins University Press. 1996. ISBN 0-8018-5414-8.

  Numerical Analysis for Statisticians. K Lange. Springer. 1999.
  ISBN 0-387-94979-8.

  Numerical Linear Algebra for Applications in Statistics. JE Gentle.
  Springer. 1998. ISBN 0-387-98542-5.
 */
#ifndef PSPP_LINREG_H
#define PSPP_LINREG_H 1
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_fit.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_cblas.h>
#include <src/design-matrix.h>
#include <src/var.h>
#define PSPP_LINREG_VAL_NOT_FOUND -1
enum
{
  PSPP_LINREG_SWEEP,
  PSPP_LINREG_SVD
};

/*
  Cache for the relevant data from the model. There are several
  members which the caller might not use, and which could use a lot of
  storage. Therefore non-essential members of the struct will be
  allocated only when requested.
 */
struct pspp_linreg_coeff
{
  double estimate; /* Estimated coefficient. */
  double std_err; /* Standard error of the estimate. */
  struct varinfo *v_info;  /* Information pertaining to the
			      variable(s) associated with this
			      coefficient.  The calling function
			      should initialize this value with the
			      functions in coefficient.c.  The
			      estimation procedure ignores this
			      member. It is here so the caller can
			      match parameters with relevant variables
			      and values. If the coefficient is
			      associated with an interaction, then
			      v_info contains information for multiple
			      variables.
			   */
  int n_vars; /* Number of variables associated with this coefficient.
	         Coefficients corresponding to interaction terms will
		 have more than one variable.
	      */
};
struct pspp_linreg_cache_struct
{
  int n_obs;			/* Number of observations. */
  int n_indeps;			/* Number of independent variables. */
  int n_coeffs;

  /* 
     The variable struct is ignored during estimation.
     It is here so the calling procedure can
     find the variable used in the model.
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
				   independent variable. 
				 */
  double sst;			/* Sum of squares total. */
  double sse;			/* Sum of squares error. */
  double mse;			/* Mean squared error. This is just sse / dfe, but
				   since it is the best unbiased estimate of the population
				   variance, it has its own entry here.
				 */
  gsl_vector *ssx;		/* Centered sums of squares for independent variables,
				   i.e. \sum (x[i] - mean(x))^2. 
				 */
  double ssy;			/* Centered sums of squares for dependent variable. */
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
};
typedef struct pspp_linreg_cache_struct pspp_linreg_cache;

/*
  Options describing what special values should be computed.
 */
struct pspp_linreg_opts_struct
{
  int resid;			/* Should the residuals be returned? */

  int get_depvar_mean_std;
  int *get_indep_mean_std;	/* Array of booleans dictating which
				   independent variables need their means
				   and standard deviations computed within
				   pspp_linreg. This array MUST be of
				   length n_indeps. If element i is 1,
				   pspp_linreg will compute the mean and
				   variance of indpendent variable i. If
				   element i is 0, it will not compute the
				   mean and standard deviation, and assume
				   the values are stored.
				   cache->indep_mean[i] is the mean and
				   cache->indep_std[i] is the sample
				   standard deviation.
				 */
};
typedef struct pspp_linreg_opts_struct pspp_linreg_opts;

int pspp_reg_sweep (gsl_matrix * A);

pspp_linreg_cache *pspp_linreg_cache_alloc (size_t n, size_t p);

void pspp_linreg_cache_free (pspp_linreg_cache * cache);

int pspp_linreg (const gsl_vector * Y, const gsl_matrix * X,
		 const pspp_linreg_opts * opts, pspp_linreg_cache * cache);
void pspp_linreg_coeff_init (pspp_linreg_cache *, struct design_matrix *);

void pspp_linreg_coeff_free (struct pspp_linreg_coeff *);

void pspp_linreg_coeff_set_estimate (struct pspp_linreg_coeff *, double);

void pspp_linreg_coeff_set_std_err (struct pspp_linreg_coeff *, double);

int pspp_linreg_coeff_get_n_vars (struct pspp_linreg_coeff *);

const struct variable *pspp_linreg_coeff_get_var (struct pspp_linreg_coeff *, int);

const union value *pspp_linreg_coeff_get_value (struct pspp_linreg_coeff *, const struct variable *);
#endif
