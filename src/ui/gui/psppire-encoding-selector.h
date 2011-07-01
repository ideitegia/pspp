/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011 Free Software Foundation, Inc.

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

#ifndef PSPPIRE_ENCODING_SELECTOR_H
#define PSPPIRE_ENCODING_SELECTOR_H 1

#include <gtk/gtk.h>

GtkWidget *psppire_encoding_selector_new (const char *default_encoding,
                                          gboolean allow_auto);
gchar *psppire_encoding_selector_get_encoding (GtkWidget *selector);

#endif /* PSPPIRE_ENCODING_SELECTOR_H */
