/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009 Free Software Foundation, Inc.

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

#ifndef MATH_SORT_H
#define MATH_SORT_H 1

struct subcase;
struct caseproto;
struct variable;

extern int min_buffers ;
extern int max_buffers ;

struct casewriter *sort_create_writer (const struct subcase *,
                                       const struct caseproto *);
struct casereader *sort_execute (struct casereader *, const struct subcase *);
struct casereader *sort_execute_1var (struct casereader *,
                                      const struct variable *);

#endif /* math/sort.h */
