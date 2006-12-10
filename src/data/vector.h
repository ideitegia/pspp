/* PSPP - computes sample statistics.
   Copyright (C) 2006  Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#ifndef DATA_VECTOR_H 
#define DATA_VECTOR_H 1

#include <stddef.h>
#include <data/variable.h>

struct dictionary;

struct vector *vector_create (const char *name,
                              struct variable **var, size_t var_cnt);
struct vector *vector_clone (const struct vector *old,
                             const struct dictionary *old_dict,
                             const struct dictionary *new_dict);
void vector_destroy (struct vector *);

const char *vector_get_name (const struct vector *);
enum var_type vector_get_type (const struct vector *);
struct variable *vector_get_var (const struct vector *, size_t idx);
size_t vector_get_var_cnt (const struct vector *);

int compare_vector_ptrs_by_name (const void *a_, const void *b_);

#endif /* data/vector.h */
