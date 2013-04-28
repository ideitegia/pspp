/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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
struct interaction;

union value ;

struct categoricals *categoricals_create (struct interaction *const*, size_t n_int,
					  const struct variable *wv, enum mv_class dep_excl,
					  enum mv_class fctr_excl);

void categoricals_destroy (struct categoricals *);

void categoricals_update (struct categoricals *cat, const struct ccase *c);


/* Return the number of categories (distinct values) for variable N */
size_t categoricals_n_count (const struct categoricals *cat, size_t n);

size_t categoricals_df (const struct categoricals *cat, size_t n);

/* Return the total number of categories */
size_t categoricals_n_total (const struct categoricals *cat);

/* Return the total degrees of freedom */
size_t categoricals_df_total (const struct categoricals *cat);


/*
  Return the total number of variables which participated in these categoricals.
  Due to the possibility of missing values, this is NOT necessarily
  equal to the number of variables passed in when the object was
  created.
*/
size_t categoricals_get_n_variables (const struct categoricals *cat);

bool categoricals_is_complete (const struct categoricals *cat);


/*
  Must be called (once) before any call to the *_by_subscript or *_by_category
  functions, but AFTER any calls to categoricals_update.
  If this function returns false, then no calls to _by_subscript or *_by_category
  are allowed.
*/
void categoricals_done (const struct categoricals *cat);

bool categoricals_sane (const struct categoricals *cat);


/*
  The *_by_subscript functions use the short map.
  Their intended use is by covariance matrix routines, where normally 1 less than 
  the total number of distinct values of each categorical variable should
  be considered.
 */
double categoricals_get_weight_by_subscript (const struct categoricals *cat, int subscript);
const struct interaction *categoricals_get_interaction_by_subscript (const struct categoricals *cat, int subscript);

double categoricals_get_sum_by_subscript (const struct categoricals *cat, int subscript);

/* Returns unity if the value in case C at SUBSCRIPT is equal to the category
   for that subscript */
double
categoricals_get_dummy_code_for_case (const struct categoricals *cat, int subscript,
				     const struct ccase *c);

/* Returns unity if the value in case C at SUBSCRIPT is equal to the category
   for that subscript. 
   Else if it is the last category, return -1.
   Otherwise return 0.
 */
double
categoricals_get_effects_code_for_case (const struct categoricals *cat, int subscript,
					const struct ccase *c);


/* These use the long map.  Useful for descriptive statistics. */


const struct ccase *
categoricals_get_case_by_category_real (const struct categoricals *cat, int iact, int n);

void *
categoricals_get_user_data_by_category_real (const struct categoricals *cat, int iact, int n);


void * categoricals_get_user_data_by_category (const struct categoricals *cat, int category);

const struct ccase * categoricals_get_case_by_category (const struct categoricals *cat, int subscript);


struct payload
{
  void* (*create)  (const void *aux1, void *aux2);
  void (*update)  (const void *aux1, void *aux2, void *user_data, const struct ccase *, double weight);
  void (*calculate) (const void *aux1, void *aux2, void *user_data);
  void (*destroy) (const void *aux1, void *aux2, void *user_data);
};


void  categoricals_set_payload (struct categoricals *cats, const struct payload *p, const void *aux1, void *aux2);


#endif
