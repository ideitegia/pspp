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

#include <output/driver.h>
#include <output/driver-provider.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <data/file-name.h>
#include <data/settings.h>
#include <libpspp/array.h>
#include <libpspp/assertion.h>
#include <libpspp/string-map.h>
#include <libpspp/string-set.h>
#include <libpspp/str.h>
#include <output/output-item.h>
#include <output/text-item.h>

#include "error.h"
#include "xalloc.h"
#include "xmemdup0.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static const struct output_driver_class *driver_classes[];

static struct output_driver **drivers;
static size_t n_drivers, allocated_drivers;

static unsigned int enabled_device_types = ((1u << OUTPUT_DEVICE_UNKNOWN)
                                            | (1u << OUTPUT_DEVICE_LISTING)
                                            | (1u << OUTPUT_DEVICE_SCREEN)
                                            | (1u << OUTPUT_DEVICE_PRINTER));

static struct output_item *deferred_syntax;
static bool in_command;

void
output_close (void)
{
  while (n_drivers > 0)
    {
      struct output_driver *d = drivers[--n_drivers];
      output_driver_destroy (d);
    }
}

static void
expand_macro (const char *name, struct string *dst, void *macros_)
{
  const struct string_map *macros = macros_;

  if (!strcmp (name, "viewwidth"))
    ds_put_format (dst, "%d", settings_get_viewwidth ());
  else if (!strcmp (name, "viewlength"))
    ds_put_format (dst, "%d", settings_get_viewlength ());
  else
    {
      const char *value = string_map_find (macros, name);
      if (value != NULL)
        ds_put_cstr (dst, value);
    }
}

/* Defines one configuration macro based on the text in BP, which
   should be of the form `KEY=VALUE'.  Returns true if
   successful, false if S is not in the proper form. */
bool
output_define_macro (const char *s, struct string_map *macros)
{
  const char *key_start, *value;
  size_t key_len;
  char *key;

  s += strspn (s, CC_SPACES);

  key_start = s;
  key_len = strcspn (s, "=" CC_SPACES);
  if (key_len == 0)
    return false;
  s += key_len;

  s += strspn (s, CC_SPACES);
  if (*s == '=')
    s++;

  s += strspn (s, CC_SPACES);
  value = s;

  key = xmemdup0 (key_start, key_len);
  if (!string_map_contains (macros, key))
    {
      struct string expanded_value = DS_EMPTY_INITIALIZER;

      fn_interp_vars (ss_cstr (value), expand_macro, &macros, &expanded_value);
      string_map_insert_nocopy (macros, key, ds_steal_cstr (&expanded_value));
    }
  else
    free (key);

  return true;
}

static void
add_driver_names (char *to, struct string_set *names)
{
  char *save_ptr = NULL;
  char *name;

  for (name = strtok_r (to, CC_SPACES, &save_ptr); name != NULL;
       name = strtok_r (NULL, CC_SPACES, &save_ptr))
    string_set_insert (names, name);
}

static void
init_default_drivers (void)
{
  error (0, 0, _("using default output driver configuration"));
  output_configure_driver ("list:ascii:listing:"
                           "length=66 width=79 output-file=\"pspp.list\"");
}

static void
warn_unused_drivers (const struct string_set *unused_drivers,
                     const struct string_set *requested_drivers)
{
  const struct string_set_node *node;
  const char *name;

  STRING_SET_FOR_EACH (name, node, unused_drivers)
    if (string_set_contains (requested_drivers, name))
      error (0, 0, _("unknown output driver `%s'"), name);
    else
      error (0, 0, _("output driver `%s' referenced but never defined"), name);
}

