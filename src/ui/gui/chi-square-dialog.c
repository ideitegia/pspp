/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2010  Free Software Foundation

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

#include "chi-square-dialog.h"

#include "psppire-dialog.h"
#include "psppire-var-view.h"
#include "psppire-acr.h"
#include "dialog-common.h"

#include "helper.h"
#include "executor.h"


#include <gtk/gtk.h>

struct chisquare_dialog
{
  PsppireDict *dict;
  GtkWidget *var_view;

  GtkWidget *button1;
  GtkWidget *button2;

  GtkWidget *range_button;
  GtkWidget *value_lower;
  GtkWidget *value_upper;

  GtkWidget *values_button;

  GtkListStore *expected_list;
};

static void
set_sensitivity (GtkToggleButton *button, GtkWidget *w)
{
  gboolean state = gtk_toggle_button_get_active (button);
  gtk_widget_set_sensitive (w, state);
}


static gboolean
dialog_state_valid (gpointer data)
{
  struct chisquare_dialog *csd = data;

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (csd->var_view));

  GtkTreeIter notused;

  if ( !gtk_tree_model_get_iter_first (vars, &notused) )
    return FALSE;

  return TRUE;
}


static void
refresh (struct chisquare_dialog *csd)
{
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (csd->var_view));

  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (csd->button1), TRUE);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (csd->button2), TRUE);
}



static char *
generate_syntax (const struct chisquare_dialog *scd)
{
  gchar *text;
  GString *string;


  string = g_string_new ("NPAR TEST\n\t/CHISQUARE=");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (scd->var_view), 0, string);


  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scd->range_button)))
    {
      g_string_append (string, "(");
      
      g_string_append (string, 
		       gtk_entry_get_text (GTK_ENTRY (scd->value_lower)));

      g_string_append (string, ", ");

      g_string_append (string,
		       gtk_entry_get_text (GTK_ENTRY (scd->value_upper)));

      g_string_append (string, ")");
    }




  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scd->values_button)))
    {
      GtkListStore *ls = scd->expected_list;
      GtkTreeIter iter;
      gboolean ok;

      g_string_append (string, "\n\t");
      g_string_append (string, "/EXPECTED = ");

      
      for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(ls),
					       &iter);
 	   ok;
	   ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (ls), &iter))
	{
	  gdouble v;

	  gtk_tree_model_get (GTK_TREE_MODEL (ls), &iter, 0, &v, -1);

	  g_string_append_printf (string, " %g", v);
	}



    }

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}



/* Pops up the Chi-Square dialog box */
void
chisquare_dialog (PsppireDataWindow *dw)
{
  gint response;

  struct chisquare_dialog csd;

  GtkBuilder *xml = builder_new ("chi-square.ui");
  PsppireVarStore *vs;

  GtkWidget *dialog = get_widget_assert   (xml, "chisquare-dialog");

  GtkWidget *range_table = get_widget_assert   (xml, "range-table");



  GtkWidget *values_acr = get_widget_assert   (xml, "psppire-acr1");
  GtkWidget *expected_value_entry =
    get_widget_assert   (xml, "expected-value-entry");


  GtkWidget *dict_view = get_widget_assert   (xml, "dict-view");

  csd.expected_list = gtk_list_store_new (1, G_TYPE_DOUBLE);

  csd.button1 = get_widget_assert   (xml, "radiobutton1");
  csd.button2 = get_widget_assert   (xml, "radiobutton3");
  csd.var_view = get_widget_assert   (xml, "variables-treeview");

  csd.range_button = get_widget_assert   (xml, "radiobutton4");
  csd.value_lower = get_widget_assert   (xml, "entry1");
  csd.value_upper = get_widget_assert   (xml, "entry2");

  csd.values_button = get_widget_assert   (xml, "radiobutton2");

  g_object_get (dw->data_editor, "var-store", &vs, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (dw));
 

  g_object_get (vs, "dictionary", &csd.dict, NULL);
  g_object_set (dict_view,
		"model", csd.dict, 
		"predicate", var_is_numeric,
		NULL);


  g_signal_connect (csd.range_button, "toggled", G_CALLBACK (set_sensitivity), 
		    range_table);


  g_signal_connect (csd.values_button, "toggled", G_CALLBACK (set_sensitivity), 
		    values_acr);

  g_signal_connect (csd.values_button, "toggled", G_CALLBACK (set_sensitivity), 
		    expected_value_entry);


  psppire_acr_set_entry (PSPPIRE_ACR (values_acr),
			 GTK_ENTRY (expected_value_entry));

  psppire_acr_set_model(PSPPIRE_ACR (values_acr), csd.expected_list);

  g_signal_connect_swapped (dialog, "refresh", G_CALLBACK (refresh),  &csd);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &csd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (generate_syntax (&csd)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (&csd)));
      break;
    default:
      break;
    }

  g_object_unref (xml);
}
