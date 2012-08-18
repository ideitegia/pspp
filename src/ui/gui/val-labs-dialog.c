/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2005, 2009, 2010, 2011, 2012  Free Software Foundation

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

#include "ui/gui/val-labs-dialog.h"

#include <string.h>

#include "data/value-labels.h"
#include "data/format.h"
#include "libpspp/i18n.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/helper.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static GObject *psppire_val_labs_dialog_constructor (GType type, guint,
                                                     GObjectConstructParam *);
static void psppire_val_labs_dialog_finalize (GObject *);

G_DEFINE_TYPE (PsppireValLabsDialog,
               psppire_val_labs_dialog,
               PSPPIRE_TYPE_DIALOG);
enum
  {
    PROP_0,
    PROP_VARIABLE,
    PROP_VALUE_LABELS
  };

static void
psppire_val_labs_dialog_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  PsppireValLabsDialog *obj = PSPPIRE_VAL_LABS_DIALOG (object);

  switch (prop_id)
    {
    case PROP_VARIABLE:
      psppire_val_labs_dialog_set_variable (obj, g_value_get_pointer (value));
      break;
    case PROP_VALUE_LABELS:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_val_labs_dialog_get_property (GObject      *object,
                                      guint         prop_id,
                                      GValue       *value,
                                      GParamSpec   *pspec)
{
  PsppireValLabsDialog *obj = PSPPIRE_VAL_LABS_DIALOG (object);

  switch (prop_id)
    {
    case PROP_VALUE_LABELS:
      g_value_set_pointer (value, obj->labs);
      break;
    case PROP_VARIABLE:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_val_labs_dialog_class_init (PsppireValLabsDialogClass *class)
{
  GObjectClass *gobject_class;
  gobject_class = G_OBJECT_CLASS (class);

  gobject_class->constructor = psppire_val_labs_dialog_constructor;
  gobject_class->finalize = psppire_val_labs_dialog_finalize;
  gobject_class->set_property = psppire_val_labs_dialog_set_property;
  gobject_class->get_property = psppire_val_labs_dialog_get_property;

  g_object_class_install_property (
    gobject_class, PROP_VARIABLE,
    g_param_spec_pointer ("variable",
                          "Variable",
                          "Variable whose value labels are to be edited.  The "
                          "variable's print format and encoding are also used "
                          "for editing.",
                          G_PARAM_WRITABLE));

  g_object_class_install_property (
    gobject_class, PROP_VALUE_LABELS,
    g_param_spec_pointer ("value-labels",
                          "Value Labels",
                          "Edited value labels.",
                          G_PARAM_READABLE));
}

static void
psppire_val_labs_dialog_init (PsppireValLabsDialog *obj)
{
  /* We do all of our work on widgets in the constructor function, because that
     runs after the construction properties have been set.  Otherwise
     PsppireDialog's "orientation" property hasn't been set and therefore we
     have no box to populate. */
  obj->labs = val_labs_create (0);
}

static void
psppire_val_labs_dialog_finalize (GObject *obj)
{
  PsppireValLabsDialog *dialog = PSPPIRE_VAL_LABS_DIALOG (obj);

  val_labs_destroy (dialog->labs);
  g_free (dialog->encoding);

  G_OBJECT_CLASS (psppire_val_labs_dialog_parent_class)->finalize (obj);
}

PsppireValLabsDialog *
psppire_val_labs_dialog_new (const struct variable *var)
{
  return PSPPIRE_VAL_LABS_DIALOG (
    g_object_new (PSPPIRE_TYPE_VAL_LABS_DIALOG,
                  "orientation", PSPPIRE_HORIZONTAL,
                  "variable", var,
                  NULL));
}

struct val_labs *
psppire_val_labs_dialog_run (GtkWindow *parent_window,
                             const struct variable *var)
{
  PsppireValLabsDialog *dialog;
  struct val_labs *labs;

  dialog = psppire_val_labs_dialog_new (var);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), parent_window);
  gtk_widget_show (GTK_WIDGET (dialog));

  labs = (psppire_dialog_run (PSPPIRE_DIALOG (dialog)) == GTK_RESPONSE_OK
          ? val_labs_clone (psppire_val_labs_dialog_get_value_labels (dialog))
          : NULL);

  gtk_widget_destroy (GTK_WIDGET (dialog));

  return labs;
}

/* This callback occurs when the text in the label entry box
   is changed */
static void
on_label_entry_change (GtkEntry *entry, gpointer data)
{
  union value v;
  const gchar *text ;
  PsppireValLabsDialog *dialog = data;
  g_assert (dialog->labs);

  text = gtk_entry_get_text (GTK_ENTRY (dialog->value_entry));

  text_to_value__ (text, &dialog->format, dialog->encoding, &v);

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

  value_destroy (&v, val_labs_get_width (dialog->labs));
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

  PsppireValLabsDialog *dialog = data;

  const gchar *text = gtk_entry_get_text (GTK_ENTRY (dialog->value_entry));

  union value v;
  text_to_value__ (text, &dialog->format, dialog->encoding, &v);

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

  value_destroy (&v, val_labs_get_width (dialog->labs));
}


/* Return the value-label pair currently selected in the dialog box  */
static void
get_selected_tuple (PsppireValLabsDialog *dialog,
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
    {
      struct val_lab *vl = val_labs_lookup (dialog->labs, &value);
      if (vl != NULL)
        *label = val_lab_get_escaped_label (vl);
    }
}


static void repopulate_dialog (PsppireValLabsDialog *dialog);

/* Callback which occurs when the "Change" button is clicked */
static void
on_change (GtkWidget *w, gpointer data)
{
  PsppireValLabsDialog *dialog = data;

  const gchar *val_text = gtk_entry_get_text (GTK_ENTRY (dialog->value_entry));

  union value v;

  text_to_value__ (val_text, &dialog->format, dialog->encoding, &v);

  val_labs_replace (dialog->labs, &v,
		    gtk_entry_get_text (GTK_ENTRY (dialog->label_entry)));

  gtk_widget_set_sensitive (dialog->change_button, FALSE);

  repopulate_dialog (dialog);
  gtk_widget_grab_focus (dialog->value_entry);

  value_destroy (&v, val_labs_get_width (dialog->labs));
}

/* Callback which occurs when the "Add" button is clicked */
static void
on_add (GtkWidget *w, gpointer data)
{
  PsppireValLabsDialog *dialog = data;

  union value v;

  const gchar *text = gtk_entry_get_text (GTK_ENTRY (dialog->value_entry));

  text_to_value__ (text, &dialog->format, dialog->encoding, &v);

  if (val_labs_add (dialog->labs, &v,
		    gtk_entry_get_text
		    ( GTK_ENTRY (dialog->label_entry)) ) )
    {
      gtk_widget_set_sensitive (dialog->add_button, FALSE);

      repopulate_dialog (dialog);
      gtk_widget_grab_focus (dialog->value_entry);
    }

  value_destroy (&v, val_labs_get_width (dialog->labs));
}

/* Callback which occurs when the "Remove" button is clicked */
static void
on_remove (GtkWidget *w, gpointer data)
{
  PsppireValLabsDialog *dialog = data;

  union value value;
  struct val_lab *vl;

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
  PsppireValLabsDialog *dialog = data;

  union value value;
  const char *label = NULL;

  gchar *text;

  get_selected_tuple (dialog, &value, &label);
  text = value_to_text__ (value, &dialog->format, dialog->encoding);

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
static GObject *
psppire_val_labs_dialog_constructor (GType                  type,
                                     guint                  n_properties,
                                     GObjectConstructParam *properties)
{
  PsppireValLabsDialog *dialog;
  GtkTreeViewColumn *column;

  GtkCellRenderer *renderer ;

  GtkBuilder *xml = builder_new ("val-labs-dialog.ui");

  GtkContainer *content_area;
  GObject *obj;

  obj = G_OBJECT_CLASS (psppire_val_labs_dialog_parent_class)->constructor (
    type, n_properties, properties);
  dialog = PSPPIRE_VAL_LABS_DIALOG (obj);

  content_area = GTK_CONTAINER (PSPPIRE_DIALOG (dialog)->box);
  gtk_container_add (GTK_CONTAINER (content_area),
                     get_widget_assert (xml, "val-labs-dialog"));

  dialog->value_entry = get_widget_assert (xml,"value_entry");
  dialog->label_entry = get_widget_assert (xml,"label_entry");

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

  dialog->labs = NULL;

  g_object_unref (xml);

  return obj;
}


/* Populate the components of the dialog box, from the 'labs' member
   variable */
static void
repopulate_dialog (PsppireValLabsDialog *dialog)
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
        value_to_text__ (vl->value, &dialog->format, dialog->encoding);

      gchar *const text = g_strdup_printf (_("%s = `%s'"), vstr,
                                           val_lab_get_escaped_label (vl));

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

void
psppire_val_labs_dialog_set_variable (PsppireValLabsDialog *dialog,
                                      const struct variable *var)
{
  val_labs_destroy (dialog->labs);
  dialog->labs = NULL;

  g_free (dialog->encoding);
  dialog->encoding = NULL;

  if (var != NULL)
    {
      dialog->labs = val_labs_clone (var_get_value_labels (var));
      dialog->encoding = g_strdup (var_get_encoding (var));
      dialog->format = *var_get_print_format (var);
    }
  else
    dialog->format = F_8_0;

  if (dialog->labs == NULL)
    dialog->labs = val_labs_create (var_get_width (var));

  repopulate_dialog (dialog);
}

const struct val_labs *
psppire_val_labs_dialog_get_value_labels (const PsppireValLabsDialog *dialog)
{
  return dialog->labs;
}
