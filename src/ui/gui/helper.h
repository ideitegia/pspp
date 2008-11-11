/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004  Free Software Foundation

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
#include <glade/glade.h>

/*
   GtkRecentChooserMenu was added in 2.10.0
   but it didn't support GtkRecentFilters until
   2.10.2
*/
#define RECENT_LISTS_AVAILABLE GTK_CHECK_VERSION (2, 10, 2)

struct fmt_spec;

/* Formats a value according to FORMAT
   The returned string must be freed when no longer required */
gchar * value_to_text (union value v, struct fmt_spec format);


gboolean text_to_value (const gchar *text, union value *v,
		       struct fmt_spec format);

GtkWidget * get_widget_assert (GladeXML *xml, const gchar *name);

/* Converts a string in the pspp locale to utf-8 */
char * pspp_locale_to_utf8 (const gchar *text, gssize len, GError **err);


void connect_help (GladeXML *);

void reference_manual (GtkMenuItem *, gpointer);

struct getl_interface;
gboolean execute_syntax (struct getl_interface *sss);

#define XML_NEW(FILE) \
   glade_xml_new (relocate(PKGDATADIR "/" FILE), NULL, NULL)


void marshaller_VOID__INT_INT_INT (GClosure     *closure,
				   GValue       *return_value,
				   guint         n_param_values,
				   const GValue *param_values,
				   gpointer      invocation_hint,
				   gpointer      marshal_data);


/* Create a deep copy of SRC */
GtkListStore * clone_list_store (const GtkListStore *src);


#endif
