/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2014 Free Software Foundation, Inc.

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

#include <ctype.h>
#include <float.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <data/casereader-provider.h>
#include <data/casereader.h>
#include <data/casewriter.h>
#include <data/lazy-casereader.h>
#include <libpspp/argv-parser.h>
#include <libpspp/array.h>
#include <libpspp/assertion.h>
#include <libpspp/hash-functions.h>
#include <libpspp/model-checker.h>
#include <libpspp/range-map.h>
#include <libpspp/range-set.h>
#include <libpspp/str.h>
#include <libpspp/taint.h>
#include <libpspp/tower.h>

#include "error.h"
#include "minmax.h"
#include "progname.h"
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
#define MAX_WIDTHS 5

/* Test params. */
struct datasheet_test_params
  {
    /* Parameters. */
    int max_rows;               /* Maximum number of rows. */
    int max_cols;               /* Maximum number of columns. */
    int backing_rows;           /* Number of rows of backing store. */
    int backing_widths[MAX_COLS]; /* Widths of columns of backing store. */
    int n_backing_cols;           /* Number of columns of backing store. */
    int widths[MAX_WIDTHS];     /* Allowed column widths. */
    int n_widths;

    /* State. */
    unsigned int next_value;
  };

static bool
check_caseproto (struct mc *mc, const struct caseproto *benchmark,
                 const struct caseproto *test, const char *test_name)
{
  size_t n_columns = caseproto_get_n_widths (benchmark);
  size_t col;
  bool ok;

  if (n_columns != caseproto_get_n_widths (test))
    {
      mc_error (mc, "%s column count (%zu) does not match expected (%zu)",
                test_name, caseproto_get_n_widths (test), n_columns);
      return false;
    }

  ok = true;
  for (col = 0; col < n_columns; col++)
    {
      int benchmark_width = caseproto_get_width (benchmark, col);
      int test_width = caseproto_get_width (test, col);
      if (benchmark_width != test_width)
        {
          mc_error (mc, "%s column %zu width (%d) differs from expected (%d)",
                    test_name, col, test_width, benchmark_width);
          ok = false;
        }
    }
  return ok;
}

/* Checks that READER contains the N_ROWS rows and N_COLUMNS
   columns of data in ARRAY, reporting any errors via MC. */
static void
check_datasheet_casereader (struct mc *mc, struct casereader *reader,
                            union value array[MAX_ROWS][MAX_COLS],
                            size_t n_rows, const struct caseproto *proto)
{
  size_t n_columns = caseproto_get_n_widths (proto);

  if (!check_caseproto (mc, proto, casereader_get_proto (reader),
                        "casereader"))
    return;
  else if (casereader_get_case_cnt (reader) != n_rows)
    {
      if (casereader_get_case_cnt (reader) == CASENUMBER_MAX
          && casereader_count_cases (reader) == n_rows)
        mc_error (mc, "datasheet casereader has unknown case count");
      else
        mc_error (mc, "casereader row count (%lu) does not match "
                  "expected (%zu)",
                  (unsigned long int) casereader_get_case_cnt (reader),
                  n_rows);
    }
  else
    {
      struct ccase *c;
      size_t row;

      for (row = 0; row < n_rows; row++)
        {
          size_t col;

          c = casereader_read (reader);
          if (c == NULL)
            {
              mc_error (mc, "casereader_read failed reading row %zu of %zu "
                        "(%zu columns)", row, n_rows, n_columns);
              return;
            }

          for (col = 0; col < n_columns; col++)
            {
              int width = caseproto_get_width (proto, col);
              if (!value_equal (case_data_idx (c, col), &array[row][col],
                                width))
                {
                  if (width == 0)
                    mc_error (mc, "element %zu,%zu (of %zu,%zu) differs: "
                              "%.*g != %.*g",
                              row, col, n_rows, n_columns,
                              DBL_DIG + 1, case_num_idx (c, col),
                              DBL_DIG + 1, array[row][col].f);
                  else
                    mc_error (mc, "element %zu,%zu (of %zu,%zu) differs: "
                              "'%.*s' != '%.*s'",
                              row, col, n_rows, n_columns,
                              width, case_str_idx (c, col),
                              width, value_str (&array[row][col], width));
                }
            }

	  case_unref (c);
        }

      c = casereader_read (reader);
      if (c != NULL)
        mc_error (mc, "casereader has extra cases (expected %zu)", n_rows);
    }
}

