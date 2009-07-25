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

#include "transpose-dialog.h"
#include "psppire-selector.h"
#include "psppire-dialog.h"
#include "executor.h"
#include "psppire-data-window.h"
#include "dict-display.h"
#include <language/syntax-string-source.h>
#include "helper.h"

#include "dialog-common.h"

#include <gtk/gtk.h>

#include <gettext.h>

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


/* FIXME: These shouldn't be here */
#include "psppire-var-store.h"


static gchar * generate_syntax (PsppireDict *dict, GtkBuilder *xml);

static void
refresh (PsppireDialog *dialog, gpointer data)
{
  GtkBuilder *xml = data;
  GtkWidget *dest = get_widget_assert (xml, "variables-treeview");
  GtkWidget *entry = get_widget_assert (xml, "new-name-entry");
  GtkTreeModel *dmodel = gtk_tree_view_get_model (GTK_TREE_VIEW (dest));

  gtk_list_store_clear (GTK_LIST_STORE (dmodel));
  gtk_entry_set_text (GTK_ENTRY (entry), "");
}

static gboolean
dialog_state_valid (gpointer data)
{
  GtkBuilder *xml = data;

  GtkWidget *tv = get_widget_assert (xml, "variables-treeview");
  GtkWidget *entry = get_widget_assert (xml, "new-name-entry");

  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (tv));

  gint n_rows = gtk_tree_model_iter_n_children  (model, NULL);

  if ( n_rows == 0 )
    return FALSE;

  if ( 0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (entry))))
    return FALSE;

  return TRUE;
}


void
transpose_dialog (GObject *o, gpointer data)
{
  gint response ;
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);
  PsppireDict *dict = NULL;

  GtkBuilder *xml = builder_new ("psppire.ui");

  PsppireVarStore *vs = NULL;

  GtkWidget *dialog = get_widget_assert (xml, "transpose-dialog");
  GtkWidget *source = get_widget_assert (xml, "source-treeview");
  GtkWidget *dest = get_widget_assert (xml, "variables-treeview");
  GtkWidget *selector1 = get_widget_assert (xml, "psppire-selector2");
  GtkWidget *selector2 = get_widget_assert (xml, "psppire-selector3");
  GtkWidget *new_name_entry = get_widget_assert (xml, "new-name-entry");

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  g_object_get (vs, "dictionary", &dict, NULL);
  g_object_set (source, "dictionary", dict, NULL);

  set_dest_model (GTK_TREE_VIEW (dest), dict);

  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector1),
				 source, dest,
				 insert_source_row_into_tree_view,
				 NULL,
				 NULL);


  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector2),
				 source, new_name_entry,
				 insert_source_row_into_entry,
				 is_currently_in_entry,
				 NULL);


  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  xml);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, xml);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (dict, xml);

	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (dict, xml);
        paste_syntax_in_new_window (syntax);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (xml);
}


  /*
     FLIP /VARIABLES=var_list /NEWNAMES=var_name.
  */
static gchar *
generate_syntax (PsppireDict *dict, GtkBuilder *xml)
{
  const gchar *text;
  GString *string = g_string_new ("FLIP");
  gchar *syntax ;

  GtkWidget *dest = get_widget_assert (xml, "variables-treeview");
  GtkWidget *entry = get_widget_assert (xml, "new-name-entry");

  g_string_append (string, " /VARIABLES = ");

  append_variable_names (string, dict, GTK_TREE_VIEW (dest), 0);

  text = gtk_entry_get_text (GTK_ENTRY (entry));

  if ( text)
    g_string_append_printf (string, " /NEWNAME = %s", text);

  g_string_append (string, ".");

  syntax = string->str;

  g_string_free (string, FALSE);

  return syntax;
}
