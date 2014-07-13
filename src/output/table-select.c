/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2014 Free Software Foundation, Inc.

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

struct table_select
  {
    struct table table;
    struct table *subtable;
    int ofs[2];
  };

static const struct table_class table_select_class;

static struct table_select *
table_select_cast (const struct table *table)
{
  assert (table->klass == &table_select_class);
  return UP_CAST (table, struct table_select, table);
}

/* Takes ownership of SUBTABLE and returns a new table whose contents are the
   rectangular subregion of SUBTABLE that contains rows RECT[TABLE_VERT][0]
   through RECT[TABLE_VERT][1], exclusive, and columns RECT[TABLE_HORZ][0]
   through RECT[TABLE_HORZ][1]. */
struct table *
table_select (struct table *subtable, int rect[TABLE_N_AXES][2])
{
  struct table_select *ts;
  int axis;

  if (rect[TABLE_HORZ][0] == 0
      && rect[TABLE_HORZ][1] == subtable->n[TABLE_HORZ]
      && rect[TABLE_VERT][0] == 0
      && rect[TABLE_VERT][1] == subtable->n[TABLE_VERT])
    return subtable;

  if (!table_is_shared (subtable) && subtable->klass->select != NULL)
    {
      struct table *selected = subtable->klass->select (subtable, rect);
      if (selected != NULL)
        return selected;
    }

  ts = xmalloc (sizeof *ts);
  table_init (&ts->table, &table_select_class);
  ts->subtable = subtable;
  for (axis = 0; axis < TABLE_N_AXES; axis++)
    {
      int h1;
      ts->ofs[axis] = rect[axis][0];
      ts->table.n[axis] = rect[axis][1] - rect[axis][0];
      if (subtable->h[axis][0] > rect[axis][0])
        ts->table.h[axis][0] = subtable->h[axis][0] - rect[axis][0];
      h1 = subtable->n[axis] - subtable->h[axis][1];
      if (h1 < rect[axis][1])
        ts->table.h[axis][1] = rect[axis][1] - h1;
    }
  return &ts->table;
}

/* Takes ownership of TABLE and returns a new table whose contents are:

        - If AXIS is TABLE_HORZ, columns Z0 through Z1 (exclusive) of SUBTABLE.
          If ADD_HEADERS is true, the returned table also includes any header
          columns in SUBTABLE.

        - If AXIS is TABLE_VERT, rows Z0 through Z1 (exclusive) of SUBTABLE.
          If ADD_HEADERS is true, the returned table also includes any header
          rows in SUBTABLE. */
struct table *
table_select_slice (struct table *subtable, enum table_axis axis,
                    int z0, int z1, bool add_headers)
{
  struct table *table;
  int rect[TABLE_N_AXES][2];
  bool h0, h1;

  h0 = add_headers && subtable->h[axis][0] > 0;
  if (h0 && z0 == subtable->h[axis][0])
    {
      z0 = 0;
      h0 = false;
    }

  h1 = add_headers && subtable->h[axis][1] > 0;
  if (h1 && z1 == subtable->n[axis] - subtable->h[axis][1])
    {
      z1 = subtable->n[axis];
      h1 = false;
    }

  if (z0 == 0 && z1 == subtable->n[axis])
    return subtable;

  if (h0)
    table_ref (subtable);
  if (h1)
    table_ref (subtable);

  rect[TABLE_HORZ][0] = 0;
  rect[TABLE_VERT][0] = 0;
  rect[TABLE_HORZ][1] = subtable->n[TABLE_HORZ];
  rect[TABLE_VERT][1] = subtable->n[TABLE_VERT];
  rect[axis][0] = z0;
  rect[axis][1] = z1;
  table = table_select (subtable, rect);

  if (h0)
    table = table_paste (
      table_select_slice (subtable, axis, 0, subtable->h[axis][0], false),
      table, axis);

  if (h1)
    table = table_paste (
      table,
      table_select_slice (subtable, axis,
                          subtable->n[axis] - subtable->h[axis][1],
                          subtable->n[axis], false),
      axis);

  return table;
}

/* Takes ownership of TABLE and returns a new table whose contents are columns
   X0 through X1 (exclusive) of SUBTABLE.  If ADD_HEADERS is true, the
   returned table also includes any header columns in SUBTABLE. */
struct table *
table_select_columns (struct table *subtable, int x0, int x1,
                      bool add_headers)
{
  return table_select_slice (subtable, TABLE_HORZ, x0, x1, add_headers);
}

/* Takes ownership of TABLE and returns a new table whose contents are rows Y0
   through Y1 (exclusive) of SUBTABLE.  If ADD_HEADERS is true, the returned
   table also includes any header rows in SUBTABLE. */
struct table *
table_select_rows (struct table *subtable, int y0, int y1,
                   bool add_headers)
{
  return table_select_slice (subtable, TABLE_VERT, y0, y1, add_headers);
}

static void
table_select_destroy (struct table *ti)
{
  struct table_select *ts = table_select_cast (ti);
  table_unref (ts->subtable);
  free (ts);
}

static void
table_select_get_cell (const struct table *ti, int x, int y,
                       struct table_cell *cell)
{
  struct table_select *ts = table_select_cast (ti);
  int axis;

  table_get_cell (ts->subtable,
                  x + ts->ofs[TABLE_HORZ],
                  y + ts->ofs[TABLE_VERT], cell);

  for (axis = 0; axis < TABLE_N_AXES; axis++)
    {
      int *d = cell->d[axis];
      int ofs = ts->ofs[axis];

      d[0] = MAX (d[0] - ofs, 0);
      d[1] = MIN (d[1] - ofs, ti->n[axis]);
    }
}

static int
table_select_get_rule (const struct table *ti,
                       enum table_axis axis,
                       int x, int y)
{
  struct table_select *ts = table_select_cast (ti);
  return table_get_rule (ts->subtable, axis,
                         x + ts->ofs[TABLE_HORZ],
                         y + ts->ofs[TABLE_VERT]);
}

static struct table *
table_select_select (struct table *ti, int rect[TABLE_N_AXES][2])
{
  struct table_select *ts = table_select_cast (ti);
  int axis;

  for (axis = 0; axis < TABLE_N_AXES; axis++)
    {
      int h1;

      if (ts->table.h[axis][0] > rect[axis][0])
        ts->table.h[axis][0] = ts->table.h[axis][0] - rect[axis][0];
      else
        ts->table.h[axis][0] = 0;

      h1 = ts->table.n[axis] - ts->table.h[axis][1];
      if (h1 < rect[axis][1])
        ts->table.h[axis][1] = rect[axis][1] - h1;
      else
        ts->table.h[axis][1] = 0;

      ts->ofs[axis] += rect[axis][0];
      ts->table.n[axis] = rect[axis][1] - rect[axis][0];
    }
  return ti;
}

static const struct table_class table_select_class =
  {
    table_select_destroy,
    table_select_get_cell,
    table_select_get_rule,
    NULL,                       /* paste */
    table_select_select,
  };
