/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2007  Free Software Foundation

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA. */

#include <config.h>
#include <gtk/gtk.h>
#include "sort-cases-dialog.h"
#include "helper.h"
#include "psppire-dialog.h"
#include "data-editor.h"
#include <gtksheet/gtksheet.h>
#include "psppire-var-store.h"
#include "dialog-common.h"
#include "dict-display.h"

#include <language/syntax-string-source.h>
#include "syntax-editor.h"

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

static char *
generate_syntax (const struct sort_cases_dialog *scd)
{
  gchar *text;
  GString *string = g_string_new ("SORT CASES BY ");
  gint n_vars = append_variable_names (string,
				       scd->dict, GTK_TREE_VIEW (scd->tv));

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
  struct data_editor *de = data;

  struct sort_cases_dialog scd;

  GladeXML *xml = XML_NEW ("psppire.glade");

  GtkWidget *dialog = get_widget_assert   (xml, "sort-cases-dialog");


  GtkWidget *source = get_widget_assert   (xml, "sort-cases-treeview1");
  GtkWidget *selector = get_widget_assert (xml, "sort-cases-selector");
  GtkWidget *dest =   get_widget_assert   (xml, "sort-cases-treeview2");

  GtkSheet *var_sheet =
    GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

  PsppireVarStore *vs = PSPPIRE_VAR_STORE (gtk_sheet_get_model (var_sheet));

  gtk_window_set_transient_for (GTK_WINDOW (dialog), de->parent.window);

  attach_dictionary_to_treeview (GTK_TREE_VIEW (source),
				 vs->dict,
				 GTK_SELECTION_MULTIPLE, NULL);

  set_dest_model (GTK_TREE_VIEW (dest), vs->dict);

  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector),
				 source,
				 dest,
				 insert_source_row_into_tree_view,
				 NULL);

  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  dest);

  scd.tv = GTK_TREE_VIEW (dest);
  scd.dict = vs->dict;
  scd.ascending =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "sort-cases-radiobutton0"));

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

	struct syntax_editor *se =
	  (struct syntax_editor *) window_create (WINDOW_SYNTAX, NULL);

	gtk_text_buffer_insert_at_cursor (se->buffer, syntax, -1);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (xml);
}