/* Checks that datasheet DS contains has N_ROWS rows, N_COLUMNS
   columns, and the same contents as ARRAY, reporting any
   mismatches via mc_error.  Then, adds DS to MC as a new state. */
static void
check_datasheet (struct mc *mc, struct datasheet *ds,
                 union value array[MAX_ROWS][MAX_COLS],
                 size_t n_rows, const struct caseproto *proto)
{
  size_t n_columns = caseproto_get_n_widths (proto);
  struct datasheet *ds2;
  struct casereader *reader;
  unsigned long int serial = 0;

  assert (n_rows < MAX_ROWS);
  assert (n_columns < MAX_COLS);

  /* Check contents of datasheet via datasheet functions. */
  if (!check_caseproto (mc, proto, datasheet_get_proto (ds), "datasheet"))
    {
      /* check_caseproto emitted errors already. */
    }
  else if (n_rows != datasheet_get_n_rows (ds))
    mc_error (mc, "row count (%lu) does not match expected (%zu)",
              (unsigned long int) datasheet_get_n_rows (ds), n_rows);
  else
    {
      size_t row, col;
      bool difference = false;

      for (row = 0; row < n_rows; row++)
        for (col = 0; col < n_columns; col++)
          {
            int width = caseproto_get_width (proto, col);
            union value *av = &array[row][col];
            union value v;

            value_init (&v, width);
            if (!datasheet_get_value (ds, row, col, &v))
              NOT_REACHED ();
            if (!value_equal (&v, av, width))
              {
                if (width == 0)
                  mc_error (mc, "element %zu,%zu (of %zu,%zu) differs: "
                            "%.*g != %.*g", row, col, n_rows, n_columns,
                            DBL_DIG + 1, v.f, DBL_DIG + 1, av->f);
                else
                  mc_error (mc, "element %zu,%zu (of %zu,%zu) differs: "
                            "'%.*s' != '%.*s'",
                            row, col, n_rows, n_columns,
                            width, value_str (&v, width),
                            width, value_str (av, width));
                difference = true;
              }
            value_destroy (&v, width);
          }

      if (difference)
        {
          struct string s;

          mc_error (mc, "expected:");
          ds_init_empty (&s);
          for (row = 0; row < n_rows; row++)
            {
              ds_clear (&s);
              ds_put_format (&s, "row %zu:", row);
              for (col = 0; col < n_columns; col++)
                {
                  const union value *v = &array[row][col];
                  int width = caseproto_get_width (proto, col);
                  if (width == 0)
                    ds_put_format (&s, " %g", v->f);
                  else
                    ds_put_format (&s, " '%.*s'", width, value_str (v, width));
                }
              mc_error (mc, "%s", ds_cstr (&s));
            }

          mc_error (mc, "actual:");
          ds_init_empty (&s);
          for (row = 0; row < n_rows; row++)
            {
              ds_clear (&s);
              ds_put_format (&s, "row %zu:", row);
              for (col = 0; col < n_columns; col++)
                {
                  int width = caseproto_get_width (proto, col);
                  union value v;
                  value_init (&v, width);
                  if (!datasheet_get_value (ds, row, col, &v))
                    NOT_REACHED ();
                  if (width == 0)
                    ds_put_format (&s, " %g", v.f);
                  else
                    ds_put_format (&s, " '%.*s'",
                                   width, value_str (&v, width));
                }
              mc_error (mc, "%s", ds_cstr (&s));
            }

          ds_destroy (&s);
        }
    }

  /* Check that datasheet contents are correct when read through
     casereader. */
  ds2 = clone_datasheet (ds);
  reader = datasheet_make_reader (ds2);
  check_datasheet_casereader (mc, reader, array, n_rows, proto);
  casereader_destroy (reader);

  /* Check that datasheet contents are correct when read through
     casereader with lazy_casereader wrapped around it.  This is
     valuable because otherwise there is no non-GUI code that
     uses the lazy_casereader. */
  ds2 = clone_datasheet (ds);
  reader = lazy_casereader_create (datasheet_get_proto (ds2), n_rows,
                                   lazy_callback, ds2, &serial);
  check_datasheet_casereader (mc, reader, array, n_rows, proto);
  if (lazy_casereader_destroy (reader, serial))
    {
      /* Lazy casereader was never instantiated.  This will
         only happen if there are no rows (because in that case
         casereader_read never gets called). */
      datasheet_destroy (ds2);
      if (n_rows != 0)
        mc_error (mc, "lazy casereader not instantiated, but should "
                  "have been (size %zu,%zu)", n_rows, n_columns);
    }
  else
    {
      /* Lazy casereader was instantiated.  This is the common
         case, in which some casereader operation
         (casereader_read in this case) was performed on the
         lazy casereader. */
      casereader_destroy (reader);
      if (n_rows == 0)
        mc_error (mc, "lazy casereader instantiated, but should not "
                  "have been (size %zu,%zu)", n_rows, n_columns);
    }

  if (mc_discard_dup_state (mc, hash_datasheet (ds)))
    datasheet_destroy (ds);
  else
    mc_add_state (mc, ds);
}

