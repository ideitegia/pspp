/* PSPP - a program for statistical analysis.
   Copyright (C) 2011 Free Software Foundation, Inc.

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


#ifndef _INTERACTION_H__
#define _INTERACTION_H__ 1

#include <stdbool.h>
#include "data/missing-values.h"

struct interaction;
struct variable;
struct string;

#include <stddef.h>
struct interaction
{
  size_t n_vars;
  const struct variable **vars;
};



struct interaction * interaction_create (const struct variable *);
void interaction_destroy (struct interaction *);
void interaction_add_variable (struct interaction *, const struct variable *);
void interaction_dump (const struct interaction *);
void interaction_to_string (const struct interaction *iact, struct string *str);


union value;

unsigned int interaction_value_hash (const struct interaction *, const union value *);
bool interaction_value_equal (const struct interaction *, const union value *, const union value *);
bool interaction_value_is_missing (const struct interaction *, const union value *, enum mv_class);

#endif
