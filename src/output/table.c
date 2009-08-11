/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009 Free Software Foundation, Inc.

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

#include "table.h"

#include <ctype.h>
#include <stdarg.h>
#include <limits.h>
#include <stdlib.h>

#include "output.h"
#include "manager.h"

#include <data/data-out.h>
#include <data/format.h>
#include <data/value.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>

#include <data/settings.h>

#include "error.h"
#include "minmax.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

const struct som_table_class tab_table_class;

/* Returns the font to use for a cell with the given OPTIONS. */
static enum outp_font
options_to_font (unsigned options)
{
  return (options & TAB_FIX ? OUTP_FIXED
          : options & TAB_EMPH ? OUTP_EMPHASIS
          : OUTP_PROPORTIONAL);
}

/* Creates a table with NC columns and NR rows. */
struct tab_table *
tab_create (int nc, int nr, int reallocable UNUSED)
{
  struct tab_table *t;

  t = pool_create_container (struct tab_table, container);
  t->ref_cnt = 1;
  t->col_style = TAB_COL_NONE;
  t->title = NULL;
  t->flags = SOMF_NONE;
  t->nr = nr;
  t->nc = t->cf = nc;
  t->l = t->r = t->t = t->b = 0;

  t->cc = pool_nmalloc (t->container, nr * nc, sizeof *t->cc);
  t->ct = pool_malloc (t->container, nr * nc);
  memset (t->ct, TAB_EMPTY, nc * nr);

  t->rh = pool_nmalloc (t->container, nc, nr + 1);
  memset (t->rh, 0, nc * (nr + 1));

  t->rv = pool_nmalloc (t->container, nr, nc + 1);
  memset (t->rv, UCHAR_MAX, nr * (nc + 1));

  t->dim = NULL;
  t->col_ofs = t->row_ofs = 0;

  return t;
}

/* Increases T's reference count and, if this causes T's
   reference count to reach 0, destroys T. */
void
tab_destroy (struct tab_table *t)
{
  assert (t->ref_cnt > 0);
  if (--t->ref_cnt > 0)
    return;
  if (t->dim_free != NULL)
    t->dim_free (t->dim_aux);
  free (t->title);
  pool_destroy (t->container);
}

/* Increases T's reference count. */
void
tab_ref (struct tab_table *t)
{
  assert (t->ref_cnt > 0);
  t->ref_cnt++;
}

/* Sets the width and height of a table, in columns and rows,
   respectively.  Use only to reduce the size of a table, since it
   does not change the amount of allocated memory. */
void
tab_resize (struct tab_table *t, int nc, int nr)
{
  assert (t != NULL);
  if (nc != -1)
    {
      assert (nc + t->col_ofs <= t->cf);
      t->nc = nc + t->col_ofs;
    }
  if (nr != -1)
    {
      assert (nr + t->row_ofs <= tab_nr (t));
      t->nr = nr + t->row_ofs;
    }
}

/* Changes either or both dimensions of a table.  Consider using the
   above routine instead if it won't waste a lot of space.

   Changing the number of columns in a table is particularly expensive
   in space and time.  Avoid doing such.  FIXME: In fact, transferring
   of rules isn't even implemented yet. */
void
tab_realloc (struct tab_table *t, int nc, int nr)
{
  int ro, co;

  assert (t != NULL);
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

      struct substring *new_cc;
      unsigned char *new_ct;
      int r;

      new_cc = pool_nmalloc (t->container, nr * nc, sizeof *new_cc);
      new_ct = pool_malloc (t->container, nr * nc);
      for (r = 0; r < mr1; r++)
	{
	  memcpy (&new_cc[r * nc], &t->cc[r * tab_nc (t)], mc1 * sizeof *t->cc);
	  memcpy (&new_ct[r * nc], &t->ct[r * tab_nc (t)], mc1);
	  memset (&new_ct[r * nc + tab_nc (t)], TAB_EMPTY, nc - tab_nc (t));
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
	  memset (&t->rv[(nc + 1) * tab_nr (t)], UCHAR_MAX,
                  (nr - tab_nr (t)) * (nc + 1));
	}
    }

  memset (&t->ct[nc * tab_nr (t)], TAB_EMPTY, nc * (nr - tab_nr (t)));

  t->nr = nr;
  t->nc = nc;

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
  assert (table != NULL);
  assert (l < table->nc);
  assert (r < table->nc);
  assert (t < table->nr);
  assert (b < table->nr);


  table->l = l;
  table->r = r;
  table->t = t;
  table->b = b;
}