/* Extracts the contents of DS into DATA. */
static void
extract_data (const struct datasheet *ds, union value data[MAX_ROWS][MAX_COLS])
{
  const struct caseproto *proto = datasheet_get_proto (ds);
  size_t n_columns = datasheet_get_n_columns (ds);
  size_t n_rows = datasheet_get_n_rows (ds);
  size_t row, col;

  assert (n_rows < MAX_ROWS);
  assert (n_columns < MAX_COLS);
  for (row = 0; row < n_rows; row++)
    for (col = 0; col < n_columns; col++)
      {
        int width = caseproto_get_width (proto, col);
        union value *v = &data[row][col];
        value_init (v, width);
        if (!datasheet_get_value (ds, row, col, v))
          NOT_REACHED ();
      }
}

/* Copies the contents of ODATA into DATA.  Each of the N_ROWS
   rows of ODATA and DATA must have prototype PROTO. */
static void
clone_data (size_t n_rows, const struct caseproto *proto,
            union value odata[MAX_ROWS][MAX_COLS],
            union value data[MAX_ROWS][MAX_COLS])
{
  size_t n_columns = caseproto_get_n_widths (proto);
  size_t row, col;

  assert (n_rows < MAX_ROWS);
  assert (n_columns < MAX_COLS);
  for (row = 0; row < n_rows; row++)
    for (col = 0; col < n_columns; col++)
      {
        int width = caseproto_get_width (proto, col);
        const union value *ov = &odata[row][col];
        union value *v = &data[row][col];
        value_init (v, width);
        value_copy (v, ov, width);
      }
}

static void
release_data (size_t n_rows, const struct caseproto *proto,
              union value data[MAX_ROWS][MAX_COLS])
{
  size_t n_columns = caseproto_get_n_widths (proto);
  size_t row, col;

  assert (n_rows < MAX_ROWS);
  assert (n_columns < MAX_COLS);
  for (col = 0; col < n_columns; col++)
    {
      int width = caseproto_get_width (proto, col);
      if (value_needs_init (width))
        for (row = 0; row < n_rows; row++)
          value_destroy (&data[row][col], width);
    }
}

/* Clones the structure and contents of ODS into *DS,
   and the contents of ODATA into DATA. */
static void
clone_model (const struct datasheet *ods,
             union value odata[MAX_ROWS][MAX_COLS],
             struct datasheet **ds,
             union value data[MAX_ROWS][MAX_COLS])
{
  *ds = clone_datasheet (ods);
  clone_data (datasheet_get_n_rows (ods), datasheet_get_proto (ods),
              odata, data);
}

static void
value_from_param (union value *value, int width, unsigned int idx)
{
  if (width == 0)
    value->f = idx & 0xffff;
  else
    {
      unsigned int hash = hash_int (idx, 0);
      uint8_t *string = value_str_rw (value, width);
      int offset;

      assert (width < 32);
      for (offset = 0; offset < width; offset++)
        string[offset] = "ABCDEFGHIJ"[(hash >> offset) % 10];
    }
}

