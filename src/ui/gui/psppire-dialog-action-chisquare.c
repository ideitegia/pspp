/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2010, 2011, 2012, 2013, 2014  Free Software Foundation

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

#include "psppire-dialog-action-chisquare.h"

#include <float.h>

#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"
#include "psppire-acr.h"
#include "dialog-common.h"

#include <libpspp/str.h>

static void psppire_dialog_action_chisquare_init            (PsppireDialogActionChisquare      *act);
static void psppire_dialog_action_chisquare_class_init      (PsppireDialogActionChisquareClass *class);

G_DEFINE_TYPE (PsppireDialogActionChisquare, psppire_dialog_action_chisquare, PSPPIRE_TYPE_DIALOG_ACTION);


static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionChisquare *scd = PSPPIRE_DIALOG_ACTION_CHISQUARE (act);

  gchar *text;
  struct string dss;

  ds_init_cstr (&dss, "NPAR TEST\n\t/CHISQUARE=");

  psppire_var_view_append_names_str (PSPPIRE_VAR_VIEW (scd->var_view), 0, &dss);

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scd->range_button)))
    {
      ds_put_cstr (&dss, "(");
      
      ds_put_cstr (&dss, 
		       gtk_entry_get_text (GTK_ENTRY (scd->value_lower)));

      ds_put_cstr (&dss, ", ");

      ds_put_cstr (&dss,
		       gtk_entry_get_text (GTK_ENTRY (scd->value_upper)));

      ds_put_cstr (&dss, ")");
    }

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scd->values_button)))
    {
      GtkListStore *ls = scd->expected_list;
      GtkTreeIter iter;
      gboolean ok;

      ds_put_cstr (&dss, "\n\t");
      ds_put_cstr (&dss, "/EXPECTED = ");

      
      for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(ls),
					       &iter);
 	   ok;
	   ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (ls), &iter))
	{
	  gdouble v;

	  gtk_tree_model_get (GTK_TREE_MODEL (ls), &iter, 0, &v, -1);

	  ds_put_c_format (&dss, " %.*g", DBL_DIG + 1, v);
	}
    }

  ds_put_cstr (&dss, ".\n");

  text = ds_steal_cstr (&dss);

  ds_destroy (&dss);

  return text;
}


static gboolean
dialog_state_valid (gpointer a)
{
  GtkTreeIter notused;
  PsppireDialogActionChisquare *act = PSPPIRE_DIALOG_ACTION_CHISQUARE (a);

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (act->var_view));

  if ( !gtk_tree_model_get_iter_first (vars, &notused) )
    return FALSE;

  return TRUE;
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionChisquare *csd = PSPPIRE_DIALOG_ACTION_CHISQUARE (rd_);

  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (csd->var_view));

  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (csd->button1), TRUE);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (csd->button2), TRUE);
  gtk_entry_set_text (GTK_ENTRY (csd->value_lower), "");
  gtk_entry_set_text (GTK_ENTRY (csd->value_upper), "");
}

static void
psppire_dialog_action_chisquare_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionChisquare *act = PSPPIRE_DIALOG_ACTION_CHISQUARE (a);

  GtkBuilder *xml = builder_new ("chi-square.ui");

  GtkWidget *range_table = get_widget_assert   (xml, "range-table");
  GtkWidget *values_acr = get_widget_assert   (xml, "psppire-acr1");
  GtkWidget *expected_value_entry =
    get_widget_assert   (xml, "expected-value-entry");

  pda->dialog = get_widget_assert   (xml, "chisquare-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");

  act->expected_list = gtk_list_store_new (1, G_TYPE_DOUBLE);

  act->button1 = get_widget_assert   (xml, "radiobutton1");
  act->button2 = get_widget_assert   (xml, "radiobutton3");
  act->var_view = get_widget_assert   (xml, "variables-treeview");

  act->range_button = get_widget_assert   (xml, "radiobutton4");
  act->value_lower = get_widget_assert   (xml, "entry1");
  act->value_upper = get_widget_assert   (xml, "entry2");

  act->values_button = get_widget_assert   (xml, "radiobutton2");

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

  g_object_unref (xml);

  g_signal_connect (act->range_button, "toggled", 
		    G_CALLBACK (set_sensitivity_from_toggle), 
		    range_table);


  g_signal_connect (act->values_button, "toggled", 
		    G_CALLBACK (set_sensitivity_from_toggle), 
		    values_acr);

  g_signal_connect (act->values_button, "toggled", 
		    G_CALLBACK (set_sensitivity_from_toggle), 
		    expected_value_entry);

  psppire_acr_set_entry (PSPPIRE_ACR (values_acr),
			 GTK_ENTRY (expected_value_entry));

  psppire_acr_set_model(PSPPIRE_ACR (values_acr), act->expected_list);


  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_chisquare_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_chisquare_parent_class)->activate (pda);
}

static void
psppire_dialog_action_chisquare_class_init (PsppireDialogActionChisquareClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_chisquare_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_chisquare_init (PsppireDialogActionChisquare *act)
{
}

