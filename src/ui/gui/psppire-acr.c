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


/*
  This widget is a GtkBox which looks roughly like:

  +-----------------------------+
  |+------------+  +----------+	|
  ||   Add      |  |	      |	|
  |+------------+  |	      |	|
  |                |	      |	|
  |+------------+  |	      |	|
  ||   Edit     |  |	      |	|
  |+------------+  |	      |	|
  |      	   |	      |	|
  |+------------+  |	      |	|
  ||  Remove    |  |	      |	|
  |+------------+  +----------+	|
  +-----------------------------+

*/

#include <config.h>
#include <gtk/gtk.h>

#include "psppire-acr.h"
#include "helper.h"

static void psppire_acr_init (PsppireAcr *);

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


static gboolean row_is_selected (const PsppireAcr *acr);


static gboolean
value_from_entry (gint col, GValue *val, gpointer data)
{
  GtkEntry *entry = data;
  const gchar *text = gtk_entry_get_text (entry);
  gdouble x = g_strtod (text, 0);

  g_value_init (val, G_TYPE_DOUBLE);
  g_value_set_double (val, x);

  return TRUE;
}


/* Returns true, if there's text in the entry */
static gboolean
entry_not_empty (gpointer data)
{
  GtkEntry *entry = data;

  const char *text = gtk_entry_get_text (entry);

  return !g_str_equal (text, "");
}


static void
clear_entry (gpointer data)
{
  GtkEntry *entry = data;
  gtk_entry_set_text (entry, "");
}


static void
on_entry_change (GtkEntry *entry, PsppireAcr *acr)
{
  gtk_widget_set_sensitive (acr->add_button, acr->enabled (entry));

  gtk_widget_set_sensitive (acr->change_button, acr->enabled (entry)
			    && row_is_selected (acr));
}

void
psppire_acr_set_entry  (PsppireAcr *acr, GtkEntry *entry)
{
  acr->get_value = value_from_entry;
  acr->get_value_data = entry;
  acr->enabled = entry_not_empty;
  acr->enabled_data = entry;
  acr->update = clear_entry;
  acr->update_data = entry;

  g_signal_connect (entry, "changed", G_CALLBACK (on_entry_change), acr);
}


/* Callback for when the Add button is clicked.
   It appends an item to the list. */
static void
on_add_button_clicked (PsppireAcr *acr)
{
  gint i;
  GtkTreeIter iter;
  gtk_list_store_append (acr->list_store, &iter);

  for (i = 0 ;
       i < gtk_tree_model_get_n_columns (GTK_TREE_MODEL (acr->list_store));
       ++i)
    {
      static GValue value;
      if ( ! acr->get_value (i, &value, acr->get_value_data) )
	continue;

      gtk_list_store_set_value (acr->list_store, &iter,
				i, &value);
      g_value_unset (&value);
    }

  if (acr->update) acr->update (acr->update_data);
}


/* Callback for when the Changed button is clicked.
   It replaces the currently selected entry. */
static void
on_change_button_clicked (PsppireAcr *acr)
{
  gint i;
  GtkTreeModel *model = GTK_TREE_MODEL (acr->list_store);

  GList *l=
    gtk_tree_selection_get_selected_rows (acr->selection,
					  &model);

  GtkTreePath *path = l->data;

  GtkTreeIter iter;

  gtk_tree_model_get_iter (model, &iter, path);

  for (i = 0 ;
       i < gtk_tree_model_get_n_columns (GTK_TREE_MODEL (acr->list_store));
       ++i)
    {
      static GValue value;
      if ( ! acr->get_value (i, &value, acr->get_value_data) )
	continue;

      gtk_list_store_set_value (acr->list_store, &iter,
				i, &value);
      g_value_unset (&value);
    }

  g_list_foreach (l, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (l);

  if ( acr->update) acr->update (acr->update_data);
}


/* Callback for when the remove button is clicked.
   It deletes the currently selected entry. */
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

/* Returns true if there is a row currently selected.
   False otherwise. */
static gboolean
row_is_selected (const PsppireAcr *acr)
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


/* Callback which occurs when an item in the treeview
   is selected */
static void
on_select (GtkTreeSelection *selection, gpointer data)
{
  PsppireAcr *acr = data;

  gtk_widget_set_sensitive (acr->remove_button, row_is_selected (acr));

  gtk_widget_set_sensitive (acr->change_button,
			    row_is_selected (acr)
			    );
}


void
psppire_acr_set_enabled (PsppireAcr *acr, gboolean status)
{

  gtk_widget_set_sensitive (acr->add_button, status);

  gtk_widget_set_sensitive (acr->change_button, status
			    && row_is_selected (acr));
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

  acr->get_value = NULL;
  acr->get_value_data = NULL;
  acr->enabled = NULL;
  acr->update = NULL;

  gtk_widget_set_sensitive (acr->change_button, FALSE);
  gtk_widget_set_sensitive (acr->remove_button, FALSE);
  gtk_widget_set_sensitive (acr->add_button, FALSE);

  psppire_box_pack_start_defaults (GTK_BOX (bb), acr->add_button);
  psppire_box_pack_start_defaults (GTK_BOX (bb), acr->change_button);
  psppire_box_pack_start_defaults (GTK_BOX (bb), acr->remove_button);

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
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
    GtkTreeViewColumn *column =
      gtk_tree_view_column_new_with_attributes ("value",
						renderer,
						"text", 0,
						NULL);

    gtk_tree_view_append_column (acr->tv, column);
  }

}


GtkWidget*
psppire_acr_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_acr_get_type (), NULL));
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


void
psppire_acr_set_enable_func (PsppireAcr *acr, EnabledFunc func, gpointer p)
{
  acr->enabled = func;
  acr->enabled_data = p;
}

void
psppire_acr_set_get_value_func (PsppireAcr *acr,
				GetValueFunc getvalue, gpointer data)
{
  acr->get_value_data = data;
  acr->get_value = getvalue;
}
