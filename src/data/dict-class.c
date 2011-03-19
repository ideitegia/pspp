/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2011 Free Software Foundation, Inc.

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

#include "data/dict-class.h"

#include "libpspp/assertion.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Returns the dictionary class corresponding to a variable named
   NAME. */
enum dict_class
dict_class_from_id (const char *name)
{
  switch (name[0])
    {
    default:
      return DC_ORDINARY;
    case '$':
      return DC_SYSTEM;
    case '#':
      return DC_SCRATCH;
    }
}

/* Returns the name of dictionary class DICT_CLASS.

   This function should probably not be used in new code as it
   can lead to difficulties for internationalization. */
const char *
dict_class_to_name (enum dict_class dict_class)
{
  switch (dict_class)
    {
    case DC_ORDINARY:
      return _("ordinary");
    case DC_SYSTEM:
      return _("system");
    case DC_SCRATCH:
      return _("scratch");
    default:
      NOT_REACHED ();
    }
}
