/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2012 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Case map.

   A case map copies data from a case that corresponds to one
   dictionary to a case that corresponds to a second dictionary.
   A few options are available for ways to create the mapping. */

#ifndef DATA_CASE_MAP_H
#define DATA_CASE_MAP_H 1

#include <stddef.h>

struct case_map;
struct casereader;
struct casewriter;
struct ccase;
struct dictionary;

struct case_map *case_map_create (void);
void case_map_destroy (struct case_map *);
struct ccase *case_map_execute (const struct case_map *, struct ccase *);

const struct caseproto *case_map_get_proto (const struct case_map *);

struct casereader *case_map_create_input_translator (struct case_map *,
                                                    struct casereader *);
struct casewriter *case_map_create_output_translator (struct case_map *,
                                                      struct casewriter *);

/* For mapping cases for one version of a dictionary to those in
   a modified version of the same dictionary. */
struct case_map_stage *case_map_stage_create (const struct dictionary *);
void case_map_stage_destroy (struct case_map_stage *);
struct case_map *case_map_stage_get_case_map (const struct case_map_stage *);

/* For eliminating "holes" in a case. */
struct case_map *case_map_to_compact_dict (const struct dictionary *d,
                                           unsigned int exclude_classes);

/* For mapping cases for one dictionary to another based on
   variable names within the dictionary. */
struct case_map *case_map_by_name (const struct dictionary *old,
                                   const struct dictionary *new);

void case_map_dump (const struct case_map *);

#endif /* data/case-map.h */
