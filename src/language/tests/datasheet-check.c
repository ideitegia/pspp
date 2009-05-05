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

#include <config.h>

#include <data/datasheet.h>
#include "datasheet-check.h"
#include "model-checker.h"

#include <stdlib.h>
#include <string.h>

#include <data/casereader-provider.h>
#include <data/casereader.h>
#include <data/casewriter.h>
#include <data/lazy-casereader.h>
#include <data/sparse-cases.h>
#include <libpspp/array.h>
#include <libpspp/assertion.h>
#include <libpspp/range-map.h>
#include <libpspp/range-set.h>
#include <libpspp/taint.h>
#include <libpspp/tower.h>

#include "minmax.h"
#include "xalloc.h"


/* lazy_casereader callback function to instantiate a casereader
   from the datasheet. */
static struct casereader *
lazy_callback (void *ds_)
{
  struct datasheet *ds = ds_;
  return datasheet_make_reader (ds);
}


/* Maximum size of datasheet supported for model checking
   purposes. */
#define MAX_ROWS 5
#define MAX_COLS 5


/* Checks that READER contains the ROW_CNT rows and COLUMN_CNT
   columns of data in ARRAY, reporting any errors via MC. */
static void
check_datasheet_casereader (struct mc *mc, struct casereader *reader,
                            double array[MAX_ROWS][MAX_COLS],
                            size_t row_cnt, size_t column_cnt)
{
  if (casereader_get_case_cnt (reader) != row_cnt)
    {
      if (casereader_get_case_cnt (reader) == CASENUMBER_MAX
          && casereader_count_cases (reader) == row_cnt)
        mc_error (mc, "datasheet casereader has unknown case count");
      else
        mc_error (mc, "casereader row count (%lu) does not match "
                  "expected (%zu)",
                  (unsigned long int) casereader_get_case_cnt (reader),
                  row_cnt);
    }
  else if (casereader_get_value_cnt (reader) != column_cnt)
    mc_error (mc, "casereader column count (%zu) does not match "
              "expected (%zu)",
              casereader_get_value_cnt (reader), column_cnt);
  else
    {
      struct ccase *c;
      size_t row;

      for (row = 0; row < row_cnt; row++)
        {
          size_t col;

          c = casereader_read (reader);
          if (c == NULL)
            {
              mc_error (mc, "casereader_read failed reading row %zu of %zu "
                        "(%zu columns)", row, row_cnt, column_cnt);
              return;
            }

          for (col = 0; col < column_cnt; col++)
            if (case_num_idx (c, col) != array[row][col])
              mc_error (mc, "element %zu,%zu (of %zu,%zu) differs: "
                        "%g != %g",
                        row, col, row_cnt, column_cnt,
                        case_num_idx (c, col), array[row][col]);

	  case_unref (c);
        }

      c = casereader_read (reader);
      if (c != NULL)
        mc_error (mc, "casereader has extra cases (expected %zu)", row_cnt);
    }
}

/* Checks that datasheet DS contains has ROW_CNT rows, COLUMN_CNT
   columns, and the same contents as ARRAY, reporting any
   mismatches via mc_error.  Then, adds DS to MC as a new state. */