void
output_read_configuration (const struct string_map *macros_,
                           const struct string_set *driver_names_)
{
  struct string_map macros = STRING_MAP_INITIALIZER (macros);
  struct string_set driver_names = STRING_SET_INITIALIZER (driver_names);
  char *devices_file_name = NULL;
  FILE *devices_file = NULL;
  struct string line = DS_EMPTY_INITIALIZER;
  int line_number;

  ds_init_empty (&line);

  devices_file_name = fn_search_path ("devices", config_path);
  if (devices_file_name == NULL)
    {
      error (0, 0, _("cannot find output initialization file "
                     "(use `-vv' to view search path)"));
      goto exit;
    }
  devices_file = fopen (devices_file_name, "r");
  if (devices_file == NULL)
    {
      error (0, errno, _("cannot open \"%s\""), devices_file_name);
      goto exit;
    }

  string_map_replace_map (&macros, macros_);
  string_set_union (&driver_names, driver_names_);
  if (string_set_is_empty (&driver_names))
    string_set_insert (&driver_names, "default");

  line_number = 0;
  for (;;)
    {
      char *cp, *delimiter, *name;

      if (!ds_read_config_line (&line, &line_number, devices_file))
	{
	  if (ferror (devices_file))
	    error (0, errno, _("reading \"%s\""), devices_file_name);
	  break;
	}

      cp = ds_cstr (&line);
      cp += strspn (cp, CC_SPACES);

      if (*cp == '\0')
        continue;
      else if (!strncmp ("define", cp, 6) && isspace ((unsigned char) cp[6]))
        {
          if (!output_define_macro (&cp[7], &macros))
            error_at_line (0, 0, devices_file_name, line_number,
                           _("\"%s\" is not a valid macro definition"),
                           &cp[7]);
          continue;
        }

      delimiter = cp + strcspn (cp, ":=");
      name = xmemdup0 (cp, delimiter - cp);
      if (*delimiter == '=')
        {
          if (string_set_delete (&driver_names, name))
            add_driver_names (delimiter + 1, &driver_names);
        }
      else if (*delimiter == ':')
        {
          if (string_set_delete (&driver_names, name))
            {
              fn_interp_vars (ds_ss (&line), expand_macro, &macros, &line);
              output_configure_driver (ds_cstr (&line));
            }
        }
      else
        error_at_line (0, 0, devices_file_name, line_number,
                       _("syntax error"));
      free (name);
    }

  warn_unused_drivers (&driver_names, driver_names_);

exit:
  if (devices_file != NULL)
    fclose (devices_file);
  free (devices_file_name);
  ds_destroy (&line);
  string_set_destroy (&driver_names);
  string_map_destroy (&macros);

  if (n_drivers == 0)
    {
      error (0, 0, _("no active output drivers"));
      init_default_drivers ();
    }
}

/* Obtains a token from S and advances its position.  Errors are
   reported against the given DRIVER_NAME.
   The token is stored in TOKEN.  Returns true if successful,
   false on syntax error.

   Caller is responsible for skipping leading spaces. */
static bool
get_option_token (char **s_, const char *driver_name,
                  struct string *token)
{
  struct substring s = ss_cstr (*s_);
  int c;

  ds_clear (token);
  c = ss_get_char (&s);
  if (c == EOF)
    {
      error (0, 0, _("syntax error parsing options for \"%s\" driver"),
             driver_name);
      return false;
    }
  else if (c == '\'' || c == '"')
    {
      int quote = c;

      for (;;)
        {
          c = ss_get_char (&s);
          if (c == quote)
            break;
          else if (c == EOF)
            {
              error (0, 0,
                     _("reached end of options inside quoted string "
                       "parsing options for \"%s\" driver"),
                     driver_name);
              return false;
            }
          else if (c != '\\')
            ds_put_char (token, c);
          else
            {
              int out;

              c = ss_get_char (&s);
              switch (c)
                {
                case '\'':
                  out = '\'';
                  break;
                case '"':
                  out = '"';
                  break;
                case '\\':
                  out = '\\';
                  break;
                case 'a':
                  out = '\a';
                  break;
                case 'b':
                  out = '\b';
                  break;
                case 'f':
                  out = '\f';
                  break;
                case 'n':
                  out = '\n';
                  break;
                case 'r':
                  out = '\r';
                  break;
                case 't':
                  out = '\t';
                  break;
                case 'v':
                  out = '\v';
                  break;
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                  out = c - '0';
                  while (ss_first (s) >= '0' && ss_first (s) <= '7')
                    out = out * 8 + (ss_get_char (&s) - '0');
                  break;
                case 'x':
                case 'X':
                  out = 0;
                  while (isxdigit (ss_first (s)))
                    {
                      c = ss_get_char (&s);
                      out *= 16;
                      if (isdigit (c))
                        out += c - '0';
                      else
                        out += tolower (c) - 'a' + 10;
                    }
                  break;
                default:
                  error (0, 0, _("syntax error in string constant "
                                 "parsing options for \"%s\" driver"),
                         driver_name);
                  return false;
                }
              ds_put_char (token, out);
            }
        }
    }
  else
    {
      for (;;)
        {
          ds_put_char (token, c);

          c = ss_first (s);
          if (c == EOF || c == '=' || isspace (c))
            break;
          ss_advance (&s, 1);
        }
    }

