/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010 Free Software Foundation, Inc.

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

#include <errno.h>
#include <stdlib.h>

#include <data/file-name.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/string-map.h>
#include <output/text-item.h>
#include <output/driver-provider.h>
#include <output/options.h>
#include <output/table-item.h>
#include <output/table-provider.h>

#include "gl/error.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Comma-separated value output driver. */
struct csv_driver
  {
    struct output_driver driver;

    char *separator;            /* Comma or tab. */
    char *file_name;            /* Output file name. */
    FILE *file;                 /* Output file. */
    int n_items;                /* Number of items output so far. */
  };

static struct csv_driver *
csv_driver_cast (struct output_driver *driver)
{
  assert (driver->class == &csv_class);
  return UP_CAST (driver, struct csv_driver, driver);
}

static struct driver_option *
opt (struct output_driver *d, struct string_map *options, const char *key,
     const char *default_value)
{
  return driver_option_get (d, options, key, default_value);
}

static struct output_driver *
csv_create (const char *name, enum output_device_type device_type,
            struct string_map *o)
{
  struct output_driver *d;
  struct csv_driver *csv;

  csv = xzalloc (sizeof *csv);
  d = &csv->driver;
  output_driver_init (&csv->driver, &csv_class, name, device_type);

  csv->separator = parse_string (opt (d, o, "separator", ","));
  csv->file_name = parse_string (opt (d, o, "output-file", "pspp.csv"));
  csv->file = fn_open (csv->file_name, "w");
  csv->n_items = 0;

  if (csv->file == NULL)
    {
      error (0, errno, _("csv: opening output file \"%s\""), csv->file_name);
      output_driver_destroy (d);
      return NULL;
    }

  return d;
}

static void
csv_destroy (struct output_driver *driver)
{
  struct csv_driver *csv = csv_driver_cast (driver);

  if (csv->file != NULL)
    fn_close (csv->file_name, csv->file);

  free (csv->separator);
  free (csv->file_name);
  free (csv);
}

static void
csv_flush (struct output_driver *driver)
{
  struct csv_driver *csv = csv_driver_cast (driver);
  if (csv->file != NULL)
    fflush (csv->file);
}

static void
csv_output_field (FILE *file, const char *field)
{
  while (*field == ' ')
    field++;

  if (field[strcspn (field, "\"\n\r,\t")])
    {
      const char *p;

      putc ('"', file);
      for (p = field; *p != '\0'; p++)
        {
          if (*p == '"')
            putc ('"', file);
          putc (*p, file);
        }
      putc ('"', file);
    }
  else
    fputs (field, file);
}

static void
csv_output_field_format (FILE *file, const char *format, ...)
  PRINTF_FORMAT (2, 3);

static void
csv_output_field_format (FILE *file, const char *format, ...)
{
  va_list args;
  char *s;

  va_start (args, format);
  s = xvasprintf (format, args);
  va_end (args);

  csv_output_field (file, s);
  free (s);
}

static void
csv_put_separator (struct csv_driver *csv)
{
  if (csv->n_items++ > 0)
    putc ('\n', csv->file);
}

static void
csv_submit (struct output_driver *driver,
            const struct output_item *output_item)
{
  struct csv_driver *csv = csv_driver_cast (driver);

  if (is_table_item (output_item))
    {
      struct table_item *table_item = to_table_item (output_item);
      const char *caption = table_item_get_caption (table_item);
      const struct table *t = table_item_get_table (table_item);
      int x, y;

      csv_put_separator (csv);

      if (caption != NULL)
        {
          csv_output_field_format (csv->file, "Table: %s", caption);
          putc ('\n', csv->file);
        }

      for (y = 0; y < table_nr (t); y++)
        {
          for (x = 0; x < table_nc (t); x++)
            {
              struct table_cell cell;

              table_get_cell (t, x, y, &cell);

              if (x > 0)
                fputs (csv->separator, csv->file);

              if (x != cell.d[TABLE_HORZ][0] || y != cell.d[TABLE_VERT][0])
                csv_output_field (csv->file, "");
              else
                csv_output_field (csv->file, cell.contents);

              table_cell_free (&cell);
            }
          putc ('\n', csv->file);
        }
    }
  else if (is_text_item (output_item))
    {
      const struct text_item *text_item = to_text_item (output_item);
      enum text_item_type type = text_item_get_type (text_item);
      const char *text = text_item_get_text (text_item);

      if (type == TEXT_ITEM_COMMAND_OPEN || type == TEXT_ITEM_COMMAND_CLOSE
          || type == TEXT_ITEM_SYNTAX)
        return;

      csv_put_separator (csv);
      switch (type)
        {
        case TEXT_ITEM_TITLE:
          csv_output_field_format (csv->file, "Title: %s", text);
          break;

        case TEXT_ITEM_SUBTITLE:
          csv_output_field_format (csv->file, "Subtitle: %s", text);
          break;

        default:
          csv_output_field (csv->file, text);
          break;
        }
      putc ('\n', csv->file);
    }
}

const struct output_driver_class csv_class =
  {
    "csv",
    csv_create,
    csv_destroy,
    csv_submit,
    csv_flush,
  };
