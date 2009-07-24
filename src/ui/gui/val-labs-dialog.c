/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2005, 2009  Free Software Foundation

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


/*  This module describes the behaviour of the Value Labels dialog box,
    used for input of the value labels in the variable sheet */

#include <config.h>

#include <string.h>

#include "helper.h"
#include "val-labs-dialog.h"
#include <data/value-labels.h>
#include <data/format.h>
#include "psppire-var-sheet.h"
#include "psppire-var-store.h"
#include <libpspp/i18n.h>

struct val_labs_dialog
{
  GtkWidget *window;

  PsppireVarStore *var_store;

  /* The variable to be updated */
  struct variable *pv;

  /* Local copy of labels */
  struct val_labs *labs;

  /* Actions */
  GtkWidget *add_button;
  GtkWidget *remove_button;
  GtkWidget *change_button;

  /* Entry Boxes */
  GtkWidget *value_entry;
  GtkWidget *label_entry;

  /* Signal handler ids */
  gint change_handler_id;
  gint value_handler_id;

  GtkWidget *treeview;
};


/* This callback occurs when the text in the label entry box
   is changed */
static void
on_label_entry_change (GtkEntry *entry, gpointer data)
{
  union value v;
  const gchar *text ;
  struct val_labs_dialog *dialog = data;
  g_assert (dialog->labs);

  text = gtk_entry_get_text (GTK_ENTRY (dialog->value_entry));

  text_to_value (text,
		 dialog->var_store->dict,
		 dialog->pv,
		 &v);

  if (val_labs_find (dialog->labs, &v))
    {
      gtk_widget_set_sensitive (dialog->change_button, TRUE);
      gtk_widget_set_sensitive (dialog->add_button, FALSE);
    }
  else
    {
      gtk_widget_set_sensitive (dialog->change_button, FALSE);
      gtk_widget_set_sensitive (dialog->add_button, TRUE);
    }

  value_destroy (&v, var_get_width (dialog->pv));
}


/* Set the TREEVIEW list cursor to the item which has the value VAL */
static void
select_treeview_from_value (GtkTreeView *treeview, union value *val)
{
  GtkTreePath *path ;

  /*
    We do this with a linear search through the model --- hardly
    efficient, but the list is short ... */
  GtkTreeIter iter;

  GtkTreeModel * model  = gtk_tree_view_get_model (treeview);

  gboolean success;
  for (success = gtk_tree_model_get_iter_first (model, &iter);
       success;
       success = gtk_tree_model_iter_next (model, &iter))
    {
      union value v;
      GValue gvalue = {0};

      gtk_tree_model_get_value (model, &iter, 1, &gvalue);

      v.f = g_value_get_double (&gvalue);

      if ( 0 == memcmp (&v, val, sizeof (union value)))
	{
	  break;
	}
    }

  path = gtk_tree_model_get_path (model, &iter);
  if ( path )
    {
      gtk_tree_view_set_cursor (treeview, path, 0, 0);
      gtk_tree_path_free (path);
    }

}


/* This callback occurs when the text in the value entry box is
   changed */
static void
on_value_entry_change (GtkEntry *entry, gpointer data)
{
  const char *s;

  struct val_labs_dialog *dialog = data;

  const gchar *text = gtk_entry_get_text (GTK_ENTRY (dialog->value_entry));

  union value v;
  text_to_value (text,
		 dialog->var_store->dict,
		 dialog->pv,
		 &v);


  g_signal_handler_block (GTK_ENTRY (dialog->label_entry),
			 dialog->change_handler_id);

  gtk_entry_set_text (GTK_ENTRY (dialog->label_entry),"");


  if ( (s = val_labs_find (dialog->labs, &v)) )
    {
      gtk_entry_set_text (GTK_ENTRY (dialog->label_entry), s);
      gtk_widget_set_sensitive (dialog->add_button, FALSE);
      gtk_widget_set_sensitive (dialog->remove_button, TRUE);
      select_treeview_from_value (GTK_TREE_VIEW (dialog->treeview), &v);
    }
  else
    {
      gtk_widget_set_sensitive (dialog->remove_button, FALSE);
      gtk_widget_set_sensitive (dialog->add_button, TRUE);
    }

  g_signal_handler_unblock (GTK_ENTRY (dialog->label_entry),
			 dialog->change_handler_id);

  value_destroy (&v, var_get_width (dialog->pv));
}


/* Callback for when the Value Labels dialog is closed using
   the OK button.*/
static gint
val_labs_ok (GtkWidget *w, gpointer data)
{
  struct val_labs_dialog *dialog = data;

  var_set_value_labels (dialog->pv, dialog->labs);

  val_labs_destroy (dialog->labs);

  dialog->labs = 0;

  gtk_widget_hide (dialog->window);

  return FALSE;
}

/* Callback for when the Value Labels dialog is closed using
   the Cancel button.*/
static void
val_labs_cancel (struct val_labs_dialog *dialog)
{
  val_labs_destroy (dialog->labs);

  dialog->labs = 0;

  gtk_widget_hide (dialog->window);
}


/* Callback for when the Value Labels dialog is closed using
   the Cancel button.*/
