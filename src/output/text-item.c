/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "output/text-item.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

#include "libpspp/cast.h"
#include "output/driver.h"
#include "output/output-item-provider.h"

#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Creates and returns a new text item containing TEXT and the specified TYPE.
   The new text item takes ownership of TEXT. */
struct text_item *
text_item_create_nocopy (enum text_item_type type, char *text)
{
  struct text_item *item = xmalloc (sizeof *item);
  output_item_init (&item->output_item, &text_item_class);
  item->text = text;
  item->type = type;
  return item;
}

/* Creates and returns a new text item containing a copy of TEXT and the
   specified TYPE.  The caller retains ownership of TEXT. */
struct text_item *
text_item_create (enum text_item_type type, const char *text)
{
  return text_item_create_nocopy (type, xstrdup (text));
}

/* Creates and returns a new text item containing a copy of FORMAT, which is
   formatted as if by printf(), and the specified TYPE.  The caller retains
   ownership of FORMAT. */
struct text_item *
text_item_create_format (enum text_item_type type, const char *format, ...)
{
  struct text_item *item;
  va_list args;

  va_start (args, format);
  item = text_item_create_nocopy (type, xvasprintf (format, args));
  va_end (args);

  return item;
}

/* Returns ITEM's type. */
enum text_item_type
text_item_get_type (const struct text_item *item)
{
  return item->type;
}

/* Returns ITEM's text, which the caller may not modify or free. */
const char *
text_item_get_text (const struct text_item *item)
{
  return item->text;
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
text_item_submit (struct text_item *item)
{
  output_submit (&item->output_item);
}

static void
text_item_destroy (struct output_item *output_item)
{
  struct text_item *item = to_text_item (output_item);
  free (item->text);
  free (item);
}

const struct output_item_class text_item_class =
  {
    text_item_destroy,
  };
