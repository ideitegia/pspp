/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2013 Free Software Foundation, Inc.

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

#include "output/tab.h"

#include <ctype.h>
#include <stdarg.h>
#include <limits.h>
#include <stdlib.h>

#include "data/data-out.h"
#include "data/format.h"
#include "data/settings.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "output/driver.h"
#include "output/table-item.h"
#include "output/table-provider.h"
#include "output/text-item.h"

#include "gl/error.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Set in the options field of cells that  */
#define TAB_JOIN (1u << TAB_FIRST_AVAILABLE)

/* Joined cell. */
struct tab_joined_cell
  {
    int d[TABLE_N_AXES][2];     /* Table region, same as struct table_cell. */
    char *contents;
  };

static const struct table_class tab_table_class;

/* Creates and returns a new table with NC columns and NR rows and initially no
   header rows or columns.  The table's cells are initially empty. */
struct tab_table *
tab_create (int nc, int nr)
{
  struct tab_table *t;

  t = pool_create_container (struct tab_table, container);
  table_init (&t->table, &tab_table_class);
  table_set_nc (&t->table, nc);
  table_set_nr (&t->table, nr);

  t->title = NULL;
  t->cf = nc;
  t->cc = pool_calloc (t->container, nr * nc, sizeof *t->cc);
  t->ct = pool_malloc (t->container, nr * nc);
  memset (t->ct, 0, nc * nr);

  t->rh = pool_nmalloc (t->container, nc, nr + 1);
  memset (t->rh, 0, nc * (nr + 1));

  t->rv = pool_nmalloc (t->container, nr, nc + 1);
  memset (t->rv, TAL_GAP, nr * (nc + 1));

  t->col_ofs = t->row_ofs = 0;

  return t;
}

/* Sets the width and height of a table, in columns and rows,
   respectively.  Use only to reduce the size of a table, since it
   does not change the amount of allocated memory.

   This function is obsolete.  Please do not add new uses of it.  (Instead, use
   table_select() or one of its helper functions.) */
void
tab_resize (struct tab_table *t, int nc, int nr)
{
  if (nc != -1)
    {
      assert (nc + t->col_ofs <= t->cf);
      table_set_nc (&t->table, nc + t->col_ofs);
    }
  if (nr != -1)
    {
      assert (nr + t->row_ofs <= tab_nr (t));
      table_set_nr (&t->table, nr + t->row_ofs);
    }
}

/* Changes either or both dimensions of a table and reallocates memory as
   necessary.

   This function is obsolete.  Please do not add new uses of it.  (Instead, use
   table_paste() or one of its helper functions to paste multiple tables
   together into a larger one.) */
void
tab_realloc (struct tab_table *t, int nc, int nr)
{
  int ro, co;

  ro = t->row_ofs;
  co = t->col_ofs;
  if (ro || co)
    tab_offset (t, 0, 0);

  if (nc == -1)
    nc = tab_nc (t);
  if (nr == -1)
    nr = tab_nr (t);

  assert (nc == tab_nc (t));

  if (nc > t->cf)
    {
      int mr1 = MIN (nr, tab_nr (t));
      int mc1 = MIN (nc, tab_nc (t));

      void **new_cc;
      unsigned char *new_ct;
      int r;

      new_cc = pool_calloc (t->container, nr * nc, sizeof *new_cc);
      new_ct = pool_malloc (t->container, nr * nc);
      for (r = 0; r < mr1; r++)
	{
	  memcpy (&new_cc[r * nc], &t->cc[r * tab_nc (t)], mc1 * sizeof *t->cc);
	  memcpy (&new_ct[r * nc], &t->ct[r * tab_nc (t)], mc1);
	  memset (&new_ct[r * nc + tab_nc (t)], 0, nc - tab_nc (t));
	}
      pool_free (t->container, t->cc);
      pool_free (t->container, t->ct);
      t->cc = new_cc;
      t->ct = new_ct;
      t->cf = nc;
    }
  else if (nr != tab_nr (t))
    {
      t->cc = pool_nrealloc (t->container, t->cc, nr * nc, sizeof *t->cc);
      t->ct = pool_realloc (t->container, t->ct, nr * nc);

      t->rh = pool_nrealloc (t->container, t->rh, nc, nr + 1);
      t->rv = pool_nrealloc (t->container, t->rv, nr, nc + 1);

      if (nr > tab_nr (t))
	{
	  memset (&t->rh[nc * (tab_nr (t) + 1)], TAL_0, (nr - tab_nr (t)) * nc);
	  memset (&t->rv[(nc + 1) * tab_nr (t)], TAL_GAP,
                  (nr - tab_nr (t)) * (nc + 1));
	}
    }

  memset (&t->ct[nc * tab_nr (t)], 0, nc * (nr - tab_nr (t)));
  memset (&t->cc[nc * tab_nr (t)], 0, nc * (nr - tab_nr (t)) * sizeof *t->cc);

  table_set_nr (&t->table, nr);
  table_set_nc (&t->table, nc);

  if (ro || co)
    tab_offset (t, co, ro);
}