static void
check_datasheet (struct mc *mc, struct datasheet *ds,
                 double array[MAX_ROWS][MAX_COLS],
                 size_t row_cnt, size_t column_cnt)
{
  struct datasheet *ds2;
  struct casereader *reader;
  unsigned long int serial = 0;

  assert (row_cnt < MAX_ROWS);
  assert (column_cnt < MAX_COLS);

  /* If it is a duplicate hash, discard the state before checking
     its consistency, to save time. */
  if (mc_discard_dup_state (mc, hash_datasheet (ds)))
    {
      datasheet_destroy (ds);
      return;
    }

  /* Check contents of datasheet via datasheet functions. */
  if (row_cnt != datasheet_get_row_cnt (ds))
    mc_error (mc, "row count (%lu) does not match expected (%zu)",
              (unsigned long int) datasheet_get_row_cnt (ds), row_cnt);
  else if (column_cnt != datasheet_get_column_cnt (ds))
    mc_error (mc, "column count (%zu) does not match expected (%zu)",
              datasheet_get_column_cnt (ds), column_cnt);
  else
    {
      size_t row, col;

      for (row = 0; row < row_cnt; row++)
        for (col = 0; col < column_cnt; col++)
          {
            union value v;
            if (!datasheet_get_value (ds, row, col, &v, 1))
              NOT_REACHED ();
            if (v.f != array[row][col])
              mc_error (mc, "element %zu,%zu (of %zu,%zu) differs: %g != %g",
                        row, col, row_cnt, column_cnt, v.f, array[row][col]);
          }
    }

  /* Check that datasheet contents are correct when read through
     casereader. */
  ds2 = clone_datasheet (ds);
  reader = datasheet_make_reader (ds2);
  check_datasheet_casereader (mc, reader, array, row_cnt, column_cnt);
  casereader_destroy (reader);

  /* Check that datasheet contents are correct when read through
     casereader with lazy_casereader wrapped around it.  This is
     valuable because otherwise there is no non-GUI code that
     uses the lazy_casereader. */
  ds2 = clone_datasheet (ds);
  reader = lazy_casereader_create (column_cnt, row_cnt,
                                   lazy_callback, ds2, &serial);
  check_datasheet_casereader (mc, reader, array, row_cnt, column_cnt);
  if (lazy_casereader_destroy (reader, serial))
    {
      /* Lazy casereader was never instantiated.  This will
         only happen if there are no rows (because in that case
         casereader_read never gets called). */
      datasheet_destroy (ds2);
      if (row_cnt != 0)
        mc_error (mc, "lazy casereader not instantiated, but should "
                  "have been (size %zu,%zu)", row_cnt, column_cnt);
    }
  else
    {
      /* Lazy casereader was instantiated.  This is the common
         case, in which some casereader operation
         (casereader_read in this case) was performed on the
         lazy casereader. */
      casereader_destroy (reader);
      if (row_cnt == 0)
        mc_error (mc, "lazy casereader instantiated, but should not "
                  "have been (size %zu,%zu)", row_cnt, column_cnt);
    }

  mc_add_state (mc, ds);
}

/* Extracts the contents of DS into DATA. */
static void
extract_data (const struct datasheet *ds, double data[MAX_ROWS][MAX_COLS])
{
  size_t column_cnt = datasheet_get_column_cnt (ds);
  size_t row_cnt = datasheet_get_row_cnt (ds);
  size_t row, col;

  assert (row_cnt < MAX_ROWS);
  assert (column_cnt < MAX_COLS);
  for (row = 0; row < row_cnt; row++)
    for (col = 0; col < column_cnt; col++)
      {
        union value v;
        if (!datasheet_get_value (ds, row, col, &v, 1))
          NOT_REACHED ();
        data[row][col] = v.f;
      }
}

/* Clones the structure and contents of ODS into *DS,
   and the contents of ODATA into DATA. */
static void
clone_model (const struct datasheet *ods, double odata[MAX_ROWS][MAX_COLS],
             struct datasheet **ds, double data[MAX_ROWS][MAX_COLS])
{
  *ds = clone_datasheet (ods);
  memcpy (data, odata, MAX_ROWS * MAX_COLS * sizeof **data);
}

