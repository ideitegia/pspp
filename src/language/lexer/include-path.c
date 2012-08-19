/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Software Foundation, Inc.

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

#include "src/language/lexer/include-path.h"

#include <stdlib.h>

#include "data/file-name.h"
#include "libpspp/string-array.h"

#include "gl/configmake.h"
#include "gl/relocatable.h"
#include "gl/xvasprintf.h"

static struct string_array the_include_path;
static struct string_array default_include_path;

static void include_path_init__ (void);

void
include_path_clear (void)
{
  include_path_init__ ();
  string_array_clear (&the_include_path);
}

void
include_path_add (const char *dir)
{
  include_path_init__ ();
  string_array_append (&the_include_path, dir);
}

char *
include_path_search (const char *base_name)
{
  return fn_search_path (base_name, include_path ());
}

const struct string_array *
include_path_default (void)
{
  include_path_init__ ();
  return &default_include_path;
}

char **
include_path (void)
{
  include_path_init__ ();
  string_array_terminate_null (&the_include_path);
  return the_include_path.strings;
}

static void
include_path_init__ (void)
{
  static bool inited;
  char *home;

  if (inited)
    return;
  inited = true;

  string_array_init (&the_include_path);
  string_array_append (&the_include_path, ".");
  home = getenv ("HOME");
  if (home != NULL)
    string_array_append_nocopy (&the_include_path,
                                xasprintf ("%s/.pspp", home));
  string_array_append (&the_include_path, relocate (PKGDATADIR));

  string_array_clone (&default_include_path, &the_include_path);
}
