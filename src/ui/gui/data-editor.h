/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2006  Free Software Foundation

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
  GladeXML *xml;
};


struct data_editor * new_data_editor (void);

void new_data_window (GtkMenuItem *, gpointer);

void open_data_window (GtkMenuItem *, gpointer);

void data_editor_select_sheet(struct data_editor *de, gint page);


#endif
