/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "output/driver.h"
#include "output/driver-provider.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "data/file-name.h"
#include "data/settings.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/llx.h"
#include "libpspp/string-map.h"
#include "libpspp/string-set.h"
#include "libpspp/str.h"
#include "output/message-item.h"
#include "output/output-item.h"
#include "output/text-item.h"

#include "gl/xalloc.h"
#include "gl/xmemdup0.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static const struct output_driver_factory *factories[];

/* Drivers currently registered with output_driver_register(). */
static struct llx_list drivers = LLX_INITIALIZER (drivers);

/* TEXT_ITEM_SYNTAX being accumulated until another kind of output arrives. */
static struct string deferred_syntax = DS_EMPTY_INITIALIZER;

void
output_close (void)
{
  while (!llx_is_empty (&drivers))
    {
      struct output_driver *d = llx_pop_head (&drivers, &llx_malloc_mgr);
      output_driver_destroy (d);
    }
}

void
output_get_supported_formats (struct string_set *formats)
{
  const struct output_driver_factory **fp;

  for (fp = factories; *fp != NULL; fp++)
    string_set_insert (formats, (*fp)->extension);
}

static void
output_submit__ (struct output_item *item)
{
  struct llx *llx, *next;

  for (llx = llx_head (&drivers); llx != llx_null (&drivers); llx = next)
    {
      struct output_driver *d = llx_data (llx);
      enum settings_output_type type;

      next = llx_next (llx);

      if (is_message_item (item))
        {
          const struct msg *m = message_item_get_msg (to_message_item (item));
          if (m->severity == MSG_S_NOTE)
            type = SETTINGS_OUTPUT_NOTE;
          else
            type = SETTINGS_OUTPUT_ERROR;
        }
      else if (is_text_item (item)
               && text_item_get_type (to_text_item (item)) == TEXT_ITEM_SYNTAX)
        type = SETTINGS_OUTPUT_SYNTAX;
      else
        type = SETTINGS_OUTPUT_RESULT;

      if (settings_get_output_routing (type) & d->device_type)
        d->class->submit (d, item);
    }

  output_item_unref (item);
}

static void
flush_deferred_syntax (void)
{
  if (!ds_is_empty (&deferred_syntax))
    {
      char *syntax = ds_steal_cstr (&deferred_syntax);
      output_submit__ (text_item_super (
                         text_item_create_nocopy (TEXT_ITEM_SYNTAX, syntax)));
    }
}

