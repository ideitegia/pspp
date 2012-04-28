/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012  Free Software Foundation

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

#include "psppire-dialog-action-reliability.h"

#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

static void psppire_dialog_action_reliability_init            (PsppireDialogActionReliability      *act);
static void psppire_dialog_action_reliability_class_init      (PsppireDialogActionReliabilityClass *class);

G_DEFINE_TYPE (PsppireDialogActionReliability, psppire_dialog_action_reliability, PSPPIRE_TYPE_DIALOG_ACTION);

enum 
  {
    ALPHA = 0,
    SPLIT = 1
  };

static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionReliability *rd = PSPPIRE_DIALOG_ACTION_RELIABILITY (act);
  gchar *text;
  GString *string = g_string_new ("RELIABILITY");

  g_string_append (string, "\n\t/VARIABLES=");
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (rd->variables), 0, string);


  g_string_append (string, "\n\t/MODEL=");

  if ( ALPHA == gtk_combo_box_get_active (GTK_COMBO_BOX (rd->model_combo)))
    g_string_append (string, "ALPHA");
  else
    g_string_append_printf (string, "SPLIT (%d)",
			    gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (rd->split_spinbutton))
			    );

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->scale_if_item_deleted_checkbutton)))
    g_string_append (string, "\n\t/SUMMARY = TOTAL");

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}


static gboolean
dialog_state_valid (gpointer user_data)
{
  PsppireDialogActionReliability *pda = user_data;
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (pda->variables));

  return (2 <= gtk_tree_model_iter_n_children (liststore, NULL));
}

static void
update_split_control (PsppireDialogActionReliability *pda)
{
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (pda->variables));

  gint n_vars = gtk_tree_model_iter_n_children (liststore, NULL);

  gint sp = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (pda->split_spinbutton));

  if (sp >= n_vars)
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (pda->split_spinbutton), n_vars - 1);

  gtk_spin_button_set_range (GTK_SPIN_BUTTON (pda->split_spinbutton),
			     0, n_vars - 1);

  gtk_widget_set_sensitive (pda->split_point_hbox,
			    ( SPLIT == gtk_combo_box_get_active (GTK_COMBO_BOX (pda->model_combo))));
}


static void
refresh (PsppireDialogAction *pda_)
{
  PsppireDialogActionReliability *pda =
    PSPPIRE_DIALOG_ACTION_RELIABILITY (pda_);
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (pda->variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_combo_box_set_active (GTK_COMBO_BOX (pda->model_combo), ALPHA);

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (pda->split_spinbutton), 0);

  gtk_spin_button_set_range (GTK_SPIN_BUTTON (pda->split_spinbutton),
			     0, 0);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pda->scale_if_item_deleted_checkbutton),
				FALSE);
}

static void
psppire_dialog_action_reliability_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionReliability *act = PSPPIRE_DIALOG_ACTION_RELIABILITY (a);
  GtkTreeModel *liststore ;
  GtkBuilder *xml = builder_new ("reliability.ui");
  pda->dialog = get_widget_assert   (xml, "reliability-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");

  act->variables = get_widget_assert   (xml, "treeview2");

  act->split_point_hbox = get_widget_assert (xml, "split-point-hbox");

  act->variables = get_widget_assert   (xml, "treeview2");

  act->model_combo = get_widget_assert   (xml, "combobox1");
  act->split_spinbutton = get_widget_assert (xml, "spinbutton1");

  liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (act->variables));


  act->scale_if_item_deleted_checkbutton = get_widget_assert (xml, "totals-checkbutton");

  g_signal_connect_swapped (act->model_combo, "changed",
			    G_CALLBACK (update_split_control), pda);


  g_signal_connect_swapped (liststore, "row-inserted",
			    G_CALLBACK (update_split_control), pda);
  g_signal_connect_swapped (liststore, "row-deleted",
			    G_CALLBACK (update_split_control), pda);


  psppire_dialog_action_set_refresh (pda, refresh);
  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_reliability_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_reliability_parent_class)->activate (pda);

  g_object_unref (xml);
}

static void
psppire_dialog_action_reliability_class_init (PsppireDialogActionReliabilityClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);
  PsppireDialogActionClass *pdac = PSPPIRE_DIALOG_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_reliability_activate;

  pdac->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_reliability_init (PsppireDialogActionReliability *act)
{
}

