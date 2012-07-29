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

/* This is a test program for the sparse array routines defined
   in sparse-xarray.c. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpspp/argv-parser.h>
#include <libpspp/assertion.h>
#include <libpspp/hash-functions.h>
#include <libpspp/model-checker.h>
#include <libpspp/sparse-xarray.h>
#include <libpspp/str.h>

#include "minmax.h"
#include "progname.h"
#include "xalloc.h"

/* Maximum size of sparse_xarray supported for model checking
   purposes. */
#define MAX_ROWS 5
#define MAX_COLS 5

/* Test parameters. */
struct test_params
  {
    /* Controlling the test state space. */
    int n_columns;              /* Number of columns in each row. */
    int max_rows;               /* Maximum number of rows. */
    int max_memory_rows;        /* Max rows before writing to disk. */
    unsigned char n_values;     /* Number of unique cell values. */
    int n_xarrays;              /* Number of sparse_xarrays in state. */

    /* Types of operations to perform. */
    bool write_cells;           /* Write to individual cells. */
    bool write_rows;            /* Write whole rows. */
    bool write_columns;         /* Write whole columns. */
    bool copy_within_xarray;    /* Copy column ranges in a single xarray. */
  };

struct test_state
  {
    struct sparse_xarray *xarrays[2];
  };

static void
test_state_destroy (const struct test_params *params, struct test_state *ts)
{
  int i;

  for (i = 0; i < params->n_xarrays; i++)
    sparse_xarray_destroy (ts->xarrays[i]);
  free (ts);
}

static struct test_state *
test_state_clone (const struct test_params *params,
                  const struct test_state *ots)
{
  struct test_state *ts;
  int i;

  ts = xmalloc (sizeof *ts);
  for (i = 0; i < params->n_xarrays; i++)
    {
      ts->xarrays[i] = sparse_xarray_clone (ots->xarrays[i]);
      if (ts->xarrays[i] == NULL)
        NOT_REACHED ();
    }
  return ts;
}

struct xarray_model
  {
    uint8_t data[MAX_ROWS][MAX_COLS];
    bool contains_row[MAX_ROWS];
  };

struct test_model
  {
    struct xarray_model models[2];
  };

/* Extracts the contents of TS into TM. */
static void
test_model_extract (const struct test_params *params,
                    const struct test_state *ts, struct test_model *tm)
{
  int i;

  for (i = 0; i < params->n_xarrays; i++)
    {
      const struct sparse_xarray *sx = ts->xarrays[i];
      struct xarray_model *model = &tm->models[i];
      size_t n_columns = sparse_xarray_get_n_columns (sx);
      size_t n_rows = sparse_xarray_get_n_rows (sx);
      size_t row;

      assert (n_rows < MAX_ROWS);
      assert (n_columns < MAX_COLS);
      for (row = 0; row < params->max_rows; row++)
        {
          model->contains_row[row] = sparse_xarray_contains_row (sx, row);
          if (!sparse_xarray_read (sx, row, 0, n_columns, model->data[row]))
            NOT_REACHED ();
        }
    }
}

/* Checks that test state TS matches the test model TM and
   reports any mismatches via mc_error.  Then, adds SX to MC as a
   new state. */
