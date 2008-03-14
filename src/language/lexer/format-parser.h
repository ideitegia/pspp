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

#ifndef LANGUAGE_LEXER_FORMAT_PARSER_H
#define LANGUAGE_LEXER_FORMAT_PARSER_H 1

#include <stdbool.h>

struct lexer;

bool parse_abstract_format_specifier (struct lexer *, char *type,
                                      int *width, int *decimals);

enum fmt_type ;
struct fmt_spec;
bool parse_format_specifier (struct lexer *, struct fmt_spec *);
bool parse_format_specifier_name (struct lexer *, enum fmt_type *type);

#endif /* language/lexer/format-parser.h. */
