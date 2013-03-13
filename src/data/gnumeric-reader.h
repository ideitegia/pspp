/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2010 Free Software Foundation, Inc.

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

#ifndef GNUMERIC_READ_H
#define GNUMERIC_READ_H 1

#include <stdbool.h>

struct casereader;
struct dictionary;
struct spreadsheet_read_info;
struct spreadsheet_read_options;

struct spreadsheet *gnumeric_probe (const char *filename, bool report_errors);

const char * gnumeric_get_sheet_name (struct spreadsheet *s, int n);
char * gnumeric_get_sheet_range (struct spreadsheet *s, int n);

struct casereader * gnumeric_make_reader (struct spreadsheet *spreadsheet,
					  const struct spreadsheet_read_options *opts);

void gnumeric_destroy (struct spreadsheet *r);


#endif
