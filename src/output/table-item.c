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

#include "output/table-provider.h"

#include <assert.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "output/driver.h"
#include "output/output-item-provider.h"
#include "output/table-item.h"

#include "gl/xalloc.h"

/* Initializes ITEM as a table item for rendering TABLE.  The new table item
   initially has the specified TITLE and CAPTION, which may each be NULL.  The
   caller retains ownership of TITLE and CAPTION. */
struct table_item *
table_item_create (struct table *table, const char *title, const char *caption)
{
  struct table_item *item = xmalloc (sizeof *item);
  output_item_init (&item->output_item, &table_item_class);
  item->table = table;
  item->title = title != NULL ? xstrdup (title) : NULL;
  item->caption = caption != NULL ? xstrdup (caption) : NULL;
  return item;
}

/* Returns the table contained by TABLE_ITEM.  The caller must not modify or
   unref the returned table. */
const struct table *
table_item_get_table (const struct table_item *table_item)
{
  return table_item->table;
}

/* Returns ITEM's title, which is a null pointer if no title has been
   set. */
const char *
table_item_get_title (const struct table_item *item)
{
  return item->title;
}

/* Sets ITEM's title to TITLE, replacing any previous title.  Specify NULL for
   TITLE to clear any title from ITEM.  The caller retains ownership of TITLE.

   This function may only be used on a table_item that is unshared. */
void
table_item_set_title (struct table_item *item, const char *title)
{
  assert (!table_item_is_shared (item));
  free (item->title);
  item->title = title != NULL ? xstrdup (title) : NULL;
}

/* Returns ITEM's caption, which is a null pointer if no caption has been
   set. */
const char *
table_item_get_caption (const struct table_item *item)
{
  return item->caption;
}

/* Sets ITEM's caption to CAPTION, replacing any previous caption.  Specify
   NULL for CAPTION to clear any caption from ITEM.  The caller retains
   ownership of CAPTION.

   This function may only be used on a table_item that is unshared. */
void
table_item_set_caption (struct table_item *item, const char *caption)
{
  assert (!table_item_is_shared (item));
  free (item->caption);
  item->caption = caption != NULL ? xstrdup (caption) : NULL;
}

/* Submits TABLE_ITEM to the configured output drivers, and transfers ownership
   to the output subsystem. */
void
table_item_submit (struct table_item *table_item)
{
  output_submit (&table_item->output_item);
}

static void
table_item_destroy (struct output_item *output_item)
{
  struct table_item *item = to_table_item (output_item);
  free (item->title);
  free (item->caption);
  table_unref (item->table);
  free (item);
}

const struct output_item_class table_item_class =
  {
    table_item_destroy,
  };
