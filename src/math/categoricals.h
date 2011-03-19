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


#ifndef _CATEGORICALS__
#define _CATEGORICALS__

#include <stddef.h>
#include "data/missing-values.h"

struct categoricals;
struct variable;
struct ccase;

union value ;

typedef void update_func (void *user_data,
			  enum mv_class exclude,
			  const struct variable *wv, 
			  const struct variable *catvar,
			  const struct ccase *c,
			  void *aux1, void *aux2);

typedef void *user_data_create_func (void *aux1, void *aux2);

struct categoricals *categoricals_create (const struct variable *const *v, size_t n_vars,
					  const struct variable *wv, enum mv_class exclude,
					  user_data_create_func *udf,
					  update_func *update, void *aux1, void *aux2);

void categoricals_destroy (struct categoricals *);

void categoricals_update (struct categoricals *cat, const struct ccase *c);


/* Return the number of categories (distinct values) for variable N */
size_t categoricals_n_count (const struct categoricals *cat, size_t n);


/* Return the total number of categories */
size_t categoricals_total (const struct categoricals *cat);

/*
  Return the total number of variables which participated in these categoricals.
  Due to the possibility of missing values, this is NOT necessarily
  equal to the number of variables passed in when the object was
  created.
*/
size_t categoricals_get_n_variables (const struct categoricals *cat);

void categoricals_done (const struct categoricals *cat);

const struct variable * categoricals_get_variable_by_subscript (const struct categoricals *cat, int subscript);

const union value * categoricals_get_value_by_subscript (const struct categoricals *cat, int subscript);

double categoricals_get_weight_by_subscript (const struct categoricals *cat, int subscript);

double categoricals_get_sum_by_subscript (const struct categoricals *cat, int subscript);

double categoricals_get_binary_by_subscript (const struct categoricals *cat, int subscript,
					     const struct ccase *c);

void * categoricals_get_user_data_by_subscript (const struct categoricals *cat, int subscript);





#endif