static bool
is_syntax_item (const struct output_item *item)
{
  return (is_text_item (item)
          && text_item_get_type (to_text_item (item)) == TEXT_ITEM_SYNTAX);
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
output_submit (struct output_item *item)
{
  if (is_syntax_item (item))
    {
      ds_put_cstr (&deferred_syntax, text_item_get_text (to_text_item (item)));
      output_item_unref (item);
      return;
    }

  flush_deferred_syntax ();
  output_submit__ (item);
}

/* Flushes output to screen devices, so that the user can see
   output that doesn't fill up an entire page. */
void
output_flush (void)
{
  struct llx *llx;

  flush_deferred_syntax ();
  for (llx = llx_head (&drivers); llx != llx_null (&drivers);
       llx = llx_next (llx))
    {
      struct output_driver *d = llx_data (llx);
      if (d->device_type & SETTINGS_DEVICE_TERMINAL && d->class->flush != NULL)
        d->class->flush (d);
    }
}

void
output_driver_init (struct output_driver *driver,
                    const struct output_driver_class *class,
                    const char *name, enum settings_output_devices type)
{
  driver->class = class;
  driver->name = xstrdup (name);
  driver->device_type = type;
}

void
output_driver_destroy (struct output_driver *driver)
{
  if (driver != NULL)
    {
      char *name = driver->name;
      if (output_driver_is_registered (driver))
        output_driver_unregister (driver);
      if (driver->class->destroy)
        driver->class->destroy (driver);
      free (name);
    }
}

const char *
output_driver_get_name (const struct output_driver *driver)
{
  return driver->name;
}

void
output_driver_register (struct output_driver *driver)
{
  assert (!output_driver_is_registered (driver));
  llx_push_tail (&drivers, driver, &llx_malloc_mgr);
}

void
output_driver_unregister (struct output_driver *driver)
{
  llx_remove (llx_find (llx_head (&drivers), llx_null (&drivers), driver),
              &llx_malloc_mgr);
}

bool
output_driver_is_registered (const struct output_driver *driver)
{
  return llx_find (llx_head (&drivers), llx_null (&drivers), driver) != NULL;
}

/* Useful functions for output driver implementation. */

void
output_driver_track_current_command (const struct output_item *output_item,
                                     char **command_namep)
{
  if (is_text_item (output_item))
    {
      const struct text_item *item = to_text_item (output_item);
      const char *text = text_item_get_text (item);
      enum text_item_type type = text_item_get_type (item);

      if (type == TEXT_ITEM_COMMAND_OPEN)
        {
          free (*command_namep);
          *command_namep = xstrdup (text);
        }
      else if (type == TEXT_ITEM_COMMAND_CLOSE)
        {
          free (*command_namep);
          *command_namep = NULL;
        }
    }
}

extern const struct output_driver_factory txt_driver_factory;
extern const struct output_driver_factory list_driver_factory;
extern const struct output_driver_factory html_driver_factory;
extern const struct output_driver_factory csv_driver_factory;
#ifdef ODF_WRITE_SUPPORT
extern const struct output_driver_factory odt_driver_factory;
#endif
#ifdef HAVE_CAIRO
extern const struct output_driver_factory pdf_driver_factory;
extern const struct output_driver_factory ps_driver_factory;
extern const struct output_driver_factory svg_driver_factory;
#endif

static const struct output_driver_factory *factories[] =
  {
    &txt_driver_factory,
    &list_driver_factory,
    &html_driver_factory,
    &csv_driver_factory,
#ifdef ODF_WRITE_SUPPORT
    &odt_driver_factory,
#endif
#ifdef HAVE_CAIRO
    &pdf_driver_factory,
    &ps_driver_factory,
    &svg_driver_factory,
#endif
    NULL
  };

static const struct output_driver_factory *
find_factory (const char *format)
{
  const struct output_driver_factory **fp;

  for (fp = factories; *fp != NULL; fp++)
    {
      const struct output_driver_factory *f = *fp;

      if (!strcmp (f->extension, format))
        return f;
    }
  return &txt_driver_factory;
}

static enum settings_output_devices
default_device_type (const char *file_name)
{
  return (!strcmp (file_name, "-")
          ? SETTINGS_DEVICE_TERMINAL
          : SETTINGS_DEVICE_LISTING);
}

struct output_driver *
output_driver_create (struct string_map *options)
{
  enum settings_output_devices device_type;
  const struct output_driver_factory *f;
  struct output_driver *driver;
  char *device_string;
  char *file_name;
  char *format;

  format = string_map_find_and_delete (options, "format");
  file_name = string_map_find_and_delete (options, "output-file");

  if (format == NULL)
    {
      if (file_name != NULL)
        {
          const char *extension = strrchr (file_name, '.');
          format = xstrdup (extension != NULL ? extension + 1 : "");
        }
      else
        format = xstrdup ("txt");
    }
  f = find_factory (format);

  if (file_name == NULL)
    file_name = xstrdup (f->default_file_name);

  /* XXX should use parse_enum(). */
  device_string = string_map_find_and_delete (options, "device");
  if (device_string == NULL || device_string[0] == '\0')
    device_type = default_device_type (file_name);
  else if (!strcmp (device_string, "terminal"))
    device_type = SETTINGS_DEVICE_TERMINAL;
  else if (!strcmp (device_string, "listing"))
    device_type = SETTINGS_DEVICE_LISTING;
  else
    {
      msg (MW, _("%s is not a valid device type (the choices are `%s' and `%s')"),
                     device_string, "terminal", "listing");
      device_type = default_device_type (file_name);
    }

  driver = f->create (file_name, device_type, options);
  if (driver != NULL)
    {
      const struct string_map_node *node;
      const char *key;

      STRING_MAP_FOR_EACH_KEY (key, node, options)
        msg (MW, _("%s: unknown option `%s'"), file_name, key);
    }
  string_map_clear (options);

  free (file_name);
  free (format);
  free (device_string);

  return driver;
}