  *s_ = s.string;
  return 1;
}

static void
parse_options (const char *driver_name, char *options,
               struct string_map *option_map)
{
  struct string key = DS_EMPTY_INITIALIZER;
  struct string value = DS_EMPTY_INITIALIZER;

  for (;;)
    {
      options += strspn (options, CC_SPACES);
      if (*options == '\0')
        break;

      if (!get_option_token (&options, driver_name, &key))
        break;

      options += strspn (options, CC_SPACES);
      if (*options != '=')
	{
	  error (0, 0, _("syntax error expecting `=' "
                         "parsing options for driver \"%s\""),
                 driver_name);
	  break;
	}
      options++;

      options += strspn (options, CC_SPACES);
      if (!get_option_token (&options, driver_name, &value))
        break;

      if (string_map_contains (option_map, ds_cstr (&key)))
        error (0, 0, _("driver \"%s\" defines option \"%s\" multiple times"),
               driver_name, ds_cstr (&key));
      else
        string_map_insert (option_map, ds_cstr (&key), ds_cstr (&value));
    }

  ds_destroy (&key);
  ds_destroy (&value);
}

static char *
trim_token (char *token)
{
  if (token != NULL)
    {
      char *end;

      /* Trim leading spaces. */
      while (isspace ((unsigned char) *token))
        token++;

      /* Trim trailing spaces. */
      end = strchr (token, '\0');
      while (end > token && isspace ((unsigned char) end[-1]))
        *--end = '\0';

      /* An empty token is a null token. */
      if (*token == '\0')
        return NULL;
    }
  return token;
}

static const struct output_driver_class *
find_output_class (const char *name)
{
  const struct output_driver_class **classp;

  for (classp = driver_classes; *classp != NULL; classp++)
    if (!strcmp ((*classp)->name, name))
      break;

  return *classp;
}

static struct output_driver *
create_driver (const char *driver_name, const char *class_name,
               const char *device_type, struct string_map *options)
{
  const struct output_driver_class *class;
  enum output_device_type type;
  struct output_driver *driver;

  type = OUTPUT_DEVICE_UNKNOWN;
  if (device_type != NULL && device_type[0] != '\0')
    {
      if (!strcmp (device_type, "listing"))
        type = OUTPUT_DEVICE_LISTING;
      else if (!strcmp (device_type, "screen"))
        type = OUTPUT_DEVICE_SCREEN;
      else if (!strcmp (device_type, "printer"))
        type = OUTPUT_DEVICE_PRINTER;
      else
        error (0, 0, _("unknown device type `%s'"), device_type);
    }

  class = find_output_class (class_name);
  if (class != NULL)
    driver = class->create (driver_name, type, options);
  else
    {
      error (0, 0, _("unknown output driver class `%s'"), class_name);
      driver = NULL;
    }

  string_map_destroy (options);

  return driver;
}

struct output_driver *
output_driver_create (const char *class_name, struct string_map *options)
{
  return create_driver (class_name, class_name, NULL, options);
}

