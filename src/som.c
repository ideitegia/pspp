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
#include "som.h"
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include "output.h"
#include "debug-print.h"

/* Table. */
int table_num = 1;
int subtable_num;

/* Increments table_num so different procedures' output can be
   distinguished. */
void
som_new_series (void)
{
  if (subtable_num != 0)
    {
      table_num++;
      subtable_num = 0;
    }
}

/* Ejects the paper for all active devices. */
void
som_eject_page (void)
{
  struct outp_driver *d;

  for (d = outp_drivers (NULL); d; d = outp_drivers (d))
    outp_eject_page (d);
}

/* Skip down a single line on all active devices. */
void
som_blank_line (void)
{
  struct outp_driver *d;
  
  for (d = outp_drivers (NULL); d; d = outp_drivers (d))
    if (d->page_open && d->cp_y != 0)
      d->cp_y += d->font_height;
}

/* Driver. */
static struct outp_driver *d=0;

/* Table. */
static struct som_table *t=0;

/* Flags. */
static unsigned flags;

/* Number of columns, rows. */
static int nc, nr;

/* Number of columns or rows in left, right, top, bottom headers. */
static int hl, hr, ht, hb;

/* Column style. */
static int cs;

/* Table height, width. */
static int th, tw;

static void render_columns (void);
static void render_simple (void);
static void render_segments (void);

static void output_table (struct outp_driver *, struct som_table *);

/* Output table T to appropriate output devices. */
void
som_submit (struct som_table *t)
{
#if GLOBAL_DEBUGGING
  static int entry;
  
  assert (entry++ == 0);
#endif

  t->class->table (t);
  t->class->flags (&flags);
  t->class->count (&nc, &nr);
  t->class->headers (&hl, &hr, &ht, &hb);

#if GLOBAL_DEBUGGING
  if (hl + hr > nc || ht + hb > nr)
    {
      printf ("headers: (l,r)=(%d,%d), (t,b)=(%d,%d) in table size (%d,%d)\n",
	      hl, hr, ht, hb, nc, nr);
      abort ();
    }
  else if (hl + hr == nc)
    printf ("warning: headers (l,r)=(%d,%d) in table width %d\n", hl, hr, nc);
  else if (ht + hb == nr)
    printf ("warning: headers (t,b)=(%d,%d) in table height %d\n", ht, hb, nr);
#endif

  t->class->columns (&cs);

  if (!(flags & SOMF_NO_TITLE))
    subtable_num++;
    
  {
    struct outp_driver *d;

    for (d = outp_drivers (NULL); d; d = outp_drivers (d))
      output_table (d, t);
  }
  
#if GLOBAL_DEBUGGING
  assert (--entry == 0);
#endif
}

/* Output table TABLE to driver DRIVER. */
static void
output_table (struct outp_driver *driver, struct som_table *table)
{
  d = driver;
  t = table;

  assert (d->driver_open);
  if (!d->page_open && !d->class->open_page (d))
    {
      d->device = OUTP_DEV_DISABLED;
      return;
    }
  
  if (d->class->special)
    {
      driver->class->submit (d, t);
      return;
    }
  
  t->class->driver (d);
  t->class->area (&tw, &th);
  
  if (!(flags & SOMF_NO_SPACING) && d->cp_y != 0)
    d->cp_y += d->font_height;
	
  if (cs != SOM_COL_NONE
      && 2 * (tw + d->prop_em_width) <= d->width
      && nr - (ht + hb) > 5)
    render_columns ();
  else if (tw < d->width && th + d->cp_y < d->length)
    render_simple ();
  else 
    render_segments ();
}

/* Render the table into multiple columns. */
static void
render_columns (void)
{
  int y0, y1;
  int max_len = 0;
  int index = 0;
  
  assert (cs == SOM_COL_DOWN);
  assert (d->cp_x == 0);

  for (y0 = ht; y0 < nr - hb; y0 = y1)
    {
      int len;
      
      t->class->cumulate (SOM_ROWS, y0, &y1, d->length - d->cp_y, &len);

      if (y0 == y1)
	{
	  assert (d->cp_y);
	  outp_eject_page (d);
	} else {
	  if (len > max_len)
	    max_len = len;

	  t->class->title (index++, 0);
	  t->class->render (0, y0, nc, y1);
	  
	  d->cp_x += tw + 2 * d->prop_em_width;
	  if (d->cp_x + tw > d->width)
	    {
	      d->cp_x = 0;
	      d->cp_y += max_len;
	      max_len = 0;
	    }
	}
    }
  
  if (d->cp_x > 0)
    {
      d->cp_x = 0;
      d->cp_y += max_len;
    }
}

/* Render the table by itself on the current page. */
static void
render_simple (void)
{
  assert (d->cp_x == 0);
  assert (tw < d->width && th + d->cp_y < d->length);

  t->class->title (0, 0);
  t->class->render (hl, ht, nc - hr, nr - hb);
  d->cp_y += th;
}

/* General table breaking routine. */
static void
render_segments (void)
{
  int count = 0;
  
  int x_index;
  int x0, x1;
  
  assert (d->cp_x == 0);

  for (x_index = 0, x0 = hl; x0 < nc - hr; x0 = x1, x_index++)
    {
      int y_index;
      int y0, y1;
      
      t->class->cumulate (SOM_COLUMNS, x0, &x1, d->width, NULL);
      if (x_index == 0 && x1 != nc - hr)
	x_index++;

      for (y_index = 0, y0 = ht; y0 < nr - hb; y0 = y1, y_index++)
	{
	  int len;
      
	  if (count++ != 0 && d->cp_y != 0)
	    d->cp_y += d->font_height;
	      
	  t->class->cumulate (SOM_ROWS, y0, &y1, d->length - d->cp_y, &len);
	  if (y_index == 0 && y1 != nr - hb)
	    y_index++;

	  if (y0 == y1)
	    {
	      assert (d->cp_y);
	      outp_eject_page (d);
	    } else {
	      t->class->title (x_index ? x_index : y_index,
			       x_index ? y_index : 0);
	      t->class->render (x0, y0, x1, y1);
	  
	      d->cp_y += len;
	    }
	}
    }
}
