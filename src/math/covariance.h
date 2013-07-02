/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

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


#ifndef COVARIANCE_H
#define COVARIANCE_H

#include <gsl/gsl_matrix.h>
#include <stddef.h>
#include "data/missing-values.h"

struct covariance;
struct variable;
struct ccase ;
struct categoricals;

struct covariance * covariance_1pass_create (size_t n_vars, const struct variable *const *vars, 
					     const struct variable *wv, enum mv_class excl);

struct covariance *
covariance_2pass_create (size_t n_vars, const struct variable *const *vars,
			 struct categoricals *cats,
			 const struct variable *wv, enum mv_class excl);

void covariance_accumulate (struct covariance *, const struct ccase *);
void covariance_accumulate_pass1 (struct covariance *, const struct ccase *);
void covariance_accumulate_pass2 (struct covariance *, const struct ccase *);

gsl_matrix * covariance_calculate (struct covariance *);
const gsl_matrix * covariance_calculate_unnormalized (struct covariance *);

void covariance_destroy (struct covariance *cov);

const gsl_matrix *covariance_moments (const struct covariance *cov, int m);

const struct categoricals * covariance_get_categoricals (const struct covariance *cov);
size_t covariance_dim (const struct covariance * cov);

struct tab_table ;
void
covariance_dump_enc (const struct covariance *cov, const struct ccase *c,
		     struct tab_table *t);

struct tab_table *
covariance_dump_enc_header (const struct covariance *cov, int length);



#endif
