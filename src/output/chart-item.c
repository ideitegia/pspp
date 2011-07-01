/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2009, 2011 Free Software Foundation, Inc.

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

#include "output/chart-item.h"
#include "output/chart-item-provider.h"

#include <assert.h>
#include <stdlib.h>

#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "output/driver.h"
#include "output/output-item-provider.h"

#include "gl/xalloc.h"

/* Initializes ITEM as a chart item of the specified CLASS.  The new chart item
   initially has the specified TITLE, which may be NULL if no title is yet
   available.  The caller retains ownership of TITLE.

   A chart item is an abstract class, that is, a plain chart_item is not useful
   on its own.  Thus, this function is normally called from the initialization
   function of some subclass of chart_item. */
void
chart_item_init (struct chart_item *item, const struct chart_item_class *class,
                 const char *title)
{
  output_item_init (&item->output_item, &chart_item_class);
  item->class = class;
  item->title = title != NULL ? xstrdup (title) : NULL;
}

/* Returns ITEM's title, which is a null pointer if no title has been set. */
const char *
chart_item_get_title (const struct chart_item *item)
{
  return item->title;
}

/* Sets ITEM's title to TITLE, replacing any previous title.  Specify NULL for
   TITLE to clear any title from ITEM.  The caller retains ownership of
   TITLE.

   This function may only be used on a chart_item that is unshared. */
void
chart_item_set_title (struct chart_item *item, const char *title)
{
  assert (!chart_item_is_shared (item));
  free (item->title);
  item->title = title != NULL ? xstrdup (title) : NULL;
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
chart_item_submit (struct chart_item *item)
{
  output_submit (&item->output_item);
}

static void
chart_item_destroy (struct output_item *output_item)
{
  struct chart_item *item = to_chart_item (output_item);
  char *title = item->title;
  item->class->destroy (item);
  free (title);
}

const struct output_item_class chart_item_class =
  {
    chart_item_destroy,
  };