static void
check_state (struct mc *mc, struct test_state *ts, const struct test_model *tm)
{
  const struct test_params *params = mc_get_aux (mc);
  int n_columns = params->n_columns;
  unsigned int hash;
  int i;

  for (i = 0; i < params->n_xarrays; i++)
    {
      const struct xarray_model *model = &tm->models[i];
      const struct sparse_xarray *sx = ts->xarrays[i];
      bool difference;
      int row, col;
      int n_rows;

      assert (n_columns < MAX_COLS);

      /* Check row count. */
      n_rows = 0;
      for (row = 0; row < params->max_rows; row++)
        if (model->contains_row[row])
          n_rows = row + 1;
      if (n_rows != sparse_xarray_get_n_rows (sx))
        mc_error (mc, "xarray %d: row count (%zu) does not match expected "
                  "(%d)", i, sparse_xarray_get_n_rows (sx), n_rows);

      /* Check row containment. */
      for (row = 0; row < params->max_rows; row++)
        {
          bool contains = sparse_xarray_contains_row (sx, row);
          if (contains && !model->contains_row[row])
            mc_error (mc, "xarray %d: row %d is contained by sparse_xarray "
                      "but should not be", i, row);
          else if (!contains && model->contains_row[row])
            mc_error (mc, "xarray %d: row %d is not contained by "
                      "sparse_xarray but should be", i, row);
        }

      /* Check contents. */
      difference = false;
      for (row = 0; row < params->max_rows; row++)
        {
          unsigned char data[MAX_COLS];

          if (!sparse_xarray_read (sx, row, 0, n_columns, data))
            NOT_REACHED ();
          for (col = 0; col < params->n_columns; col++)
            if (data[col] != model->data[row][col])
              {
                mc_error (mc, "xarray %d: element %d,%d (of %d,%d) "
                          "differs: %d should be %d",
                          i, row, col, n_rows, n_columns, data[col],
                          model->data[row][col]);
                difference = true;
              }
        }

      if (difference)
        {
          struct string ds;

          mc_error (mc, "xarray %d: expected:", i);
          ds_init_empty (&ds);
          for (row = 0; row < params->max_rows; row++)
            {
              ds_clear (&ds);
              for (col = 0; col < n_columns; col++)
                ds_put_format (&ds, " %d", model->data[row][col]);
              mc_error (mc, "xarray %d: row %d:%s", i, row, ds_cstr (&ds));
            }

          mc_error (mc, "xarray %d: actual:", i);
          ds_init_empty (&ds);
          for (row = 0; row < params->max_rows; row++)
            {
              unsigned char data[MAX_COLS];

              if (!sparse_xarray_read (sx, row, 0, n_columns, data))
                NOT_REACHED ();

              ds_clear (&ds);
              for (col = 0; col < n_columns; col++)
                ds_put_format (&ds, " %d", data[col]);
              mc_error (mc, "xarray %d: row %d:%s", i, row, ds_cstr (&ds));
            }

          ds_destroy (&ds);
        }
    }

  hash = 0;
  for (i = 0; i < params->n_xarrays; i++)
    hash = sparse_xarray_model_checker_hash (ts->xarrays[i], hash);
  if (mc_discard_dup_state (mc, hash))
    test_state_destroy (params, ts);
  else
    mc_add_state (mc, ts);
}

static bool
next_data (unsigned char *data, int n, int n_values)
{
  int i;
  for (i = n - 1; i >= 0; i--)
    {
      data[i]++;
      if (data[i] < n_values)
        return true;
      data[i] = 0;
    }
  return false;
}

struct copy_columns_params
  {
    int n;                      /* Number of columns to copy. */
    int src;                    /* Offset of first source column. */
    int dst;                    /* Offset of first destination column. */
  };

static bool
copy_columns (const void *src_, void *dst_, void *copy_)
{
  const struct copy_columns_params *copy = copy_;
  const uint8_t *src = src_;
  uint8_t *dst = dst_;

  memmove (dst + copy->dst, src + copy->src, copy->n);
  return true;
}

/* "init" function for struct mc_class. */
static void
sparse_xarray_mc_init (struct mc *mc)
{
  struct test_params *params = mc_get_aux (mc);
  struct test_state *ts;
  struct test_model tm;
  int i;

  mc_name_operation (mc, "empty sparse_xarray with n_columns=%d, "
                     "max_memory_rows=%d",
                     params->n_columns, params->max_memory_rows);
  ts = xmalloc (sizeof *ts);
  for (i = 0; i < params->n_xarrays; i++)
    ts->xarrays[i] = sparse_xarray_create (params->n_columns,
                                           params->max_memory_rows);
  memset (&tm, 0, sizeof tm);
  check_state (mc, ts, &tm);
}

