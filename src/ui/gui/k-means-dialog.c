/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011  Free Software Foundation

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

#include "k-means-dialog.h"
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




struct k_means
{
  GtkBuilder *xml;
  PsppireDict *dict;

  GtkWidget *variables;
  PsppireDataWindow *de ;

  GtkWidget *entry;
};

static char * generate_syntax (const struct k_means *rd);


static void
refresh (struct k_means *fd)
{
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (fd->variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_entry_set_text (GTK_ENTRY (fd->entry), "");
}


static gboolean
dialog_state_valid (gpointer data)
{
  struct k_means *fd = data;

  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (fd->variables));

  if  (gtk_tree_model_iter_n_children (liststore, NULL) < 2)
    return FALSE;

  if (atoi (gtk_entry_get_text (GTK_ENTRY (fd->entry))) < 2)
    return FALSE;

  return TRUE;
}


/* Pops up the K_Means dialog box */
void
k_means_dialog (PsppireDataWindow *dw)
{
  struct k_means fd;
  gint response;

  PsppireVarStore *vs;

  GtkWidget *dialog ;
  GtkWidget *source ;

  fd.xml = builder_new ("k-means.ui");

  dialog = get_widget_assert   (fd.xml, "k-means-dialog");
  source = get_widget_assert   (fd.xml, "dict-view");

  fd.entry = get_widget_assert   (fd.xml, "entry1");

  fd.de = dw;

  g_signal_connect_swapped (dialog, "refresh", G_CALLBACK (refresh),  &fd);


  fd.variables = get_widget_assert   (fd.xml, "psppire-var-view1");

  g_object_get (fd.de->data_editor, "var-store", &vs, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fd.de));

  g_object_get (vs, "dictionary", &fd.dict, NULL);
  g_object_set (source, "model", fd.dict,
		"predicate", var_is_numeric,
		NULL);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &fd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (dw, generate_syntax (&fd)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (&fd)));
      break;
    default:
      break;
    }

  g_object_unref (fd.xml);
}




static char *
generate_syntax (const struct k_means *km)
{
  gchar *text;

  GString *string = g_string_new ("QUICK CLUSTER ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (km->variables), 0, string);

  g_string_append_printf (string, "\n\t/CRITERIA=CLUSTERS(%d)",
			  atoi (gtk_entry_get_text (GTK_ENTRY (km->entry))));

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}
