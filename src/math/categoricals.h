/* PSPP - a program for statistical analysis.
   Copyright (C) 2009 Free Software Foundation, Inc.

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

struct categoricals;
struct variable;
struct ccase;

union value ;

struct categoricals *categoricals_create (const struct variable **v, size_t n_vars,
					  const struct variable *wv);

void categoricals_destroy (struct categoricals *);

void categoricals_update (struct categoricals *cat, const struct ccase *c);


/* Return the number of categories (distinct values) for variable N */
size_t categoricals_n_count (const struct categoricals *cat, size_t n);


/* Return the total number of categories */
size_t categoricals_total (const struct categoricals *cat);

/* Return the index for variable N */
int categoricals_index (const struct categoricals *cat, size_t n, const union value *val);

void categoricals_done (struct categoricals *cat);

const struct variable * categoricals_get_variable_by_subscript (const struct categoricals *cat, int subscript);

const union value * categoricals_get_value_by_subscript (const struct categoricals *cat, int subscript);

double categoricals_get_binary_by_subscript (const struct categoricals *cat, int subscript,
					     const struct ccase *c);


#endif
