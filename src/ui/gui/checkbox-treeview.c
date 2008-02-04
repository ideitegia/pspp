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

#include <config.h>

#include "checkbox-treeview.h"
#include <gtk/gtk.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


/* Callback for checkbox cells in the statistics tree view.
   Toggles the checkbox. */
static void
toggle (GtkCellRendererToggle *cell_renderer, gchar *path_str, gpointer data)
{
  GtkTreeView *tv = GTK_TREE_VIEW (data);
  GtkTreeModel *model = gtk_tree_view_get_model (tv);
  GtkTreeIter iter;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
  gboolean selected;

  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter, CHECKBOX_COLUMN_SELECTED, &selected, -1);
  gtk_list_store_set (GTK_LIST_STORE (model), &iter, CHECKBOX_COLUMN_SELECTED,
                      !selected, -1);
  gtk_tree_path_free (path);
}


static void
treeview_create_checkbox_model (GtkTreeView *treeview,
				guint default_items,
				gint n_items,
				const struct checkbox_entry_item *items
				)
{
  GtkListStore *list;
  size_t i;

  list = gtk_list_store_new (N_CHECKBOX_COLUMNS,
			     G_TYPE_STRING, G_TYPE_BOOLEAN);
  gtk_tree_view_set_model (GTK_TREE_VIEW (treeview), GTK_TREE_MODEL (list));

  for (i = 0; i < n_items; i++)
    {
      GtkTreeIter iter;
      gtk_list_store_append (list, &iter);
      gtk_list_store_set (list, &iter,
                          CHECKBOX_COLUMN_LABEL, items[i].label,
                          CHECKBOX_COLUMN_SELECTED,
			  (default_items & (1u << i)) != 0,
                          -1);
    }
}

static void
treeview_checkbox_populate (GtkTreeView *treeview)
{
  GtkTreeViewColumn *col;
  GtkCellRenderer *renderer;

  /* Checkbox column. */
  col = gtk_tree_view_column_new ();
  renderer = gtk_cell_renderer_toggle_new ();

  gtk_tree_view_column_pack_start (col, renderer, TRUE);

  gtk_tree_view_append_column (treeview, col);

  gtk_tree_view_column_add_attribute  (col, renderer, "active", CHECKBOX_COLUMN_SELECTED);

  g_signal_connect (GTK_CELL_RENDERER_TOGGLE (renderer),
                    "toggled", G_CALLBACK (toggle), treeview);


  /* Label column. */
  col = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (col, _("Statistic"));
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (col, renderer, TRUE);

  gtk_tree_view_column_add_attribute  (col, renderer, "text", CHECKBOX_COLUMN_LABEL);

  g_object_set (renderer, "ellipsize-set", TRUE, NULL);
  g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  gtk_tree_view_column_set_min_width (col, 200);
  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_resizable (col, TRUE);
  gtk_tree_view_append_column (treeview, col);
}


void
put_checkbox_items_in_treeview (GtkTreeView *treeview,
				guint default_items,
				gint n_items,
				const struct checkbox_entry_item *items
				)
{
  treeview_create_checkbox_model (treeview, default_items, n_items, items);
  treeview_checkbox_populate (treeview);
}