/* Sets the number of header rows on each side of TABLE to L on the
   left, R on the right, T on the top, B on the bottom.  Header rows
   are repeated when a table is broken across multiple columns or
   multiple pages. */
void
tab_headers (struct tab_table *table, int l, int r, int t, int b)
{
  table_set_hl (&table->table, l);
  table_set_hr (&table->table, r);
  table_set_ht (&table->table, t);
  table_set_hb (&table->table, b);
}

/* Rules. */

/* Draws a vertical line to the left of cells at horizontal position X
   from Y1 to Y2 inclusive in style STYLE, if style is not -1. */
void
tab_vline (struct tab_table *t, int style, int x, int y1, int y2)
{
#if DEBUGGING
  if (x + t->col_ofs < 0 || x + t->col_ofs > tab_nc (t)
      || y1 + t->row_ofs < 0 || y1 + t->row_ofs >= tab_nr (t)
      || y2 + t->row_ofs < 0 || y2 + t->row_ofs >= tab_nr (t))
    {
      printf (_("bad vline: x=%d+%d=%d y=(%d+%d=%d,%d+%d=%d) in "
		"table size (%d,%d)\n"),
	      x, t->col_ofs, x + t->col_ofs,
	      y1, t->row_ofs, y1 + t->row_ofs,
	      y2, t->row_ofs, y2 + t->row_ofs,
	      tab_nc (t), tab_nr (t));
      return;
    }
#endif

  x += t->col_ofs;
  y1 += t->row_ofs;
  y2 += t->row_ofs;

  assert (x >= 0);
  assert (x <= tab_nc (t));
  assert (y1 >= 0);
  assert (y2 >= y1);
  assert (y2 <= tab_nr (t));

  if (style != -1)
    {
      int y;
      for (y = y1; y <= y2; y++)
        t->rv[x + (t->cf + 1) * y] = style;
    }
}

/* Draws a horizontal line above cells at vertical position Y from X1
   to X2 inclusive in style STYLE, if style is not -1. */
void
tab_hline (struct tab_table * t, int style, int x1, int x2, int y)
{
#if DEBUGGING
  if (y + t->row_ofs < 0 || y + t->row_ofs > tab_nr (t)
      || x1 + t->col_ofs < 0 || x1 + t->col_ofs >= tab_nc (t)
      || x2 + t->col_ofs < 0 || x2 + t->col_ofs >= tab_nc (t))
    {
      printf (_("bad hline: x=(%d+%d=%d,%d+%d=%d) y=%d+%d=%d in "
		"table size (%d,%d)\n"),
              x1, t->col_ofs, x1 + t->col_ofs,
              x2, t->col_ofs, x2 + t->col_ofs,
              y, t->row_ofs, y + t->row_ofs,
	      tab_nc (t), tab_nr (t));
      return;
    }
#endif

  x1 += t->col_ofs;
  x2 += t->col_ofs;
  y += t->row_ofs;

  assert (y >= 0);
  assert (y <= tab_nr (t));
  assert (x2 >= x1 );
  assert (x1 >= 0 );
  assert (x2 < tab_nc (t));

  if (style != -1)
    {
      int x;
      for (x = x1; x <= x2; x++)
        t->rh[x + t->cf * y] = style;
    }
}

