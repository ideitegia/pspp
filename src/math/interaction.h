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

#ifndef INTERACTION_H
#define INTERACTION_H
#include <data/case.h>

struct interaction_variable;
struct interaction_value;

struct interaction_variable * interaction_variable_create (const struct variable **, int);
void interaction_variable_destroy (struct interaction_variable *);
struct interaction_value * interaction_value_create (const struct interaction_variable *, const union value **);
void interaction_value_destroy (struct interaction_value *);
size_t interaction_variable_get_n_vars (const struct interaction_variable *);
double interaction_value_get_nonzero_entry (const struct interaction_value *);
const union value *interaction_value_get (const struct interaction_value *);
const struct variable * interaction_get_variable (const struct interaction_variable *);
size_t interaction_get_n_numeric (const struct interaction_variable *);
size_t interaction_get_n_alpha (const struct interaction_variable *);
size_t interaction_get_n_vars (const struct interaction_variable *);
const struct variable * interaction_get_member (const struct interaction_variable *, size_t);
bool is_interaction (const struct variable *, const struct interaction_variable **, size_t);
struct interaction_value *
interaction_case_data (const struct ccase *, const struct interaction_variable *);
double interaction_value_get_nonzero_entry (const struct interaction_value *);
#endif
