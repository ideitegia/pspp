/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2007 Free Software Foundation, Inc.

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

#ifndef DATA_CASE_H
#define DATA_CASE_H 1

#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include "value.h"

struct variable;

/* A count of cases or the index of a case within a collection of
   them. */
#define CASENUMBER_MAX LONG_MAX
typedef long int casenumber;

/* Opaque structure that represents a case.  Use accessor
   functions instead of accessing any members directly.  Use
   case_move() or case_clone() instead of copying.  */
struct ccase
  {
    struct case_data *case_data;        /* Actual data. */
  };

void case_nullify (struct ccase *);
bool case_is_null (const struct ccase *);

void case_create (struct ccase *, size_t value_cnt);
void case_clone (struct ccase *, const struct ccase *);
void case_move (struct ccase *, struct ccase *);
void case_destroy (struct ccase *);

size_t case_get_value_cnt (const struct ccase *);

void case_resize (struct ccase *, size_t new_cnt);
void case_swap (struct ccase *, struct ccase *);

bool case_try_create (struct ccase *, size_t value_cnt);
bool case_try_clone (struct ccase *, const struct ccase *);

void case_copy (struct ccase *dst, size_t dst_idx,
                const struct ccase *src, size_t src_idx,
                size_t cnt);

void case_copy_out (const struct ccase *,
                       size_t start_idx, union value *, size_t value_cnt);
void case_copy_in (struct ccase *,
                       size_t start_idx, const union value *, size_t value_cnt);

const union value *case_data (const struct ccase *, const struct variable *);
double case_num (const struct ccase *, const struct variable *);
const char *case_str (const struct ccase *, const struct variable *);
union value *case_data_rw (struct ccase *, const struct variable *);

const union value *case_data_idx (const struct ccase *, size_t idx);
double case_num_idx (const struct ccase *, size_t idx);
const char *case_str_idx (const struct ccase *, size_t idx);
union value *case_data_rw_idx (struct ccase *, size_t idx);

int case_compare (const struct ccase *, const struct ccase *,
                  const struct variable *const *, size_t var_cnt);
int case_compare_2dict (const struct ccase *, const struct ccase *,
                        const struct variable *const *,
			const struct variable *const *,
                        size_t var_cnt);

const union value *case_data_all (const struct ccase *);
union value *case_data_all_rw (struct ccase *);

#endif /* data/case.h */