/* Draws a box around cells (X1,Y1)-(X2,Y2) inclusive with horizontal
   lines of style F_H and vertical lines of style F_V.  Fills the
   interior of the box with horizontal lines of style I_H and vertical
   lines of style I_V.  Any of the line styles may be -1 to avoid
   drawing those lines.  This is distinct from 0, which draws a null
   line. */
void
tab_box (struct tab_table *t, int f_h, int f_v, int i_h, int i_v,
	 int x1, int y1, int x2, int y2)
{
#if DEBUGGING
  if (x1 + t->col_ofs < 0 || x1 + t->col_ofs >= tab_nc (t)
      || x2 + t->col_ofs < 0 || x2 + t->col_ofs >= tab_nc (t)
      || y1 + t->row_ofs < 0 || y1 + t->row_ofs >= tab_nr (t)
      || y2 + t->row_ofs < 0 || y2 + t->row_ofs >= tab_nr (t))
    {
      printf (_("bad box: (%d+%d=%d,%d+%d=%d)-(%d+%d=%d,%d+%d=%d) "
		"in table size (%d,%d)\n"),
	      x1, t->col_ofs, x1 + t->col_ofs,
	      y1, t->row_ofs, y1 + t->row_ofs,
	      x2, t->col_ofs, x2 + t->col_ofs,
	      y2, t->row_ofs, y2 + t->row_ofs,
	      tab_nc (t), tab_nr (t));
      NOT_REACHED ();
    }
#endif

  x1 += t->col_ofs;
  x2 += t->col_ofs;
  y1 += t->row_ofs;
  y2 += t->row_ofs;

  assert (x2 >= x1);
  assert (y2 >= y1);
  assert (x1 >= 0);
  assert (y1 >= 0);
  assert (x2 < tab_nc (t));
  assert (y2 < tab_nr (t));

  if (f_h != -1)
    {
      int x;
      for (x = x1; x <= x2; x++)
        {
          t->rh[x + t->cf * y1] = f_h;
          t->rh[x + t->cf * (y2 + 1)] = f_h;
        }
    }
  if (f_v != -1)
    {
      int y;
      for (y = y1; y <= y2; y++)
        {
          t->rv[x1 + (t->cf + 1) * y] = f_v;
          t->rv[(x2 + 1) + (t->cf + 1) * y] = f_v;
        }
    }

  if (i_h != -1)
    {
      int y;

      for (y = y1 + 1; y <= y2; y++)
	{
	  int x;

          for (x = x1; x <= x2; x++)
            t->rh[x + t->cf * y] = i_h;
	}
    }
  if (i_v != -1)
    {
      int x;

      for (x = x1 + 1; x <= x2; x++)
	{
	  int y;

          for (y = y1; y <= y2; y++)
            t->rv[x + (t->cf + 1) * y] = i_v;
	}
    }
}

/* Cells. */

/* Sets cell (C,R) in TABLE, with options OPT, to have a value taken
   from V, displayed with format spec F. */
void
tab_value (struct tab_table *table, int c, int r, unsigned char opt,
	   const union value *v, const struct variable *var,
	   const struct fmt_spec *f)
{
  char *contents;

#if DEBUGGING
  if (c + table->col_ofs < 0 || r + table->row_ofs < 0
      || c + table->col_ofs >= tab_nc (table)
      || r + table->row_ofs >= tab_nr (table))
    {
      printf ("tab_value(): bad cell (%d+%d=%d,%d+%d=%d) in table size "
	      "(%d,%d)\n",
	      c, table->col_ofs, c + table->col_ofs,
	      r, table->row_ofs, r + table->row_ofs,
	      tab_nc (table), tab_nr (table));
      return;
    }
#endif

  contents = data_out_stretchy (v, var_get_encoding (var),
                                f != NULL ? f : var_get_print_format (var),
                                table->container);

  table->cc[c + r * table->cf] = contents;
  table->ct[c + r * table->cf] = opt;
}

