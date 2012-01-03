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

#ifndef _BUILDER_WRAPPER_H
#define _BUILDER_WRAPPER_H

#include <gtk/gtk.h>

#include "relocatable.h"
#include "gl/configmake.h"


GtkBuilder *builder_new_real (const gchar *name);

GtkBuilder * builder_new_x (const gchar *obj_name);

#define builder_new(NAME) (builder_new_real (relocate (PKGDATADIR "/" NAME)))

GObject *get_object_assert (GtkBuilder *builder, const gchar *name, GType type);
GtkAction * get_action_assert (GtkBuilder *builder, const gchar *name);
GtkWidget * get_widget_assert (GtkBuilder *builder, const gchar *name);


#endif
