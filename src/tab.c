/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include "tab.h"
#include <ctype.h>
#include <assert.h>
#include <stdarg.h>
#include <limits.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "format.h"
#include "magic.h"
#include "misc.h"
#include "output.h"
#include "pool.h"
#include "som.h"
#include "var.h"

#include "debug-print.h"

struct som_table_class tab_table_class;

#if DEBUGGING
#define DEFFIRST(NAME, LABEL) LABEL,
#define DEFTAB(NAME, LABEL) LABEL,
/*
static const char *tab_names[] =
  {
#include "tab.def"
  };
*/
#undef DEFFIRST
#undef DEFTAB
#endif

/* Creates a table with NC columns and NR rows.  If REALLOCABLE is
   nonzero then the table's size can be increased later; otherwise,
   its size can only be reduced. */
struct tab_table *
tab_create (int nc, int nr, int reallocable)
{
  void *(*alloc_func) (struct pool *, size_t);

  struct tab_table *t;
  
  {
    struct pool *container = pool_create ();
    t = pool_alloc (container, sizeof *t);
    t->container = container;
  }
  
  t->col_style = TAB_COL_NONE;
  t->col_group = 0;
  ls_null (&t->title);
  t->flags = SOMF_NONE;
  t->nr = nr;
  t->nc = t->cf = nc;
  t->l = t->r = t->t = t->b = 0;

  alloc_func = reallocable ? pool_malloc : pool_alloc;
#if GLOBAL_DEBUGGING
  t->reallocable = reallocable;
#endif

  t->cc = alloc_func (t->container, nr * nc * sizeof *t->cc);
  t->ct = alloc_func (t->container, nr * nc);
  memset (t->ct, TAB_EMPTY, nc * nr);

  t->rh = alloc_func (t->container, nc * (nr + 1));
  memset (t->rh, 0, nc * (nr + 1));

  t->hrh = alloc_func (t->container, sizeof *t->hrh * (nr + 1));
  memset (t->hrh, 0, sizeof *t->hrh * (nr + 1));

  t->trh = alloc_func (t->container, nr + 1);
  memset (t->trh, 0, nr + 1);

  t->rv = alloc_func (t->container, (nc + 1) * nr);
  memset (t->rv, 0, (nc + 1) * nr);

  t->wrv = alloc_func (t->container, sizeof *t->wrv * (nc + 1));
  memset (t->wrv, 0, sizeof *t->wrv * (nc + 1));

  t->trv = alloc_func (t->container, nc + 1);
  memset (t->trv, 0, nc + 1);

  t->dim = NULL;
  t->w = t->h = NULL;
  t->col_ofs = t->row_ofs = 0;
  
  return t;
}

/* Destroys table T. */
void
tab_destroy (struct tab_table *t)
{
  assert (t != NULL);
  pool_destroy (t->container);
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
      assert (nr + t->row_ofs <= t->nr);
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
#if GLOBAL_DEBUGGING
  assert (t->reallocable);
#endif
  ro = t->row_ofs;
  co = t->col_ofs;
  if (ro || co)
    tab_offset (t, 0, 0);

  if (nc == -1)
    nc = t->nc;
  if (nr == -1)
    nr = t->nr;
  
  assert (nc == t->nc);
  
  if (nc > t->cf)
    {
      int mr1 = min (nr, t->nr);
      int mc1 = min (nc, t->nc);
      
      struct len_string *new_cc;
      unsigned char *new_ct;
      int r;

      new_cc = pool_malloc (t->container, nr * nc * sizeof *new_cc);
      new_ct = pool_malloc (t->container, nr * nc);
      for (r = 0; r < mr1; r++)
	{
	  memcpy (&new_cc[r * nc], &t->cc[r * t->nc], mc1 * sizeof *t->cc);
	  memcpy (&new_ct[r * nc], &t->ct[r * t->nc], mc1);
	  memset (&new_ct[r * nc + t->nc], TAB_EMPTY, nc - t->nc);
	}
      pool_free (t->container, t->cc);
      pool_free (t->container, t->ct);
      t->cc = new_cc;
      t->ct = new_ct;
      t->cf = nc;
    }
  else if (nr != t->nr)
    {
      t->cc = pool_realloc (t->container, t->cc, nr * nc * sizeof *t->cc);
      t->ct = pool_realloc (t->container, t->ct, nr * nc);

      t->rh = pool_realloc (t->container, t->rh, nc * (nr + 1));
      t->rv = pool_realloc (t->container, t->rv, (nc + 1) * nr);
      t->trh = pool_realloc (t->container, t->trh, nr + 1);
      t->hrh = pool_realloc (t->container, t->hrh,
			     sizeof *t->hrh * (nr + 1));
      
      if (nr > t->nr)
	{
	  memset (&t->rh[nc * (t->nr + 1)], 0, (nr - t->nr) * nc);
	  memset (&t->rv[(nc + 1) * t->nr], 0, (nr - t->nr) * (nc + 1));
	  memset (&t->trh[t->nr + 1], 0, nr - t->nr);
	}
    }

  memset (&t->ct[nc * t->nr], TAB_EMPTY, nc * (nr - t->nr));
  
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
  table->l = l;
  table->r = r;
  table->t = t;
  table->b = b;
}