/* Set up table T so that, when it is an appropriate size, it will be
   displayed across the page in columns.

   STYLE is a TAB_COL_* constant. */
void
tab_columns (struct tab_table *t, int style)
{
  assert (t != NULL);
  t->col_style = style;
}

/* Rules. */

/* Draws a vertical line to the left of cells at horizontal position X
   from Y1 to Y2 inclusive in style STYLE, if style is not -1. */
void
tab_vline (struct tab_table *t, int style, int x, int y1, int y2)
{
  assert (t != NULL);

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

  assert (x  > 0);
  assert (x  < tab_nc (t));
  assert (y1 >= 0);
  assert (y2 >= y1);
  assert (y2 <=  tab_nr (t));

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
  assert (t != NULL);

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
  assert (t != NULL);

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

/* Set the title of table T to TITLE, which is formatted as if
   passed to printf(). */
void
tab_title (struct tab_table *t, const char *title, ...)
{
  va_list args;

  assert (t != NULL && title != NULL);
  va_start (args, title);
  t->title = xvasprintf (title, args);
  va_end (args);
}

/* Set DIM_FUNC, which will be passed auxiliary data AUX, as the
   dimension function for table T.

   DIM_FUNC must not assume that it is called from the same
   context as tab_dim; for example, table T might be kept in
   memory and, thus, DIM_FUNC might be called after the currently
   running command completes.  If it is non-null, FREE_FUNC is
   called when the table is destroyed, to allow any data
   allocated for use by DIM_FUNC to be freed.  */
void
tab_dim (struct tab_table *t,
         tab_dim_func *dim_func, tab_dim_free_func *free_func, void *aux)
{
  assert (t->dim == NULL);
  t->dim = dim_func;
  t->dim_free = free_func;
  t->dim_aux = aux;
}

/* Returns the natural width of column C in table T for driver D, that
   is, the smallest width necessary to display all its cells without
   wrapping.  The width will be no larger than the page width minus
   left and right rule widths. */
int
tab_natural_width (const struct tab_rendering *r, int col)
{
  const struct tab_table *t = r->table;
  int width, row, max_width;

  assert (col >= 0 && col < tab_nc (t));

  width = 0;
  for (row = 0; row < tab_nr (t); row++)
    {
      struct outp_text text;
      unsigned char opt = t->ct[col + row * t->cf];
      int w;

      if (opt & (TAB_JOIN | TAB_EMPTY))
        continue;

      text.string = t->cc[col + row * t->cf];
      text.justification = OUTP_LEFT;
      text.font = options_to_font (opt);
      text.h = text.v = INT_MAX;

      r->driver->class->text_metrics (r->driver, &text, &w, NULL);
      if (w > width)
        width = w;
    }

  if (width == 0)
    {
      /* FIXME: This is an ugly kluge to compensate for the fact
         that we don't let joined cells contribute to column
         widths. */
      width = r->driver->prop_em_width * 8;
    }

  max_width = r->driver->width - r->wrv[0] - r->wrv[tab_nc (t)];
  return MIN (width, max_width);
}

/* Returns the natural height of row R in table T for driver D, that
   is, the minimum height necessary to display the information in the
   cell at the widths set for each column. */
int
tab_natural_height (const struct tab_rendering *r, int row)
{
  const struct tab_table *t = r->table;
  int height, col;

  assert (row >= 0 && row < tab_nr (t));

  height = r->driver->font_height;
  for (col = 0; col < tab_nc (t); col++)
    {
      struct outp_text text;
      unsigned char opt = t->ct[col + row * t->cf];
      int h;

      if (opt & (TAB_JOIN | TAB_EMPTY))
        continue;

      text.string = t->cc[col + row * t->cf];
      text.justification = OUTP_LEFT;
      text.font = options_to_font (opt);
      text.h = r->w[col];
      text.v = INT_MAX;
      r->driver->class->text_metrics (r->driver, &text, NULL, &h);

      if (h > height)
        height = h;
    }

  return height;
}

/* Callback function to set all columns and rows to their natural
   dimensions.  Not really meant to be called directly.  */
void
tab_natural_dimensions (struct tab_rendering *r, void *aux UNUSED)
{
  const struct tab_table *t = r->table;
  int i;

  for (i = 0; i < tab_nc (t); i++)
    r->w[i] = tab_natural_width (r, i);

  for (i = 0; i < tab_nr (t); i++)
    r->h[i] = tab_natural_height (r, i);
}


/* Cells. */

/* Sets cell (C,R) in TABLE, with options OPT, to have a value taken
   from V, displayed with format spec F. */
void
tab_value (struct tab_table *table, int c, int r, unsigned char opt,
	   const union value *v, const struct fmt_spec *f)
{
  char *contents;

  assert (table != NULL && v != NULL && f != NULL);
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

  contents = pool_alloc (table->container, f->w);
  table->cc[c + r * table->cf] = ss_buffer (contents, f->w);
  table->ct[c + r * table->cf] = opt;

  data_out (v, f, contents);
}

/* Sets cell (C,R) in TABLE, with options OPT, to have value VAL
   with NDEC decimal places. */
void
tab_fixed (struct tab_table *table, int c, int r, unsigned char opt,
	   double val, int w, int d)
{
  char *contents;
  char buf[40], *cp;

  struct fmt_spec f;
  union value double_value;

  assert (table != NULL && w <= 40);

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
  data_out (&double_value, &f, buf);

  cp = buf;
  while (isspace ((unsigned char) *cp) && cp < &buf[w])
    cp++;
  f.w = w - (cp - buf);

  contents = pool_alloc (table->container, f.w);
  table->cc[c + r * table->cf] = ss_buffer (contents, f.w);
  table->ct[c + r * table->cf] = opt;
  memcpy (contents, cp, f.w);
}

/* Sets cell (C,R) in TABLE, with options OPT, to have value VAL as
   formatted by FMT.
   If FMT is null, then the default print format will be used.
*/
void
tab_double (struct tab_table *table, int c, int r, unsigned char opt,
	   double val, const struct fmt_spec *fmt)
{
  int w;
  char *contents;
  char buf[40], *cp;

  union value double_value;

  assert (table != NULL);

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
  data_out (&double_value, fmt, buf);

  cp = buf;
  while (isspace ((unsigned char) *cp) && cp < &buf[fmt->w])
    cp++;
  w = fmt->w - (cp - buf);

  contents = pool_alloc (table->container, w);
  table->cc[c + r * table->cf] = ss_buffer (contents, w);
  table->ct[c + r * table->cf] = opt;
  memcpy (contents, cp, w);
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

  table->cc[c + r * table->cf] = ss_cstr (text);
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
  j->x1 = x1 + table->col_ofs;
  j->y1 = y1 + table->row_ofs;
  j->x2 = ++x2 + table->col_ofs;
  j->y2 = ++y2 + table->row_ofs;
  j->contents = ss_cstr (text);

  opt |= TAB_JOIN;

  {
    struct substring *cc = &table->cc[x1 + y1 * table->cf];
    unsigned char *ct = &table->ct[x1 + y1 * table->cf];
    const int ofs = table->cf - (x2 - x1);

    int y;

    for (y = y1; y < y2; y++)
      {
	int x;

	for (x = x1; x < x2; x++)
	  {
	    *cc++ = ss_buffer ((char *) j, 0);
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

/* Sets cell (C,R) in TABLE, with options OPT, to contents STRING. */
void
tab_raw (struct tab_table *table, int c, int r, unsigned opt,
	 struct substring *string)
{
  assert (table != NULL && string != NULL);

#if DEBUGGING
  if (c + table->col_ofs < 0 || r + table->row_ofs < 0
      || c + table->col_ofs >= tab_nc (table)
      || r + table->row_ofs >= tab_nr (table))
    {
      printf ("tab_raw(): bad cell (%d+%d=%d,%d+%d=%d) in table size "
	      "(%d,%d)\n",
	      c, table->col_ofs, c + table->col_ofs,
	      r, table->row_ofs, r + table->row_ofs,
	      tab_nc (table), tab_nr (table));
      return;
    }
#endif

  table->cc[c + r * table->cf] = *string;
  table->ct[c + r * table->cf] = opt;
}

/* Miscellaneous. */

/* Sets the widths of all the columns and heights of all the rows in
   table T for driver D. */
static void
nowrap_dim (struct tab_rendering *r, void *aux UNUSED)
{
  r->w[0] = tab_natural_width (r, 0);
  r->h[0] = r->driver->font_height;
}

/* Sets the widths of all the columns and heights of all the rows in
   table T for driver D. */
static void
wrap_dim (struct tab_rendering *r, void *aux UNUSED)
{
  r->w[0] = tab_natural_width (r, 0);
  r->h[0] = tab_natural_height (r, 0);
}

static void
do_tab_output_text (struct tab_table *t, int options, char *text)
{
  do_tab_text (t, 0, 0, options, text);
  tab_flags (t, SOMF_NO_TITLE | SOMF_NO_SPACING);
  tab_dim (t, options & TAT_NOWRAP ? nowrap_dim : wrap_dim, NULL, NULL);
  tab_submit (t);
}

/* Outputs TEXT as a table with a single cell having cell options
   OPTIONS, which is a combination of the TAB_* and TAT_*
   constants.  */
void
tab_output_text (int options, const char *text)
{
  struct tab_table *table = tab_create (1, 1, 0);
  do_tab_output_text (table, options, pool_strdup (table->container, text));
}

/* Outputs FORMAT as a table with a single cell having cell
   options OPTIONS, which is a combination of the TAB_* and TAT_*
   constants.  FORMAT is formatted as if it was passed through
   printf. */
void
tab_output_text_format (int options, const char *format, ...)
{
  struct tab_table *table;
  va_list args;

  table = tab_create (1, 1, 0);

  va_start (args, format);
  do_tab_output_text (table, options,
                      pool_vasprintf (table->container, format, args));
  va_end (args);
}

/* Set table flags to FLAGS. */
void
tab_flags (struct tab_table *t, unsigned flags)
{
  assert (t != NULL);
  t->flags = flags;
}

/* Easy, type-safe way to submit a tab table to som. */
void
tab_submit (struct tab_table *t)
{
  struct som_entity s;

  assert (t != NULL);
  s.class = &tab_table_class;
  s.ext = t;
  s.type = SOM_TABLE;
  som_submit (&s);
  tab_destroy (t);
}

/* Editing. */

/* Set table row and column offsets for all functions that affect
   cells or rules. */
void
tab_offset (struct tab_table *t, int col, int row)
{
  int diff = 0;

  assert (t != NULL);
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
  assert (t != NULL);
  t->cc += t->cf;
  t->ct += t->cf;
  if (++t->row_ofs >= tab_nr (t))
    tab_realloc (t, -1, tab_nr (t) * 4 / 3);
}

/* Return the number of columns and rows in the table into N_COLUMNS
   and N_ROWS, respectively. */
static void
tabi_count (struct som_entity *t_, int *n_columns, int *n_rows)
{
  struct tab_table *t = t_->ext;
  *n_columns = t->nc;
  *n_rows = t->nr;
}

/* Return the column style for this table into STYLE. */
static void
tabi_columns (struct som_entity *t_, int *style)
{
  struct tab_table *t = t_->ext;
  *style = t->col_style;
}

/* Return the number of header rows/columns on the left, right, top,
   and bottom sides into HL, HR, HT, and HB, respectively. */
static void
tabi_headers (struct som_entity *t_, int *hl, int *hr, int *ht, int *hb)
{
  struct tab_table *t = t_->ext;
  *hl = t->l;
  *hr = t->r;
  *ht = t->t;
  *hb = t->b;
}

/* Return flags set for the current table into FLAGS. */
static void
tabi_flags (struct som_entity *t_, unsigned *flags)
{
  struct tab_table *t = t_->ext;
  *flags = t->flags;
}

/* Returns the line style to use for spacing purposes for a rule
   of the given TYPE. */
static enum outp_line_style
rule_to_spacing_type (unsigned char type)
{
  switch (type)
    {
    case TAL_0:
      return OUTP_L_NONE;
    case TAL_GAP:
    case TAL_1:
      return OUTP_L_SINGLE;
    case TAL_2:
      return OUTP_L_DOUBLE;
    default:
      NOT_REACHED ();
    }
}

static void *
tabi_render_init (struct som_entity *t_, struct outp_driver *driver,
                  int hl, int hr, int ht, int hb)
{
  const struct tab_table *t = t_->ext;
  struct tab_rendering *r;
  int col, row;
  int i;

  tab_offset (t_->ext, 0, 0);

  r = xmalloc (sizeof *r);
  r->table = t;
  r->driver = driver;
  r->w = xnmalloc (tab_nc (t), sizeof *r->w);
  r->h = xnmalloc (tab_nr (t), sizeof *r->h);
  r->hrh = xnmalloc (tab_nr (t) + 1, sizeof *r->hrh);
  r->wrv = xnmalloc (tab_nc (t) + 1, sizeof *r->wrv);
  r->l = hl;
  r->r = hr;
  r->t = ht;
  r->b = hb;

  /* Figure out sizes of rules. */
  for (row = 0; row <= tab_nr (t); row++)
    {
      int width = 0;
      for (col = 0; col < tab_nc (t); col++)
        {
          unsigned char rh = t->rh[col + row * t->cf];
          int w = driver->horiz_line_width[rule_to_spacing_type (rh)];
          if (w > width)
            width = w;
        }
      r->hrh[row] = width;
    }

  for (col = 0; col <= tab_nc (t); col++)
    {
      int width = 0;
      for (row = 0; row < tab_nr (t); row++)
        {
          unsigned char *rv = &t->rv[col + row * (t->cf + 1)];
          int w;
          if (*rv == UCHAR_MAX)
            *rv = col != 0 && col != tab_nc (t) ? TAL_GAP : TAL_0;
          w = driver->vert_line_width[rule_to_spacing_type (*rv)];
          if (w > width)
            width = w;
        }
      r->wrv[col] = width;
    }

  /* Determine row heights and columns widths. */
  for (i = 0; i < tab_nr (t); i++)
    r->h[i] = -1;
  for (i = 0; i < tab_nc (t); i++)
    r->w[i] = -1;

  t->dim (r, t->dim_aux);

  for (i = 0; i < tab_nr (t); i++)
    if (r->h[i] < 0)
      error (0, 0, "height of table row %d is %d (not initialized?)",
             i, r->h[i]);
  for (i = 0; i < tab_nc (t); i++)
    if (r->w[i] < 0)
      error (0, 0, "width of table column %d is %d (not initialized?)",
             i, r->w[i]);

  /* Add up header sizes. */
  for (i = 0, r->wl = r->wrv[0]; i < r->l; i++)
    r->wl += r->w[i] + r->wrv[i + 1];
  for (i = 0, r->ht = r->hrh[0]; i < r->t; i++)
    r->ht += r->h[i] + r->hrh[i + 1];
  for (i = tab_nc (t) - r->r, r->wr = r->wrv[i]; i < tab_nc (t); i++)
    r->wr += r->w[i] + r->wrv[i + 1];
  for (i = tab_nr (t) - r->b, r->hb = r->hrh[i]; i < tab_nr (t); i++)
    r->hb += r->h[i] + r->hrh[i + 1];

  /* Title. */
  if (!(t->flags & SOMF_NO_TITLE))
    r->ht += driver->font_height;

  return r;
}

static void
tabi_render_free (void *r_)
{
  struct tab_rendering *r = r_;

  free (r->w);
  free (r->h);
  free (r->hrh);
  free (r->wrv);
  free (r);
}

/* Return the horizontal and vertical size of the entire table,
   including headers, for the current output device, into HORIZ and
   VERT. */
static void
tabi_area (void *r_, int *horiz, int *vert)
{
  struct tab_rendering *r = r_;
  const struct tab_table *t = r->table;
  int width, col;
  int height, row;

  width = 0;
  for (col = r->l + 1, width = r->wl + r->wr + r->w[tab_l (t)];
       col < tab_nc (t) - r->r; col++)
    width += r->w[col] + r->wrv[col];
  *horiz = width;

  height = 0;
  for (row = r->t + 1, height = r->ht + r->hb + r->h[tab_t (t)];
       row < tab_nr (t) - tab_b (t); row++)
    height += r->h[row] + r->hrh[row];
  *vert = height;
}

/* Determines the number of rows or columns (including appropriate
   headers), depending on CUMTYPE, that will fit into the space
   specified.  Takes rows/columns starting at index START and attempts
   to fill up available space MAX.  Returns in END the index of the
   last row/column plus one; returns in ACTUAL the actual amount of
   space the selected rows/columns (including appropriate headers)
   filled. */
static void
tabi_cumulate (void *r_, int cumtype, int start, int *end,
               int max, int *actual)
{
  const struct tab_rendering *r = r_;
  const struct tab_table *t = r->table;
  int limit;
  int *cells, *rules;
  int total;
  int idx;

  assert (end != NULL && (cumtype == SOM_ROWS || cumtype == SOM_COLUMNS));
  if (cumtype == SOM_ROWS)
    {
      assert (start >= 0 && start < tab_nr (t));
      limit = tab_nr (t) - r->b;
      cells = &r->h[start];
      rules = &r->hrh[start + 1];
      total = r->ht + r->hb;
    }
  else
    {
      assert (start >= 0 && start < tab_nc (t));
      limit = tab_nc (t) - tab_r (t);
      cells = &r->w[start];
      rules = &r->wrv[start + 1];
      total = r->wl + r->wr;
    }

  total += *cells++;
  if (total > max)
    {
      if (end)
	*end = start;
      if (actual)
	*actual = 0;
      return;
    }

  for (idx = start + 1; idx < limit; idx++)
    {
      int amt = *cells++ + *rules++;

      total += amt;
      if (total > max)
        {
          total -= amt;
          break;
        }
    }

  if (end)
    *end = idx;

  if (actual)
    *actual = total;
}

/* Render title for current table, with major index X and minor index
   Y.  Y may be zero, or X and Y may be zero, but X should be nonzero
   if Y is nonzero. */
static void
tabi_title (void *r_, int x, int y, int table_num, int subtable_num,
            const char *command_name)
{
  const struct tab_rendering *r = r_;
  const struct tab_table *t = r->table;
  struct outp_text text;
  struct string title;

  if (t->flags & SOMF_NO_TITLE)
    return;

  ds_init_empty (&title);
  ds_put_format (&title,"%d.%d", table_num, subtable_num);
  if (x && y)
    ds_put_format (&title, "(%d:%d)", x, y);
  else if (x)
    ds_put_format (&title, "(%d)", x);
  if (command_name != NULL)
    ds_put_format (&title, " %s", command_name);
  ds_put_cstr (&title, ".  ");
  if (t->title != NULL)
    ds_put_cstr (&title, t->title);

  text.font = OUTP_PROPORTIONAL;
  text.justification = OUTP_LEFT;
  text.string = ds_ss (&title);
  text.h = r->driver->width;
  text.v = r->driver->font_height;
  text.x = 0;
  text.y = r->driver->cp_y;
  r->driver->class->text_draw (r->driver, &text);

  ds_destroy (&title);
}

static int render_strip (const struct tab_rendering *,
                         int x, int y, int r, int c1, int c2, int r1, int r2);

static void
add_range (int ranges[][2], int *np, int start, int end)
{
  int n = *np;
  if (n == 0 || start > ranges[n - 1][1])
    {
      ranges[n][0] = start;
      ranges[n][1] = end;
      ++*np;
    }
  else
    ranges[n - 1][1] = end;
}

/* Draws table region (C0,R0)-(C1,R1), plus headers, at the
   current position on the current output device.  */
static void
tabi_render (void *r_, int c0, int r0, int c1, int r1)
{
  const struct tab_rendering *r = r_;
  const struct tab_table *t = r->table;
  int rows[3][2], cols[3][2];
  int n_row_ranges, n_col_ranges;
  int y, i;

  /* Rows to render, counting horizontal rules as rows.  */
  n_row_ranges = 0;
  add_range (rows, &n_row_ranges, 0, tab_t (t) * 2 + 1);
  add_range (rows, &n_row_ranges, r0 * 2 + 1, r1 * 2);
  add_range (rows, &n_row_ranges, (tab_nr (t) - tab_b (t)) * 2,
             tab_nr (t) * 2 + 1);

  /* Columns to render, counting vertical rules as columns. */
  n_col_ranges = 0;
  add_range (cols, &n_col_ranges, 0, r->l * 2 + 1);
  add_range (cols, &n_col_ranges, c0 * 2 + 1, c1 * 2);
  add_range (cols, &n_col_ranges, (tab_nc (t) - r->r) * 2, tab_nc (t) * 2 + 1);

  y = r->driver->cp_y;
  if (!(t->flags & SOMF_NO_TITLE))
    y += r->driver->font_height;
  for (i = 0; i < n_row_ranges; i++)
    {
      int row;

      for (row = rows[i][0]; row < rows[i][1]; row++)
        {
          int x, j;

          x = r->driver->cp_x;
          for (j = 0; j < n_col_ranges; j++)
            x = render_strip (r, x, y, row,
                              cols[j][0], cols[j][1],
                              rows[i][0], rows[i][1]);

          y += (row & 1) ? r->h[row / 2] : r->hrh[row / 2];
        }
    }
}

const struct som_table_class tab_table_class =
  {
    tabi_count,
    tabi_columns,
    tabi_headers,
    tabi_flags,

    tabi_render_init,
    tabi_render_free,

    tabi_area,
    tabi_cumulate,
    tabi_title,
    tabi_render,
  };

static enum outp_justification
translate_justification (unsigned int opt)
{
  switch (opt & TAB_ALIGN_MASK)
    {
    case TAB_RIGHT:
      return OUTP_RIGHT;
    case TAB_LEFT:
      return OUTP_LEFT;
    case TAB_CENTER:
      return OUTP_CENTER;
    default:
      NOT_REACHED ();
    }
}

/* Returns the line style to use for drawing a rule of the given
   TYPE. */
static enum outp_line_style
rule_to_draw_type (unsigned char type)
{
  switch (type)
    {
    case TAL_0:
    case TAL_GAP:
      return OUTP_L_NONE;
    case TAL_1:
      return OUTP_L_SINGLE;
    case TAL_2:
      return OUTP_L_DOUBLE;
    default:
      NOT_REACHED ();
    }
}

/* Returns the horizontal rule at the given column and row. */
static int
get_hrule (const struct tab_table *t, int col, int row)
{
  return t->rh[col + row * t->cf];
}

/* Returns the vertical rule at the given column and row. */
static int
get_vrule (const struct tab_table *t, int col, int row)
{
  return t->rv[col + row * (t->cf + 1)];
}

/* Renders the horizontal rule at the given column and row
   at (X,Y) on the page. */
static void
render_horz_rule (const struct tab_rendering *r,
                  int x, int y, int col, int row)
{
  enum outp_line_style style;
  style = rule_to_draw_type (get_hrule (r->table, col, row));
  if (style != OUTP_L_NONE)
    r->driver->class->line (r->driver, x, y, x + r->w[col], y + r->hrh[row],
                            OUTP_L_NONE, style, OUTP_L_NONE, style);
}

/* Renders the vertical rule at the given column and row
   at (X,Y) on the page. */
static void
render_vert_rule (const struct tab_rendering *r,
                  int x, int y, int col, int row)
{
  enum outp_line_style style;
  style = rule_to_draw_type (get_vrule (r->table, col, row));
  if (style != OUTP_L_NONE)
    r->driver->class->line (r->driver, x, y, x + r->wrv[col], y + r->h[row],
                            style, OUTP_L_NONE, style, OUTP_L_NONE);
}

/* Renders the rule intersection at the given column and row
   at (X,Y) on the page. */
static void
render_rule_intersection (const struct tab_rendering *r,
                          int x, int y, int col, int row)
{
  const struct tab_table *t = r->table;

  /* Bounds of intersection. */
  int x0 = x;
  int y0 = y;
  int x1 = x + r->wrv[col];
  int y1 = y + r->hrh[row];

  /* Lines on each side of intersection. */
  int top = row > 0 ? get_vrule (t, col, row - 1) : TAL_0;
  int left = col > 0 ? get_hrule (t, col - 1, row) : TAL_0;
  int bottom = row < tab_nr (t) ? get_vrule (t, col, row) : TAL_0;
  int right = col < tab_nc (t) ? get_hrule (t, col, row) : TAL_0;

  /* Output style for each line. */
  enum outp_line_style o_top = rule_to_draw_type (top);
  enum outp_line_style o_left = rule_to_draw_type (left);
  enum outp_line_style o_bottom = rule_to_draw_type (bottom);
  enum outp_line_style o_right = rule_to_draw_type (right);

  if (o_top != OUTP_L_NONE || o_left != OUTP_L_NONE
      || o_bottom != OUTP_L_NONE || o_right != OUTP_L_NONE)
    r->driver->class->line (r->driver, x0, y0, x1, y1,
                            o_top, o_left, o_bottom, o_right);
}

/* Returns the width of columns C1...C2 exclusive,
   including interior but not exterior rules. */
static int
strip_width (const struct tab_rendering *r, int c1, int c2)
{
  int width = 0;
  int c;

  for (c = c1; c < c2; c++)
    width += r->w[c] + r->wrv[c + 1];
  if (c1 < c2)
    width -= r->wrv[c2];
  return width;
}

/* Returns the height of rows R1...R2 exclusive,
   including interior but not exterior rules. */
static int
strip_height (const struct tab_rendering *r, int r1, int r2)
{
  int height = 0;
  int row;

  for (row = r1; row < r2; row++)
    height += r->h[row] + r->hrh[row + 1];
  if (r1 < r2)
    height -= r->hrh[r2];
  return height;
}

/* Renders the cell at the given column and row at (X,Y) on the
   page.  Also renders joined cells that extend as far to the
   right as C1 and as far down as R1. */
static void
render_cell (const struct tab_rendering *r,
             int x, int y, int col, int row, int c1, int r1)
{
  const struct tab_table *t = r->table;
  const int index = col + (row * t->cf);
  unsigned char type = t->ct[index];
  struct substring *content = &t->cc[index];

  if (!(type & TAB_JOIN))
    {
      if (!(type & TAB_EMPTY))
        {
          struct outp_text text;
          text.font = options_to_font (type);
          text.justification = translate_justification (type);
          text.string = *content;
          text.h = r->w[col];
          text.v = r->h[row];
          text.x = x;
          text.y = y;
          r->driver->class->text_draw (r->driver, &text);
        }
    }
  else
    {
      struct tab_joined_cell *j
        = (struct tab_joined_cell *) ss_data (*content);

      if (j->x1 == col && j->y1 == row)
        {
          struct outp_text text;
          text.font = options_to_font (type);
          text.justification = translate_justification (type);
          text.string = j->contents;
          text.x = x;
          text.y = y;
          text.h = strip_width (r, j->x1, MIN (j->x2, c1));
          text.v = strip_height (r, j->y1, MIN (j->y2, r1));
          r->driver->class->text_draw (r->driver, &text);
        }
    }
}

/* Render contiguous strip consisting of columns C0...C1, exclusive,
   on row ROW, at (X,Y).  Returns X position after rendering.
   Also renders joined cells that extend beyond that strip,
   cropping them to lie within rendering region (C0,R0)-(C1,R1).
   C0 and C1 count vertical rules as columns.
   ROW counts horizontal rules as rows, but R0 and R1 do not. */
static int
render_strip (const struct tab_rendering *r,
              int x, int y, int row, int c0, int c1, int r0 UNUSED, int r1)
{
  int col;

  for (col = c0; col < c1; col++)
    if (col & 1)
      {
        if (row & 1)
          render_cell (r, x, y, col / 2, row / 2, c1 / 2, r1);
        else
          render_horz_rule (r, x, y, col / 2, row / 2);
        x += r->w[col / 2];
      }
    else
      {
        if (row & 1)
          render_vert_rule (r, x, y, col / 2, row / 2);
        else
          render_rule_intersection (r, x, y, col / 2, row / 2);
        x += r->wrv[col / 2];
      }

  return x;
}
