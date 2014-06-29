/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2014 Free Software Foundation, Inc.

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

#include "output/options.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/str.h"
#include "libpspp/string-map.h"
#include "output/driver-provider.h"
#include "output/measure.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Creates and returns a new struct driver_option that contains copies of
   all of the supplied arguments.  All of the arguments must be nonnull,
   except that VALUE may be NULL (if the user did not supply a value for this
   option).

   Refer to struct driver_option for the meaning of each argument. */
struct driver_option *
driver_option_create (const char *driver_name, const char *name,
                      const char *value, const char *default_value)
{
  struct driver_option *o = xmalloc (sizeof *o);
  o->driver_name = xstrdup (driver_name);
  o->name = xstrdup (name);
  o->value = value != NULL ? xstrdup (value) : NULL;
  o->default_value = default_value ? xstrdup (default_value) : NULL;
  return o;
}

/* Creates and returns a new struct driver_option for output driver DRIVER
   (which is needed only to the extent that its name will be used in error
   messages).  The option named NAME is extracted from OPTIONS.  DEFAULT_VALUE
   is the default value of the option, used if the given option was not
   supplied or was invalid. */
struct driver_option *
driver_option_get (struct output_driver *driver, struct string_map *options,
                   const char *name, const char *default_value)
{
  struct driver_option *option;
  char *value;

  value = string_map_find_and_delete (options, name);
  option = driver_option_create (output_driver_get_name (driver), name, value,
                                 default_value);
  free (value);
  return option;
}

/* Frees driver option O. */
void
driver_option_destroy (struct driver_option *o)
{
  if (o != NULL)
    {
      free (o->driver_name);
      free (o->name);
      free (o->value);
      free (o->default_value);
      free (o);
    }
}

/* Stores the paper size of the value of option O into *H and *V, in 1/72000"
   units.  Any syntax accepted by measure_paper() may be used.

   Destroys O. */
void
parse_paper_size (struct driver_option *o, int *h, int *v)
{
  if (o->value == NULL || !measure_paper (o->value, h, v))
    measure_paper (o->default_value, h, v);
  driver_option_destroy (o);
}

static int
do_parse_boolean (const char *driver_name, const char *key,
                  const char *value)
{
  if (!strcmp (value, "on") || !strcmp (value, "true")
      || !strcmp (value, "yes") || !strcmp (value, "1"))
    return true;
  else if (!strcmp (value, "off") || !strcmp (value, "false")
           || !strcmp (value, "no") || !strcmp (value, "0"))
    return false;
  else
    {
      msg (MW, _("%s: `%s' is `%s' but a Boolean value is required"),
             driver_name, value, key);
      return -1;
    }
}

/* Parses and return O's value as a Boolean value.  "true" and "false", "yes"
   and "no", "on" and "off", and "1" and "0" are acceptable boolean strings.

   Destroys O. */
bool
parse_boolean (struct driver_option *o)
{
  bool retval;

  retval = do_parse_boolean (o->driver_name, o->name, o->default_value) > 0;
  if (o->value != NULL)
    {
      int value = do_parse_boolean (o->driver_name, o->name, o->value);
      if (value >= 0)
        retval = value;
    }

  driver_option_destroy (o);

  return retval;
}

/* Parses O's value as an enumeration constant.  The arguments to this function
   consist of a series of string/int pairs, terminated by a null pointer value.
   O's value is compared to each string in turn, and parse_enum() returns the
   int associated with the first matching string.  If there is no match, or if
   O has no user-specified value, then O's default value is treated the same
   way.  If the default value still does not match, parse_enum() returns 0.

   Example: parse_enum (o, "a", 1, "b", 2, NULL_SENTINEL) returns 1 if O's
   value if "a", 2 if O's value is "b".

   Destroys O. */
int
parse_enum (struct driver_option *o, ...)
{
  va_list args;
  int retval;

  retval = 0;
  va_start (args, o);
  for (;;)
    {
      const char *s;
      int value;

      s = va_arg (args, const char *);
      if (s == NULL)
        {
          if (o->value != NULL)
            {
              struct string choices;
              int i;

              ds_init_empty (&choices);
              va_end (args);
              va_start (args, o);
              for (i = 0; ; i++)
                {
                  s = va_arg (args, const char *);
                  if (s == NULL)
                    break;
                  value = va_arg (args, int);

                  if (i > 0)
                    ds_put_cstr (&choices, ", ");
                  ds_put_format (&choices, "`%s'", s);
                }

              msg (MW, _("%s: `%s' is `%s' but one of the following "
                             "is required: %s"),
                     o->driver_name, o->name, o->value, ds_cstr (&choices));
              ds_destroy (&choices);
            }
          break;
        }
      value = va_arg (args, int);

      if (o->value != NULL && !strcmp (s, o->value))
        {
          retval = value;
          break;
        }
      else if (!strcmp (s, o->default_value))
        retval = value;
    }
  va_end (args);
  driver_option_destroy (o);
  return retval;
}