/* Sets cell (C,R) in TABLE, with options OPT, to have value VAL
   with NDEC decimal places. */
void
tab_fixed (struct tab_table *table, int c, int r, unsigned char opt,
	   double val, int w, int d)
{
  struct fmt_spec f;
  union value double_value;
  char *s;

  assert (c >= 0);
  assert (c < tab_nc (table));
  assert (r >= 0);
  assert (r < tab_nr (table));

  f = fmt_for_output (FMT_F, w, d);

#if DEBUGGING
  if (c + table->col_ofs < 0 || r + table->row_ofs < 0
      || c + table->col_ofs >= tab_nc (table)
      || r + table->row_ofs >= tab_nr (table))
    {
      printf ("tab_fixed(): bad cell (%d+%d=%d,%d+%d=%d) in table size "
	      "(%d,%d)\n",
	      c, table->col_ofs, c + table->col_ofs,
	      r, table->row_ofs, r + table->row_ofs,
	      tab_nc (table), tab_nr (table));
      return;
    }
#endif

  double_value.f = val;
  s = data_out_stretchy (&double_value, C_ENCODING, &f, table->container);

  table->cc[c + r * table->cf] = s + strspn (s, " ");
  table->ct[c + r * table->cf] = opt;
}

/* Sets cell (C,R) in TABLE, with options OPT, to have value VAL as
   formatted by FMT.
   If FMT is null, then the default print format will be used.
*/
void
tab_double (struct tab_table *table, int c, int r, unsigned char opt,
	   double val, const struct fmt_spec *fmt)
{
  union value double_value ;
  char *s;

  assert (c >= 0);
  assert (c < tab_nc (table));
  assert (r >= 0);
  assert (r < tab_nr (table));

  if ( fmt == NULL)
    fmt = settings_get_format ();

  fmt_check_output (fmt);

#if DEBUGGING
  if (c + table->col_ofs < 0 || r + table->row_ofs < 0
      || c + table->col_ofs >= tab_nc (table)
      || r + table->row_ofs >= tab_nr (table))
    {
      printf ("tab_double(): bad cell (%d+%d=%d,%d+%d=%d) in table size "
	      "(%d,%d)\n",
	      c, table->col_ofs, c + table->col_ofs,
	      r, table->row_ofs, r + table->row_ofs,
	      tab_nc (table), tab_nr (table));
      return;
    }
#endif

  double_value.f = val;
  s = data_out_stretchy (&double_value, C_ENCODING, fmt, table->container);
  table->cc[c + r * table->cf] = s + strspn (s, " ");
  table->ct[c + r * table->cf] = opt;
}


static void
do_tab_text (struct tab_table *table, int c, int r, unsigned opt, char *text)
{
  assert (c >= 0 );
  assert (r >= 0 );
  assert (c < tab_nc (table));
  assert (r < tab_nr (table));

#if DEBUGGING
  if (c + table->col_ofs < 0 || r + table->row_ofs < 0
      || c + table->col_ofs >= tab_nc (table)
      || r + table->row_ofs >= tab_nr (table))
    {
      printf ("tab_text(): bad cell (%d+%d=%d,%d+%d=%d) in table size "
	      "(%d,%d)\n",
	      c, table->col_ofs, c + table->col_ofs,
	      r, table->row_ofs, r + table->row_ofs,
	      tab_nc (table), tab_nr (table));
      return;
    }
#endif

  table->cc[c + r * table->cf] = text;
  table->ct[c + r * table->cf] = opt;
}

/* Sets cell (C,R) in TABLE, with options OPT, to have text value
   TEXT. */
void
tab_text (struct tab_table *table, int c, int r, unsigned opt,
          const char *text)
{
  do_tab_text (table, c, r, opt, pool_strdup (table->container, text));
}

/* Sets cell (C,R) in TABLE, with options OPT, to have text value
   FORMAT, which is formatted as if passed to printf. */
