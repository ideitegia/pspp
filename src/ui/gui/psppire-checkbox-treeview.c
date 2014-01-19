/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2012, 2013  Free Software Foundation

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
   This module provides a subclass of GtkTreeView, designed for dialogs
   which need lists of annotated checkbox items.
   The object contains the necessary model and renderers, which means that
   the user does not have to create these herself.
 */

#include <config.h>
#include <gtk/gtk.h>

#include "psppire-checkbox-treeview.h"


#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_checkbox_treeview_init          (PsppireCheckboxTreeview      *cbtv);

GType
psppire_checkbox_treeview_get_type (void)
{
  static GType psppire_checkbox_treeview_type = 0;

  if (!psppire_checkbox_treeview_type)
    {
      static const GTypeInfo psppire_checkbox_treeview_info =
      {
	sizeof (PsppireCheckboxTreeviewClass),
	(GBaseInitFunc) NULL,
        (GBaseFinalizeFunc) NULL,
	(GClassInitFunc) NULL,
	(GClassFinalizeFunc) NULL,
	NULL,
        sizeof (PsppireCheckboxTreeview),
	0,
	(GInstanceInitFunc) psppire_checkbox_treeview_init,
      };

      psppire_checkbox_treeview_type =
	g_type_register_static (GTK_TYPE_TREE_VIEW, "PsppireCheckboxTreeview",
				&psppire_checkbox_treeview_info, 0);
    }

  return psppire_checkbox_treeview_type;
}



/* Callback for checkbox cells in the statistics tree view.
   Toggles the checkbox. */
static void
toggle (GtkCellRendererToggle *cell_renderer, const gchar *path_str, gpointer data)
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

/* Create the necessary columns and renderers and add them to the widget */
static void
treeview_checkbox_populate (GtkTreeView *treeview)
{
  /* Checkbox column. */
  GtkTreeViewColumn *col = gtk_tree_view_column_new ();
  GtkCellRenderer *renderer = gtk_cell_renderer_toggle_new ();

  gtk_tree_view_column_pack_start (col, renderer, TRUE);

  gtk_tree_view_append_column (treeview, col);

  gtk_tree_view_column_add_attribute  (col, renderer, "active", CHECKBOX_COLUMN_SELECTED);

  g_signal_connect (renderer, "toggled", G_CALLBACK (toggle), treeview);

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

static void
psppire_checkbox_treeview_init (PsppireCheckboxTreeview *cbtv)
{
  cbtv->list = GTK_TREE_MODEL (gtk_list_store_new (N_CHECKBOX_COLUMNS,
						   G_TYPE_STRING, 
						   G_TYPE_BOOLEAN,
						   G_TYPE_STRING));

  gtk_tree_view_set_model (GTK_TREE_VIEW (cbtv), cbtv->list);
  g_object_unref (cbtv->list);

  treeview_checkbox_populate (GTK_TREE_VIEW (cbtv));
}


/*
  Load the object's model from the array ITEMS.
  N_ITEMS is the size of the array.
  DEFAULT_ITEMS is a bitwise field indicating the initial state
  of the items.
*/
void
psppire_checkbox_treeview_populate (PsppireCheckboxTreeview *cbtv,
				    guint default_items,
				    gint n_items,
				    const struct checkbox_entry_item *items)
{
  size_t i;
  for (i = 0; i < n_items; ++i)
    {
      GtkTreeIter iter;
      gtk_list_store_append (GTK_LIST_STORE (cbtv->list), &iter);
      gtk_list_store_set (GTK_LIST_STORE (cbtv->list), &iter,
                          CHECKBOX_COLUMN_LABEL, gettext (items[i].label),
                          CHECKBOX_COLUMN_SELECTED,  (default_items & (1u << i)) != 0,
			  CHECKBOX_COLUMN_TOOLTIP, gettext (items[i].tooltip),
                          -1);
    }

  gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (cbtv), CHECKBOX_COLUMN_TOOLTIP);
}