/* "init" function for struct mc_class. */
static void
datasheet_mc_init (struct mc *mc)
{
  struct datasheet_test_params *params = mc_get_aux (mc);
  struct datasheet *ds;

  if (params->backing_rows == 0 && params->n_backing_cols == 0)
    {
      /* Create unbacked datasheet. */
      struct caseproto *proto;
      ds = datasheet_create (NULL);
      mc_name_operation (mc, "empty datasheet");
      proto = caseproto_create ();
      check_datasheet (mc, ds, NULL, 0, proto);
      caseproto_unref (proto);
    }
  else
    {
      /* Create datasheet with backing. */
      struct casewriter *writer;
      struct casereader *reader;
      union value data[MAX_ROWS][MAX_COLS];
      struct caseproto *proto;
      int row, col;

      assert (params->backing_rows > 0 && params->backing_rows <= MAX_ROWS);
      assert (params->n_backing_cols > 0
              && params->n_backing_cols <= MAX_COLS);

      proto = caseproto_create ();
      for (col = 0; col < params->n_backing_cols; col++)
        proto = caseproto_add_width (proto, params->backing_widths[col]);

      writer = mem_writer_create (proto);
      for (row = 0; row < params->backing_rows; row++)
        {
          struct ccase *c;

          c = case_create (proto);
          for (col = 0; col < params->n_backing_cols; col++)
            {
              int width = params->backing_widths[col];
              union value *value = &data[row][col];
              value_init (value, width);
              value_from_param (value, width, params->next_value++);
              value_copy (case_data_rw_idx (c, col), value, width);
            }
          casewriter_write (writer, c);
        }

      reader = casewriter_make_reader (writer);
      assert (reader != NULL);

      ds = datasheet_create (reader);
      mc_name_operation (mc, "datasheet with (%d,%d) backing",
                         params->backing_rows, params->n_backing_cols);
      check_datasheet (mc, ds, data,
                       params->backing_rows, proto);
      release_data (params->backing_rows, proto, data);
      caseproto_unref (proto);
    }
}

struct resize_cb_aux
  {
    int old_width;
    int new_width;
  };

static void
resize_cb (const union value *old_value, union value *new_value, const void *aux_)
{
  const struct resize_cb_aux *aux = aux_;

  value_from_param (new_value, aux->new_width,
                    value_hash (old_value, aux->old_width, 0));
}

