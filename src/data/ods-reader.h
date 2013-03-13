/* PSPP - a program for statistical analysis.
   Copyright (C) 2011 Free Software Foundation, Inc.

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

#ifndef ODS_READ_H
#define ODS_READ_H 1

struct casereader;
struct dictionary;

struct spreadsheet_read_options;
struct spreadsheet;

const char * ods_get_sheet_name (struct spreadsheet *s, int n);
char * ods_get_sheet_range (struct spreadsheet *s, int n);

struct spreadsheet *ods_probe (const char *filename, bool report_errors);

struct casereader * ods_make_reader (struct spreadsheet *spreadsheet, 
				     const struct spreadsheet_read_options *opts);

void ods_destroy (struct spreadsheet *s);


#endif
