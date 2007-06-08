/* PSPP - computes sample statistics.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

/* Sparse array data structure.

   Implements a dictionary that associates a "unsigned long int"
   key with fixed-size values (elements).

   The implementation allocates elements in groups of moderate
   size, so it achieves maximum space efficiency when elements
   are clustered into groups of consecutive keys.  For the same
   reason, elements should be kept relatively small, perhaps a
   few pointer elements in size.

   The implementation is slightly more efficient both in time and
   space when indexes are kept small.  Thus, for example, if the
   indexes in use start from some fixed base value, consider
   using the offset from that base as the index value. */

#ifndef LIBPSPP_SPARSE_ARRAY_H
#define LIBPSPP_SPARSE_ARRAY_H 1

#include <stddef.h>
#include <stdbool.h>

#include <libpspp/hash.h>

struct sparse_array *sparse_array_create (size_t elem_size);
struct sparse_array *sparse_array_create_pool (struct pool *,
                                               size_t elem_size);
void sparse_array_destroy (struct sparse_array *);

unsigned long int sparse_array_count (const struct sparse_array *);

void *sparse_array_insert (struct sparse_array *, unsigned long int key);
void *sparse_array_get (const struct sparse_array *, unsigned long int key);
bool sparse_array_remove (struct sparse_array *, unsigned long int key);

void *sparse_array_scan (const struct sparse_array *,
                         unsigned long int *skip,
                         unsigned long int *key);

#endif /* libpspp/sparse-array.h */
