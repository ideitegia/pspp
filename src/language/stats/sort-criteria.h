/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#ifndef SORT_CRITERIA_H
#define SORT_CRITERIA_H

#include <stdbool.h>
#include <stddef.h>

struct dictionary;
struct lexer;
struct variable;
struct subcase;

bool parse_sort_criteria (struct lexer *, const struct dictionary *,
                          struct subcase *, const struct variable ***vars,
                          bool *saw_direction);


#endif /* sort-criteria.h */