/* String LINE is in format:
   DRIVERNAME:CLASSNAME:DEVICETYPE:OPTIONS
*/
void
output_configure_driver (const char *line_)
{
  char *save_ptr = NULL;
  char *line = xstrdup (line_);
  char *driver_name = trim_token (strtok_r (line, ":", &save_ptr));
  char *class_name = trim_token (strtok_r (NULL, ":", &save_ptr));
  char *device_type = trim_token (strtok_r (NULL, ":", &save_ptr));
  char *options = trim_token (strtok_r (NULL, "", &save_ptr));

  if (driver_name && class_name)
    {
      struct string_map option_map;
      struct output_driver *driver;

      string_map_init (&option_map);
      if (options != NULL)
        parse_options (driver_name, options, &option_map);

      driver = create_driver (driver_name, class_name,
                              device_type, &option_map);
      if (driver != NULL)
        output_driver_register (driver);
    }
  else
    error (0, 0,
           _("driver definition line missing driver name or class name"));

  free (line);
}

/* Display on stdout a list of all registered driver classes. */
void
output_list_classes (void)
{
  const struct output_driver_class **classp;

  printf (_("Driver classes:"));
  for (classp = driver_classes; *classp != NULL; classp++)
    printf (" %s", (*classp)->name);
  putc ('\n', stdout);
}

static bool
driver_is_enabled (const struct output_driver *d)
{
  return (1u << d->device_type) & enabled_device_types;
}

static void
output_submit__ (struct output_item *item)
{
  size_t i;

  for (i = 0; i < n_drivers; i++)
    {
      struct output_driver *d = drivers[i];
      if (driver_is_enabled (d))
        d->class->submit (d, item);
    }

  output_item_unref (item);
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
output_submit (struct output_item *item)
{
  if (is_text_item (item))
    {
      struct text_item *text = to_text_item (item);
      switch (text_item_get_type (text))
        {
        case TEXT_ITEM_SYNTAX:
          if (!in_command)
            {
              if (deferred_syntax != NULL)
                output_submit__ (deferred_syntax);
              deferred_syntax = item;
              return;
            }
          break;

        case TEXT_ITEM_COMMAND_OPEN:
          output_submit__ (item);
          if (deferred_syntax != NULL)
            {
              output_submit__ (deferred_syntax);
              deferred_syntax = NULL;
            }
          in_command = true;
          return;

        case TEXT_ITEM_COMMAND_CLOSE:
          in_command = false;
          break;

        default:
          break;
        }
    }

  output_submit__ (item);
}

/* Flushes output to screen devices, so that the user can see
   output that doesn't fill up an entire page. */
void
output_flush (void)
{
  size_t i;

  for (i = 0; i < n_drivers; i++)
    {
      struct output_driver *d = drivers[i];
      if (driver_is_enabled (d) && d->class->flush != NULL)
        d->class->flush (d);
    }
}

unsigned int
output_get_enabled_types (void)
{
  return enabled_device_types;
}

void
output_set_enabled_types (unsigned int types)
{
  enabled_device_types = types;
}

void
output_set_type_enabled (bool enable, enum output_device_type type)
{
  unsigned int bit = 1u << type;
  if (enable)
    enabled_device_types |= bit;
  else
    enabled_device_types |= ~bit;
}

void
output_driver_init (struct output_driver *driver,
                    const struct output_driver_class *class,
                    const char *name, enum output_device_type type)
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
  if (n_drivers >= allocated_drivers)
    drivers = x2nrealloc (drivers, &allocated_drivers, sizeof *drivers);
  drivers[n_drivers++] = driver;
}

void
output_driver_unregister (struct output_driver *driver)
{
  size_t i;

  for (i = 0; i < n_drivers; i++)
    if (drivers[i] == driver)
      {
        remove_element (drivers, n_drivers, sizeof *drivers, i);
        return;
      }
  NOT_REACHED ();
}

bool
output_driver_is_registered (const struct output_driver *driver)
{
  size_t i;

  for (i = 0; i < n_drivers; i++)
    if (drivers[i] == driver)
      return true;
  return false;
}

/* Known driver classes. */

static const struct output_driver_class *driver_classes[] =
  {
    &ascii_class,
#ifdef HAVE_CAIRO
    &cairo_class,
#endif
    &html_class,
    &odt_class,
    &csv_class,
    NULL,
  };

