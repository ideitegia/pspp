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

#include "output/table.h"
#include "output/table-provider.h"

#include <assert.h>
#include <stdlib.h>

#include "libpspp/cast.h"
#include "libpspp/compiler.h"

#include "gl/xalloc.h"

/* Increases TABLE's reference count, indicating that it has an additional
   owner.  An table that is shared among multiple owners must not be
   modified. */
struct table *
table_ref (const struct table *table_)
{
  struct table *table = CONST_CAST (struct table *, table_);
  table->ref_cnt++;
  return table;
}

/* Decreases TABLE's reference count, indicating that it has one fewer owner.
   If TABLE no longer has any owners, it is freed. */
void
table_unref (struct table *table)
{
  if (table != NULL)
    {
      assert (table->ref_cnt > 0);
      if (--table->ref_cnt == 0)
        table->klass->destroy (table);
    }
}

/* Returns true if TABLE has more than one owner.  A table item that is shared
   among multiple owners must not be modified. */
bool
table_is_shared (const struct table *table)
{
  return table->ref_cnt > 1;
}

/* Sets the number of left header columns in TABLE to HL. */
void
table_set_hl (struct table *table, int hl)
{
  assert (!table_is_shared (table));
  table->h[TABLE_HORZ][0] = hl;
}

/* Sets the number of right header columns in TABLE to HR. */
void
table_set_hr (struct table *table, int hr)
{
  assert (!table_is_shared (table));
  table->h[TABLE_HORZ][1] = hr;
}

/* Sets the number of top header rows in TABLE to HT. */
void
table_set_ht (struct table *table, int ht)
{
  assert (!table_is_shared (table));
  table->h[TABLE_VERT][0] = ht;
}

/* Sets the number of top header rows in TABLE to HB. */
void
table_set_hb (struct table *table, int hb)
{
  assert (!table_is_shared (table));
  table->h[TABLE_VERT][1] = hb;
}

/* Initializes TABLE as a table of the specified CLASS, initially with a
   reference count of 1.

   TABLE initially has 0 rows and columns and no headers.  The table
   implementation should update the numbers of rows and columns.  The table
   implementation (or its client) may update the header rows and columns.

   A table is an abstract class, that is, a plain struct table is not useful on
   its own.  Thus, this function is normally called from the initialization
   function of some subclass of table. */
void
table_init (struct table *table, const struct table_class *class)
{
  table->klass = class;
  table->n[TABLE_HORZ] = table->n[TABLE_VERT] = 0;
  table->h[TABLE_HORZ][0] = table->h[TABLE_HORZ][1] = 0;
  table->h[TABLE_VERT][0] = table->h[TABLE_VERT][1] = 0;
  table->ref_cnt = 1;
}

/* Sets the number of columns in TABLE to NC. */
void
table_set_nc (struct table *table, int nc)
{
  assert (!table_is_shared (table));
  table->n[TABLE_HORZ] = nc;
}

/* Sets the number of rows in TABLE to NR. */
void
table_set_nr (struct table *table, int nr)
{
  assert (!table_is_shared (table));
  table->n[TABLE_VERT] = nr;
}

/* Initializes CELL with the contents of the table cell at column X and row Y
   within TABLE.  When CELL is no longer needed, the caller is responsible for
   freeing it by calling table_cell_free(CELL).

   The caller must ensure that CELL is destroyed before TABLE is unref'ed. */
void
table_get_cell (const struct table *table, int x, int y,
                struct table_cell *cell)
{
  assert (x >= 0 && x < table->n[TABLE_HORZ]);
  assert (y >= 0 && y < table->n[TABLE_VERT]);
  table->klass->get_cell (table, x, y, cell);
}

/* Frees CELL, which should have been initialized by calling
   table_get_cell(). */
void
table_cell_free (struct table_cell *cell)
{
  if (cell->destructor != NULL)
    cell->destructor (cell->destructor_aux);
}

/* Returns one of the TAL_* enumeration constants (declared in output/table.h)
   representing a rule running alongside one of the cells in TABLE.

   Suppose NC is the number of columns in TABLE and NR is the number of rows.
   Then, if AXIS is TABLE_HORZ, then 0 <= X <= NC and 0 <= Y < NR.  If (X,Y) =
   (0,0), the return value is the rule that runs vertically on the left side of
   cell (0,0); if (X,Y) = (1,0), it is the vertical rule between that cell and
   cell (1,0); and so on, up to (NC,0), which runs vertically on the right of
   cell (NC-1,0).

   The following diagram illustrates the meaning of (X,Y) for AXIS = TABLE_HORZ
   within a 7x7 table.  The '|' characters at the intersection of the X labels
   and Y labels show the rule whose style would be returned by calling
   table_get_rule with those X and Y values:

                           0  1  2  3  4  5  6  7
                           +--+--+--+--+--+--+--+
                         0 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+
                         1 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+
                         2 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+
                         3 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+
                         4 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+
                         5 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+
                         6 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+

   Similarly, if AXIS is TABLE_VERT, then 0 <= X < NC and 0 <= Y <= NR.  If
   (X,Y) = (0,0), the return value is the rule that runs horizontally above
   the top of cell (0,0); if (X,Y) = (0,1), it is the horizontal rule
   between that cell and cell (0,1); and so on, up to (0,NR), which runs
   horizontally below cell (0,NR-1). */
