/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009 Free Software Foundation, Inc.

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

#ifndef DATA_DATASHEET_TEST_H
#define DATA_DATASHEET_TEST_H 1

#if 0
#include <data/case.h>
#include <data/value.h>

struct casereader;

/* A datasheet is a 2-d array of data that may be stored in
   memory or on disk.  It efficiently supports data storage and
   retrieval, as well as adding, removing, and rearranging both
   rows and columns.  */

struct datasheet *datasheet_create (struct casereader *);
void datasheet_destroy (struct datasheet *);
struct datasheet *datasheet_rename (struct datasheet *);

bool datasheet_error (const struct datasheet *);
void datasheet_force_error (struct datasheet *);
const struct taint *datasheet_get_taint (const struct datasheet *);

struct casereader *datasheet_make_reader (struct datasheet *);

/* Columns. */
size_t datasheet_get_column_cnt (const struct datasheet *);
bool datasheet_insert_columns (struct datasheet *,
                               const union value[], size_t cnt,
                               size_t before);
void datasheet_delete_columns (struct datasheet *, size_t start, size_t cnt);
void datasheet_move_columns (struct datasheet *,
                             size_t old_start, size_t new_start,
                             size_t cnt);

/* Rows. */
casenumber datasheet_get_row_cnt (const struct datasheet *);
bool datasheet_insert_rows (struct datasheet *,
                            casenumber before, struct ccase *rows[],
                            casenumber cnt);
void datasheet_delete_rows (struct datasheet *,
                            casenumber first, casenumber cnt);
void datasheet_move_rows (struct datasheet *,
                          size_t old_start, size_t new_start,
                          size_t cnt);

/* Data. */
struct ccase *datasheet_get_row (const struct datasheet *, casenumber);
bool datasheet_put_row (struct datasheet *, casenumber, struct ccase *);
bool datasheet_get_value (const struct datasheet *, casenumber, size_t column,
                          union value *, int width);
bool datasheet_put_value (struct datasheet *, casenumber, size_t column,
                          const union value *, int width);

#endif

/* Testing. */
struct mc_options;

struct datasheet_test_params
  {
    /* Parameters. */
    int max_rows;
    int max_cols;
    int backing_rows;
    int backing_cols;

    /* State. */
    int next_value;
  };

struct mc_results *datasheet_test (struct mc_options *options, void *params);

#endif /* data/datasheet.h */
