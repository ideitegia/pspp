/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010, 2011  Free Software Foundation, Inc.

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

#ifndef DATA_VECTOR_H
#define DATA_VECTOR_H 1

#include <stddef.h>
#include "data/variable.h"

struct dictionary;

struct vector *vector_create (const char *name,
                              struct variable **var, size_t var_cnt);
struct vector *vector_clone (const struct vector *old,
                             const struct dictionary *old_dict,
                             const struct dictionary *new_dict);
void vector_destroy (struct vector *);

const char *vector_get_name (const struct vector *);
enum val_type vector_get_type (const struct vector *);
struct variable *vector_get_var (const struct vector *, size_t idx);
size_t vector_get_var_cnt (const struct vector *);

bool vector_is_valid_name (const char *name, bool issue_error);

int compare_vector_ptrs_by_name (const void *a_, const void *b_);

#endif /* data/vector.h */
