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

/* Sparse array of cases.

   Implements a 2-d sparse array in which each row represents a
   case, each column represents a variable, and each intersection
   contains a `union value'.  Data in the array may be accessed
   randomly by column and row.  When the number of cases stored
   in the array is small, the data is stored in memory in memory;
   when it is large, the data is stored in a temporary file.

   The sparse_cases_write_columns function provides a somewhat
   unusual ability: to write a given value to every row in a
   column or set of columns.  This overwrites any values
   previously written into those columns.  For rows that have
   never been written, this function sets "default" values that
   later writes can override.

   The array keeps track of which row have been written.  If
   sparse_cases_write_columns has been used, reading from a row
   that has never been written yields the default values;
   otherwise, reading from such a row in an error.  It is
   permissible to write to only some columns in a row and leave
   the rest of the row's data undefined (or, if
   sparse_cases_write_columns has been used, at the default
   values).  The array does not keep track of which columns in a
   row have never been written, but reading values that have
   never been written or set as defaults yields undefined
   behavior. */

#ifndef DATA_SPARSE_CASES_H
#define DATA_SPARSE_CASES_H 1

#include <stddef.h>
#include <stdbool.h>
#include <data/case.h>

struct sparse_cases *sparse_cases_create (size_t value_cnt);
struct sparse_cases *sparse_cases_clone (const struct sparse_cases *);
void sparse_cases_destroy (struct sparse_cases *);

size_t sparse_cases_get_value_cnt (const struct sparse_cases *);

bool sparse_cases_contains_row (const struct sparse_cases *, casenumber row);
bool sparse_cases_read (struct sparse_cases *, casenumber row, size_t column,
                        union value[], size_t value_cnt);
bool sparse_cases_write (struct sparse_cases *, casenumber row, size_t column,
                         const union value[], size_t value_cnt);
bool sparse_cases_write_columns (struct sparse_cases *, size_t start_column,
                                 const union value[], size_t value_cnt);

#endif /* data/sparse-cases.h */
