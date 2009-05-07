/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009 Free Software Foundation, Inc.

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

/* Sparse 2-d array.

   Implements a sparse array, in particular a sparse array of
   byte arrays.  Each row is either present or absent, and each
   row that is present consists of a fixed number of bytes
   (columns).  Data in the array may be accessed randomly by
   column and row.  When the number of rows stored in the array
   is small, the data is stored in memory; when it is large, the
   data is stored in a temporary file.

   The sparse_xarray_write_columns function provides a somewhat
   unusual ability: to write a given value to every row in a
   column or set of columns.  This overwrites any values
   previously written into those columns.  For rows that have
   never been written, this function sets "default" values that
   later writes can override.  The default values are initially
   all zero bytes.

   The array keeps track of which row have been written.  Reading
   from a row that has never been written yields the default
   values.  It is permissible to write to only some columns in a
   row and leave the rest of the row's data at the default
   values. */

#ifndef LIBPSPP_SPARSE_XARRAY_H
#define LIBPSPP_SPARSE_XARRAY_H 1

#include <stddef.h>
#include <stdbool.h>

struct sparse_xarray *sparse_xarray_create (size_t n_columns,
                                            size_t max_memory_rows);
struct sparse_xarray *sparse_xarray_clone (const struct sparse_xarray *);
void sparse_xarray_destroy (struct sparse_xarray *);

size_t sparse_xarray_get_n_columns (const struct sparse_xarray *);
size_t sparse_xarray_get_n_rows (const struct sparse_xarray *);

bool sparse_xarray_contains_row (const struct sparse_xarray *,
                                 unsigned long int row);
bool sparse_xarray_read (const struct sparse_xarray *, unsigned long int row,
                         size_t start, size_t n, void *);
bool sparse_xarray_write (struct sparse_xarray *, unsigned long int row,
                          size_t start, size_t n, const void *);
bool sparse_xarray_write_columns (struct sparse_xarray *, size_t start,
                                  size_t n, const void *);
bool sparse_xarray_copy (const struct sparse_xarray *src,
                         struct sparse_xarray *dst,
                         bool (*cb) (const void *src, void *dst, void *aux),
                         void *aux);

/* For testing purposes only. */
unsigned int sparse_xarray_model_checker_hash (const struct sparse_xarray *,
                                               unsigned int basis);

#endif /* libpspp/sparse-libpspp.h */
