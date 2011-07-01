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

#ifndef SPREADSHEET_READ_H
#define SPREADSHEET_READ_H 1

#include <stdbool.h>

/* Default width of string variables. */
#define SPREADSHEET_DEFAULT_WIDTH 8

struct spreadsheet_read_info
{
  char *sheet_name ;            /* In UTF-8. */
  char *file_name ;             /* In filename encoding. */
  char *cell_range ;            /* In UTF-8. */
  int sheet_index ;
  bool read_names ;
  int asw ;
};

int pseudo_base26 (const char *str);

bool convert_cell_ref (const char *ref,
		       int *col0, int *row0,
		       int *coli, int *rowi);


#define _xml(X) (CHAR_CAST (const xmlChar *, X))

#define _xmlchar_to_int(X) (atoi(CHAR_CAST (const char *, X)))


#endif
