/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2009  Free Software Foundation

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


#ifndef __MISC_H__
#define __MISC_H__

#include "relocatable.h"

#include <data/format.h>
#include <data/value.h>

#include <gtk/gtk.h>



void paste_syntax_in_new_window (const gchar *syntax);

struct fmt_spec;

/* Formats a value according to FORMAT
   The returned string must be freed when no longer required */
gchar * value_to_text (union value v, struct fmt_spec format);


gboolean text_to_value (const gchar *text, union value *v,
		       struct fmt_spec format);

GObject *get_object_assert (GtkBuilder *builder, const gchar *name, GType type);
GtkAction * get_action_assert (GtkBuilder *builder, const gchar *name);
GtkWidget * get_widget_assert (GtkBuilder *builder, const gchar *name);

/* Converts a string in the pspp locale to utf-8 */
char * pspp_locale_to_utf8 (const gchar *text, gssize len, GError **err);


void connect_help (GtkBuilder *);

void reference_manual (GtkMenuItem *, gpointer);

struct getl_interface;
gboolean execute_syntax (struct getl_interface *sss);


#define builder_new(NAME) builder_new_real (relocate (PKGDATADIR "/" NAME))

GtkBuilder *builder_new_real (const gchar *name);


/* Create a deep copy of SRC */
GtkListStore * clone_list_store (const GtkListStore *src);


#endif