static void
on_cancel (GtkWidget *w, gpointer data)
{
  struct val_labs_dialog *dialog = data;

  val_labs_cancel (dialog);
}


/* Callback for when the Value Labels dialog is closed using
   the window delete button.*/
static gint
on_delete (GtkWidget *w, GdkEvent *e, gpointer data)
{
  struct val_labs_dialog *dialog = data;

  val_labs_cancel (dialog);

  return TRUE;
}


/* Return the value-label pair currently selected in the dialog box  */
static void
get_selected_tuple (struct val_labs_dialog *dialog,
                    union value *valuep, const char **label)
{
  GtkTreeView *treeview = GTK_TREE_VIEW (dialog->treeview);

  GtkTreeIter iter ;
  GValue the_value = {0};
  union value value;

  GtkTreeSelection* sel =  gtk_tree_view_get_selection (treeview);

  GtkTreeModel * model  = gtk_tree_view_get_model (treeview);

  gtk_tree_selection_get_selected (sel, &model, &iter);

  gtk_tree_model_get_value (model, &iter, 1, &the_value);

  value.f = g_value_get_double (&the_value);
  g_value_unset (&the_value);

  if (valuep != NULL)
    *valuep = value;
  if (label != NULL)
    *label = val_labs_find (dialog->labs, &value);
}


static void repopulate_dialog (struct val_labs_dialog *dialog);

/* Callback which occurs when the "Change" button is clicked */
static void
on_change (GtkWidget *w, gpointer data)
{
  struct val_labs_dialog *dialog = data;

  const gchar *val_text = gtk_entry_get_text (GTK_ENTRY (dialog->value_entry));

  union value v;

  text_to_value (val_text,
		 dialog->var_store->dict,
		 dialog->pv,
		 &v);

  val_labs_replace (dialog->labs, &v,
		    gtk_entry_get_text (GTK_ENTRY (dialog->label_entry)));

  gtk_widget_set_sensitive (dialog->change_button, FALSE);

  repopulate_dialog (dialog);
  gtk_widget_grab_focus (dialog->value_entry);

  value_destroy (&v, var_get_width (dialog->pv));
}

/* Callback which occurs when the "Add" button is clicked */
static void
on_add (GtkWidget *w, gpointer data)
{
  struct val_labs_dialog *dialog = data;

  union value v;

  const gchar *text = gtk_entry_get_text (GTK_ENTRY (dialog->value_entry));

  text_to_value (text,
		 dialog->var_store->dict,
		 dialog->pv,
		 &v);

  if (val_labs_add (dialog->labs, &v,
		    gtk_entry_get_text
		    ( GTK_ENTRY (dialog->label_entry)) ) )
    {
      gtk_widget_set_sensitive (dialog->add_button, FALSE);

      repopulate_dialog (dialog);
      gtk_widget_grab_focus (dialog->value_entry);
    }

  value_destroy (&v, var_get_width (dialog->pv));
}

/* Callback which occurs when the "Remove" button is clicked */
static void
on_remove (GtkWidget *w, gpointer data)
{
  struct val_labs_dialog *dialog = data;

  union value value;
  const struct val_lab *vl;

  get_selected_tuple (dialog, &value, NULL);
  vl = val_labs_lookup (dialog->labs, &value);
  if (vl != NULL)
    val_labs_remove (dialog->labs, vl);

  repopulate_dialog (dialog);
  gtk_widget_grab_focus (dialog->value_entry);

  gtk_widget_set_sensitive (dialog->remove_button, FALSE);
}



/* Callback which occurs when a line item is selected in the list of
   value--label pairs.*/
static void
on_select_row (GtkTreeView *treeview, gpointer data)
{
  struct val_labs_dialog *dialog = data;

  union value value;
  const char *label = NULL;

  gchar *text;

  get_selected_tuple (dialog, &value, &label);
  text = value_to_text (value, dialog->var_store->dict, *var_get_write_format (dialog->pv));

  g_signal_handler_block (GTK_ENTRY (dialog->value_entry),
			 dialog->value_handler_id);

  gtk_entry_set_text (GTK_ENTRY (dialog->value_entry), text);

  g_signal_handler_unblock (GTK_ENTRY (dialog->value_entry),
			 dialog->value_handler_id);
  g_free (text);

  g_signal_handler_block (GTK_ENTRY (dialog->label_entry),
			 dialog->change_handler_id);


  gtk_entry_set_text (GTK_ENTRY (dialog->label_entry),
		      label);

  g_signal_handler_unblock (GTK_ENTRY (dialog->label_entry),
			 dialog->change_handler_id);

  gtk_widget_set_sensitive (dialog->remove_button, TRUE);
  gtk_widget_set_sensitive (dialog->change_button, FALSE);
}


/* Create a new dialog box
   (there should  normally be only one)*/