/* "mutate" function for struct mc_class. */
static void
sparse_xarray_mc_mutate (struct mc *mc, const void *ots_)
{
  struct test_params *params = mc_get_aux (mc);
  size_t n_columns = params->n_columns;
  const struct test_state *ots = ots_;
  struct test_model otm;
  int i;

  test_model_extract (params, ots, &otm);
  for (i = 0; i < params->n_xarrays; i++)
    {
      unsigned char value;
      int row, col, n, src, dst;

      /* Write all possible values to each possible single cell. */
      if (params->write_cells)
        for (row = 0; row < params->max_rows; row++)
          for (col = 0; col < n_columns; col++)
            for (value = 0; value < params->n_values; value++)
              if (mc_include_state (mc))
                {
                  struct test_state *ts = test_state_clone (params, ots);
                  struct sparse_xarray *sx = ts->xarrays[i];
                  struct test_model tm = otm;
                  struct xarray_model *model = &tm.models[i];

                  mc_name_operation (mc, "xarray %d: set (%d,%d) to %d",
                                     i, row, col, value);
                  if (!sparse_xarray_write (sx, row, col, 1, &value))
                    NOT_REACHED ();
                  model->data[row][col] = value;
                  model->contains_row[row] = true;
                  check_state (mc, ts, &tm);
                }

      /* Write all possible row contents to each row. */
      if (params->write_rows)
        for (row = 0; row < params->max_rows; row++)
          {
            struct test_model tm = otm;
            struct xarray_model *model = &tm.models[i];

            memset (model->data[row], 0, n_columns);
            model->contains_row[row] = true;
            do
              {
                if (mc_include_state (mc))
                  {
                    struct test_state *ts = test_state_clone (params, ots);
                    struct sparse_xarray *sx = ts->xarrays[i];
                    char row_string[MAX_COLS + 1];

                    mc_name_operation (mc, "xarray %d: set row %d to %s",
                                       i, row, row_string);
                    for (col = 0; col < n_columns; col++)
                      {
                        value = model->data[row][col];
                        row_string[col] = value < 10 ? '0' + value : '*';
                      }
                    row_string[n_columns] = '\0';
                    if (!sparse_xarray_write (sx, row, 0, n_columns,
                                              model->data[row]))
                      NOT_REACHED ();
                    check_state (mc, ts, &tm);
                  }
              }
            while (next_data (model->data[row], n_columns, params->n_values));
          }

      /* Write all possible values to each possible column. */
      if (params->write_columns)
        for (col = 0; col < n_columns; col++)
          for (value = 0; value < params->n_values; value++)
            if (mc_include_state (mc))
              {
                struct test_state *ts = test_state_clone (params, ots);
                struct sparse_xarray *sx = ts->xarrays[i];
                struct test_model tm = otm;
                struct xarray_model *model = &tm.models[i];

                mc_name_operation (mc, "xarray %d: write value %d to "
                                   "column %d", i, value, col);
                if (!sparse_xarray_write_columns (sx, col, 1, &value))
                  NOT_REACHED ();
                for (row = 0; row < params->max_rows; row++)
                  model->data[row][col] = value;
                check_state (mc, ts, &tm);
              }

      /* Copy all possible column ranges within a single sparse_xarray. */
      if (params->copy_within_xarray)
        for (n = 1; n <= n_columns; n++)
          for (src = 0; src <= n_columns - n; src++)
            for (dst = 0; dst <= n_columns - n; dst++)
              if (mc_include_state (mc))
                {
                  struct copy_columns_params copy_aux;
                  struct test_state *ts = test_state_clone (params, ots);
                  struct sparse_xarray *sx = ts->xarrays[i];
                  struct test_model tm = otm;
                  struct xarray_model *model = &tm.models[i];

                  mc_name_operation (mc, "xarray %d: copy %d columns from "
                                     "offset %d to offset %d", i, n, src, dst);

                  copy_aux.n = n;
                  copy_aux.src = src;
                  copy_aux.dst = dst;
                  if (!sparse_xarray_copy (sx, sx, copy_columns, &copy_aux))
                    NOT_REACHED ();

                  for (row = 0; row < params->max_rows; row++)
                    memmove (&model->data[row][dst],
                             &model->data[row][src], n);

                  check_state (mc, ts, &tm);
                }
    }

  if (params->n_xarrays == 2)
    {
      int row, n, src, dst;

      /* Copy all possible column ranges from xarrays[0] to xarrays[1]. */
      for (n = 1; n <= n_columns; n++)
        for (src = 0; src <= n_columns - n; src++)
          for (dst = 0; dst <= n_columns - n; dst++)
            if (mc_include_state (mc))
              {
                struct copy_columns_params copy_aux;
                struct test_state *ts = test_state_clone (params, ots);
                struct test_model tm = otm;

                mc_name_operation (mc, "copy %d columns from offset %d in "
                                   "xarray 0 to offset %d in xarray 1",
                                   n, src, dst);

                copy_aux.n = n;
                copy_aux.src = src;
                copy_aux.dst = dst;
                if (!sparse_xarray_copy (ts->xarrays[0], ts->xarrays[1],
                                         copy_columns, &copy_aux))
                  NOT_REACHED ();

                for (row = 0; row < params->max_rows; row++)
                  {
                    if (tm.models[0].contains_row[row])
                      tm.models[1].contains_row[row] = true;
                    memmove (&tm.models[1].data[row][dst],
                             &tm.models[0].data[row][src], n);
                  }

                check_state (mc, ts, &tm);
              }
    }
}

