/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007  Free Software Foundation

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


#ifndef __CHECKBOX_TREEVIEW_H__
#define __CHECKBOX_TREEVIEW_H__ 1


#include <gtk/gtk.h>

struct checkbox_entry_item
  {
    const char *name;
    const char *label;
  };

enum
  {
    CHECKBOX_COLUMN_LABEL,
    CHECKBOX_COLUMN_SELECTED,
    N_CHECKBOX_COLUMNS
  };


void put_checkbox_items_in_treeview (GtkTreeView *treeview,
				     guint default_items,
				     gint n_items,
				     const struct checkbox_entry_item *items
				     );
#endif
