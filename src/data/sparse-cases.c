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

#include <data/sparse-cases.h>

#include <stdlib.h>
#include <string.h>

#include <data/case.h>
#include <data/settings.h>
#include <data/case-tmpfile.h>
#include <libpspp/assertion.h>
#include <libpspp/range-set.h>
#include <libpspp/sparse-array.h>

#include "xalloc.h"

/* A sparse array of cases. */
struct sparse_cases
  {
    size_t column_cnt;                  /* Number of values per case. */
    union value *default_columns;       /* Defaults for unwritten cases. */
    casenumber max_memory_cases;        /* Max cases before dumping to disk. */
    struct sparse_array *memory;        /* Backing, if stored in memory. */
    struct case_tmpfile *disk;          /* Backing, if stored on disk. */
    struct range_set *disk_cases;       /* Allocated cases, if on disk. */
  };

/* Creates and returns a new sparse array of cases with
   COLUMN_CNT values per case. */
struct sparse_cases *
sparse_cases_create (size_t column_cnt)
{
  struct sparse_cases *sc = xmalloc (sizeof *sc);
  sc->column_cnt = column_cnt;
  sc->default_columns = NULL;
  sc->max_memory_cases = settings_get_workspace_cases (column_cnt);
  sc->memory = sparse_array_create (sizeof (struct ccase *));
  sc->disk = NULL;
  sc->disk_cases = NULL;
  return sc;
}

/* Creates and returns a new sparse array of cases that contains
   the same data as OLD. */
struct sparse_cases *
sparse_cases_clone (const struct sparse_cases *old)
{
  struct sparse_cases *new = xmalloc (sizeof *new);

  new->column_cnt = old->column_cnt;

  if (old->default_columns != NULL)
    new->default_columns
      = xmemdup (old->default_columns,
                 old->column_cnt * sizeof *old->default_columns);
  else
    new->default_columns = NULL;

  new->max_memory_cases = old->max_memory_cases;

  if (old->memory != NULL)
    {
      unsigned long int idx;
      struct ccase **cp;

      new->memory = sparse_array_create (sizeof (struct ccase *));
      for (cp = sparse_array_first (old->memory, &idx); cp != NULL;
           cp = sparse_array_next (old->memory, idx, &idx))
        {
          struct ccase **ncp = sparse_array_insert (new->memory, idx);
          *ncp = case_ref (*cp);
        }
    }
  else
    new->memory = NULL;

  if (old->disk != NULL)
    {
      const struct range_set_node *node;

      new->disk = case_tmpfile_create (old->column_cnt);
      new->disk_cases = range_set_create ();
      for (node = range_set_first (old->disk_cases); node != NULL;
           node = range_set_next (old->disk_cases, node))
        {
          unsigned long int start = range_set_node_get_start (node);
          unsigned long int end = range_set_node_get_end (node);
          unsigned long int idx;

          for (idx = start; idx < end; idx++) 
            {
              struct ccase *c = case_tmpfile_get_case (old->disk, idx);
              if (c == NULL || !case_tmpfile_put_case (new->disk, idx, c))
                {
                  sparse_cases_destroy (new);
                  return NULL;
                }
            }
        }
    }
  else
    {
      new->disk = NULL;
      new->disk_cases = NULL;
    }

  return new;
}

/* Destroys sparse array of cases SC. */
void
sparse_cases_destroy (struct sparse_cases *sc)
{
  if (sc != NULL)
    {
      if (sc->memory != NULL)
        {
          unsigned long int idx;
          struct ccase **cp;
          for (cp = sparse_array_first (sc->memory, &idx); cp != NULL;
               cp = sparse_array_next (sc->memory, idx, &idx))
            case_unref (*cp);
          sparse_array_destroy (sc->memory);
        }
      free (sc->default_columns);
      case_tmpfile_destroy (sc->disk);
      range_set_destroy (sc->disk_cases);
      free (sc);
    }
}

/* Returns the number of `union value's in each case in SC. */
size_t
sparse_cases_get_value_cnt (const struct sparse_cases *sc)
{
  return sc->column_cnt;
}

/* Dumps the cases in SC, which must currently be stored in
   memory, to disk.  Returns true if successful, false on I/O
   error. */
static bool
dump_sparse_cases_to_disk (struct sparse_cases *sc)
{
  unsigned long int idx;
  struct ccase **cp;

  assert (sc->memory != NULL);
  assert (sc->disk == NULL);

  sc->disk = case_tmpfile_create (sc->column_cnt);
  sc->disk_cases = range_set_create ();

  for (cp = sparse_array_first (sc->memory, &idx); cp != NULL;
       cp = sparse_array_next (sc->memory, idx, &idx))
    {
      if (!case_tmpfile_put_case (sc->disk, idx, *cp))
        {
          case_tmpfile_destroy (sc->disk);
          sc->disk = NULL;
          range_set_destroy (sc->disk_cases);
          sc->disk_cases = NULL;
          return false;
        }
      range_set_insert (sc->disk_cases, idx, 1);
    }
  sparse_array_destroy (sc->memory);
  sc->memory = NULL;
  return true;
}

/* Returns true if any data has ever been written to ROW in SC,
   false otherwise. */
