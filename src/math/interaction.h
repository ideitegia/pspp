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
struct interaction * interaction_clone (const struct interaction *);
void interaction_destroy (struct interaction *);
void interaction_add_variable (struct interaction *, const struct variable *);
void interaction_dump (const struct interaction *);
void interaction_to_string (const struct interaction *iact, struct string *str);
bool interaction_is_proper_subset (const struct interaction *x, const struct interaction *y);
bool interaction_is_subset (const struct interaction *x, const struct interaction *y);


struct ccase;
unsigned int interaction_case_hash (const struct interaction *, const struct ccase *, unsigned int base);
bool interaction_case_equal (const struct interaction *, const struct ccase *, const struct ccase *);
bool interaction_case_is_missing (const struct interaction *, const struct ccase *, enum mv_class);
int interaction_case_cmp_3way (const struct interaction *, const struct ccase *, const struct ccase *);


#endif