int
table_get_rule (const struct table *table, enum table_axis axis, int x, int y)
{
  assert (x >= 0 && x < table->n[TABLE_HORZ] + (axis == TABLE_HORZ));
  assert (y >= 0 && y < table->n[TABLE_VERT] + (axis == TABLE_VERT));
  return table->klass->get_rule (table, axis, x, y);
}

struct table_unshared
  {
    struct table table;
    struct table *subtable;
  };

static const struct table_class table_unshared_class;

/* Takes ownership of TABLE and returns a table with the same contents but
   which is guaranteed not to be shared (as returned by table_is_shared()).

   If TABLE is unshared, just returns TABLE.

   The only real use for this function is to create a copy of TABLE in which
   the headers can be adjusted, which is a pretty specialized use case. */
struct table *
table_unshare (struct table *table)
{
  if (!table_is_shared (table))
    return table;
  else
    {
      struct table_unshared *tiu = xmalloc (sizeof *tiu);
      table_init (&tiu->table, &table_unshared_class);
      table_set_nc (&tiu->table, table_nc (table));
      table_set_nr (&tiu->table, table_nr (table));
      table_set_hl (&tiu->table, table_hl (table));
      table_set_hr (&tiu->table, table_hr (table));
      table_set_ht (&tiu->table, table_ht (table));
      table_set_hb (&tiu->table, table_hb (table));
      tiu->subtable = table;
      return &tiu->table;
    }
}

static struct table_unshared *
table_unshared_cast (const struct table *table)
{
  assert (table->klass == &table_unshared_class);
  return UP_CAST (table, struct table_unshared, table);
}

static void
table_unshared_destroy (struct table *tiu_)
{
  struct table_unshared *tiu = table_unshared_cast (tiu_);
  table_unref (tiu->subtable);
  free (tiu);
}

static void
table_unshared_get_cell (const struct table *tiu_, int x, int y,
                              struct table_cell *cell)
{
  struct table_unshared *tiu = table_unshared_cast (tiu_);
  table_get_cell (tiu->subtable, x, y, cell);
}

static int
table_unshared_get_rule (const struct table *tiu_,
                              enum table_axis axis, int x, int y)
{
  struct table_unshared *tiu = table_unshared_cast (tiu_);
  return table_get_rule (tiu->subtable, axis, x, y);
}

static const struct table_class table_unshared_class =
  {
    table_unshared_destroy,
    table_unshared_get_cell,
    table_unshared_get_rule,
    NULL,                       /* paste */
    NULL,                       /* select */
  };

struct table_string
  {
    struct table table;
    char *string;
    unsigned int options;
  };

static const struct table_class table_string_class;

/* Returns a table that contains a single cell, whose contents are S with
   options OPTIONS (a combination of TAB_* values).  */
struct table *
table_from_string (unsigned int options, const char *s)
{
  struct table_string *ts = xmalloc (sizeof *ts);
  table_init (&ts->table, &table_string_class);
  ts->table.n[TABLE_HORZ] = ts->table.n[TABLE_VERT] = 1;
  ts->string = xstrdup (s);
  ts->options = options;
  return &ts->table;
}

static struct table_string *
table_string_cast (const struct table *table)
{
  assert (table->klass == &table_string_class);
  return UP_CAST (table, struct table_string, table);
}

static void
table_string_destroy (struct table *ts_)
{
  struct table_string *ts = table_string_cast (ts_);
  free (ts->string);
  free (ts);
}

static void
table_string_get_cell (const struct table *ts_, int x UNUSED, int y UNUSED,
                       struct table_cell *cell)
{
  struct table_string *ts = table_string_cast (ts_);
  cell->d[TABLE_HORZ][0] = 0;
  cell->d[TABLE_HORZ][1] = 1;
  cell->d[TABLE_VERT][0] = 0;
  cell->d[TABLE_VERT][1] = 1;
  cell->contents = ts->string;
  cell->options = ts->options;
  cell->destructor = NULL;
}


static int
table_string_get_rule (const struct table *ts UNUSED,
                       enum table_axis axis UNUSED, int x UNUSED, int y UNUSED)
{
  return TAL_0;
}

static const struct table_class table_string_class =
  {
    table_string_destroy,
    table_string_get_cell,
    table_string_get_rule,
    NULL,                       /* paste */
    NULL,                       /* select */
  };
