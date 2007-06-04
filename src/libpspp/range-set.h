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

/* Bitmap, implemented as a balanced binary tree.

   Each operation has O(lg N) cost, where N is the number of
   contiguous regions of 1-bits in the bitmap.  Also, a cache
   reduces the second and subsequent containment tests within a
   single contiguous region to O(1). */

#ifndef LIBPSPP_RANGE_SET_H
#define LIBPSPP_RANGE_SET_H

#include <stdbool.h>

struct pool;

struct range_set *range_set_create (void);
struct range_set *range_set_create_pool (struct pool *);
struct range_set *range_set_clone (const struct range_set *, struct pool *);
void range_set_destroy (struct range_set *);

void range_set_insert (struct range_set *,
                       unsigned long int start, unsigned long int width);
void range_set_delete (struct range_set *,
                       unsigned long int start, unsigned long int width);
bool range_set_allocate (struct range_set *, unsigned long int request,
                         unsigned long int *start, unsigned long int *width);
bool range_set_contains (const struct range_set *, unsigned long int position);

bool range_set_is_empty (const struct range_set *);

const struct range_set_node *range_set_first (const struct range_set *);
const struct range_set_node *range_set_next (const struct range_set *,
                                             const struct range_set_node *);
unsigned long int range_set_node_get_start (const struct range_set_node *);
unsigned long int range_set_node_get_end (const struct range_set_node *);
unsigned long int range_set_node_get_width (const struct range_set_node *);

#endif /* libpspp/range-set.h */
