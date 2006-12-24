/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2004  Free Software Foundation

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA. */


#ifndef __MISC_H__
#define __MISC_H__

#include <data/value.h>
#include <data/format.h>

#include <gtk/gtk.h>
#include <glade/glade.h>

/* Formats a value according to FORMAT
   The returned string must be freed when no longer required */
gchar * value_to_text (union value v, struct fmt_spec format);


gboolean text_to_value (const gchar *text, union value *v,
		       struct fmt_spec format);

GtkWidget * get_widget_assert (GladeXML *xml, const gchar *name);

/* Converts a string in the pspp locale to utf-8 */
char * pspp_locale_to_utf8 (const gchar *text, gssize len, GError **err);


#endif