bool
sparse_cases_contains_row (const struct sparse_cases *sc, casenumber row)
{
  return (sc->memory != NULL
          ? sparse_array_get (sc->memory, row) != NULL
          : range_set_contains (sc->disk_cases, row));
}

/* Reads columns COLUMNS...(COLUMNS + VALUE_CNT), exclusive, in
   the given ROW in SC, into the VALUE_CNT values in VALUES.
   Returns true if successful, false on I/O error. */
bool
sparse_cases_read (struct sparse_cases *sc, casenumber row, size_t column,
                   union value values[], size_t value_cnt)
{
  assert (value_cnt <= sc->column_cnt);
  assert (column + value_cnt <= sc->column_cnt);

  if (sparse_cases_contains_row (sc, row))
    {
      struct ccase *c;
      if (sc->memory != NULL)
        {
          struct ccase **cp = sparse_array_get (sc->memory, row);
          c = case_ref (*cp);
        }
      else
        {
          c = case_tmpfile_get_case (sc->disk, row);
          if (c == NULL)
            return false;
        }
      case_copy_out (c, column, values, value_cnt);
      case_unref (c);
    }
  else
    {
      assert (sc->default_columns != NULL);
      memcpy (values, sc->default_columns + column,
              sizeof *values * value_cnt);
    }

  return true;
}

/* Implements sparse_cases_write for an on-disk sparse_cases. */
static bool
write_disk_case (struct sparse_cases *sc, casenumber row, size_t column,
                 const union value values[], size_t value_cnt)
{
  struct ccase *c;
  bool ok;

  /* Get current case data. */
  if (column == 0 && value_cnt == sc->column_cnt)
    c = case_create (sc->column_cnt);
  else
    {
      c = case_tmpfile_get_case (sc->disk, row);
      if (c == NULL)
        return false;
    }

  /* Copy in new data. */
  case_copy_in (c, column, values, value_cnt);

  /* Write new case. */
  ok = case_tmpfile_put_case (sc->disk, row, c);
  if (ok)
    range_set_insert (sc->disk_cases, row, 1);

  return ok;
}

/* Writes the VALUE_CNT values in VALUES into columns
   COLUMNS...(COLUMNS + VALUE_CNT), exclusive, in the given ROW
   in SC.
   Returns true if successful, false on I/O error. */
bool
sparse_cases_write (struct sparse_cases *sc, casenumber row, size_t column,
                    const union value values[], size_t value_cnt)
{
  if (sc->memory != NULL)
    {
      struct ccase *c, **cp;
      cp = sparse_array_get (sc->memory, row);
      if (cp != NULL)
        c = *cp = case_unshare (*cp);
      else
        {
          if (sparse_array_count (sc->memory) >= sc->max_memory_cases)
            {
              if (!dump_sparse_cases_to_disk (sc))
                return false;
              return write_disk_case (sc, row, column, values, value_cnt);
            }

          cp = sparse_array_insert (sc->memory, row);
          c = *cp = case_create (sc->column_cnt);
          if (sc->default_columns != NULL
              && (column != 0 || value_cnt != sc->column_cnt))
            case_copy_in (c, 0, sc->default_columns, sc->column_cnt);
        }
      case_copy_in (c, column, values, value_cnt);
      return true;
    }
  else
    return write_disk_case (sc, row, column, values, value_cnt);
}

/* Writes the VALUE_CNT values in VALUES to columns
   START_COLUMN...(START_COLUMN + VALUE_CNT), exclusive, in every
   row in SC, even those rows that have not yet been written.
   Returns true if successful, false on I/O error.

   The runtime of this function is linear in the number of rows
   in SC that have already been written. */
bool
sparse_cases_write_columns (struct sparse_cases *sc, size_t start_column,
                            const union value values[], size_t value_cnt)
{
  assert (value_cnt <= sc->column_cnt);
  assert (start_column + value_cnt <= sc->column_cnt);

  /* Set defaults. */
  if (sc->default_columns == NULL)
    sc->default_columns = xnmalloc (sc->column_cnt,
                                    sizeof *sc->default_columns);
  memcpy (sc->default_columns + start_column, values,
          value_cnt * sizeof *sc->default_columns);

  /* Set individual rows. */
  if (sc->memory != NULL)
    {
      struct ccase **cp;
      unsigned long int idx;

      for (cp = sparse_array_first (sc->memory, &idx); cp != NULL;
           cp = sparse_array_next (sc->memory, idx, &idx))
        {
          *cp = case_unshare (*cp);
          case_copy_in (*cp, start_column, values, value_cnt);
        }
    }
  else
    {
      const struct range_set_node *node;

      for (node = range_set_first (sc->disk_cases); node != NULL;
           node = range_set_next (sc->disk_cases, node))
        {
          unsigned long int start = range_set_node_get_start (node);
          unsigned long int end = range_set_node_get_end (node);
          unsigned long int row;

          for (row = start; row < end; row++)
            case_tmpfile_put_values (sc->disk, row,
                                     start_column, values, value_cnt);
        }

      if (case_tmpfile_error (sc->disk))
        return false;
    }
  return true;
}