/* "mutate" function for struct mc_class. */
static void
datasheet_mc_mutate (struct mc *mc, const void *ods_)
{
  struct datasheet_test_params *params = mc_get_aux (mc);

  const struct datasheet *ods = ods_;
  union value odata[MAX_ROWS][MAX_COLS];
  union value data[MAX_ROWS][MAX_COLS];
  const struct caseproto *oproto = datasheet_get_proto (ods);
  size_t n_columns = datasheet_get_n_columns (ods);
  size_t n_rows = datasheet_get_n_rows (ods);
  size_t pos, new_pos, cnt, width_idx;

  extract_data (ods, odata);

  /* Insert a column in each possible position. */
  if (n_columns < params->max_cols)
    for (pos = 0; pos <= n_columns; pos++)
      for (width_idx = 0; width_idx < params->n_widths; width_idx++)
        if (mc_include_state (mc))
          {
            int width = params->widths[width_idx];
            struct caseproto *proto;
            struct datasheet *ds;
            union value new;
            size_t i;

            mc_name_operation (mc, "insert column at %zu "
                               "(from %zu to %zu columns)",
                               pos, n_columns, n_columns + 1);
            clone_model (ods, odata, &ds, data);

            value_init (&new, width);
            value_from_param (&new, width, params->next_value++);
            if (!datasheet_insert_column (ds, &new, width, pos))
              mc_error (mc, "datasheet_insert_column failed");
            proto = caseproto_insert_width (caseproto_ref (oproto),
                                            pos, width);

            for (i = 0; i < n_rows; i++)
              {
                insert_element (&data[i][0], n_columns, sizeof data[i][0],
                                pos);
                value_init (&data[i][pos], width);
                value_copy (&data[i][pos], &new, width);
              }
            value_destroy (&new, width);

            check_datasheet (mc, ds, data, n_rows, proto);
            release_data (n_rows, proto, data);
            caseproto_unref (proto);
          }

  /* Resize each column to each possible new size. */
  for (pos = 0; pos < n_columns; pos++)
    for (width_idx = 0; width_idx < params->n_widths; width_idx++)
      {
        int owidth = caseproto_get_width (oproto, pos);
        int width = params->widths[width_idx];
        if (mc_include_state (mc))
          {
            struct resize_cb_aux aux;
            struct caseproto *proto;
            struct datasheet *ds;
            size_t i;

            mc_name_operation (mc, "resize column %zu (of %zu) "
                               "from width %d to %d",
                               pos, n_columns, owidth, width);
            clone_model (ods, odata, &ds, data);

            aux.old_width = owidth;
            aux.new_width = width;
            if (!datasheet_resize_column (ds, pos, width, resize_cb, &aux))
              NOT_REACHED ();
            proto = caseproto_set_width (caseproto_ref (oproto), pos, width);

            for (i = 0; i < n_rows; i++)
              {
                union value *old_value = &data[i][pos];
                union value new_value;
                value_init (&new_value, width);
                resize_cb (old_value, &new_value, &aux);
                value_swap (old_value, &new_value);
                value_destroy (&new_value, owidth);
              }

            check_datasheet (mc, ds, data, n_rows, proto);
            release_data (n_rows, proto, data);
            caseproto_unref (proto);
          }
      }

  /* Delete all possible numbers of columns from all possible
     positions. */
  for (pos = 0; pos < n_columns; pos++)
    for (cnt = 1; cnt < n_columns - pos; cnt++)
      if (mc_include_state (mc))
        {
          struct caseproto *proto;
          struct datasheet *ds;
          size_t i, j;

          mc_name_operation (mc, "delete %zu columns at %zu "
                             "(from %zu to %zu columns)",
                             cnt, pos, n_columns, n_columns - cnt);
          clone_model (ods, odata, &ds, data);

          datasheet_delete_columns (ds, pos, cnt);
          proto = caseproto_remove_widths (caseproto_ref (oproto), pos, cnt);

          for (i = 0; i < n_rows; i++)
            {
              for (j = pos; j < pos + cnt; j++)
                value_destroy (&data[i][j], caseproto_get_width (oproto, j));
              remove_range (&data[i], n_columns, sizeof *data[i], pos, cnt);
            }

          check_datasheet (mc, ds, data, n_rows, proto);
          release_data (n_rows, proto, data);
          caseproto_unref (proto);
        }

  /* Move all possible numbers of columns from all possible
     existing positions to all possible new positions. */
  for (pos = 0; pos < n_columns; pos++)
    for (cnt = 1; cnt < n_columns - pos; cnt++)
      for (new_pos = 0; new_pos < n_columns - cnt; new_pos++)
        if (mc_include_state (mc))
          {
            struct caseproto *proto;
            struct datasheet *ds;
            size_t i;

            clone_model (ods, odata, &ds, data);
            mc_name_operation (mc, "move %zu columns (of %zu) from %zu to %zu",
                               cnt, n_columns, pos, new_pos);

            datasheet_move_columns (ds, pos, new_pos, cnt);

            for (i = 0; i < n_rows; i++)
              move_range (&data[i], n_columns, sizeof data[i][0],
                          pos, new_pos, cnt);
            proto = caseproto_move_widths (caseproto_ref (oproto),
                                           pos, new_pos, cnt);

            check_datasheet (mc, ds, data, n_rows, proto);
            release_data (n_rows, proto, data);
            caseproto_unref (proto);
          }

  /* Insert all possible numbers of rows in all possible
     positions. */
  for (pos = 0; pos <= n_rows; pos++)
    for (cnt = 1; cnt <= params->max_rows - n_rows; cnt++)
      if (mc_include_state (mc))
        {
          struct datasheet *ds;
          struct ccase *c[MAX_ROWS];
          size_t i, j;

          clone_model (ods, odata, &ds, data);
          mc_name_operation (mc, "insert %zu rows at %zu "
                             "(from %zu to %zu rows)",
                             cnt, pos, n_rows, n_rows + cnt);

          for (i = 0; i < cnt; i++)
            {
              c[i] = case_create (oproto);
              for (j = 0; j < n_columns; j++)
                value_from_param (case_data_rw_idx (c[i], j),
                                  caseproto_get_width (oproto, j),
                                  params->next_value++);
            }

          insert_range (data, n_rows, sizeof data[pos], pos, cnt);
          for (i = 0; i < cnt; i++)
            for (j = 0; j < n_columns; j++)
              {
                int width = caseproto_get_width (oproto, j);
                value_init (&data[i + pos][j], width);
                value_copy (&data[i + pos][j], case_data_idx (c[i], j), width);
              }

          if (!datasheet_insert_rows (ds, pos, c, cnt))
            mc_error (mc, "datasheet_insert_rows failed");

          check_datasheet (mc, ds, data, n_rows + cnt, oproto);
          release_data (n_rows + cnt, oproto, data);
        }

  /* Delete all possible numbers of rows from all possible
     positions. */
  for (pos = 0; pos < n_rows; pos++)
    for (cnt = 1; cnt < n_rows - pos; cnt++)
      if (mc_include_state (mc))
        {
          struct datasheet *ds;

          clone_model (ods, odata, &ds, data);
          mc_name_operation (mc, "delete %zu rows at %zu "
                             "(from %zu to %zu rows)",
                             cnt, pos, n_rows, n_rows - cnt);

          datasheet_delete_rows (ds, pos, cnt);

          release_data (cnt, oproto, &data[pos]);
          remove_range (&data[0], n_rows, sizeof data[0], pos, cnt);

          check_datasheet (mc, ds, data, n_rows - cnt, oproto);
          release_data (n_rows - cnt, oproto, data);
        }

  /* Move all possible numbers of rows from all possible existing
     positions to all possible new positions. */
  for (pos = 0; pos < n_rows; pos++)
    for (cnt = 1; cnt < n_rows - pos; cnt++)
      for (new_pos = 0; new_pos < n_rows - cnt; new_pos++)
        if (mc_include_state (mc))
          {
            struct datasheet *ds;

            clone_model (ods, odata, &ds, data);
            mc_name_operation (mc, "move %zu rows (of %zu) from %zu to %zu",
                               cnt, n_rows, pos, new_pos);

            datasheet_move_rows (ds, pos, new_pos, cnt);

            move_range (&data[0], n_rows, sizeof data[0],
                        pos, new_pos, cnt);

            check_datasheet (mc, ds, data, n_rows, oproto);
            release_data (n_rows, oproto, data);
          }

  release_data (n_rows, oproto, odata);
}

