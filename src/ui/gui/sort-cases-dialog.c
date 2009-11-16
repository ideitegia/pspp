/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007  Free Software Foundation

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
#include <gtk/gtk.h>
#include "sort-cases-dialog.h"
#include "executor.h"
#include "psppire-dialog.h"
#include "psppire-data-window.h"
#include "psppire-var-store.h"
#include "dialog-common.h"
#include "psppire-selector.h"
#include "dict-display.h"

#include <language/syntax-string-source.h>
#include "helper.h"

static void
refresh (PsppireDialog *dialog, GtkTreeView *dest)
{
  GtkTreeModel *liststore = gtk_tree_view_get_model (dest);


  gtk_list_store_clear (GTK_LIST_STORE (liststore));
}


struct sort_cases_dialog
{
  GtkTreeView *tv;
  PsppireDict *dict;
  GtkToggleButton *ascending;
};


static gboolean
dialog_state_valid (gpointer data)
{
  struct sort_cases_dialog *scd = data;
  GtkTreeModel *model = gtk_tree_view_get_model (scd->tv);

  gint n_rows = gtk_tree_model_iter_n_children  (model, NULL);

  if ( n_rows == 0 )
    return FALSE;

  return TRUE;
}

static char *
generate_syntax (const struct sort_cases_dialog *scd)
{
  gchar *text;
  GString *string = g_string_new ("SORT CASES BY ");
  gint n_vars = append_variable_names (string,
				       scd->dict, GTK_TREE_VIEW (scd->tv), 0);

  if ( n_vars == 0 )
    g_string_assign (string, "");
  else
    {
      const char up_down =
	gtk_toggle_button_get_active (scd->ascending) ? 'A' : 'D';
      g_string_append_printf (string, "(%c)", up_down);
      g_string_append (string, ".");
    }


  text = string->str;

  g_string_free (string, FALSE);

  return text;
}


/* Pops up the Sort Cases dialog box */
void
sort_cases_dialog (GObject *o, gpointer data)
{
  gint response;
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);

  struct sort_cases_dialog scd;

  GtkBuilder *xml = builder_new ("sort.ui");

  GtkWidget *dialog = get_widget_assert   (xml, "sort-cases-dialog");


  GtkWidget *source = get_widget_assert   (xml, "sort-cases-treeview1");
  GtkWidget *selector = get_widget_assert (xml, "sort-cases-selector");
  GtkWidget *dest =   get_widget_assert   (xml, "sort-cases-treeview2");

  PsppireVarStore *vs = NULL;

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  g_object_get (vs, "dictionary", &scd.dict, NULL);
  g_object_set (source, "model", scd.dict, NULL);

  set_dest_model (GTK_TREE_VIEW (dest), scd.dict);

  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector),
				 insert_source_row_into_tree_view,
				 NULL,
				 NULL);

  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  dest);

  scd.tv = GTK_TREE_VIEW (dest);
  scd.ascending =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "sort-cases-radiobutton0"));


  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &scd);


  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (&scd);

	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (&scd);
        paste_syntax_in_new_window (syntax);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (xml);
}

