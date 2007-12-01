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


/* A GtkTreeCellDataFunc which renders a checkbox that determines
   whether to calculate the statistic. */
static void
stat_calculate_cell_data_func (GtkTreeViewColumn *col,
                               GtkCellRenderer *cell,
                               GtkTreeModel *model,
                               GtkTreeIter *iter,
                               gpointer data)
{
  gboolean selected;

  gtk_tree_model_get (model, iter, CHECKBOX_COLUMN_SELECTED, &selected, -1);
  g_object_set (cell, "active", selected, NULL);
}


/* A GtkTreeCellDataFunc which renders the label of the statistic. */
static void
stat_label_cell_data_func (GtkTreeViewColumn *col,
                           GtkCellRenderer *cell,
                           GtkTreeModel *model,
                           GtkTreeIter *iter,
                           gpointer statistic)
{
  gchar *label = NULL;
  gtk_tree_model_get (model, iter, CHECKBOX_COLUMN_LABEL, &label, -1);
  g_object_set (cell, "text", gettext (label), NULL);
  g_free (label);
}

/* Callback for checkbox cells in the statistics tree view.
   Toggles the checkbox. */
static void
toggle (GtkCellRendererToggle *cell_renderer, gchar *path_str, gpointer data)
{
  GtkTreeModel *model = (GtkTreeModel *)data;
  GtkTreeIter iter;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
  gboolean selected;

  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter, CHECKBOX_COLUMN_SELECTED, &selected, -1);
  gtk_list_store_set (GTK_LIST_STORE (model), &iter, CHECKBOX_COLUMN_SELECTED,
                      !selected, -1);
  gtk_tree_path_free (path);
}


void
put_checkbox_items_in_treeview (GtkTreeView *treeview,
				guint default_items,
				gint n_items,
				const struct checkbox_entry_item *items
				)
{
  GtkTreeViewColumn *col;
  GtkCellRenderer *renderer;
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

  /* Calculate column. */
  col = gtk_tree_view_column_new ();
  renderer = gtk_cell_renderer_toggle_new ();
  g_signal_connect (GTK_CELL_RENDERER_TOGGLE (renderer),
                    "toggled", G_CALLBACK (toggle), GTK_TREE_MODEL (list));
  gtk_tree_view_column_pack_start (col, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (col, renderer,
					   stat_calculate_cell_data_func,
					   NULL, NULL);
  gtk_tree_view_append_column (treeview, col);

  /* Statistic column. */
  col = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (col, _("Statistic"));
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (col, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (col, renderer,
					   stat_label_cell_data_func,
					   NULL, NULL);
  g_object_set (renderer, "ellipsize-set", TRUE, NULL);
  g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  gtk_tree_view_column_set_min_width (col, 200);
  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_resizable (col, TRUE);
  gtk_tree_view_append_column (treeview, col);
}
