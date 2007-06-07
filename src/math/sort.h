/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

#ifndef MATH_SORT_H
#define MATH_SORT_H 1

#include <stddef.h>
#include <stdbool.h>

struct case_ordering;

extern int min_buffers ;
extern int max_buffers ;

struct casewriter *sort_create_writer (struct case_ordering *);
struct casereader *sort_execute (struct casereader *, struct case_ordering *);

#endif /* math/sort.h */