/* Parses O's value as an integer in the range MIN_VALUE to MAX_VALUE
   (inclusive) and returns the integer.

   Destroys O. */
int
parse_int (struct driver_option *o, int min_value, int max_value)
{
  int retval = strtol (o->default_value, NULL, 0);

  if (o->value != NULL)
    {
      int value;
      char *tail;

      errno = 0;
      value = strtol (o->value, &tail, 0);
      if (tail != o->value && *tail == '\0' && errno != ERANGE
          && value >= min_value && value <= max_value)
        retval = value;
      else if (max_value == INT_MAX)
        {
          if (min_value == 0)
            msg (MW, _("%s: `%s' is `%s' but a nonnegative integer "
                           "is required"),
                   o->driver_name, o->name, o->value);
          else if (min_value == 1)
            msg (MW, _("%s: `%s' is `%s' but a positive integer is "
                           "required"), o->driver_name, o->name, o->value);
          else if (min_value == INT_MIN)
            msg (MW, _("%s: `%s' is `%s' but an integer is required"),
                   o->driver_name, o->name, o->value);
          else
            msg (MW, _("%s: `%s' is `%s' but an integer greater "
                           "than %d is required"),
                   o->driver_name, o->name, o->value, min_value - 1);
        }
      else
        msg (MW, _("%s: `%s' is `%s'  but an integer between %d and "
                       "%d is required"),
               o->driver_name, o->name, o->value, min_value, max_value);
    }

  driver_option_destroy (o);
  return retval;
}

/* Parses O's value as a dimension, as understood by measure_dimension(), and
   returns its length in units of 1/72000".

   Destroys O. */
int
parse_dimension (struct driver_option *o)
{
  int retval;

  retval = (o->value != NULL ? measure_dimension (o->value)
            : o->default_value != NULL ? measure_dimension (o->default_value)
            : -1);

  driver_option_destroy (o);
  return retval;
}

/* Parses O's value as a string and returns it as a malloc'd string that the
   caller is responsible for freeing.

   Destroys O. */
char *
parse_string (struct driver_option *o)
{
  char *retval = xstrdup (o->value != NULL ? o->value : o->default_value);
  driver_option_destroy (o);
  return retval;
}

static char *
default_chart_file_name (const char *file_name)
{
  if (strcmp (file_name, "-"))
    {
      const char *extension = strrchr (file_name, '.');
      int stem_length = extension ? extension - file_name : strlen (file_name);
      return xasprintf ("%.*s-#.png", stem_length, file_name);
    }
  else
    return NULL;
}

/* Parses and returns a chart file name, or NULL if no charts should be output.
   If a nonnull string is returned, it will contain at least one '#' character,
   which the client will presumably replace by a number as part of writing
   charts to separate files.

   If O->value is "none", then this function returns NULL.

   If O->value is non-NULL but not "none", returns a copy of that string (if it
   contains '#').

   If O->value is NULL, then O's default_value should be the name of the main
   output file.  Returns NULL if default_value is "-", and otherwise returns a
   copy of string string with its extension stripped off and "-#.png" appended.

   Destroys O. */
char *
parse_chart_file_name (struct driver_option *o)
{
  char *chart_file_name;

  if (o->value != NULL)
    {
      if (!strcmp (o->value, "none"))
        chart_file_name = NULL;
      else if (strchr (o->value, '#') != NULL)
        chart_file_name = xstrdup (o->value);
      else
        {
          msg (MW, _("%s: `%s' is `%s' but a file name that contains "
                         "`#' is required."),
                 o->name, o->value, o->driver_name);
          chart_file_name = default_chart_file_name (o->default_value);
        }
    }
  else
    chart_file_name = default_chart_file_name (o->default_value);

  driver_option_destroy (o);

  return chart_file_name;
}