/* "destroy" function for struct mc_class. */
static void
sparse_xarray_mc_destroy (const struct mc *mc UNUSED, void *ts_)
{
  struct test_params *params = mc_get_aux (mc);
  struct test_state *ts = ts_;

  test_state_destroy (params, ts);
}

static void
usage (void)
{
  printf ("%s, for testing the sparse_xarray implementation.\n"
          "Usage: %s [OPTION]...\n"
          "\nTest state space parameters (min...max, default):\n"
          "  --columns=N          Number of columns per row (0...5, 3)\n"
          "  --max-rows=N         Maximum number of rows (0...5, 3)\n"
          "  --max-memory-rows=N  Max rows before paging to disk (0...5, 3)\n"
          "  --values=N           Number of unique cell values (1...254, 3)\n"
          "  --xarrays=N          Number of xarrays at a time (1...2, 1)\n"
          "\nTest operation parameters:\n"
          "  --no-write-cells     Do not write individual cells\n"
          "  --no-write-rows      Do not write whole rows\n"
          "  --no-write-columns   Do not write whole columns\n"
          "  --no-copy-columns    Do not copy column ranges in an xarray\n",
          program_name, program_name);
  mc_options_usage ();
  fputs ("\nOther options:\n"
         "  --help               Display this help message\n"
         "\nReport bugs to <bug-gnu-pspp@gnu.org>\n",
         stdout);
  exit (0);
}

enum
  {
    OPT_COLUMNS,
    OPT_MAX_ROWS,
    OPT_MAX_MEMORY_ROWS,
    OPT_VALUES,
    OPT_XARRAYS,
    OPT_NO_WRITE_CELLS,
    OPT_NO_WRITE_ROWS,
    OPT_NO_WRITE_COLUMNS,
    OPT_NO_COPY_COLUMNS,
    OPT_HELP,
    N_SPARSE_XARRAY_OPTIONS
  };

static struct argv_option sparse_xarray_argv_options[N_SPARSE_XARRAY_OPTIONS] =
  {
    {"columns", 0, required_argument, OPT_COLUMNS},
    {"max-rows", 0, required_argument, OPT_MAX_ROWS},
    {"max-memory-rows", 0, required_argument, OPT_MAX_MEMORY_ROWS},
    {"values", 0, required_argument, OPT_VALUES},
    {"xarrays", 0, required_argument, OPT_XARRAYS},
    {"no-write-cells", 0, no_argument, OPT_NO_WRITE_CELLS},
    {"no-write-rows", 0, no_argument, OPT_NO_WRITE_ROWS},
    {"no-write-columns", 0, no_argument, OPT_NO_WRITE_COLUMNS},
    {"no-copy-columns", 0, no_argument, OPT_NO_COPY_COLUMNS},
    {"help", 'h', no_argument, OPT_HELP},
  };

