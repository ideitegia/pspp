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

struct casereeader;

/* Default width of string variables. */
#define SPREADSHEET_DEFAULT_WIDTH 8

/* These elements are read/write.
   They may be passed in NULL (for pointers) or negative for integers, in which
   case they will be filled in be the function.
*/
struct spreadsheet_read_options
{
  char *sheet_name ;       /* The name of the sheet to open (in UTF-8) */
  int sheet_index ;        /* The index of the sheet to open (only used if sheet_name is NULL) */
  char *cell_range ;       /* The cell range (in UTF-8) */
  bool read_names ;        /* True if the first row is to be used as the names of the variables */
  int asw ;                /* The width of string variables in the created dictionary */
};

int ps26_to_int (const char *str);
char * int_to_ps26 (int);

bool convert_cell_ref (const char *ref,
		       int *col0, int *row0,
		       int *coli, int *rowi);


#define _xml(X) (CHAR_CAST (const xmlChar *, (X)))

#define _xmlchar_to_int(X) ((X) ? atoi (CHAR_CAST (const char *, (X))) : -1)

enum spreadsheet_type
  {
    SPREADSHEET_NONE,
    SPREADSHEET_GNUMERIC,
    SPREADSHEET_ODS
  };


struct spreadsheet
{
  const char *file_name;

  enum spreadsheet_type type;

  /* The total number of sheets in the "workbook" */
  int n_sheets;

  /* The dictionary */
  struct dictionary *dict;
};


struct casereader * spreadsheet_make_reader (struct spreadsheet *, const struct spreadsheet_read_options *);

const char * spreadsheet_get_sheet_name (struct spreadsheet *s, int n);
char * spreadsheet_get_sheet_range (struct spreadsheet *s, int n);


char * create_cell_ref (int col0, int row0);
char *create_cell_range (int col0, int row0, int coli, int rowi);

void spreadsheet_destroy (struct spreadsheet *);





#define SPREADSHEET_CAST(X) ((struct spreadsheet *)(X))

#endif