/* "init" function for struct mc_class. */
static void
datasheet_mc_init (struct mc *mc)
{
  struct datasheet_test_params *params = mc_get_aux (mc);
  struct datasheet *ds;

  if (params->backing_rows == 0 && params->backing_cols == 0)
    {
      /* Create unbacked datasheet. */
      ds = datasheet_create (NULL);
      mc_name_operation (mc, "empty datasheet");
      check_datasheet (mc, ds, NULL, 0, 0);
    }
  else
    {
      /* Create datasheet with backing. */
      struct casewriter *writer;
      struct casereader *reader;
      double data[MAX_ROWS][MAX_COLS];
      int row;

      assert (params->backing_rows > 0 && params->backing_rows <= MAX_ROWS);
      assert (params->backing_cols > 0 && params->backing_cols <= MAX_COLS);

      writer = mem_writer_create (params->backing_cols);
      for (row = 0; row < params->backing_rows; row++)
        {
          struct ccase *c;
          int col;

          c = case_create (params->backing_cols);
          for (col = 0; col < params->backing_cols; col++)
            {
              double value = params->next_value++;
              data[row][col] = value;
              case_data_rw_idx (c, col)->f = value;
            }
          casewriter_write (writer, c);
        }
      reader = casewriter_make_reader (writer);
      assert (reader != NULL);

      ds = datasheet_create (reader);
      mc_name_operation (mc, "datasheet with (%d,%d) backing",
                         params->backing_rows, params->backing_cols);
      check_datasheet (mc, ds, data,
                       params->backing_rows, params->backing_cols);
    }
}