struct val_labs_dialog *
val_labs_dialog_create (GtkWindow *toplevel, PsppireVarStore *var_store)
{
  GtkTreeViewColumn *column;

  GtkCellRenderer *renderer ;

  GtkBuilder *xml = builder_new ("var-sheet-dialogs.ui");

  struct val_labs_dialog *dialog = g_malloc (sizeof (*dialog));

  dialog->var_store = var_store;
  dialog->window = get_widget_assert (xml,"val_labs_dialog");
  dialog->value_entry = get_widget_assert (xml,"value_entry");
  dialog->label_entry = get_widget_assert (xml,"label_entry");

  gtk_window_set_transient_for
    (GTK_WINDOW (dialog->window), toplevel);

  dialog->add_button = get_widget_assert (xml, "val_labs_add");
  dialog->remove_button = get_widget_assert (xml, "val_labs_remove");
  dialog->change_button = get_widget_assert (xml, "val_labs_change");

  dialog->treeview = get_widget_assert (xml,"treeview1");

  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (dialog->treeview), FALSE);

  renderer = gtk_cell_renderer_text_new ();

  column = gtk_tree_view_column_new_with_attributes ("Title",
						     renderer,
						     "text",
						     0,
						     NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (dialog->treeview), column);

  g_signal_connect (get_widget_assert (xml, "val_labs_cancel"),
		   "clicked",
		   G_CALLBACK (on_cancel), dialog);

  g_signal_connect (dialog->window, "delete-event",
		    G_CALLBACK (on_delete), dialog);

  g_signal_connect (get_widget_assert (xml, "val_labs_ok"),
		   "clicked",
		   G_CALLBACK (val_labs_ok), dialog);

  dialog->change_handler_id =
    g_signal_connect (dialog->label_entry,
		     "changed",
		     G_CALLBACK (on_label_entry_change), dialog);

  dialog->value_handler_id  =
    g_signal_connect (dialog->value_entry,
		     "changed",
		     G_CALLBACK (on_value_entry_change), dialog);

  g_signal_connect (dialog->change_button,
		   "clicked",
		   G_CALLBACK (on_change), dialog);


  g_signal_connect (dialog->treeview, "cursor-changed",
		   G_CALLBACK (on_select_row), dialog);

  g_signal_connect (dialog->remove_button, "clicked",
		   G_CALLBACK (on_remove), dialog);

  g_signal_connect (dialog->add_button, "clicked",
		   G_CALLBACK (on_add), dialog);

  dialog->labs = 0;

  g_object_unref (xml);

  return dialog;
}


void
val_labs_dialog_set_target_variable (struct val_labs_dialog *dialog,
				     struct variable *var)
{
  dialog->pv = var;
}



/* Populate the components of the dialog box, from the 'labs' member
   variable */
static void
repopulate_dialog (struct val_labs_dialog *dialog)
{
  const struct val_lab **labels;
  size_t n_labels;
  size_t i;

  GtkTreeIter iter;

  GtkListStore *list_store = gtk_list_store_new (2,
						 G_TYPE_STRING,
						 G_TYPE_DOUBLE);

  g_signal_handler_block (GTK_ENTRY (dialog->label_entry),
			 dialog->change_handler_id);
  g_signal_handler_block (GTK_ENTRY (dialog->value_entry),
			 dialog->value_handler_id);

  gtk_entry_set_text (GTK_ENTRY (dialog->value_entry), "");
  gtk_entry_set_text (GTK_ENTRY (dialog->label_entry), "");

  g_signal_handler_unblock (GTK_ENTRY (dialog->value_entry),
			 dialog->value_handler_id);
  g_signal_handler_unblock (GTK_ENTRY (dialog->label_entry),
			   dialog->change_handler_id);

  labels = val_labs_sorted (dialog->labs);
  n_labels = val_labs_count (dialog->labs);
  for (i = 0; i < n_labels; i++)
    {
      const struct val_lab *vl = labels[i];

      gchar *const vstr  =
	value_to_text (vl->value, dialog->var_store->dict,
		      *var_get_write_format (dialog->pv));

      gchar *const text = g_strdup_printf ("%s = \"%s\"",
					   vstr, val_lab_get_label (vl));

      gtk_list_store_append (list_store, &iter);
      gtk_list_store_set (list_store, &iter,
                          0, text,
			  1, vl->value.f,
			  -1);

      g_free (text);
      g_free (vstr);
    }
  free (labels);

  gtk_tree_view_set_model (GTK_TREE_VIEW (dialog->treeview),
			  GTK_TREE_MODEL (list_store));

  g_object_unref (list_store);

}

/* Initialise and display the dialog box */
void
val_labs_dialog_show (struct val_labs_dialog *dialog)
{
  const struct val_labs *value_labels;

  g_assert (!dialog->labs);

  value_labels = var_get_value_labels (dialog->pv);

  if (value_labels)
    dialog->labs = val_labs_clone ( value_labels );
  else
    dialog->labs = val_labs_create ( var_get_width (dialog->pv));

  gtk_widget_set_sensitive (dialog->remove_button, FALSE);
  gtk_widget_set_sensitive (dialog->change_button, FALSE);
  gtk_widget_set_sensitive (dialog->add_button, FALSE);

  gtk_widget_grab_focus (dialog->value_entry);

  repopulate_dialog (dialog);
  gtk_widget_show (dialog->window);
}

