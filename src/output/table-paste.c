/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011, 2014 Free Software Foundation, Inc.

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
#include "libpspp/tower.h"
#include "output/table-provider.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

struct paste_subtable
  {
    struct tower_node node;
    struct table *table;
  };

static struct paste_subtable *
paste_subtable_cast (struct tower_node *node)
{
  return tower_data (node, struct paste_subtable, node);
}

struct table_paste
  {
    struct table table;
    struct tower subtables;
    enum table_axis orientation;
  };

static const struct table_class table_paste_class;

static struct table_paste *
table_paste_cast (const struct table *table)
{
  assert (table->klass == &table_paste_class);
  return UP_CAST (table, struct table_paste, table);
}

static bool
is_table_paste (const struct table *table, int orientation)
{
  return (table->klass == &table_paste_class
          && table_paste_cast (table)->orientation == orientation);
}

static struct paste_subtable *
paste_subtable_lookup (struct table_paste *tp, unsigned long int offset,
                       unsigned long int *start)
{
  return paste_subtable_cast (tower_lookup (&tp->subtables, offset, start));
}

/* This must be called *before* adding TABLE to TP, otherwise the test for
   whether TP is empty will not have the correct effect. */
static void
table_paste_increase_size (struct table_paste *tp,
                           const struct table *table)
{
  int o = tp->orientation;
  int h0, h1;

  tp->table.n[o] += table->n[o];
  tp->table.n[!o] = MAX (tp->table.n[!o], table->n[!o]);

  h0 = table->h[!o][0];
  h1 = table->h[!o][1];
  if (tower_is_empty (&tp->subtables))
    {
      tp->table.h[!o][0] = h0;
      tp->table.h[!o][1] = h1;
    }
  else
    {
      tp->table.h[!o][0] = MIN (tp->table.h[!o][0], h0);

      /* XXX this is not quite right */
      tp->table.h[!o][1] = MIN (tp->table.h[!o][1], h1);
    }
}

static void
reassess_headers (struct table_paste *tp)
{
  int o = tp->orientation;
  if (tower_is_empty (&tp->subtables))
    tp->table.h[o][0] = tp->table.h[o][1] = 0;
  else
    {
      struct paste_subtable *h0, *h1;

      h0 = paste_subtable_cast (tower_first (&tp->subtables));
      tp->table.h[o][0] = h0->table->h[o][0];

      h1 = paste_subtable_cast (tower_last (&tp->subtables));
      tp->table.h[o][1] = h1->table->h[o][1];
    }
}

static void
table_paste_insert_subtable (struct table_paste *tp,
                             struct table *table,
                             struct tower_node *under)
{
  struct paste_subtable *subtable;

  subtable = xmalloc (sizeof *subtable);
  table_paste_increase_size (tp, table);
  tower_insert (&tp->subtables, table->n[tp->orientation],
                &subtable->node, under);
  subtable->table = table;
  reassess_headers (tp);
}

/* Takes ownership of A and B and returns a table that consists of tables A and
   B "pasted together", that is, a table whose size is the sum of the sizes of
   A and B along the axis specified by ORIENTATION.  A and B should have the
   same size along the axis opposite ORIENTATION; the handling of tables that
   have different sizes along that axis may vary.

   The rules at the seam between A and B are combined.  The exact way in which
   they are combined is unspecified, but the method of table_rule_combine() is
   typical.

   If A or B is null, returns the other argument. */
struct table *
table_paste (struct table *a, struct table *b, enum table_axis orientation)
{
  struct table_paste *tp;

  /* Handle nulls. */
  if (a == NULL)
    return b;
  if (b == NULL)
    return a;

  assert (a->n[!orientation] == b->n[!orientation]);

  /* Handle tables that know how to paste themselves. */
  if (!table_is_shared (a) && !table_is_shared (b) && a != b)
    {
      if (a->klass->paste != NULL)
        {
          struct table *new = a->klass->paste (a, b, orientation);
          if (new != NULL)
            return new;
        }
      if (b->klass->paste != NULL && a->klass != b->klass)
        {
          struct table *new = b->klass->paste (a, b, orientation);
          if (new != NULL)
            return new;
        }
    }

  /* Create new table_paste and insert A and B into it. */
  tp = xmalloc (sizeof *tp);
  table_init (&tp->table, &table_paste_class);
  tower_init (&tp->subtables);
  tp->orientation = orientation;
  table_paste_insert_subtable (tp, a, NULL);
  table_paste_insert_subtable (tp, b, NULL);
  return &tp->table;
}

