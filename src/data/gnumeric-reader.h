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


struct spreadsheet *gnumeric_probe (const char *filename);

struct casereader * gnumeric_open_reader (const struct spreadsheet_read_info *, 
					  struct spreadsheet_read_options *,
					  struct dictionary **);


#endif
