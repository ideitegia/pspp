/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

#ifndef LANGUAGE_DATA_IO_DATA_PARSER_H
#define LANGUAGE_DATA_IO_DATA_PARSER_H

/* Abstraction of a DATA LIST or GET DATA TYPE=TXT data parser. */

#include <stdbool.h>
#include <data/case.h>
#include <libpspp/str.h>

struct dataset;
struct dfm_reader;
struct dictionary;
struct file_handle;
struct fmt_spec;
struct substring;

/* Type of data read by a data parser. */
enum data_parser_type
  {
    DP_FIXED,                   /* Fields in fixed column positions. */
    DP_DELIMITED                /* Fields delimited by e.g. commas. */
  };

/* Creating and configuring any parser. */
struct data_parser *data_parser_create (void);
void data_parser_destroy (struct data_parser *);

enum data_parser_type data_parser_get_type (const struct data_parser *);
void data_parser_set_type (struct data_parser *, enum data_parser_type);

void data_parser_set_skip (struct data_parser *, int initial_records_to_skip);
void data_parser_set_case_limit (struct data_parser *, casenumber max_cases);
void data_parser_set_case_percent (struct data_parser *, int case_percent);

/* For configuring delimited parsers only. */
bool data_parser_get_span (const struct data_parser *);
void data_parser_set_span (struct data_parser *, bool may_cases_span_records);

void data_parser_set_empty_line_has_field (struct data_parser *,
                                           bool empty_line_has_field);
void data_parser_set_quotes (struct data_parser *, struct substring);
void data_parser_set_quote_escape (struct data_parser *, bool escape);
void data_parser_set_soft_delimiters (struct data_parser *, struct substring);
void data_parser_set_hard_delimiters (struct data_parser *, struct substring);

/* For configuring fixed parsers only. */
int data_parser_get_records (const struct data_parser *);
void data_parser_set_records (struct data_parser *, int records_per_case);

/* Field setup and parsing. */
void data_parser_add_delimited_field (struct data_parser *,
                                      const struct fmt_spec *, int fv,
                                      const char *name);
void data_parser_add_fixed_field (struct data_parser *,
                                  const struct fmt_spec *, int fv,
                                  const char *name,
                                  int record, int first_column);
bool data_parser_any_fields (const struct data_parser *);
bool data_parser_parse (struct data_parser *,
                        struct dfm_reader *, struct ccase *);

/* Uses for a configured parser. */
void data_parser_output_description (struct data_parser *,
                                     const struct file_handle *);
void data_parser_make_active_file (struct data_parser *, struct dataset *,
                                   struct dfm_reader *, struct dictionary *);

#endif /* language/data-io/data-parser.h */