void
tab_text_format (struct tab_table *table, int c, int r, unsigned opt,
                 const char *format, ...)
{
  va_list args;

  va_start (args, format);
  do_tab_text (table, c, r, opt,
               pool_vasprintf (table->container, format, args));
  va_end (args);
}

static void
do_tab_joint_text (struct tab_table *table, int x1, int y1, int x2, int y2,
                   unsigned opt, char *text)
{
  struct tab_joined_cell *j;

  assert (x1 + table->col_ofs >= 0);
  assert (y1 + table->row_ofs >= 0);
  assert (y2 >= y1);
  assert (x2 >= x1);
  assert (y2 + table->row_ofs < tab_nr (table));
  assert (x2 + table->col_ofs < tab_nc (table));

#if DEBUGGING
  if (x1 + table->col_ofs < 0 || x1 + table->col_ofs >= tab_nc (table)
      || y1 + table->row_ofs < 0 || y1 + table->row_ofs >= tab_nr (table)
      || x2 < x1 || x2 + table->col_ofs >= tab_nc (table)
      || y2 < y2 || y2 + table->row_ofs >= tab_nr (table))
    {
      printf ("tab_joint_text(): bad cell "
	      "(%d+%d=%d,%d+%d=%d)-(%d+%d=%d,%d+%d=%d) in table size (%d,%d)\n",
	      x1, table->col_ofs, x1 + table->col_ofs,
	      y1, table->row_ofs, y1 + table->row_ofs,
	      x2, table->col_ofs, x2 + table->col_ofs,
	      y2, table->row_ofs, y2 + table->row_ofs,
	      tab_nc (table), tab_nr (table));
      return;
    }
#endif

  tab_box (table, -1, -1, TAL_0, TAL_0, x1, y1, x2, y2);

  j = pool_alloc (table->container, sizeof *j);
  j->d[TABLE_HORZ][0] = x1 + table->col_ofs;
  j->d[TABLE_VERT][0] = y1 + table->row_ofs;
  j->d[TABLE_HORZ][1] = ++x2 + table->col_ofs;
  j->d[TABLE_VERT][1] = ++y2 + table->row_ofs;
  j->contents = text;

  opt |= TAB_JOIN;

  {
    void **cc = &table->cc[x1 + y1 * table->cf];
    unsigned char *ct = &table->ct[x1 + y1 * table->cf];
    const int ofs = table->cf - (x2 - x1);

    int y;

    for (y = y1; y < y2; y++)
      {
	int x;

	for (x = x1; x < x2; x++)
	  {
	    *cc++ = j;
	    *ct++ = opt;
	  }

	cc += ofs;
	ct += ofs;
      }
  }
}

/* Joins cells (X1,X2)-(Y1,Y2) inclusive in TABLE, and sets them with
   options OPT to have text value TEXT. */
void
tab_joint_text (struct tab_table *table, int x1, int y1, int x2, int y2,
                unsigned opt, const char *text)
{
  do_tab_joint_text (table, x1, y1, x2, y2, opt,
                     pool_strdup (table->container, text));
}

/* Joins cells (X1,X2)-(Y1,Y2) inclusive in TABLE, and sets them
   with options OPT to have text value FORMAT, which is formatted
   as if passed to printf. */
void
tab_joint_text_format (struct tab_table *table, int x1, int y1, int x2, int y2,
                       unsigned opt, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  do_tab_joint_text (table, x1, y1, x2, y2, opt,
                     pool_vasprintf (table->container, format, args));
  va_end (args);
}

bool
tab_cell_is_empty (const struct tab_table *table, int c, int r)
{
  return table->cc[c + r * table->cf] == NULL;
}

/* Miscellaneous. */

/* Set the title of table T to TITLE, which is formatted as if
   passed to printf(). */
void
tab_title (struct tab_table *t, const char *title, ...)
{
  va_list args;

  free (t->title);
  va_start (args, title);
  t->title = xvasprintf (title, args);
  va_end (args);
}

/* Easy, type-safe way to submit a tab table to som. */
void
tab_submit (struct tab_table *t)
{
  table_item_submit (table_item_create (&t->table, t->title));
}

