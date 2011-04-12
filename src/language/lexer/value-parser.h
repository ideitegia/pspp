/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2009, 2011 Free Software Foundation, Inc.

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

#ifndef VALUE_PARSER_H
#define VALUE_PARSER_H 1

#include <stdbool.h>

struct lexer;
enum fmt_type;
struct variable;
union value;
bool parse_num_range (struct lexer *,
                      double *x, double *y, const enum fmt_type *fmt);
bool parse_value (struct lexer *, union value *, const struct variable *);

#endif /* value-parser.h */
