/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009 Free Software Foundation, Inc.

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
#include "manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <libpspp/assertion.h>
#include "output.h"

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

/* Flushes output on all active devices. */
void
som_flush (void)
{
  struct outp_driver *d;

  for (d = outp_drivers (NULL); d; d = outp_drivers (d))
    outp_flush (d);
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

static void render_columns (void *r, struct outp_driver *, struct som_entity *,
                            int tw, int th,
                            int hl, int hr, int ht, int hb);
static void render_simple (void *r, struct outp_driver *, struct som_entity *,
                           int tw, int th,
                           int hl, int hr, int ht, int hb);
static void render_segments (void *r, struct outp_driver *,
                             struct som_entity *,
                             int tw, int th,
                             int hl, int hr, int ht, int hb);

static void output_entity (struct outp_driver *, struct som_entity *);

/* Output table T to appropriate output devices. */
void
som_submit (struct som_entity *t)
{
  struct outp_driver *d;

#if DEBUGGING
  static int entry;

  assert (entry++ == 0);
#endif

  if (t->type == SOM_TABLE)
    {
      unsigned int flags;
      int hl, hr, ht, hb;
      int nc, nr;

      /* Set up to render the table. */
      t->class->flags (t, &flags);
      if (!(flags & SOMF_NO_TITLE))
	subtable_num++;

      /* Do some basic error checking. */
      t->class->count (t, &nc, &nr);
      t->class->headers (t, &hl, &hr, &ht, &hb);
      if (hl + hr > nc || ht + hb > nr)
	{
	  fprintf (stderr, "headers: (l,r)=(%d,%d), (t,b)=(%d,%d) "
                   "in table size (%d,%d)\n",
                   hl, hr, ht, hb, nc, nr);
	  NOT_REACHED ();
	}
      else if (hl + hr == nc)
	fprintf (stderr, "warning: headers (l,r)=(%d,%d) in table width %d\n",
                hl, hr, nc);
      else if (ht + hb == nr)
	fprintf (stderr, "warning: headers (t,b)=(%d,%d) in table height %d\n",
                ht, hb, nr);
    }

  for (d = outp_drivers (NULL); d; d = outp_drivers (d))
    output_entity (d, t);

#if DEBUGGING
  assert (--entry == 0);
#endif
}

static bool
check_fits_width (struct som_entity *t, const struct outp_driver *d, void *r)
{
  int hl, hr, ht, hb;
  int nc, nr;
  int i;

  t->class->headers (t, &hl, &hr, &ht, &hb);
  t->class->count (t, &nc, &nr);
  for (i = hl; i < nc - hr; i++)
    {
      int end, actual;
      t->class->cumulate (r, SOM_COLUMNS, i, &end, d->width, &actual);
      if (end == i)
        return false;
    }

  return true;
}

static bool
check_fits_length (struct som_entity *t, const struct outp_driver *d, void *r)
{
  int hl, hr, ht, hb;
  int nc, nr;
  int i;

  t->class->headers (t, &hl, &hr, &ht, &hb);
  t->class->count (t, &nc, &nr);
  for (i = ht; i < nr - hb; i++)
    {
      int end, actual;
      t->class->cumulate (r, SOM_ROWS, i, &end, d->length, &actual);
      if (end == i)
        return false;
    }

  return true;
}

/* Output entity T to driver D. */
static void
output_entity (struct outp_driver *d, struct som_entity *t)
{
  bool fits_width, fits_length;
  unsigned int flags;
  int hl, hr, ht, hb;
  int tw, th;
  int nc, nr;
  int cs;
  void *r;

  outp_open_page (d);
  if (d->class->special || t->type == SOM_CHART)
    {
      d->class->submit (d, t);
      return;
    }

  t->class->headers (t, &hl, &hr, &ht, &hb);
  t->class->count (t, &nc, &nr);
  t->class->columns (t, &cs);
  t->class->flags (t, &flags);

  r = t->class->render_init (t, d, hl, hr, ht, hb);

  fits_width = check_fits_width (t, d, r);
  fits_length = check_fits_length (t, d, r);
  if (!fits_width || !fits_length)
    {
      t->class->render_free (r);

      if (!fits_width)
        hl = hr = 0;
      if (!fits_length)
        ht = hb = 0;

      r = t->class->render_init (t, d, hl, hr, ht, hb);
    }
  t->class->area (r, &tw, &th);

  if (!(flags & SOMF_NO_SPACING) && d->cp_y != 0)
    d->cp_y += d->font_height;

  if (cs != SOM_COL_NONE
      && 2 * (tw + d->prop_em_width) <= d->width
      && nr - (ht + hb) > 5)
    render_columns (r, d, t, tw, th, hl, hr, ht, hb);
  else if (tw < d->width && th + d->cp_y < d->length)
    render_simple (r, d, t, tw, th, hl, hr, ht, hb);
  else
    render_segments (r, d, t, tw, th, hl, hr, ht, hb);

  t->class->render_free (r);
}

/* Render the table into multiple columns. */
static void
render_columns (void *r, struct outp_driver *d, struct som_entity *t,
                int tw, int th UNUSED,
                int hl UNUSED, int hr UNUSED, int ht, int hb)
{
  int y0, y1;
  int max_len = 0;
  int index = 0;
  int nc, nr;
  int cs;

  t->class->count (t, &nc, &nr);
  t->class->columns (t, &cs);

  assert (cs == SOM_COL_DOWN);
  assert (d->cp_x == 0);

  for (y0 = ht; y0 < nr - hb; y0 = y1)
    {
      int len;

      t->class->cumulate (r, SOM_ROWS, y0, &y1, d->length - d->cp_y, &len);

      if (y0 == y1)
	{
	  assert (d->cp_y);
	  outp_eject_page (d);
	}
      else
        {
	  if (len > max_len)
	    max_len = len;

	  t->class->title (r, index++, 0);
	  t->class->render (r, 0, y0, nc, y1);

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
render_simple (void *r, struct outp_driver *d, struct som_entity *t,
               int tw, int th,
               int hl, int hr, int ht, int hb)
{
  int nc, nr;

  t->class->count (t, &nc, &nr);

  assert (d->cp_x == 0);
  assert (tw < d->width && th + d->cp_y < d->length);

  t->class->title (r, 0, 0);
  t->class->render (r, hl, ht, nc - hr, nr - hb);
  d->cp_y += th;
}

/* General table breaking routine. */
static void
render_segments (void *r, struct outp_driver *d, struct som_entity *t,
                 int tw UNUSED, int th UNUSED,
                 int hl, int hr, int ht, int hb)
{
  int count = 0;

  int x_index;
  int x0, x1;

  int nc, nr;

  assert (d->cp_x == 0);

  t->class->count (t, &nc, &nr);
  for (x_index = 0, x0 = hl; x0 < nc - hr; x0 = x1, x_index++)
    {
      int y_index;
      int y0, y1;

      t->class->cumulate (r, SOM_COLUMNS, x0, &x1, d->width, NULL);
      if (x_index == 0 && x1 != nc - hr)
	x_index++;

      for (y_index = 0, y0 = ht; y0 < nr - hb; y0 = y1, y_index++)
	{
	  int len;

	  if (count++ != 0 && d->cp_y != 0)
	    d->cp_y += d->font_height;

	  t->class->cumulate (r, SOM_ROWS, y0, &y1, d->length - d->cp_y, &len);
	  if (y_index == 0 && y1 != nr - hb)
	    y_index++;

	  if (y0 == y1)
	    {
	      assert (d->cp_y);
	      outp_eject_page (d);
	    }
          else
            {
	      t->class->title (r, x_index ? x_index : y_index,
			       x_index ? y_index : 0);
	      t->class->render (r, x0, y0, x1, y1);

	      d->cp_y += len;
	    }
	}
    }
}
