/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2006, 2007  Free Software Foundation

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


#ifndef DATA_EDITOR_H
#define DATA_EDITOR_H

#include <glade/glade.h>
#include <gtk/gtk.h>
#include "window-manager.h"

struct data_editor
{
  struct editor_window parent;

  GtkAction *action_data_new;
  GtkAction *action_data_open;
  GtkAction *action_data_save_as;
  GtkAction *action_data_save;

  GtkAction *invoke_weight_cases_dialog;

  GladeXML *xml;

  gboolean save_as_portable;

  /* Name of the file this data is associated with (ie, was loaded from or
     has been  saved to), in "filename encoding",  or NULL, if it's not
     associated with any file */
  gchar *file_name;
};


struct data_editor * new_data_editor (void);

void new_data_window (GtkMenuItem *, gpointer);

void data_editor_select_sheet (struct data_editor *de, gint page);


#endif