/* Set up table T so that, when it is an appropriate size, it will be
   displayed across the page in columns.

   STYLE is a TAB_COL_* constant.  GROUP is the number of rows to take
   as a unit. */
void
tab_columns (struct tab_table *t, int style, int group)
{
  assert (t != NULL);
  t->col_style = style;
  t->col_group = group;
}

/* Rules. */

/* Draws a vertical line to the left of cells at horizontal position X
   from Y1 to Y2 inclusive in style STYLE, if style is not -1. */
void
tab_vline (struct tab_table *t, int style, int x, int y1, int y2)
{
  int y;

  assert (t != NULL);
#if GLOBAL_DEBUGGING
  if (x + t->col_ofs < 0 || x + t->col_ofs > t->nc
      || y1 + t->row_ofs < 0 || y1 + t->row_ofs >= t->nr
      || y2 + t->row_ofs < 0 || y2 + t->row_ofs >= t->nr)
    {
      printf (_("bad vline: x=%d+%d=%d y=(%d+%d=%d,%d+%d=%d) in "
		"table size (%d,%d)\n"),
	      x, t->col_ofs, x + t->col_ofs,
	      y1, t->row_ofs, y1 + t->row_ofs,
	      y2, t->row_ofs, y2 + t->row_ofs,
	      t->nc, t->nr);
      return;
    }
#endif

  x += t->col_ofs;
  y1 += t->row_ofs;
  y2 += t->row_ofs;

  if (style != -1)
    {
      if ((style & TAL_SPACING) == 0)
	for (y = y1; y <= y2; y++)
	  t->rv[x + (t->cf + 1) * y] = style;
      t->trv[x] |= (1 << (style & ~TAL_SPACING));
    }
}

/* Draws a horizontal line above cells at vertical position Y from X1
   to X2 inclusive in style STYLE, if style is not -1. */
