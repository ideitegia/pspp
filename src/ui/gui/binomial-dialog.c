/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2010, 2011, 2012  Free Software Foundation

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

#include "binomial-dialog.h"

#include "psppire-dialog.h"
#include "psppire-var-view.h"
#include "psppire-acr.h"
#include "dialog-common.h"

#include "builder-wrapper.h"
#include "executor.h"
#include "helper.h"

#include <gtk/gtk.h>

struct binomial_dialog
{
  PsppireDict *dict;
  GtkWidget *var_view;

  GtkWidget *button1;

  GtkWidget *prop_entry;

  GtkWidget *cutpoint_button;
  GtkWidget *cutpoint_entry;
};

static void
set_sensitivity (GtkToggleButton *button, GtkWidget *w)
{
  gboolean state = gtk_toggle_button_get_active (button);
  gtk_widget_set_sensitive (w, state);
}


static gboolean
get_proportion (const struct binomial_dialog *bin_d, double *prop)
{
    const gchar *text = gtk_entry_get_text (GTK_ENTRY (bin_d->prop_entry));
    gchar *endptr = NULL;
     *prop = g_strtod (text, &endptr);

    if (endptr == text)
      return FALSE;

    return TRUE; 
}

static gboolean
dialog_state_valid (gpointer data)
{
  double prop;
  struct binomial_dialog *bin_d = data;

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (bin_d->var_view));

  GtkTreeIter notused;

  if ( !gtk_tree_model_get_iter_first (vars, &notused) )
    return FALSE;

  if ( ! get_proportion (bin_d, &prop))
    return FALSE;

  if (prop < 0 || prop > 1.0)
    return FALSE;

  return TRUE;
}


static void
refresh (struct binomial_dialog *bin_d)
{
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (bin_d->var_view));

  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bin_d->button1), TRUE);

  gtk_entry_set_text (GTK_ENTRY (bin_d->prop_entry), "0.5");

  gtk_entry_set_text (GTK_ENTRY (bin_d->cutpoint_entry), "");
}



static char *
generate_syntax (const struct binomial_dialog *scd)
{
  gchar *text;
  double prop;
  GString *string;

  string = g_string_new ("NPAR TEST\n\t/BINOMIAL");

  if ( get_proportion (scd, &prop))
    g_string_append_printf (string, "(%g)", prop);

  g_string_append (string, " =");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (scd->var_view), 0, string);

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scd->cutpoint_button)))
    {
      const gchar *cutpoint = gtk_entry_get_text (GTK_ENTRY (scd->cutpoint_entry));
      g_string_append_printf (string, "(%s)", cutpoint);
    }

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}



/* Pops up the Chi-Square dialog box */
void
binomial_dialog (PsppireDataWindow *dw)
{
  gint response;

  struct binomial_dialog bin_d;

  GtkBuilder *xml = builder_new ("binomial.ui");
  PsppireVarStore *vs;

  GtkWidget *dialog = get_widget_assert   (xml, "binomial-dialog");



  GtkWidget *dict_view = get_widget_assert   (xml, "dict-view");

  g_object_get (dw->data_editor, "var-store", &vs, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (dw));

  bin_d.var_view  = get_widget_assert (xml, "variables-treeview");
  bin_d.button1   = get_widget_assert (xml, "radiobutton3");
  bin_d.prop_entry = get_widget_assert (xml, "proportion-entry");

  bin_d.cutpoint_entry =     get_widget_assert   (xml, "cutpoint-entry");
  bin_d.cutpoint_button =    get_widget_assert   (xml, "radiobutton4");

  g_object_get (vs, "dictionary", &bin_d.dict, NULL);
  g_object_set (dict_view,
		"model", bin_d.dict, 
		"predicate", var_is_numeric,
		NULL);

  g_signal_connect (bin_d.cutpoint_button, "toggled", G_CALLBACK (set_sensitivity),
		    bin_d.cutpoint_entry);

  g_signal_connect_swapped (dialog, "refresh", G_CALLBACK (refresh),  &bin_d);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &bin_d);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (dw, generate_syntax (&bin_d)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (&bin_d)));
      break;
    default:
      break;
    }

  g_object_unref (xml);
}
