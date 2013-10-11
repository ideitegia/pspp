/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011, 2012, 2013  Free Software Foundation

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

#include "psppire-dialog-action-count.h"

#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "psppire-selector.h"
#include "builder-wrapper.h"
#include "psppire-acr.h"
#include "dialog-common.h"

#include <ui/syntax-gen.h>

#include "psppire-val-chooser.h"
#include "helper.h"


static void values_dialog (PsppireDialogActionCount *cd);


static void psppire_dialog_action_count_init            (PsppireDialogActionCount      *act);
static void psppire_dialog_action_count_class_init      (PsppireDialogActionCountClass *class);

G_DEFINE_TYPE (PsppireDialogActionCount, psppire_dialog_action_count, PSPPIRE_TYPE_DIALOG_ACTION);


static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionCount *cnt = PSPPIRE_DIALOG_ACTION_COUNT (act);
  gchar *text = NULL;
  const gchar *s = NULL;
  gboolean ok;
  GtkTreeIter iter;
  struct string dds;
  
  ds_init_empty (&dds);

  ds_put_cstr (&dds, "\nCOUNT ");

  ds_put_cstr (&dds, gtk_entry_get_text (GTK_ENTRY (cnt->target)));

  ds_put_cstr (&dds, " =");

  psppire_var_view_append_names_str (PSPPIRE_VAR_VIEW (cnt->variable_treeview), 0, &dds);

  ds_put_cstr (&dds, "(");
  for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (cnt->value_list),
					   &iter);
       ok;
       ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (cnt->value_list), &iter))
    {
      GValue a_value = {0};
      struct old_value *ov;

      gtk_tree_model_get_value (GTK_TREE_MODEL (cnt->value_list), &iter,
				0, &a_value);

      ov = g_value_get_boxed (&a_value);

      ds_put_cstr (&dds, " ");
      old_value_append_syntax (&dds, ov);
    }
  ds_put_cstr (&dds, ").");


  s = gtk_entry_get_text (GTK_ENTRY (cnt->label));
  if (0 != strcmp (s, ""))
  {
    ds_put_cstr (&dds, "\nVARIABLE LABELS ");

    ds_put_cstr (&dds, gtk_entry_get_text (GTK_ENTRY (cnt->target)));

    ds_put_cstr (&dds, " ");

    syntax_gen_string (&dds, ss_cstr (s));

    ds_put_cstr (&dds, ".");
  }

  ds_put_cstr (&dds, "\nEXECUTE.\n");

  text = ds_steal_cstr (&dds);

  ds_destroy (&dds);

  return text;
}


static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (data);
  PsppireDialogActionCount *cnt = PSPPIRE_DIALOG_ACTION_COUNT (pda);

  GtkTreeIter iter;

  if (! cnt->value_list)
    return FALSE;

  if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (cnt->value_list),	&iter) )
    return FALSE;

  if (!gtk_tree_model_get_iter_first (gtk_tree_view_get_model (GTK_TREE_VIEW (cnt->variable_treeview)), &iter))
    return FALSE;

  if (0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (cnt->target))))
    return FALSE;

  return TRUE;
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionCount *cnt = PSPPIRE_DIALOG_ACTION_COUNT (rd_);

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (cnt->variable_treeview));

  gtk_list_store_clear (GTK_LIST_STORE (vars));

  gtk_entry_set_text (GTK_ENTRY (cnt->target), "");
  gtk_entry_set_text (GTK_ENTRY (cnt->label), "");
  gtk_list_store_clear (GTK_LIST_STORE (cnt->value_list));
}

static void
psppire_dialog_action_count_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionCount *act = PSPPIRE_DIALOG_ACTION_COUNT (a);

  GtkBuilder *xml = builder_new ("count.ui");
  GtkWidget *selector = get_widget_assert (xml, "count-selector1");
  GtkWidget *button = get_widget_assert (xml, "button1");

  pda->dialog = get_widget_assert   (xml, "count-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");


  act->target = get_widget_assert (xml, "entry1");
  act->label = get_widget_assert (xml, "entry2");
  act->variable_treeview = get_widget_assert (xml, "treeview2");

  act->value_list = gtk_list_store_new (1, old_value_get_type ());

  psppire_selector_set_allow (PSPPIRE_SELECTOR (selector),  numeric_only);

  g_signal_connect_swapped (button, "clicked", G_CALLBACK (values_dialog), act);


  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

  g_object_unref (xml);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_count_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_count_parent_class)->activate (pda);
}

static void
psppire_dialog_action_count_class_init (PsppireDialogActionCountClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_count_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_count_init (PsppireDialogActionCount *act)
{
}






/* Callback which gets called when a new row is selected
   in the acr's variable treeview.
   We use if to set the togglebuttons and entries to correspond to the
   selected row.
*/
static void
on_acr_selection_change (GtkTreeSelection *selection, gpointer data)
{
  GtkTreeIter iter;
  struct old_value *ov = NULL;
  GtkTreeModel *model = NULL;
  PsppireDialogActionCount *cnt = PSPPIRE_DIALOG_ACTION_COUNT (data);
  GValue ov_value = {0};

  if ( ! gtk_tree_selection_get_selected (selection, &model, &iter) )
    return;

  gtk_tree_model_get_value (GTK_TREE_MODEL (model), &iter,
			    0, &ov_value);

  ov = g_value_get_boxed (&ov_value);
  psppire_val_chooser_set_status (PSPPIRE_VAL_CHOOSER (cnt->chooser), ov);
}


/* A function to set a value in a column in the ACR */
static gboolean
set_value (gint col, GValue  *val, gpointer data)
{
  PsppireDialogActionCount *cnt = PSPPIRE_DIALOG_ACTION_COUNT (data);
  PsppireValChooser *vc = PSPPIRE_VAL_CHOOSER (cnt->chooser);
  struct old_value ov;
	
  g_assert (col == 0);

  psppire_val_chooser_get_status (vc, &ov);

  g_value_init (val, old_value_get_type ());
  g_value_set_boxed (val, &ov);

  return TRUE;
}


static void
values_dialog (PsppireDialogActionCount *cd)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (cd);
  gint response;
  GtkListStore *local_store = clone_list_store (cd->value_list);
  GtkBuilder *builder = builder_new ("count.ui");

  GtkWidget *dialog = get_widget_assert (builder, "values-dialog");

  GtkWidget *acr = get_widget_assert (builder, "acr");
  cd->chooser = get_widget_assert (builder, "value-chooser");

  psppire_acr_set_enabled (PSPPIRE_ACR (acr), TRUE);

  psppire_acr_set_model (PSPPIRE_ACR (acr), local_store);
  psppire_acr_set_get_value_func (PSPPIRE_ACR (acr), set_value, cd);

  {
    GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (PSPPIRE_ACR(acr)->tv));
    g_signal_connect (sel, "changed",
		    G_CALLBACK (on_acr_selection_change), cd);
  }

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  if ( response == PSPPIRE_RESPONSE_CONTINUE )
    {
      g_object_unref (cd->value_list);
      cd->value_list = local_store;
    }
  else
    {
      g_object_unref (local_store);
    }

  psppire_dialog_notify_change (PSPPIRE_DIALOG (pda->dialog));

  g_object_unref (builder);
}


