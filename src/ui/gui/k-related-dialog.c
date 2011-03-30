/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2010, 2011  Free Software Foundation

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

#include "k-related-dialog.h"

#include "psppire-dialog.h"
#include "psppire-var-view.h"
#include "psppire-acr.h"
#include "dialog-common.h"

#include "helper.h"
#include "executor.h"


#include <gtk/gtk.h>

struct k_related_dialog
{
  PsppireDict *dict;
  GtkWidget *var_view;

  GtkWidget *friedman;
  GtkWidget *kendal;
  GtkWidget *cochran;
};

static gboolean
dialog_state_valid (gpointer data)
{
  struct k_related_dialog *krd = data;

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (krd->var_view));

  /* Tests using less than 3 variables are not useful */
  if (gtk_tree_model_iter_n_children (vars, NULL) < 3)
    return FALSE;

  /* At least one checkbutton must be active */
  if ( 
      ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (krd->friedman))
      && 
      ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (krd->kendal))
      && 
      ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (krd->cochran))
       )
    return FALSE;

  return TRUE;
}


static void
refresh (struct k_related_dialog *krd)
{
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (krd->var_view));

  gtk_list_store_clear (GTK_LIST_STORE (liststore));


  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (krd->friedman), TRUE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (krd->kendal), FALSE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (krd->cochran), FALSE);
}


static char *
generate_syntax (const struct k_related_dialog *krd)
{
  gchar *text;
  GString *string;

  string = g_string_new ("NPAR TEST");

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (krd->friedman)))
    {
      g_string_append (string, "\n\t/FRIEDMAN = ");
      psppire_var_view_append_names (PSPPIRE_VAR_VIEW (krd->var_view), 0, string);
    }

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (krd->kendal)))
    {
      g_string_append (string, "\n\t/KENDALL = ");
      psppire_var_view_append_names (PSPPIRE_VAR_VIEW (krd->var_view), 0, string);
    }

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (krd->cochran)))
    {
      g_string_append (string, "\n\t/COCHRAN = ");
      psppire_var_view_append_names (PSPPIRE_VAR_VIEW (krd->var_view), 0, string);
    }

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}



/* Pops up the K-Related dialog box */
void
k_related_dialog (PsppireDataWindow *dw)
{
  gint response;

  struct k_related_dialog krd;

  GtkBuilder *xml = builder_new ("k-related.ui");
  PsppireVarStore *vs;

  GtkWidget *dialog = get_widget_assert   (xml, "k-related-dialog");

  GtkWidget *dict_view = get_widget_assert   (xml, "dict-view");

  g_object_get (dw->data_editor, "var-store", &vs, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (dw));

  krd.var_view  = get_widget_assert (xml, "variables-treeview");

  krd.friedman =  get_widget_assert (xml, "friedman-checkbutton");
  krd.kendal =  get_widget_assert (xml, "kendal-checkbutton");
  krd.cochran =  get_widget_assert (xml, "cochran-checkbutton");

  g_object_get (vs, "dictionary", &krd.dict, NULL);
  g_object_set (dict_view,
		"model", krd.dict, 
		"predicate", var_is_numeric,
		NULL);


  g_signal_connect_swapped (dialog, "refresh", G_CALLBACK (refresh),  &krd);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &krd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (dw, generate_syntax (&krd)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (&krd)));
      break;
    default:
      break;
    }

  g_object_unref (xml);
}
