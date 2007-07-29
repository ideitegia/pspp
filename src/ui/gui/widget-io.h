/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007  Free Software Foundation

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

#include <gtk/gtk.h>


/* Returns a string generated from FMT and a list of GtkEntry widgets.
   Each conversion in FMT will be replaced with the text from the
   corresponding GtkEntry.  The normal printf semantics will be ignored.
   Note that the GtkEntry widgets may be GtkSpinbuttons or any other widget
   derived from GtkEntry.
   The returned string should be freed when no longer required.
 */
gchar * widget_printf (const gchar *fmt, ...);

/*
   Returns a GtkHBox populated with an GtkLabel and GtkEntry widgets.
   Each conversion in FMT will cause a GtkEntry (possibly a GtkSpinButton) to
   be created.  Any text between conversions produces a GtkLabel.
   There should be N arguments following FMT should be of type GtkEntry **,
   where N is the number of conversions.
   These arguments will be filled with a pointer to the corresponding widgets.
   Their properties may be changed, but they should not be unrefed.
 */
GtkWidget *widget_scanf (const gchar *fmt, ...);
