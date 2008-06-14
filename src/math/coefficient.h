/* PSPP - a program for statistical analysis.
   Copyright (C) 2005 Free Software Foundation, Inc.

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


#ifndef COEFFICIENT_H
#define COEFFICIENT_H

#include <assert.h>
#include <src/data/variable.h>
#include <src/data/value.h>

/*
  This file contains definitions of data structures for storing
  coefficients of a statistical model. The coefficients are the point
  in the model where the theoretical aspects of the model meet the
  data. As such, the coefficients are the interface where users need
  to match variable names and values with any information about the
  model itself. This file and coefficient.c provide this interface
  between data and model structures.
 */

struct design_matrix;

/*
  Cache for the relevant data from the model. There are several
  members which the caller might not use, and which could use a lot of
  storage. Therefore non-essential members of the struct will be
  allocated only when requested.
 */
struct pspp_coeff
{
  double estimate;		/* Estimated coefficient. */
  double std_err;		/* Standard error of the estimate. */
  struct varinfo *v_info;	/* Information pertaining to the variable(s)
				   associated with this coefficient.  The
				   calling function should initialize this
				   value with the functions in coefficient.c.
				   The estimation procedure ignores this
				   member. It is here so the caller can match
				   parameters with relevant variables and
				   values. If the coefficient is associated
				   with an interaction, then v_info contains
				   information for multiple variables. */
  int n_vars;			/* Number of variables associated with this
				   coefficient. Coefficients corresponding to
				   interaction terms will have more than one
				   variable. */
};
typedef struct pspp_coeff coefficient;

void pspp_coeff_free (struct pspp_coeff *);

/*
  Initialize the variable and value pointers inside the
  coefficient structures for the linear model.
 */
void pspp_coeff_init (struct pspp_coeff **, const struct design_matrix *);


void
pspp_coeff_set_estimate (struct pspp_coeff *, double estimate);

void
pspp_coeff_set_std_err (struct pspp_coeff *, double std_err);

/*
  Accessor functions for matching coefficients and variables.
 */

/*
  Return the estimated value of the coefficient.
 */
double pspp_coeff_get_est (const struct pspp_coeff *);

/*
  Return the standard error of the estimated coefficient.
*/
double pspp_coeff_get_std_err (const struct pspp_coeff *);

/*
  How many variables are associated with this coefficient?
 */
int pspp_coeff_get_n_vars (struct pspp_coeff *);

/*
  Which variable does this coefficient match? The int argument is usually
  0, unless the coefficient refers to an interaction.
 */
const struct variable *pspp_coeff_get_var (struct pspp_coeff *,
						  int);
/*
  Which value is associated with this coefficient/variable comination?
 */
const union value *pspp_coeff_get_value (struct pspp_coeff *,
						const struct variable *);
#endif
