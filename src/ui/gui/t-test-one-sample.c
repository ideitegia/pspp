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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <config.h>
#include <gtk/gtk.h>
#include "t-test-one-sample.h"
#include "psppire-dict.h"
#include "psppire-var-store.h"
#include "helper.h"
#include "psppire-data-window.h"
#include "psppire-dialog.h"
#include "dialog-common.h"
#include "dict-display.h"
#include "widget-io.h"
#include "executor.h"

#include "t-test-options.h"
#include <language/syntax-string-source.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


struct tt_one_sample_dialog
{
  PsppireDict *dict;
  GtkWidget *vars_treeview;
  GtkWidget *test_value_entry;
  struct tt_options_dialog *opt;
};


static gchar *
generate_syntax (const struct tt_one_sample_dialog *d)
{
  gchar *text;

  GString *str = g_string_new ("T-TEST ");

  g_string_append_printf (str, "/TESTVAL=%s",
			  gtk_entry_get_text (GTK_ENTRY (d->test_value_entry)));

  g_string_append (str, "\n\t/VARIABLES=");

  append_variable_names (str, d->dict, GTK_TREE_VIEW (d->vars_treeview), 0);

  tt_options_dialog_append_syntax (d->opt, str);

  g_string_append (str, ".\n");

  text = str->str;

  g_string_free (str, FALSE);

  return text;
}



static void
refresh (const struct tt_one_sample_dialog *d)
{
  GtkTreeModel *model =
    gtk_tree_view_get_model (GTK_TREE_VIEW (d->vars_treeview));

  gtk_entry_set_text (GTK_ENTRY (d->test_value_entry), "");

  gtk_list_store_clear (GTK_LIST_STORE (model));
}



static gboolean
dialog_state_valid (gpointer data)
{
  gchar *s = NULL;
  const gchar *text;
  struct tt_one_sample_dialog *tt_d = data;

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (tt_d->vars_treeview));

  GtkTreeIter notused;

  text = gtk_entry_get_text (GTK_ENTRY (tt_d->test_value_entry));

  if ( 0 == strcmp ("", text))
    return FALSE;

  /* Check to see if the entry is numeric */
  g_strtod (text, &s);

  if (s - text != strlen (text))
    return FALSE;


  if ( 0 == gtk_tree_model_get_iter_first (vars, &notused))
    return FALSE;

  return TRUE;
}


/* Pops up the dialog box */
void
t_test_one_sample_dialog (GObject *o, gpointer data)
{
  struct tt_one_sample_dialog tt_d;
  gint response;
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);

  PsppireVarStore *vs = NULL;

  GtkBuilder *xml = builder_new ("t-test.ui");

  GtkWidget *dict_view =
    get_widget_assert (xml, "one-sample-t-test-treeview2");

  GtkWidget *options_button =
    get_widget_assert (xml, "button1");

  GtkWidget *selector = get_widget_assert (xml, "psppire-selector1");

  GtkWidget *dialog = get_widget_assert (xml, "t-test-one-sample-dialog");

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  g_object_get (vs, "dictionary", &tt_d.dict, NULL);
  tt_d.vars_treeview = get_widget_assert (xml, "one-sample-t-test-treeview1");
  tt_d.test_value_entry = get_widget_assert (xml, "test-value-entry");
  tt_d.opt = tt_options_dialog_create (xml, GTK_WINDOW (de));

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  g_object_set (dict_view, "model",
		tt_d.dict,
		"predicate",
		var_is_numeric, NULL);

  set_dest_model (GTK_TREE_VIEW (tt_d.vars_treeview), tt_d.dict);


  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector),
				 insert_source_row_into_tree_view,
				 NULL,
				 NULL);


  g_signal_connect_swapped (dialog, "refresh",
			    G_CALLBACK (refresh),  &tt_d);


  g_signal_connect_swapped (options_button, "clicked",
			    G_CALLBACK (tt_options_dialog_run), tt_d.opt);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &tt_d);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (&tt_d);

	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (&tt_d);

        paste_syntax_in_new_window (syntax);

	g_free (syntax);
      }
      break;
    default:
      break;
    }


  g_object_unref (xml);
  tt_options_dialog_destroy (tt_d.opt);
}


