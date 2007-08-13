/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006, 2007 Free Software Foundation, Inc.

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

#ifndef LANGUAGE_DATA_IO_CASE_MAP_H
#define LANGUAGE_DATA_IO_CASE_MAP_H 1

/* Case map.

   A case map copies data from a case that corresponds for one
   dictionary to a case that corresponds to a second dictionary
   derived from the first by, optionally, deleting, reordering,
   or renaming variables.  (No new variables may be created.)
   */

struct dictionary;
struct ccase;

void case_map_prepare (struct dictionary *);
struct case_map *case_map_finish (struct dictionary *);
void case_map_execute (const struct case_map *,
                       const struct ccase *, struct ccase *);
void case_map_destroy (struct case_map *);

#endif /* language/data-io/case-map.h */
