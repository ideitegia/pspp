/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2009, 2010, 2011, 2012  Free Software Foundation

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <config.h>

#include "builder-wrapper.h"


GtkBuilder *
builder_new_real (const gchar *name)
{
  GtkBuilder *builder = gtk_builder_new ();

  GError *err = NULL;
  if ( ! gtk_builder_add_from_file (builder, name,  &err))
    {
      g_critical ("Couldn\'t open user interface  file %s: %s", name, err->message);
      g_clear_error (&err);
    }

  return builder;
}


GtkBuilder * 
builder_new_x (const gchar *obj_name)
{
  GtkBuilder *b;
  GString *str = g_string_new (PKGDATADIR);
  g_string_append (str, "/");
  g_string_append (str, obj_name);

  b = builder_new_real (relocate (str->str));

  g_string_free (str, TRUE);

  return b;
}



GObject *
get_object_assert (GtkBuilder *builder, const gchar *name, GType type)
{
  GObject *o = NULL;
  g_assert (name);

  o = gtk_builder_get_object (builder, name);

  if ( !o )
    g_critical ("Object `%s' could not be found\n", name);
  else if ( ! g_type_is_a (G_OBJECT_TYPE (o), type))
   {
     g_critical ("Object `%s' was expected to have type %s, but in fact has type %s", 
	name, g_type_name (type), G_OBJECT_TYPE_NAME (o));
   }

  return o;
}


GtkAction *
get_action_assert (GtkBuilder *builder, const gchar *name)
{
  return GTK_ACTION (get_object_assert (builder, name, GTK_TYPE_ACTION));
}

GtkWidget *
get_widget_assert (GtkBuilder *builder, const gchar *name)
{
  GtkWidget *w = GTK_WIDGET (get_object_assert (builder, name, GTK_TYPE_WIDGET));
  
  g_object_set (w, "name", name, NULL);

  return w;
}
