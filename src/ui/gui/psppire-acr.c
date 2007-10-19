/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

#include <gtk/gtk.h>

#include "psppire-acr.h"

static void psppire_acr_init (PsppireAcr *);

enum {
  COLUMN_DOUBLE,
  n_COLUMNS
};


GType
psppire_acr_get_type (void)
{
  static GType acr_type = 0;

  if (!acr_type)
    {
      static const GTypeInfo acr_info =
      {
	sizeof (PsppireAcrClass),
	NULL, /* base_init */
        NULL, /* base_finalize */
	NULL, /* class_init */
        NULL, /* class_finalize */
	NULL, /* class_data */
        sizeof (PsppireAcr),
	0,
	(GInstanceInitFunc) psppire_acr_init,
      };

      acr_type = g_type_register_static (GTK_TYPE_HBOX, "PsppireAcr",
					&acr_info, 0);
    }

  return acr_type;
}

static void
on_add_button_clicked (PsppireAcr *acr)
{
  GtkTreeIter iter;
  gdouble x;

  const gchar *text = gtk_entry_get_text (acr->entry);
  x = g_strtod (text, 0);

  gtk_list_store_append (acr->list_store, &iter);

  gtk_list_store_set (acr->list_store, &iter,
		      COLUMN_DOUBLE, x,
		      -1);

  gtk_entry_set_text (acr->entry, "");
}

static void
on_change_button_clicked (PsppireAcr *acr)
{
  const gchar *text;
  GValue value = {0};
  GtkTreeModel *model = GTK_TREE_MODEL (acr->list_store);

  GList *l=
    gtk_tree_selection_get_selected_rows (acr->selection,
					  &model);

  GtkTreePath *path = l->data;

  GtkTreeIter iter;

  gtk_tree_model_get_iter (model, &iter, path);

  text = gtk_entry_get_text (acr->entry);

  g_value_init (&value, G_TYPE_DOUBLE);
  g_value_set_double (&value, g_strtod (text, NULL));
  gtk_list_store_set_value (acr->list_store, &iter, 0, &value);

  g_list_foreach (l, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (l);
}

static void
on_remove_button_clicked (PsppireAcr *acr)
{
  GtkTreeModel *model = GTK_TREE_MODEL (acr->list_store);

  GList *l=
    gtk_tree_selection_get_selected_rows (acr->selection,
					  &model);

  GtkTreePath *path = l->data;

  GtkTreeIter iter;

  gtk_tree_model_get_iter (model, &iter, path);

  gtk_list_store_remove (acr->list_store, &iter);

  g_list_foreach (l, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (l);
}

static gboolean
value_present (const PsppireAcr *acr)
{
  const char *text = gtk_entry_get_text (acr->entry);
  return !g_str_equal (text, "");
}

static gboolean
row_is_selected (PsppireAcr *acr)
{
  gboolean result;
  GtkTreeModel *model = GTK_TREE_MODEL (acr->list_store);
  GList *l = gtk_tree_selection_get_selected_rows (acr->selection,
						   &model);

  result = (l != NULL);

  g_list_foreach (l, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (l);

  return result;
}


static void
on_select (GtkTreeSelection *selection, gpointer data)
{
  PsppireAcr *acr = data;

  gtk_widget_set_sensitive (acr->remove_button, row_is_selected (acr));

  gtk_widget_set_sensitive (acr->change_button,
			    row_is_selected (acr) && value_present (acr));
}



static void
psppire_acr_init (PsppireAcr *acr)
{
  GtkWidget *bb  = gtk_vbutton_box_new ();

  GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);

  acr->tv = GTK_TREE_VIEW (gtk_tree_view_new ());

  acr->add_button = gtk_button_new_from_stock (GTK_STOCK_ADD);
  acr->change_button = gtk_button_new_from_stock (GTK_STOCK_EDIT);
  acr->remove_button = gtk_button_new_from_stock (GTK_STOCK_REMOVE);

  gtk_widget_set_sensitive (acr->change_button, FALSE);
  gtk_widget_set_sensitive (acr->remove_button, FALSE);
  gtk_widget_set_sensitive (acr->add_button, FALSE);

  gtk_box_pack_start_defaults (GTK_BOX (bb), acr->add_button);
  gtk_box_pack_start_defaults (GTK_BOX (bb), acr->change_button);
  gtk_box_pack_start_defaults (GTK_BOX (bb), acr->remove_button);

  gtk_box_pack_start (GTK_BOX (acr), bb, FALSE, TRUE, 5);

  g_object_set (sw,
		"hscrollbar-policy", GTK_POLICY_NEVER,
		"vscrollbar-policy", GTK_POLICY_AUTOMATIC,
		"shadow-type", GTK_SHADOW_ETCHED_IN,
		NULL);

  gtk_container_add (GTK_CONTAINER (sw), GTK_WIDGET (acr->tv));

  gtk_box_pack_start (GTK_BOX (acr), sw, TRUE, TRUE, 5);


  g_signal_connect_swapped (acr->add_button, "clicked",
			    G_CALLBACK (on_add_button_clicked), acr);
  g_signal_connect_swapped (acr->change_button, "clicked",
			    G_CALLBACK (on_change_button_clicked), acr);
  g_signal_connect_swapped (acr->remove_button, "clicked",
			    G_CALLBACK (on_remove_button_clicked), acr);

  gtk_widget_show_all (bb);


  g_object_set (acr->tv, "headers-visible", FALSE, NULL);

  acr->list_store = NULL;

  psppire_acr_set_model (acr, acr->list_store);

  acr->selection = gtk_tree_view_get_selection (acr->tv);

  g_signal_connect (acr->selection, "changed", G_CALLBACK (on_select), acr);

  gtk_widget_set_sensitive (GTK_WIDGET (acr), FALSE);

  gtk_widget_show_all (sw);

  {
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_text_new ();
    column = gtk_tree_view_column_new_with_attributes ("header",
						       renderer,
						       "text", COLUMN_DOUBLE,
						       NULL);

    gtk_tree_view_append_column (acr->tv, column);
  }

  acr->entry = NULL;
}


GtkWidget*
psppire_acr_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_acr_get_type (), NULL));
}

static void
on_entry_change (GtkEntry *entry, PsppireAcr *acr)
{
  gtk_widget_set_sensitive (acr->add_button, value_present (acr));

  gtk_widget_set_sensitive (acr->change_button, value_present (acr)
			    && row_is_selected (acr));
}

void
psppire_acr_set_entry  (PsppireAcr *acr, GtkEntry *entry)
{
  acr->entry = entry;

  g_signal_connect (entry, "changed", G_CALLBACK (on_entry_change), acr);
}



/* Set the widget's treemodel */
void
psppire_acr_set_model (PsppireAcr *acr, GtkListStore *liststore)
{
  acr->list_store = liststore;

  gtk_tree_view_set_model (GTK_TREE_VIEW (acr->tv),
			   GTK_TREE_MODEL (liststore));

  gtk_widget_set_sensitive (GTK_WIDGET (acr), liststore != NULL);
}