/* "mutate" function for struct mc_class. */
static void
datasheet_mc_mutate (struct mc *mc, const void *ods_)
{
  struct datasheet_test_params *params = mc_get_aux (mc);

  const struct datasheet *ods = ods_;
  double odata[MAX_ROWS][MAX_COLS];
  double data[MAX_ROWS][MAX_COLS];
  size_t column_cnt = datasheet_get_column_cnt (ods);
  size_t row_cnt = datasheet_get_row_cnt (ods);
  size_t pos, new_pos, cnt;

  extract_data (ods, odata);

  /* Insert all possible numbers of columns in all possible
     positions. */
  for (pos = 0; pos <= column_cnt; pos++)
    for (cnt = 0; cnt <= params->max_cols - column_cnt; cnt++)
      if (mc_include_state (mc))
        {
          struct datasheet *ds;
          union value new[MAX_COLS];
          size_t i, j;

          mc_name_operation (mc, "insert %zu columns at %zu", cnt, pos);
          clone_model (ods, odata, &ds, data);

          for (i = 0; i < cnt; i++)
            new[i].f = params->next_value++;

          if (!datasheet_insert_columns (ds, new, cnt, pos))
            mc_error (mc, "datasheet_insert_columns failed");

          for (i = 0; i < row_cnt; i++)
            {
              insert_range (&data[i][0], column_cnt, sizeof data[i][0],
                            pos, cnt);
              for (j = 0; j < cnt; j++)
                data[i][pos + j] = new[j].f;
            }

          check_datasheet (mc, ds, data, row_cnt, column_cnt + cnt);
        }

  /* Delete all possible numbers of columns from all possible
     positions. */
  for (pos = 0; pos < column_cnt; pos++)
    for (cnt = 0; cnt < column_cnt - pos; cnt++)
      if (mc_include_state (mc))
        {
          struct datasheet *ds;
          size_t i;

          mc_name_operation (mc, "delete %zu columns at %zu", cnt, pos);
          clone_model (ods, odata, &ds, data);

          datasheet_delete_columns (ds, pos, cnt);

          for (i = 0; i < row_cnt; i++)
            remove_range (&data[i], column_cnt, sizeof *data[i], pos, cnt);

          check_datasheet (mc, ds, data, row_cnt, column_cnt - cnt);
        }

  /* Move all possible numbers of columns from all possible
     existing positions to all possible new positions. */
  for (pos = 0; pos < column_cnt; pos++)
    for (cnt = 0; cnt < column_cnt - pos; cnt++)
      for (new_pos = 0; new_pos < column_cnt - cnt; new_pos++)
        if (mc_include_state (mc))
          {
            struct datasheet *ds;
            size_t i;

            clone_model (ods, odata, &ds, data);
            mc_name_operation (mc, "move %zu columns from %zu to %zu",
                               cnt, pos, new_pos);

            datasheet_move_columns (ds, pos, new_pos, cnt);

            for (i = 0; i < row_cnt; i++)
              move_range (&data[i], column_cnt, sizeof data[i][0],
                          pos, new_pos, cnt);

            check_datasheet (mc, ds, data, row_cnt, column_cnt);
          }

  /* Insert all possible numbers of rows in all possible
     positions. */
  for (pos = 0; pos <= row_cnt; pos++)
    for (cnt = 0; cnt <= params->max_rows - row_cnt; cnt++)
      if (mc_include_state (mc))
        {
          struct datasheet *ds;
          struct ccase *c[MAX_ROWS];
          size_t i, j;

          clone_model (ods, odata, &ds, data);
          mc_name_operation (mc, "insert %zu rows at %zu", cnt, pos);

          for (i = 0; i < cnt; i++)
            {
              c[i] = case_create (column_cnt);
              for (j = 0; j < column_cnt; j++)
                case_data_rw_idx (c[i], j)->f = params->next_value++;
            }

          insert_range (data, row_cnt, sizeof data[pos], pos, cnt);
          for (i = 0; i < cnt; i++)
            for (j = 0; j < column_cnt; j++)
              data[i + pos][j] = case_num_idx (c[i], j);

          if (!datasheet_insert_rows (ds, pos, c, cnt))
            mc_error (mc, "datasheet_insert_rows failed");

          check_datasheet (mc, ds, data, row_cnt + cnt, column_cnt);
        }

  /* Delete all possible numbers of rows from all possible
     positions. */
  for (pos = 0; pos < row_cnt; pos++)
    for (cnt = 0; cnt < row_cnt - pos; cnt++)
      if (mc_include_state (mc))
        {
          struct datasheet *ds;

          clone_model (ods, odata, &ds, data);
          mc_name_operation (mc, "delete %zu rows at %zu", cnt, pos);

          datasheet_delete_rows (ds, pos, cnt);

          remove_range (&data[0], row_cnt, sizeof data[0], pos, cnt);

          check_datasheet (mc, ds, data, row_cnt - cnt, column_cnt);
        }

  /* Move all possible numbers of rows from all possible existing
     positions to all possible new positions. */
  for (pos = 0; pos < row_cnt; pos++)
    for (cnt = 0; cnt < row_cnt - pos; cnt++)
      for (new_pos = 0; new_pos < row_cnt - cnt; new_pos++)
        if (mc_include_state (mc))
          {
            struct datasheet *ds;

            clone_model (ods, odata, &ds, data);
            mc_name_operation (mc, "move %zu rows from %zu to %zu",
                               cnt, pos, new_pos);

            datasheet_move_rows (ds, pos, new_pos, cnt);

            move_range (&data[0], row_cnt, sizeof data[0],
                        pos, new_pos, cnt);

            check_datasheet (mc, ds, data, row_cnt, column_cnt);
          }
}

/* "destroy" function for struct mc_class. */
static void
datasheet_mc_destroy (const struct mc *mc UNUSED, void *ds_)
{
  struct datasheet *ds = ds_;
  datasheet_destroy (ds);
}

/* Executes the model checker on the datasheet test driver with
   the given OPTIONS and passing in the given PARAMS, which must
   point to a modifiable "struct datasheet_test_params".  If any
   value in PARAMS is out of range, it will be adjusted into the
   valid range before running the test.

   Returns the results of the model checking run. */
struct mc_results *
datasheet_test (struct mc_options *options, void *params_)
{
  struct datasheet_test_params *params = params_;
  static const struct mc_class datasheet_mc_class =
    {
      datasheet_mc_init,
      datasheet_mc_mutate,
      datasheet_mc_destroy,
    };

  params->next_value = 1;
  params->max_rows = MIN (params->max_rows, MAX_ROWS);
  params->max_cols = MIN (params->max_cols, MAX_COLS);
  params->backing_rows = MIN (params->backing_rows, params->max_rows);
  params->backing_cols = MIN (params->backing_cols, params->max_cols);

  mc_options_set_aux (options, params);
  return mc_run (&datasheet_mc_class, options);
}