static void
sparse_xarray_option_callback (int id, void *params_)
{
  struct test_params *params = params_;
  switch (id)
    {
    case OPT_COLUMNS:
      params->n_columns = atoi (optarg);
      break;

    case OPT_MAX_ROWS:
      params->max_rows = atoi (optarg);
      break;

    case OPT_MAX_MEMORY_ROWS:
      params->max_memory_rows = atoi (optarg);
      break;

    case OPT_VALUES:
      params->n_values = atoi (optarg);
      break;

    case OPT_XARRAYS:
      params->n_xarrays = atoi (optarg);
      break;

    case OPT_NO_WRITE_CELLS:
      params->write_cells = false;
      break;

    case OPT_NO_WRITE_ROWS:
      params->write_rows = false;
      break;

    case OPT_NO_WRITE_COLUMNS:
      params->write_columns = false;
      break;

    case OPT_NO_COPY_COLUMNS:
      params->copy_within_xarray = false;
      break;

    case OPT_HELP:
      usage ();
      break;

    default:
      NOT_REACHED ();
    }
}

int
main (int argc, char *argv[])
{
  static const struct mc_class sparse_xarray_mc_class =
    {
      sparse_xarray_mc_init,
      sparse_xarray_mc_mutate,
      sparse_xarray_mc_destroy,
    };

  struct test_params params;
  struct mc_options *options;
  struct mc_results *results;
  struct argv_parser *parser;
  int verbosity;
  bool success;

  set_program_name (argv[0]);

  /* Default parameters. */
  params.n_columns = 3;
  params.max_rows = 3;
  params.max_memory_rows = 3;
  params.n_values = 3;
  params.n_xarrays = 1;
  params.write_cells = true;
  params.write_rows = true;
  params.write_columns = true;
  params.copy_within_xarray = true;

  /* Parse command line. */
  parser = argv_parser_create ();
  options = mc_options_create ();
  mc_options_register_argv_parser (options, parser);
  argv_parser_add_options (parser, sparse_xarray_argv_options,
                           N_SPARSE_XARRAY_OPTIONS,
                           sparse_xarray_option_callback, &params);
  if (!argv_parser_run (parser, argc, argv))
    exit (EXIT_FAILURE);
  argv_parser_destroy (parser);
  verbosity = mc_options_get_verbosity (options);

  /* Force parameters into allowed ranges. */
  params.n_columns = MAX (0, MIN (params.n_columns, MAX_COLS));
  params.max_rows = MAX (0, MIN (params.max_rows, MAX_ROWS));
  params.max_memory_rows = MAX (0, MIN (params.max_memory_rows,
                                        params.max_rows));
  params.n_values = MIN (254, MAX (1, params.n_values));
  params.n_xarrays = MAX (1, MIN (2, params.n_xarrays));
  mc_options_set_aux (options, &params);
  results = mc_run (&sparse_xarray_mc_class, options);

  /* Output results. */
  success = (mc_results_get_stop_reason (results) != MC_MAX_ERROR_COUNT
             && mc_results_get_stop_reason (results) != MC_INTERRUPTED);
  if (verbosity > 0 || !success)
    {
      printf ("Parameters: "
              "--columns=%d --max-rows=%d --max-memory-rows=%d --values=%d "
              "--xarrays=%d",
              params.n_columns, params.max_rows, params.max_memory_rows,
              params.n_values, params.n_xarrays);
      if (!params.write_cells)
        printf (" --no-write-cells");
      if (!params.write_rows)
        printf (" --no-write-rows");
      if (!params.write_columns)
        printf (" --no-write-columns");
      if (!params.copy_within_xarray)
        printf (" --no-copy-columns");
      printf ("\n\n");
      mc_results_print (results, stdout);
    }
  mc_results_destroy (results);

  return success ? 0 : EXIT_FAILURE;
}
