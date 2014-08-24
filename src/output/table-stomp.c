/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011, 2013, 2014 Free Software Foundation, Inc.

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

#include <string.h>

#include "libpspp/assertion.h"
#include "libpspp/tower.h"
#include "output/table-provider.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

/* This file uses TABLE_HORZ and TABLE_VERT enough to warrant abbreviating. */
#define H TABLE_HORZ
#define V TABLE_VERT

struct table_stomp
  {
    struct table table;
    struct table *subtable;
  };

static const struct table_class table_stomp_class;

static struct table_stomp *
table_stomp_cast (const struct table *table)
{
  assert (table->klass == &table_stomp_class);
  return UP_CAST (table, struct table_stomp, table);
}

/* Returns a new table based on SUBTABLE with exactly one row.  Each cell in
   that row consists of the contents of all of the rows stacked together into a
   single cell.  So, for example, if SUBTABLE has one column and three rows,
   then the returned table has one column and one row, and the single cell in
   the returned table has all of the content of the three cells in
   SUBTABLE.

   SUBTABLE should have the same column structure in every row, i.e. don't
   stomp a table that has rows with differently joined cells. */
struct table *
table_stomp (struct table *subtable)
{
  struct table_stomp *ts;

  if (subtable->n[V] == 1)
    return subtable;

  ts = xmalloc (sizeof *ts);
  table_init (&ts->table, &table_stomp_class);
  ts->table.n[H] = subtable->n[H];
  ts->table.n[V] = 1;
  ts->subtable = subtable;
  return &ts->table;
}

static void
table_stomp_destroy (struct table *t)
{
  struct table_stomp *ts = table_stomp_cast (t);

  table_unref (ts->subtable);
  free (ts);
}

struct table_stomp_subcells
  {
    struct cell_contents *contents;

    size_t n_subcells;
    struct table_cell subcells[];
  };

static void
table_stomp_free_cell (void *sc_)
{
  struct table_stomp_subcells *sc = sc_;
  size_t i;

  for (i = 0; i < sc->n_subcells; i++)
    table_cell_free (&sc->subcells[i]);
  free (sc->contents);
  free (sc);
}

static void
table_stomp_get_cell (const struct table *t, int x, int y UNUSED,
                      struct table_cell *cell)
{
  struct table_stomp *ts = table_stomp_cast (t);
  size_t n_rows = ts->subtable->n[V];
  struct table_stomp_subcells *sc;
  size_t row;
  size_t ofs;
  size_t i;

  sc = xzalloc (sizeof *sc + n_rows * sizeof *sc->subcells);
  sc->n_subcells = 0;

  cell->n_contents = 0;
  for (row = 0; row < n_rows; )
    {
      struct table_cell *subcell = &sc->subcells[sc->n_subcells++];

      table_get_cell (ts->subtable, x, row, subcell);
      cell->n_contents += subcell->n_contents;
      row = subcell->d[V][1];
    }

  cell->d[H][0] = sc->subcells[0].d[H][0];
  cell->d[V][0] = 0;
  cell->d[H][1] = sc->subcells[0].d[H][1];
  cell->d[V][1] = 1;

  sc->contents = xmalloc (cell->n_contents * sizeof *cell->contents);
  cell->contents = sc->contents;

  ofs = 0;
  for (i = 0; i < sc->n_subcells; i++)
    {
      struct table_cell *subcell = &sc->subcells[i];

      memcpy (&sc->contents[ofs], subcell->contents,
              subcell->n_contents * sizeof *subcell->contents);
      ofs += subcell->n_contents;
    }

  cell->destructor = table_stomp_free_cell;
  cell->destructor_aux = sc;
}

static int
table_stomp_get_rule (const struct table *t,
                      enum table_axis axis, int x, int y)
{
  struct table_stomp *ts = table_stomp_cast (t);

  return table_get_rule (ts->subtable, axis, x,
                         axis == H || y == 0 ? y : ts->subtable->n[V]);
}

static const struct table_class table_stomp_class =
  {
    table_stomp_destroy,
    table_stomp_get_cell,
    table_stomp_get_rule,
    NULL,                       /* paste */
    NULL,                       /* select */
  };
