/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Software Foundation, Inc.

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

#ifndef CSV_FILE_WRITER_H
#define CSV_FILE_WRITER_H 1

#include <stdbool.h>

/* Writing comma-separated value (CSV) files. */

/* Options for creating CSV files. */
struct csv_writer_options
  {
    bool recode_user_missing;   /* Recode user-missing to system-missing? */
    bool include_var_names;     /* Add header row with variable names? */
    bool use_value_labels;      /* Write value labels where available? */
    bool use_print_formats;     /* Honor variables' print formats? */
    char decimal;               /* Decimal point character. */
    char delimiter;             /* Field separator. */
    char qualifier;             /* Quote character. */
  };

void csv_writer_options_init (struct csv_writer_options *);

struct file_handle;
struct dictionary;
struct casewriter *csv_writer_open (struct file_handle *,
                                    const struct dictionary *,
                                    const struct csv_writer_options *);

#endif /* csv-file-writer.h */
