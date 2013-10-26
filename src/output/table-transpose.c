/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011 Free Software Foundation, Inc.

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

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "output/table-provider.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

struct table_transpose
  {
    struct table table;
    struct table *subtable;
  };

static const struct table_class table_transpose_class;

static struct table_transpose *
table_transpose_cast (const struct table *table)
{
  assert (table->klass == &table_transpose_class);
  return UP_CAST (table, struct table_transpose, table);
}

/* Takes ownership of SUBTABLE and returns a new table whose contents are
   SUBTABLE with rows and columns transposed. */
struct table *
table_transpose (struct table *subtable)
{
  if (subtable->n[TABLE_HORZ] == subtable->n[TABLE_VERT]
      && subtable->n[TABLE_HORZ] <= 1)
    return subtable;
  else if (subtable->klass == &table_transpose_class)
    {
      struct table_transpose *tt = table_transpose_cast (subtable);
      struct table *table = table_ref (tt->subtable);
      table_unref (subtable);
      return table;
    }
  else
    {
      struct table_transpose *tt;
      int axis;

      tt = xmalloc (sizeof *tt);
      table_init (&tt->table, &table_transpose_class);
      tt->subtable = subtable;

      for (axis = 0; axis < TABLE_N_AXES; axis++)
        {
          tt->table.n[axis] = subtable->n[!axis];
          tt->table.h[axis][0] = subtable->h[!axis][0];
          tt->table.h[axis][1] = subtable->h[!axis][1];
        }
      return &tt->table;
    }
}

static void
table_transpose_destroy (struct table *ti)
{
  struct table_transpose *tt = table_transpose_cast (ti);
  table_unref (tt->subtable);
  free (tt);
}

static void
swap (int *x, int *y)
{
  int t = *x;
  *x = *y;
  *y = t;
}

static void
table_transpose_get_cell (const struct table *ti, int x, int y,
                          struct table_cell *cell)
{
  struct table_transpose *tt = table_transpose_cast (ti);
  int i;

  table_get_cell (tt->subtable, y, x, cell);
  for (i = 0; i < 2; i++)
    swap (&cell->d[TABLE_HORZ][i], &cell->d[TABLE_VERT][i]);
}

static int
table_transpose_get_rule (const struct table *ti,
                          enum table_axis axis,
                          int x, int y)
{
  struct table_transpose *tt = table_transpose_cast (ti);
  return table_get_rule (tt->subtable, !axis, y, x);
}

static const struct table_class table_transpose_class =
  {
    table_transpose_destroy,
    table_transpose_get_cell,
    table_transpose_get_rule,
    NULL,                       /* paste */
    NULL,                       /* select */
  };
