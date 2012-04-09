/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012 Free Software Foundation

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
#include "ui/gui/psppire-format.h"

static gpointer
psppire_format_copy (gpointer boxed)
{
  struct fmt_spec *format = boxed;
  return g_memdup (format, sizeof *format);
}

static void
psppire_format_free (gpointer boxed)
{
  struct fmt_spec *format = boxed;
  g_free (format);
}

GType
psppire_format_get_type (void)
{
  static GType type = 0;

  if (type == 0)
    type = g_boxed_type_register_static ("PsppireFormat",
                                         psppire_format_copy,
                                         psppire_format_free);

  return type;
}