/* "destroy" function for struct mc_class. */
static void
datasheet_mc_destroy (const struct mc *mc UNUSED, void *ds_)
{
  struct datasheet *ds = ds_;
  datasheet_destroy (ds);
}

enum
  {
    OPT_MAX_ROWS,
    OPT_MAX_COLUMNS,
    OPT_BACKING_ROWS,
    OPT_BACKING_WIDTHS,
    OPT_WIDTHS,
    OPT_HELP,
    N_DATASHEET_OPTIONS
  };

static const struct argv_option datasheet_argv_options[N_DATASHEET_OPTIONS] =
  {
    {"max-rows", 0, required_argument, OPT_MAX_ROWS},
    {"max-columns", 0, required_argument, OPT_MAX_COLUMNS},
    {"backing-rows", 0, required_argument, OPT_BACKING_ROWS},
    {"backing-widths", 0, required_argument, OPT_BACKING_WIDTHS},
    {"widths", 0, required_argument, OPT_WIDTHS},
    {"help", 'h', no_argument, OPT_HELP},
  };

static void usage (void);

static void
datasheet_option_callback (int id, void *params_)
{
  struct datasheet_test_params *params = params_;
  switch (id)
    {
    case OPT_MAX_ROWS:
      params->max_rows = atoi (optarg);
      break;

    case OPT_MAX_COLUMNS:
      params->max_cols = atoi (optarg);
      break;

    case OPT_BACKING_ROWS:
      params->backing_rows = atoi (optarg);
      break;

    case OPT_BACKING_WIDTHS:
      {
        char *w;

        params->n_backing_cols = 0;
        for (w = strtok (optarg, ", "); w != NULL; w = strtok (NULL, ", "))
          {
            int value = atoi (w);

            if (params->n_backing_cols >= MAX_COLS)
              error (1, 0, "Too many widths on --backing-widths "
                     "(only %d are allowed)", MAX_COLS);
            if (!isdigit (w[0]) || value < 0 || value > 31)
              error (1, 0, "--backing-widths argument must be a list of 1 to "
                     "%d integers between 0 and 31 in increasing order",
                     MAX_COLS);
            params->backing_widths[params->n_backing_cols++] = value;
          }
      }
      break;

    case OPT_WIDTHS:
      {
        int last = -1;
        char *w;

        params->n_widths = 0;
        for (w = strtok (optarg, ", "); w != NULL; w = strtok (NULL, ", "))
          {
            int value = atoi (w);

            if (params->n_widths >= MAX_WIDTHS)
              error (1, 0, "Too many widths on --widths (only %d are allowed)",
                     MAX_WIDTHS);
            if (!isdigit (w[0]) || value < 0 || value > 31)
              error (1, 0, "--widths argument must be a list of 1 to %d "
                     "integers between 0 and 31 in increasing order",
                     MAX_WIDTHS);

            /* This is an artificial requirement merely to ensure
               that there are no duplicates.  Duplicates aren't a
               real problem but they would waste time. */
            if (value <= last)
              error (1, 0, "--widths arguments must be in increasing order");

            params->widths[params->n_widths++] = value;
          }
        if (params->n_widths == 0)
          error (1, 0, "at least one value must be specified on --widths");
      }
      break;

    case OPT_HELP:
      usage ();
      break;

    default:
      NOT_REACHED ();
    }
}

