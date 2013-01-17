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
builder_new_real (const gchar *pathname, const gchar *filename)
{
  GtkBuilder *builder = gtk_builder_new ();
  guint result = 0;
  GError *err = NULL;
  result = gtk_builder_add_from_file (builder, pathname,  &err);
  if (result == 0)
    {
      gchar *srcdir = getenv ("PSPPIRE_SOURCE_DIR");
      if (srcdir)
	{
	  gchar *altpathname = g_strdup_printf ("%s/%s", srcdir, filename);
	  g_warning ("Trying alternative path for ui file %s", altpathname);
	  result = gtk_builder_add_from_file (builder, altpathname,  NULL);
	  g_free (altpathname);
	}
      if (result == 0)
	g_critical ("Couldn\'t open user interface  file %s: %s", pathname, err->message);

      g_clear_error (&err);
    }

  return builder;
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
