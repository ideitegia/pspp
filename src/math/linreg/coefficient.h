/* lib/linreg/coefficient.c

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


#ifndef COEFFICIENT_H
#define COEFFICIENT_H

#include <assert.h>
#include <math/linreg/linreg.h>
#include <src/data/variable.h>
#include <src/data/value.h>

struct design_matrix;

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



/*
  Accessor functions for matching coefficients and variables.
 */

void pspp_linreg_coeff_free (struct pspp_linreg_coeff *);

/*
  Initialize the variable and value pointers inside the
  coefficient structures for the linear model.
 */
void
pspp_linreg_coeff_init (pspp_linreg_cache *, 
			struct design_matrix *);


void
pspp_linreg_coeff_set_estimate (struct pspp_linreg_coeff *,
				double estimate);

void
pspp_linreg_coeff_set_std_err (struct pspp_linreg_coeff *,
			       double std_err);
/*
  Return the estimated value of the coefficient.
 */
double
pspp_linreg_coeff_get_est (const struct pspp_linreg_coeff *);

/*
  Return the standard error of the estimated coefficient.
*/
double 
pspp_linreg_coeff_get_std_err (const struct pspp_linreg_coeff *);

/*
  How many variables are associated with this coefficient?
 */
int
pspp_linreg_coeff_get_n_vars (struct pspp_linreg_coeff *);

/*
  Which variable does this coefficient match?
 */
const struct variable *
pspp_linreg_coeff_get_var (struct pspp_linreg_coeff *, int );

/* 
   Which value is associated with this coefficient/variable comination? 
*/
const union value *
pspp_linreg_coeff_get_value (struct pspp_linreg_coeff *,
			     const struct variable *);

const struct pspp_linreg_coeff *
pspp_linreg_get_coeff (const pspp_linreg_cache *,
		       const struct variable *,
		       const union value *);
#endif
