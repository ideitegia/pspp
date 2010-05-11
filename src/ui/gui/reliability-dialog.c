/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009  Free Software Foundation

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

#include "dialog-common.h"
#include <language/syntax-string-source.h>
#include "reliability-dialog.h"
#include "psppire-selector.h"
#include "psppire-dictview.h"
#include "psppire-dialog.h"

#include "psppire-data-window.h"
#include "psppire-var-view.h"

#include "executor.h"
#include "helper.h"

#include <gtk/gtk.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


struct reliability
{
  PsppireDict *dict;
  GtkWidget *model_combo;
  GtkWidget *variables;
  GtkWidget *split_point_hbox;
  GtkWidget *split_spinbutton;
};


static char * generate_syntax (const struct reliability *rd);


static void
on_vars_changed (struct reliability *rd)
{
  GtkTreeModel *tm =
    gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variables));

  gint n_vars = gtk_tree_model_iter_n_children (tm, NULL);

  gint current_value =
    gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (rd->split_spinbutton));

  gint new_value = current_value;

  gtk_spin_button_set_range (GTK_SPIN_BUTTON (rd->split_spinbutton),
			     0, n_vars - 1);

  if ( current_value > n_vars - 1)
    new_value = n_vars - 1;

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (rd->split_spinbutton),
			     new_value);
}

static void
on_method_change (struct reliability *rd)
{
  gtk_widget_set_sensitive (rd->split_point_hbox,
			    ( 1 == gtk_combo_box_get_active (GTK_COMBO_BOX (rd->model_combo))));

}

static void
refresh (PsppireDialog *dialog, struct reliability *rd)
{
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_combo_box_set_active (GTK_COMBO_BOX (rd->model_combo), 0);

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (rd->split_spinbutton), 0);

  gtk_spin_button_set_range (GTK_SPIN_BUTTON (rd->split_spinbutton),
			     0, 0);
}


static gboolean
dialog_state_valid (gpointer data)
{
  struct reliability *rd = data;

  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variables));

  return (2 <= gtk_tree_model_iter_n_children (liststore, NULL));
}


/* Pops up the Reliability dialog box */
void
reliability_dialog (PsppireDataWindow *de)
{
  struct reliability rd;
  gint response;

  GtkBuilder *xml = builder_new ("reliability.ui");
  PsppireVarStore *vs;

  GtkWidget *dialog = get_widget_assert   (xml, "reliability-dialog");
  GtkWidget *source = get_widget_assert   (xml, "dict-view");

  rd.split_point_hbox = get_widget_assert (xml, "split-point-hbox");

  rd.variables = get_widget_assert   (xml, "treeview2");

  rd.model_combo = get_widget_assert   (xml, "combobox1");
  rd.split_spinbutton = get_widget_assert (xml, "spinbutton1");

  g_signal_connect_swapped (rd.model_combo, "changed",
			    G_CALLBACK (on_method_change), &rd);

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  g_object_get (vs, "dictionary", &rd.dict, NULL);
  g_object_set (source, "model", rd.dict, NULL);

  {
    GtkTreeModel *tm =
      gtk_tree_view_get_model (GTK_TREE_VIEW (rd.variables));


    g_signal_connect_swapped (tm, "row-inserted",
		      G_CALLBACK (on_vars_changed), &rd);

    g_signal_connect_swapped (tm, "row-deleted",
		      G_CALLBACK (on_vars_changed), &rd);
  }

  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &rd);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &rd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (&rd);

	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (&rd);
        paste_syntax_in_new_window (syntax);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (xml);
}




static char *
generate_syntax (const struct reliability *rd)
{
  gchar *text;
  GString *string = g_string_new ("RELIABILITY");

  g_string_append (string, "\n\t/VARIABLES=");
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (rd->variables), 0, string);


  g_string_append (string, "\n\t/MODEL=");

  if ( 0 == gtk_combo_box_get_active (GTK_COMBO_BOX (rd->model_combo)))
    g_string_append (string, "ALPHA");
  else
    g_string_append_printf (string, "SPLIT (%d)",
			    gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (rd->split_spinbutton))
			    );

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}