/* Editing. */

/* Set table row and column offsets for all functions that affect
   cells or rules. */
void
tab_offset (struct tab_table *t, int col, int row)
{
  int diff = 0;

#if DEBUGGING
  if (row < -1 || row > tab_nr (t))
    {
      printf ("tab_offset(): row=%d in %d-row table\n", row, tab_nr (t));
      NOT_REACHED ();
    }
  if (col < -1 || col > tab_nc (t))
    {
      printf ("tab_offset(): col=%d in %d-column table\n", col, tab_nc (t));
      NOT_REACHED ();
    }
#endif

  if (row != -1)
    diff += (row - t->row_ofs) * t->cf, t->row_ofs = row;
  if (col != -1)
    diff += (col - t->col_ofs), t->col_ofs = col;

  t->cc += diff;
  t->ct += diff;
}

/* Increment the row offset by one. If the table is too small,
   increase its size. */
void
tab_next_row (struct tab_table *t)
{
  t->cc += t->cf;
  t->ct += t->cf;
  if (++t->row_ofs >= tab_nr (t))
    tab_realloc (t, -1, tab_nr (t) * 4 / 3);
}

/* Writes STRING to the output.  OPTIONS may be any valid combination of TAB_*
   bits.

   This function is obsolete.  Please do not add new uses of it.  Instead, use
   a text_item (see output/text-item.h). */
void
tab_output_text (int options, const char *string)
{
  enum text_item_type type = (options & TAB_EMPH ? TEXT_ITEM_SUBHEAD
                              : options & TAB_FIX ? TEXT_ITEM_MONOSPACE
                              : TEXT_ITEM_PARAGRAPH);
  text_item_submit (text_item_create (type, string));
}

/* Same as tab_output_text(), but FORMAT is passed through printf-like
   formatting before output. */
void
tab_output_text_format (int options, const char *format, ...)
{
  va_list args;
  char *text;

  va_start (args, format);
  text = xvasprintf (format, args);
  va_end (args);

  tab_output_text (options, text);

  free (text);
}

/* Table class implementation. */

static void
tab_destroy (struct table *table)
{
  struct tab_table *t = tab_cast (table);
  free (t->title);
  t->title = NULL;
  pool_destroy (t->container);
}

static void
tab_get_cell (const struct table *table, int x, int y, struct table_cell *cell)
{
  const struct tab_table *t = tab_cast (table);
  int index = x + y * t->cf;
  unsigned char opt = t->ct[index];
  const void *content = t->cc[index];

  cell->options = opt;
  if (opt & TAB_JOIN)
    {
      const struct tab_joined_cell *jc = content;
      cell->d[TABLE_HORZ][0] = jc->d[TABLE_HORZ][0];
      cell->d[TABLE_HORZ][1] = jc->d[TABLE_HORZ][1];
      cell->d[TABLE_VERT][0] = jc->d[TABLE_VERT][0];
      cell->d[TABLE_VERT][1] = jc->d[TABLE_VERT][1];
      cell->contents = jc->contents;
    }
  else
    {
      cell->d[TABLE_HORZ][0] = x;
      cell->d[TABLE_HORZ][1] = x + 1;
      cell->d[TABLE_VERT][0] = y;
      cell->d[TABLE_VERT][1] = y + 1;
      cell->contents = content != NULL ? content : "";
    }
  cell->destructor = NULL;
}

static int
tab_get_rule (const struct table *table, enum table_axis axis, int x, int y)
{
  const struct tab_table *t = tab_cast (table);
  return (axis == TABLE_VERT
          ? t->rh[x + t->cf * y]
          : t->rv[x + (t->cf + 1) * y]);
}

static const struct table_class tab_table_class =
  {
    tab_destroy,
    tab_get_cell,
    tab_get_rule,
    NULL,                       /* paste */
    NULL,                       /* select */
  };

struct tab_table *
tab_cast (const struct table *table)
{
  assert (table->klass == &tab_table_class);
  return UP_CAST (table, struct tab_table, table);
}
