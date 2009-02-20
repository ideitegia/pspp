/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009 Free Software Foundation, Inc.

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

#ifndef DATA_CASE_MATCHER_H
#define DATA_CASE_MATCHER_H 1

#include <stdbool.h>

struct ccase;
struct subcase;
union value;

struct case_matcher *case_matcher_create (void);
void case_matcher_add_input (struct case_matcher *, const struct subcase *,
                             struct ccase **, bool *is_minimal);
void case_matcher_destroy (struct case_matcher *);

bool case_matcher_match (struct case_matcher *, union value **by);

#endif /* data/case-matcher.h */
