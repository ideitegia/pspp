/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2009, 2010, 2011  Free Software Foundation

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

#include "psppire-dict.h"

#include "gl/configmake.h"

gchar *paste_syntax_to_window (gchar *syntax);

struct fmt_spec;

/* Returns a new GParamSpec for a string.  An attempt to store the empty string
   in the parameter will be silently translated into storing a null pointer. */
static inline GParamSpec *
null_if_empty_param (const gchar *name, const gchar *nick,
                     const gchar *blurb, const gchar *default_value,
                     GParamFlags flags)
{
  GParamSpec *param;

  param = g_param_spec_string (name, nick, blurb, default_value, flags);
  ((GParamSpecString *) param)->null_fold_if_empty = TRUE;
  return param;
}


gchar * value_to_text (union value v, const struct variable *);


union value *
text_to_value (const gchar *text,
	       const struct variable *var,
	       union value *);

GObject *get_object_assert (GtkBuilder *builder, const gchar *name, GType type);
GtkAction * get_action_assert (GtkBuilder *builder, const gchar *name);
GtkWidget * get_widget_assert (GtkBuilder *builder, const gchar *name);

gchar * convert_glib_filename_to_system_filename (const gchar *fname,
						  GError **err);

void connect_help (GtkBuilder *);

#define builder_new(NAME) builder_new_real (relocate (PKGDATADIR "/" NAME))

GtkBuilder *builder_new_real (const gchar *name);


/* Create a deep copy of SRC */
GtkListStore * clone_list_store (const GtkListStore *src);

void psppire_box_pack_start_defaults (GtkBox *box, GtkWidget *widget);




#if ! GTK_CHECK_VERSION (2,20,0)
static inline gboolean gtk_widget_get_realized (GtkWidget *w)
{
  return GTK_WIDGET_REALIZED (w);
}

static inline gboolean gtk_widget_get_mapped (GtkWidget *w)
{
  return GTK_WIDGET_MAPPED (w);
}
#endif


#endif
