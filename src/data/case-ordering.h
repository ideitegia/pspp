/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

/* Sort order for comparing cases. */

#ifndef DATA_CASE_ORDERING_H
#define DATA_CASE_ORDERING_H 1

#include <stddef.h>
#include <data/case.h>

struct dictionary;

/* Sort direction. */
enum sort_direction
  {
    SRT_ASCEND,			/* A, B, C, ..., X, Y, Z. */
    SRT_DESCEND			/* Z, Y, X, ..., C, B, A. */
  };

/* Creation and destruction. */
struct case_ordering *case_ordering_create (void);
struct case_ordering *case_ordering_clone (const struct case_ordering *);
void case_ordering_destroy (struct case_ordering *);

/* Modification. */
bool case_ordering_add_var (struct case_ordering *,
                            const struct variable *, enum sort_direction);

/* Comparing cases. */
int case_ordering_compare_cases (const struct ccase *, const struct ccase *,
                                 const struct case_ordering *);

/* Inspection. */
size_t case_ordering_get_value_cnt (const struct case_ordering *);
size_t case_ordering_get_var_cnt (const struct case_ordering *);
const struct variable *case_ordering_get_var (const struct case_ordering *,
                                              size_t);
enum sort_direction case_ordering_get_direction (const struct case_ordering *,
                                                 size_t);
void case_ordering_get_vars (const struct case_ordering *,
                             const struct variable ***, size_t *);

#endif /* data/case-ordering.h */