/* Shorthand for table_paste (left, right, TABLE_HORZ). */
struct table *
table_hpaste (struct table *left, struct table *right)
{
  return table_paste (left, right, TABLE_HORZ);
}

/* Shorthand for table_paste (top, bottom, TABLE_VERT). */
struct table *
table_vpaste (struct table *top, struct table *bottom)
{
  return table_paste (top, bottom, TABLE_VERT);
}

static void
table_paste_destroy (struct table *t)
{
  struct table_paste *tp = table_paste_cast (t);
  struct tower_node *node, *next;

  for (node = tower_first (&tp->subtables); node != NULL; node = next)
    {
      struct paste_subtable *ps = paste_subtable_cast (node);
      table_unref (ps->table);
      next = tower_delete (&tp->subtables, node);
      free (node);
    }
  free (tp);
}

static void
table_paste_get_cell (const struct table *t, int x, int y,
                      struct table_cell *cell)
{
  struct table_paste *tp = table_paste_cast (t);
  struct paste_subtable *ps;
  unsigned long int start;
  int d[TABLE_N_AXES];

  d[TABLE_HORZ] = x;
  d[TABLE_VERT] = y;
  ps = paste_subtable_lookup (tp, d[tp->orientation], &start);
  d[tp->orientation] -= start;
  table_get_cell (ps->table, d[TABLE_HORZ], d[TABLE_VERT], cell);
  cell->d[tp->orientation][0] += start;
  cell->d[tp->orientation][1] += start;
}

static int
table_paste_get_rule (const struct table *t,
                      enum table_axis axis, int x, int y)
{
  struct table_paste *tp = table_paste_cast (t);
  int h = tp->orientation == TABLE_HORZ ? x : y;
  int k = tp->orientation == TABLE_HORZ ? y : x;
  struct paste_subtable *ps;
  unsigned long int start;

  if (tp->orientation == axis)
    {
      int r;

      ps = paste_subtable_lookup (tp, h == 0 ? 0 : h - 1, &start);
      if (tp->orientation == TABLE_HORZ) /* XXX */
        r = table_get_rule (ps->table, axis, h - start, k);
      else
        r = table_get_rule (ps->table, axis, k, h - start);
      if (h == start + tower_node_get_size (&ps->node))
        {
          struct tower_node *ps2_ = tower_next (&tp->subtables, &ps->node);
          if (ps2_ != NULL)
            {
              struct paste_subtable *ps2 = paste_subtable_cast (ps2_);
              int r2;

              if (tp->orientation == TABLE_HORZ) /* XXX */
                r2 = table_get_rule (ps2->table, axis, 0, k);
              else
                r2 = table_get_rule (ps2->table, axis, k, 0);
              return table_rule_combine (r, r2);
            }
        }
      return r;
    }
  else
    {
      ps = paste_subtable_lookup (tp, h, &start);
      if (tp->orientation == TABLE_HORZ) /* XXX */
        return table_get_rule (ps->table, axis, h - start, k);
      else
        return table_get_rule (ps->table, axis, k, h - start);
    }
}

static struct table *
table_paste_paste (struct table *a, struct table *b,
                   enum table_axis orientation)
{
  struct table_paste *ta, *tb;

  ta = is_table_paste (a, orientation) ? table_paste_cast (a) : NULL;
  tb = is_table_paste (b, orientation) ? table_paste_cast (b) : NULL;

  if (ta != NULL)
    {
      if (tb != NULL)
        {
          /* Append all of B's subtables onto A, then destroy B. */
          table_paste_increase_size (ta, b);
          tower_splice (&ta->subtables, NULL,
                        &tb->subtables, tower_first (&tb->subtables), NULL);
          table_unref (b);
        }
      else
        {
          /* Append B to A's stack of subtables. */
          table_paste_insert_subtable (ta, b, NULL);
        }
      reassess_headers (ta);
      return a;
    }
  else if (tb != NULL)
    {
      /* Insert A at the beginning of B's stack of subtables. */
      table_paste_insert_subtable (tb, a, tower_first (&tb->subtables));
      reassess_headers (tb);
      return b;
    }
  else
    return NULL;
}

static const struct table_class table_paste_class =
  {
    table_paste_destroy,
    table_paste_get_cell,
    table_paste_get_rule,
    table_paste_paste,
    NULL,                       /* select */
  };
