/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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

#ifndef LANGUAGE_DATA_IO_PLACEMENT_PARSER_H
#define LANGUAGE_DATA_IO_PLACEMENT_PARSER_H 1

#include <stdbool.h>
#include <stddef.h>

struct fmt_spec;
struct pool;
struct lexer;

bool parse_record_placement (struct lexer *, int *record, int *column);
bool parse_var_placements (struct lexer *, struct pool *, size_t var_cnt, bool for_input,
                           struct fmt_spec **, size_t *format_cnt);
bool execute_placement_format (const struct fmt_spec *,
                               int *record, int *column);
bool parse_column_range (struct lexer *, int *first_column, int *last_column,
                         bool *range_specified);

#endif /* language/data-io/placement-parser.h */
