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

#include "output/table-provider.h"

#include "data/casereader.h"
#include "data/data-out.h"
#include "data/format.h"
#include "libpspp/i18n.h"

#include "gl/xalloc.h"

struct table_casereader
  {
    struct table table;
    struct casereader *reader;
    char *heading;
    struct fmt_spec format;
  };

static const struct table_class table_casereader_class;

static struct table_casereader *
table_casereader_cast (const struct table *table)
{
  assert (table->klass == &table_casereader_class);
  return UP_CAST (table, struct table_casereader, table);
}

/* Returns a new table that has one column and the same number of rows as
   READER.  Each row in the table is derived from column COLUMN in the same row
   of READER by formatting with data_out() using the specified FORMAT (which
   must be a valid format for the column's width).

   If HEADING is nonnull, adds an additional row above the first row of data
   that contains HEADING, and sets that row as a header row.

   The returned table has no rules, except that if HEADING is nonnull, a single
   line (TAL_1) separates HEADING from the first row if data. */
struct table *
table_from_casereader (const struct casereader *reader, size_t column,
                       const char *heading, const struct fmt_spec *format)
{
  struct table_casereader *tc;
  struct table *t;

  assert (fmt_check_width_compat (format,
                                  caseproto_get_width (
                                    casereader_get_proto (reader), column)));

  tc = xmalloc (sizeof *tc);
  t = &tc->table;
  table_init (t, &table_casereader_class);
  table_set_nc (t, 1);
  table_set_nr (t, casereader_count_cases (reader));
  tc->reader = casereader_project_1 (casereader_clone (reader), column);
  tc->heading = NULL;
  tc->format = *format;

  if (heading != NULL)
    {
      tc->heading = xstrdup (heading);
      table_set_nr (t, table_nr (t) + 1);
      table_set_ht (t, 1);
    }

  return t;
}

static void
table_casereader_destroy (struct table *t)
{
  struct table_casereader *tc = table_casereader_cast (t);
  casereader_destroy (tc->reader);
  free (tc->heading);
  free (t);
}

static void
free_string (void *s_)
{
  char *s = s_;
  free (s);
}

static void
table_casereader_get_cell (const struct table *t, int x, int y,
                           struct table_cell *cell)
{
  struct table_casereader *tc = table_casereader_cast (t);
  struct ccase *c;
  char *s;

  cell->d[TABLE_HORZ][0] = x;
  cell->d[TABLE_HORZ][1] = x + 1;
  cell->d[TABLE_VERT][0] = y;
  cell->d[TABLE_VERT][1] = y + 1;
  cell->contents = &cell->inline_contents;
  cell->n_contents = 1;
  cell->inline_contents.options = TAB_RIGHT;
  cell->inline_contents.table = NULL;
  cell->inline_contents.n_footnotes = 0;
  if (tc->heading != NULL)
    {
      if (y == 0)
        {
          s = xstrdup (tc->heading);
          cell->inline_contents.text = s;
          cell->destructor = free_string;
          cell->destructor_aux = s;
          return;
        }
      y--;
    }

  c = casereader_peek (tc->reader, y);
  if (c == NULL)
    s = xstrdup ("I/O Error");
  else
    {
      s = data_out (case_data_idx (c, 0), UTF8, &tc->format);
      case_unref (c);
    }
  cell->inline_contents.text = s;
  cell->destructor = free_string;
  cell->destructor_aux = s;
}

static int
table_casereader_get_rule (const struct table *t, enum table_axis axis,
                           int x UNUSED, int y)
{
  struct table_casereader *tc = table_casereader_cast (t);
  if (axis == TABLE_VERT)
    return tc->heading != NULL && y == 1 ? TAL_1 : TAL_0;
  else
    return TAL_GAP;
}

static const struct table_class table_casereader_class =
  {
    table_casereader_destroy,
    table_casereader_get_cell,
    table_casereader_get_rule,
    NULL,                       /* paste */
    NULL,                       /* select (XXX) */
  };