void
tab_hline (struct tab_table * t, int style, int x1, int x2, int y)
{
  int x;

  assert (t != NULL);
#if GLOBAL_DEBUGGING
  if (x1 + t->col_ofs < 0 || x1 + t->col_ofs >= t->nc 
      || x2 + t->col_ofs < 0 || x2 + t->col_ofs >= t->nc
      || y + t->row_ofs < 0 || y + t->row_ofs > t->nr)
    {
      printf (_("bad hline: x=(%d+%d=%d,%d+%d=%d) y=%d+%d=%d "
		"in table size (%d,%d)\n"),
	      x1, t->col_ofs, x1 + t->col_ofs,
	      x2, t->col_ofs, x2 + t->col_ofs,
	      y, t->row_ofs, y + t->row_ofs,
	      t->nc, t->nr);
      return;
    }
#endif

  x1 += t->col_ofs;
  x2 += t->col_ofs;
  y += t->row_ofs;

  if (style != -1)
    {
      if ((style & TAL_SPACING) == 0)
	for (x = x1; x <= x2; x++)
	  t->rh[x + t->cf * y] = style;
      t->trh[y] |= (1 << (style & ~TAL_SPACING));
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
#if GLOBAL_DEBUGGING
  if (x1 + t->col_ofs < 0 || x1 + t->col_ofs >= t->nc 
      || x2 + t->col_ofs < 0 || x2 + t->col_ofs >= t->nc
      || y1 + t->row_ofs < 0 || y1 + t->row_ofs >= t->nr 
      || y2 + t->row_ofs < 0 || y2 + t->row_ofs >= t->nr)
    {
      printf (_("bad box: (%d+%d=%d,%d+%d=%d)-(%d+%d=%d,%d+%d=%d) "
		"in table size (%d,%d)\n"),
	      x1, t->col_ofs, x1 + t->col_ofs,
	      y1, t->row_ofs, y1 + t->row_ofs,
	      x2, t->col_ofs, x2 + t->col_ofs,
	      y2, t->row_ofs, y2 + t->row_ofs,
	      t->nc, t->nr);
      abort ();
    }
#endif

  x1 += t->col_ofs;
  x2 += t->col_ofs;
  y1 += t->row_ofs;
  y2 += t->row_ofs;

  if (f_h != -1)
    {
      int x;
      if ((f_h & TAL_SPACING) == 0)
	for (x = x1; x <= x2; x++)
	  {
	    t->rh[x + t->cf * y1] = f_h;
	    t->rh[x + t->cf * (y2 + 1)] = f_h;
	  }
      t->trh[y1] |= (1 << (f_h & ~TAL_SPACING));
      t->trh[y2 + 1] |= (1 << (f_h & ~TAL_SPACING));
    }
  if (f_v != -1)
    {
      int y;
      if ((f_v & TAL_SPACING) == 0)
	for (y = y1; y <= y2; y++)
	  {
	    t->rv[x1 + (t->cf + 1) * y] = f_v;
	    t->rv[(x2 + 1) + (t->cf + 1) * y] = f_v;
	  }
      t->trv[x1] |= (1 << (f_v & ~TAL_SPACING));
      t->trv[x2 + 1] |= (1 << (f_v & ~TAL_SPACING));
    }

  if (i_h != -1)
    {
      int y;
      
      for (y = y1 + 1; y <= y2; y++)
	{
	  int x;

	  if ((i_h & TAL_SPACING) == 0)
	    for (x = x1; x <= x2; x++)
	      t->rh[x + t->cf * y] = i_h;

	  t->trh[y] |= (1 << (i_h & ~TAL_SPACING));
	}
    }
  if (i_v != -1)
    {
      int x;
      
      for (x = x1 + 1; x <= x2; x++)
	{
	  int y;
	  
	  if ((i_v & TAL_SPACING) == 0)
	    for (y = y1; y <= y2; y++)
	      t->rv[x + (t->cf + 1) * y] = i_v;

	  t->trv[x] |= (1 << (i_v & ~TAL_SPACING));
	}
    }
}

/* Formats text TEXT and arguments ARGS as indicated in OPT and sets
   the resultant string into S in TABLE's pool. */
static void
text_format (struct tab_table *table, int opt, const char *text, va_list args,
	     struct len_string *s)
{
  int len;
  
  assert (table != NULL && text != NULL && s != NULL);
  
  if (opt & TAT_PRINTF)
    {
      char *temp_buf = local_alloc (1024);
      
      len = nvsprintf (temp_buf, text, args);
      text = temp_buf;
    }
  else
    len = strlen (text);

  ls_create_buffer (table->container, s, text, len);
  
  if (opt & TAT_PRINTF)
    local_free (text);
}

/* Set the title of table T to TITLE, which is formatted with printf
   if FORMAT is nonzero. */
void
tab_title (struct tab_table *t, int format, const char *title, ...)
{
  va_list args;

  assert (t != NULL && title != NULL);
  va_start (args, title);
  text_format (t, format ? TAT_PRINTF : TAT_NONE, title, args, &t->title);
  va_end (args);
}

/* Set DIM_FUNC as the dimension function for table T. */
void
tab_dim (struct tab_table *t, tab_dim_func *dim_func)
{
  assert (t != NULL && t->dim == NULL);
  t->dim = dim_func;
}

/* Returns the natural width of column C in table T for driver D, that
   is, the smallest width necessary to display all its cells without
   wrapping.  The width will be no larger than the page width minus
   left and right rule widths. */
int
tab_natural_width (struct tab_table *t, struct outp_driver *d, int c)
{
  int width;

  assert (t != NULL && c >= 0 && c < t->nc);
  {
    int r;

    for (width = r = 0; r < t->nr; r++)
      {
	struct outp_text text;
	unsigned char opt = t->ct[c + r * t->cf];
		
	if (opt & (TAB_JOIN | TAB_EMPTY))
	  continue;

	text.s = t->cc[c + r * t->cf];
	assert (!ls_null_p (&text.s));
	text.options = OUTP_T_JUST_LEFT;

	d->class->text_metrics (d, &text);
	if (text.h > width)
	  width = text.h;
      }
  }

  if (width == 0)
    {
      width = d->prop_em_width * 8;
#if GLOBAL_DEBUGGING
      printf ("warning: table column %d contains no data.\n", c);
#endif
    }
  
  {
    const int clamp = d->width - t->wrv[0] - t->wrv[t->nc];
    
    if (width > clamp)
      width = clamp;
  }

  return width;
}

/* Returns the natural height of row R in table T for driver D, that
   is, the minimum height necessary to display the information in the
   cell at the widths set for each column. */
int
tab_natural_height (struct tab_table *t, struct outp_driver *d, int r)
{
  int height;

  assert (t != NULL && r >= 0 && r < t->nr);
  
  {
    int c;
    
    for (height = d->font_height, c = 0; c < t->nc; c++)
      {
	struct outp_text text;
	unsigned char opt = t->ct[c + r * t->cf];

	assert (t->w[c] != NOT_INT);
	if (opt & (TAB_JOIN | TAB_EMPTY))
	  continue;

	text.s = t->cc[c + r * t->cf];
	assert (!ls_null_p (&text.s));
	text.options = OUTP_T_HORZ | OUTP_T_JUST_LEFT;
	text.h = t->w[c];
	d->class->text_metrics (d, &text);

	if (text.v > height)
	  height = text.v;
      }
  }

  return height;
}

/* Callback function to set all columns and rows to their natural
   dimensions.  Not really meant to be called directly.  */
void
tab_natural_dimensions (struct tab_table *t, struct outp_driver *d)
{
  int i;

  assert (t != NULL);
  
  for (i = 0; i < t->nc; i++)
    t->w[i] = tab_natural_width (t, d, i);
  
  for (i = 0; i < t->nr; i++)
    t->h[i] = tab_natural_height (t, d, i);
}


/* Cells. */

/* Sets cell (C,R) in TABLE, with options OPT, to have a value taken
   from V, displayed with format spec F. */
void
tab_value (struct tab_table *table, int c, int r, unsigned char opt,
	   const union value *v, const struct fmt_spec *f)
{
  char *contents;
  union value temp_val;

  assert (table != NULL && v != NULL && f != NULL);
#if GLOBAL_DEBUGGING
  if (c + table->col_ofs < 0 || r + table->row_ofs < 0
      || c + table->col_ofs >= table->nc
      || r + table->row_ofs >= table->nr)
    {
      printf ("tab_value(): bad cell (%d+%d=%d,%d+%d=%d) in table size "
	      "(%d,%d)\n",
	      c, table->col_ofs, c + table->col_ofs,
	      r, table->row_ofs, r + table->row_ofs,
	      table->nc, table->nr);
      return;
    }
#endif

  contents = pool_alloc (table->container, f->w);
  ls_init (&table->cc[c + r * table->cf], contents, f->w);
  table->ct[c + r * table->cf] = opt;
  
  if (formats[f->type].cat & FCAT_STRING)
    {
      temp_val.c = (char *) v->s;
      v = &temp_val;
    }
  data_out (contents, f, v);
}

/* Sets cell (C,R) in TABLE, with options OPT, to have value VAL
   with NDEC decimal places. */
void
tab_float (struct tab_table *table, int c, int r, unsigned char opt,
	   double val, int w, int d)
{
  char *contents;
  char buf[40], *cp;
  
  struct fmt_spec f;

  assert (table != NULL && w <= 40);
  
  f.type = FMT_F;
  f.w = w;
  f.d = d;
  
#if GLOBAL_DEBUGGING
  if (c + table->col_ofs < 0 || r + table->row_ofs < 0
      || c + table->col_ofs >= table->nc
      || r + table->row_ofs >= table->nr)
    {
      printf ("tab_float(): bad cell (%d+%d=%d,%d+%d=%d) in table size "
	      "(%d,%d)\n",
	      c, table->col_ofs, c + table->col_ofs,
	      r, table->row_ofs, r + table->row_ofs,
	      table->nc, table->nr);
      return;
    }
#endif

  data_out (buf, &f, (union value *) &val);
  cp = buf;
  while (isspace ((unsigned char) *cp) && cp < &buf[w])
    cp++;
  f.w = w - (cp - buf);

  contents = pool_alloc (table->container, f.w);
  ls_init (&table->cc[c + r * table->cf], contents, f.w);
  table->ct[c + r * table->cf] = opt;
  memcpy (contents, cp, f.w);
}

/* Sets cell (C,R) in TABLE, with options OPT, to have text value
   TEXT. */
void
tab_text (struct tab_table *table, int c, int r, unsigned opt, const char *text, ...)
{
  va_list args;

  assert (table != NULL && text != NULL);
#if GLOBAL_DEBUGGING
  if (c + table->col_ofs < 0 || r + table->row_ofs < 0
      || c + table->col_ofs >= table->nc
      || r + table->row_ofs >= table->nr)
    {
      printf ("tab_text(): bad cell (%d+%d=%d,%d+%d=%d) in table size "
	      "(%d,%d)\n",
	      c, table->col_ofs, c + table->col_ofs,
	      r, table->row_ofs, r + table->row_ofs,
	      table->nc, table->nr);
      return;
    }
#endif
    
  va_start (args, text);
  text_format (table, opt, text, args, &table->cc[c + r * table->cf]);
  table->ct[c + r * table->cf] = opt;
  va_end (args);
}

/* Joins cells (X1,X2)-(Y1,Y2) inclusive in TABLE, and sets them with
   options OPT to have text value TEXT. */
void
tab_joint_text (struct tab_table *table, int x1, int y1, int x2, int y2,
		unsigned opt, const char *text, ...)
{
  struct tab_joined_cell *j;

  assert (table != NULL && text != NULL);
#if GLOBAL_DEBUGGING
  if (x1 + table->col_ofs < 0 || x1 + table->col_ofs >= table->nc
      || y1 + table->row_ofs < 0 || y1 + table->row_ofs >= table->nr
      || x2 < x1 || x2 + table->col_ofs >= table->nc
      || y2 < y2 || y2 + table->row_ofs >= table->nr)
    {
      printf ("tab_joint_text(): bad cell "
	      "(%d+%d=%d,%d+%d=%d)-(%d+%d=%d,%d+%d=%d) in table size (%d,%d)\n",
	      x1, table->col_ofs, x1 + table->col_ofs,
	      y1, table->row_ofs, y1 + table->row_ofs,
	      x2, table->col_ofs, x2 + table->col_ofs,
	      y2, table->row_ofs, y2 + table->row_ofs,
	      table->nc, table->nr);
      return;
    }
#endif
  
  j = pool_alloc (table->container, sizeof *j);
  j->hit = 0;
  j->x1 = x1 + table->col_ofs;
  j->y1 = y1 + table->row_ofs;
  j->x2 = ++x2 + table->col_ofs;
  j->y2 = ++y2 + table->row_ofs;
  
  {
    va_list args;
    
    va_start (args, text);
    text_format (table, opt, text, args, &j->contents);
    va_end (args);
  }
  
  opt |= TAB_JOIN;
  
  {
    struct len_string *cc = &table->cc[x1 + y1 * table->cf];
    unsigned char *ct = &table->ct[x1 + y1 * table->cf];
    const int ofs = table->cf - (x2 - x1);

    int y;
    
    for (y = y1; y < y2; y++)
      {
	int x;
	
	for (x = x1; x < x2; x++)
	  {
	    ls_init (cc++, (char *) j, 0);
	    *ct++ = opt;
	  }
	
	cc += ofs;
	ct += ofs;
      }
  }
}

/* Sets cell (C,R) in TABLE, with options OPT, to contents STRING. */
void
tab_raw (struct tab_table *table, int c, int r, unsigned opt,
	 struct len_string *string)
{
  assert (table != NULL && string != NULL);
  
#if GLOBAL_DEBUGGING
  if (c + table->col_ofs < 0 || r + table->row_ofs < 0
      || c + table->col_ofs >= table->nc
      || r + table->row_ofs >= table->nr)
    {
      printf ("tab_float(): bad cell (%d+%d=%d,%d+%d=%d) in table size "
	      "(%d,%d)\n",
	      c, table->col_ofs, c + table->col_ofs,
	      r, table->row_ofs, r + table->row_ofs,
	      table->nc, table->nr);
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
nowrap_dim (struct tab_table *t, struct outp_driver *d)
{
  t->w[0] = tab_natural_width (t, d, 0);
  t->h[0] = d->font_height;
}

/* Sets the widths of all the columns and heights of all the rows in
   table T for driver D. */
static void
wrap_dim (struct tab_table *t, struct outp_driver *d)
{
  t->w[0] = tab_natural_width (t, d, 0);
  t->h[0] = tab_natural_height (t, d, 0);
}

/* Outputs text BUF as a table with a single cell having cell options
   OPTIONS, which is a combination of the TAB_* and TAT_*
   constants. */
void
tab_output_text (int options, const char *buf, ...)
{
  struct tab_table *t = tab_create (1, 1, 0);

  assert (buf != NULL);
  if (options & TAT_PRINTF)
    {
      va_list args;
      char *temp_buf = local_alloc (4096);
      
      va_start (args, buf);
      nvsprintf (temp_buf, buf, args);
      buf = temp_buf;
      va_end (args);
    }
  
  if (options & TAT_FIX)
    {
      struct outp_driver *d;

      for (d = outp_drivers (NULL); d; d = outp_drivers (d))
	{
	  if (!d->page_open)
	    d->class->open_page (d);

          if (d->class->text_set_font_by_name != NULL)
            d->class->text_set_font_by_name (d, "FIXED");
          else 
            {
              /* FIXME */
            }
	}
    }

  tab_text (t, 0, 0, options &~ TAT_PRINTF, buf);
  tab_flags (t, SOMF_NO_TITLE | SOMF_NO_SPACING);
  if (options & TAT_NOWRAP)
    tab_dim (t, nowrap_dim);
  else
    tab_dim (t, wrap_dim);
  tab_submit (t);

  if (options & TAT_FIX)
    {
      struct outp_driver *d;

      for (d = outp_drivers (NULL); d; d = outp_drivers (d))
        if (d->class->text_set_font_by_name != NULL)
          d->class->text_set_font_by_name (d, "PROP");
        else 
          {
            /* FIXME */
          }
    }
  
  if (options & TAT_PRINTF)
    local_free (buf);
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
  struct som_table s;

  assert (t != NULL);
  s.class = &tab_table_class;
  s.ext = t;
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
#if GLOBAL_DEBUGGING
  if (row < -1 || row >= t->nr)
    {
      printf ("tab_offset(): row=%d in %d-row table\n", row, t->nr);
      abort ();
    }
  if (col < -1 || col >= t->nc)
    {
      printf ("tab_offset(): col=%d in %d-column table\n", col, t->nc);
      abort ();
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
  if (++t->row_ofs >= t->nr)
    tab_realloc (t, -1, t->nr * 4 / 3);
}

static struct tab_table *t;
static struct outp_driver *d;
int tab_hit;

/* Set the current table to TABLE. */
static void
tabi_table (struct som_table *table)
{
  assert (table != NULL);
  t = table->ext;
  tab_offset (t, 0, 0);
  
  assert (t->w == NULL && t->h == NULL);
  t->w = pool_alloc (t->container, sizeof *t->w * t->nc);
  t->h = pool_alloc (t->container, sizeof *t->h * t->nr);
}

/* Set the current output device to DRIVER. */
static void
tabi_driver (struct outp_driver *driver)
{
  int i;

  assert (driver != NULL);
  d = driver;
  
  /* Figure out sizes of rules. */
  for (t->hr_tot = i = 0; i <= t->nr; i++)
    t->hr_tot += t->hrh[i] = d->horiz_line_spacing[t->trh[i]];
  for (t->vr_tot = i = 0; i <= t->nc; i++)
    t->vr_tot += t->wrv[i] = d->vert_line_spacing[t->trv[i]];

#if GLOBAL_DEBUGGING
  for (i = 0; i < t->nr; i++)
    t->h[i] = -1;
  for (i = 0; i < t->nc; i++)
    t->w[i] = -1;
#endif

  assert (t->dim != NULL);
  t->dim (t, d);

#if GLOBAL_DEBUGGING
  {
    int error = 0;

    for (i = 0; i < t->nr; i++)
      {
	if (t->h[i] == -1)
	  {
	    printf ("Table row %d height not initialized.\n", i);
	    error = 1;
	  }
	assert (t->h[i] > 0);
      }
    
    for (i = 0; i < t->nc; i++)
      {
	if (t->w[i] == -1)
	  {
	    printf ("Table column %d width not initialized.\n", i);
	    error = 1;
	  }
	assert (t->w[i] > 0);
      }
  }
#endif
    
  /* Add up header sizes. */
  for (i = 0, t->wl = t->wrv[0]; i < t->l; i++)
    t->wl += t->w[i] + t->wrv[i + 1];
  for (i = 0, t->ht = t->hrh[0]; i < t->t; i++)
    t->ht += t->h[i] + t->hrh[i + 1];
  for (i = t->nc - t->r, t->wr = t->wrv[i]; i < t->nc; i++)
    t->wr += t->w[i] + t->wrv[i + 1];
  for (i = t->nr - t->b, t->hb = t->hrh[i]; i < t->nr; i++)
    t->hb += t->h[i] + t->hrh[i + 1];
  
  /* Title. */
  if (!(t->flags & SOMF_NO_TITLE))
    t->ht += d->font_height;
}

/* Return the number of columns and rows in the table into N_COLUMNS
   and N_ROWS, respectively. */
static void
tabi_count (int *n_columns, int *n_rows)
{
  assert (n_columns != NULL && n_rows != NULL);
  *n_columns = t->nc;
  *n_rows = t->nr;
}

static void tabi_cumulate (int cumtype, int start, int *end, int max, int *actual);

/* Return the horizontal and vertical size of the entire table,
   including headers, for the current output device, into HORIZ and
   VERT. */
static void
tabi_area (int *horiz, int *vert)
{
  assert (horiz != NULL && vert != NULL);
  
  {
    int w, c;
    
    for (c = t->l + 1, w = t->wl + t->wr + t->w[t->l];
	 c < t->nc - t->r; c++)
      w += t->w[c] + t->wrv[c];
    *horiz = w;
  }
  
  {
    int h, r;
    for (r = t->t + 1, h = t->ht + t->hb + t->h[t->t];
	 r < t->nr - t->b; r++)
      h += t->h[r] + t->hrh[r];
    *vert = h;
  }
}

/* Return the column style for this table into STYLE. */
static void
tabi_columns (int *style)
{
  assert (style != NULL);
  *style = t->col_style;
}

/* Return the number of header rows/columns on the left, right, top,
   and bottom sides into HL, HR, HT, and HB, respectively. */
static void
tabi_headers (int *hl, int *hr, int *ht, int *hb)
{
  assert (hl != NULL && hr != NULL && ht != NULL && hb != NULL);
  *hl = t->l;
  *hr = t->r;
  *ht = t->t;
  *hb = t->b;
}

/* Determines the number of rows or columns (including appropriate
   headers), depending on CUMTYPE, that will fit into the space
   specified.  Takes rows/columns starting at index START and attempts
   to fill up available space MAX.  Returns in END the index of the
   last row/column plus one; returns in ACTUAL the actual amount of
   space the selected rows/columns (including appropriate headers)
   filled. */
static void
tabi_cumulate (int cumtype, int start, int *end, int max, int *actual)
{
  int n;
  int *d;
  int *r;
  int total;
  
  assert (end != NULL && (cumtype == SOM_ROWS || cumtype == SOM_COLUMNS));
  if (cumtype == SOM_ROWS)
    {
      assert (start >= 0 && start < t->nr);
      n = t->nr - t->b;
      d = &t->h[start];
      r = &t->hrh[start + 1];
      total = t->ht + t->hb;
    } else {
      assert (start >= 0 && start < t->nc);
      n = t->nc - t->r;
      d = &t->w[start];
      r = &t->wrv[start + 1];
      total = t->wl + t->wr;
    }
  
  total += *d++;
  if (total > max)
    {
      if (end)
	*end = start;
      if (actual)
	*actual = 0;
      return;
    }
    
  {
    int x;
      
    for (x = start + 1; x < n; x++)
      {
	int amt = *d++ + *r++;
	
	total += amt;
	if (total > max)
	  {
	    total -= amt;
	    break;
	  }
      }

    if (end)
      *end = x;
    
    if (actual)
      *actual = total;
  }
}

/* Return flags set for the current table into FLAGS. */
static void
tabi_flags (unsigned *flags)
{
  assert (flags != NULL);
  *flags = t->flags;
}

/* Render title for current table, with major index X and minor index
   Y.  Y may be zero, or X and Y may be zero, but X should be nonzero
   if Y is nonzero. */
static void
tabi_title (int x, int y)
{
  char buf[1024];
  char *cp;

  if (t->flags & SOMF_NO_TITLE)
    return;
  
  cp = spprintf (buf, "%d.%d", table_num, subtable_num);
  if (x && y)
    cp = spprintf (cp, "(%d:%d)", x, y);
  else if (x)
    cp = spprintf (cp, "(%d)", x);
  if (cur_proc)
    cp = spprintf (cp, " %s", cur_proc);
  cp = stpcpy (cp, ".  ");
  if (!ls_empty_p (&t->title))
    {
      memcpy (cp, ls_value (&t->title), ls_length (&t->title));
      cp += ls_length (&t->title);
    }
  *cp = 0;
  
  {
    struct outp_text text;

    text.options = OUTP_T_JUST_LEFT | OUTP_T_HORZ | OUTP_T_VERT;
    ls_init (&text.s, buf, cp - buf);
    text.h = d->width;
    text.v = d->font_height;
    text.x = 0;
    text.y = d->cp_y;
    d->class->text_draw (d, &text);
  }
}

static int render_strip (int x, int y, int r, int c1, int c2, int r1, int r2);

/* Draws the table region in rectangle (X1,Y1)-(X2,Y2), where column
   X2 and row Y2 are not included in the rectangle, at the current
   position on the current output device.  Draws headers as well. */
static void
tabi_render (int x1, int y1, int x2, int y2)
{
  int i, y;
  int ranges[3][2];
  
  tab_hit++;

  y = d->cp_y;
  if (!(t->flags & SOMF_NO_TITLE))
    y += d->font_height;

  /* Top headers. */
  ranges[0][0] = 0;
  ranges[0][1] = t->t * 2 + 1;

  /* Requested rows. */
  ranges[1][0] = y1 * 2 + 1;
  ranges[1][1] = y2 * 2;

  /* Bottom headers. */
  ranges[2][0] = (t->nr - t->b) * 2;
  ranges[2][1] = t->nr * 2 + 1;

  for (i = 0; i < 3; i++) 
    {
      int r;

      for (r = ranges[i][0]; r < ranges[i][1]; r++) 
        {
          int x = d->cp_x;
          x += render_strip (x, y, r, 0, t->l * 2 + 1, y1, y2);
          x += render_strip (x, y, r, x1 * 2 + 1, x2 * 2, y1, y2);
          x += render_strip (x, y, r, (t->nc - t->r) * 2,
                             t->nc * 2 + 1, y1, y2);
          y += (r & 1) ? t->h[r / 2] : t->hrh[r / 2]; 
        }
    }
}

struct som_table_class tab_table_class =
  {
    tabi_table,
    tabi_driver,
    
    tabi_count,
    tabi_area,
    NULL,
    NULL,
    tabi_columns,
    NULL,
    tabi_headers,
    NULL,
    tabi_cumulate,
    tabi_flags,
    
    NULL,
    NULL,

    tabi_title,
    tabi_render,
  };

/* Render contiguous strip consisting of columns C1...C2, exclusive,
   on row R, at location (X,Y).  Return width of the strip thus
   rendered.

   Renders joined cells, even those outside the strip, within the
   rendering region (C1,R1)-(C2,R2).

   For the purposes of counting rows and columns in this function
   only, horizontal rules are considered rows and vertical rules are
   considered columns.

   FIXME: Doesn't use r1?  Huh?  */
static int
render_strip (int x, int y, int r, int c1, int c2, int r1 unused, int r2)
{
  int x_origin = x;

  /* Horizontal rules. */
  if ((r & 1) == 0)
    {
      int hrh = t->hrh[r / 2];
      int c;

      for (c = c1; c < c2; c++)
	{
	  if (c & 1)
	    {
	      int style = t->rh[(c / 2) + (r / 2 * t->cf)];

	      if (style != TAL_0)
		{
		  const struct color clr = {0, 0, 0, 0};
		  struct rect rct;

		  rct.x1 = x;
		  rct.y1 = y;
		  rct.x2 = x + t->w[c / 2];
		  rct.y2 = y + hrh;
		  d->class->line_horz (d, &rct, &clr, style);
		}
	      x += t->w[c / 2];
	    } else {
	      const struct color clr = {0, 0, 0, 0};
	      struct rect rct;
	      struct outp_styles s;

	      rct.x1 = x;
	      rct.y1 = y;
	      rct.x2 = x + t->wrv[c / 2];
	      rct.y2 = y + hrh;

	      s.t = r > 0 ? t->rv[(c / 2) + (t->cf + 1) * (r / 2 - 1)] : 0;
	      s.b = r < 2 * t->nr ? t->rv[(c / 2) + (t->cf + 1) * (r / 2)] : 0;
	      s.l = c > 0 ? t->rh[(c / 2 - 1) + t->cf * (r / 2)] : 0;
	      s.r = c < 2 * t->nc ? t->rh[(c / 2) + t->cf * (r / 2)] : 0;

	      if (s.t | s.b | s.l | s.r)
		d->class->line_intersection (d, &rct, &clr, &s);
	      
	      x += t->wrv[c / 2];
	    }
	}
    } else {
      int c;

      for (c = c1; c < c2; c++)
	{
	  if (c & 1)
	    {
	      const int index = (c / 2) + (r / 2 * t->cf);

	      if (!(t->ct[index] & TAB_JOIN))
		{
		  struct outp_text text;

		  text.options = ((t->ct[index] & OUTP_T_JUST_MASK)
				  | OUTP_T_HORZ | OUTP_T_VERT);
		  if ((t->ct[index] & TAB_EMPTY) == 0)
		    {
		      text.s = t->cc[index];
		      assert (!ls_null_p (&text.s));
		      text.h = t->w[c / 2];
		      text.v = t->h[r / 2];
		      text.x = x;
		      text.y = y;
		      d->class->text_draw (d, &text);
		    }
		} else {
		  struct tab_joined_cell *j =
		    (struct tab_joined_cell *) ls_value (&t->cc[index]);

		  if (j->hit != tab_hit)
		    {
		      j->hit = tab_hit;

		      if (j->x1 == c / 2 && j->y1 == r / 2
			  && j->x2 <= c2 && j->y2 <= r2)
			{
			  struct outp_text text;

			  text.options = ((t->ct[index] & OUTP_T_JUST_MASK)
					  | OUTP_T_HORZ | OUTP_T_VERT);
			  text.s = j->contents;
			  text.x = x;
			  text.y = y;
			  
			  {
			    int c;

			    for (c = j->x1, text.h = -t->wrv[j->x2];
				 c < j->x2; c++)
			      text.h += t->w[c] + t->wrv[c + 1];
			  }
			  
			  {
			    int r;

			    for (r = j->y1, text.v = -t->hrh[j->y2];
				 r < j->y2; r++)
			      text.v += t->h[r] + t->hrh[r + 1];
			  }
			  d->class->text_draw (d, &text);
			}
		    }
		}
	      x += t->w[c / 2];
	    } else {
	      int style = t->rv[(c / 2) + (r / 2 * (t->cf + 1))];

	      if (style != TAL_0)
		{
		  const struct color clr = {0, 0, 0, 0};
		  struct rect rct;

		  rct.x1 = x;
		  rct.y1 = y;
		  rct.x2 = x + t->wrv[c / 2];
		  rct.y2 = y + t->h[r / 2];
		  d->class->line_vert (d, &rct, &clr, style);
		}
	      x += t->wrv[c / 2];
	    }
	}
    }

  return x - x_origin;
}