static void
usage (void)
{
  printf ("%s, for testing the datasheet implementation.\n"
          "Usage: %s [OPTION]...\n"
          "\nTest state space parameters (min...max, default):\n"
          "  --max-rows=N         Maximum number of rows (0...5, 3)\n"
          "  --max-rows=N         Maximum number of columns (0...5, 3)\n"
          "  --backing-rows=N     Rows of backing store (0...max_rows, 0)\n"
          "  --backing-widths=W[,W]...  Backing store widths to test (0=num)\n"
          "  --widths=W[,W]...    Column widths to test, where 0=numeric,\n"
          "                       other values are string widths (0,1,11)\n",
          program_name, program_name);
  mc_options_usage ();
  fputs ("\nOther options:\n"
         "  --help               Display this help message\n"
         "\nReport bugs to <bug-gnu-pspp@gnu.org>\n",
         stdout);
  exit (0);
}

int
main (int argc, char *argv[])
{
  static const struct mc_class datasheet_mc_class =
    {
      datasheet_mc_init,
      datasheet_mc_mutate,
      datasheet_mc_destroy,
    };

  struct datasheet_test_params params;
  struct mc_options *options;
  struct mc_results *results;
  struct argv_parser *parser;
  int verbosity;
  bool success;

  set_program_name (argv[0]);

  /* Default parameters. */
  params.max_rows = 3;
  params.max_cols = 3;
  params.backing_rows = 0;
  params.n_backing_cols = 0;
  params.widths[0] = 0;
  params.widths[1] = 1;
  params.widths[2] = 11;
  params.n_widths = 3;
  params.next_value = 1;

  /* Parse comand line. */
  parser = argv_parser_create ();
  options = mc_options_create ();
  mc_options_register_argv_parser (options, parser);
  argv_parser_add_options (parser, datasheet_argv_options, N_DATASHEET_OPTIONS,
                           datasheet_option_callback, &params);
  if (!argv_parser_run (parser, argc, argv))
    exit (EXIT_FAILURE);
  argv_parser_destroy (parser);
  verbosity = mc_options_get_verbosity (options);

  /* Force parameters into allowed ranges. */
  params.max_rows = MIN (params.max_rows, MAX_ROWS);
  params.max_cols = MIN (params.max_cols, MAX_COLS);
  params.backing_rows = MIN (params.backing_rows, params.max_rows);
  params.n_backing_cols = MIN (params.n_backing_cols, params.max_cols);
  mc_options_set_aux (options, &params);
  results = mc_run (&datasheet_mc_class, options);

  /* Output results. */
  success = (mc_results_get_stop_reason (results) != MC_MAX_ERROR_COUNT
             && mc_results_get_stop_reason (results) != MC_INTERRUPTED);
  if (verbosity > 0 || !success)
    {
      int i;

      printf ("Parameters: --max-rows=%d --max-columns=%d --backing-rows=%d ",
              params.max_rows, params.max_cols, params.backing_rows);

      printf ("--backing-widths=");
      for (i = 0; i < params.n_backing_cols; i++)
        {
          if (i > 0)
            printf (",");
          printf ("%d", params.backing_widths[i]);
        }
      printf (" ");

      printf ("--widths=");
      for (i = 0; i < params.n_widths; i++)
        {
          if (i > 0)
            printf (",");
          printf ("%d", params.widths[i]);
        }
      printf ("\n\n");
      mc_results_print (results, stdout);
    }
  mc_results_destroy (results);

  return success ? 0 : EXIT_FAILURE;
}
