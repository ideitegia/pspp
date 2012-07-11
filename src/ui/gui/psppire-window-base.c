/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012  Free Software Foundation

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

/*
 This is an abstract base class upon which all (well almost all) windows in
 psppire are based.   The exceptions are transient windows such as the 
 splash screen and popups.

 It currently provides the feature where the window's geometry "persists"
 so that she gets the windows appearing in her favourite size/shape/position.
*/


#include <config.h>

#include "psppire-window-base.h"
#include "psppire-conf.h"
#include <string.h>

#include <gtk/gtk.h>

static void psppire_window_base_class_init    (PsppireWindowBaseClass *class);
static void psppire_window_base_init          (PsppireWindowBase      *window);

G_DEFINE_ABSTRACT_TYPE (PsppireWindowBase, psppire_window_base, GTK_TYPE_WINDOW);


/* Obtain a string identifying this window.

   If the window has a name, we use that.
   Otherwise we fall back on the class name.
 */
static const char *
get_window_id (GtkWidget *wb)
{
  const gchar *name = gtk_widget_get_name (wb);
  if (NULL == name || 0 == strcmp ("", name))
    name = G_OBJECT_TYPE_NAME (wb);

  return name;
}

/*
  On realization, we read the desired geometry from the config, and set the
  window accordingly.
 */
static void
realize (GtkWidget *wb)
{
  PsppireConf *conf = psppire_conf_new ();

  psppire_conf_set_window_geometry (conf, get_window_id (wb), GTK_WINDOW (wb));

  if (GTK_WIDGET_CLASS (psppire_window_base_parent_class)->realize)
    return GTK_WIDGET_CLASS (psppire_window_base_parent_class)->realize (wb) ;
}


/*
  When the window is resized/repositioned, write the new geometry to the config.
*/
static gboolean
configure_event (GtkWidget *wb, GdkEventConfigure *event)
{
  if (gtk_widget_get_mapped (wb))
    {
      PsppireConf *conf = psppire_conf_new ();

      psppire_conf_save_window_geometry (conf, get_window_id (wb), GTK_WINDOW (wb));
    }

  if (GTK_WIDGET_CLASS (psppire_window_base_parent_class)->configure_event)
    return GTK_WIDGET_CLASS (psppire_window_base_parent_class)->configure_event (wb, event) ;

  return FALSE;
}

static void 
psppire_window_base_class_init    (PsppireWindowBaseClass *class)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
  widget_class->configure_event = configure_event;
  widget_class->realize = realize;
}

static void 
psppire_window_base_init          (PsppireWindowBase      *window)
{
}

