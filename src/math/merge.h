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

#ifndef MATH_MERGE_H
#define MATH_MERGE_H 1

#include <stdbool.h>

struct case_ordering;
struct casereader;

struct merge *merge_create (const struct case_ordering *);
void merge_destroy (struct merge *);
void merge_append (struct merge *, struct casereader *);
struct casereader *merge_make_reader (struct merge *);

#endif /* math/merge.h */
