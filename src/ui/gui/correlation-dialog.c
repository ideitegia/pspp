/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009, 2010  Free Software Foundation

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
#include <ui/syntax-gen.h>
#include <libpspp/str.h>

#include "correlation-dialog.h"
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


struct correlation
{
  PsppireDict *dict;

  GtkWidget *variables ;

  GtkWidget *significant;
  GtkWidget *two_tailed;
};


static char * generate_syntax (const struct correlation *rd);


static void
refresh (struct correlation *rd)
{
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->significant), FALSE);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->two_tailed), TRUE);
}


static gboolean
dialog_state_valid (gpointer data)
{
  struct correlation *corr = data;

  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (corr->variables));

  if  (gtk_tree_model_iter_n_children (liststore, NULL) > 1)
    return TRUE;

  return FALSE;
}


/* Pops up the Correlation dialog box */
void
correlation_dialog (PsppireDataWindow *de)
{
  struct correlation rd;
  gint response;

  GtkBuilder *xml = builder_new ("correlation.ui");
  PsppireVarStore *vs;

  GtkWidget *dialog = get_widget_assert   (xml, "correlation-dialog");
  GtkWidget *source = get_widget_assert   (xml, "dict-view");

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  g_object_get (vs, "dictionary", &rd.dict, NULL);
  g_object_set (source,
		"model", rd.dict, 
		"predicate", var_is_numeric,
		NULL);

  rd.variables = get_widget_assert (xml, "psppire-var-view1");
  rd.significant = get_widget_assert (xml, "button-flag-significants");
  rd.two_tailed = get_widget_assert (xml, "button-two-tailed");

  g_signal_connect_swapped (dialog, "refresh", G_CALLBACK (refresh),  &rd);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &rd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (generate_syntax (&rd)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (&rd)));
      break;
    default:
      break;
    }

  g_object_unref (xml);
}




static char *
generate_syntax (const struct correlation *rd)
{
  gchar *text;
  GString *string = g_string_new ("CORRELATION");
  g_string_append (string, "\n\t/VARIABLES = ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (rd->variables), 0, string);


  g_string_append (string, "\n\t/PRINT =");

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->two_tailed)))
    g_string_append (string, " TWOTAIL");
  else
    g_string_append (string, " ONETAIL");


  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->significant)))
    g_string_append (string, " NOSIG");
  else
    g_string_append (string, " SIG");


  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}
