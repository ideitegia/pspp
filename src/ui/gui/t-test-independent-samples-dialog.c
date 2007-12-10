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
#include <glade/glade.h>
#include <gtk/gtk.h>
#include "t-test-independent-samples-dialog.h"
#include "psppire-dict.h"
#include "psppire-var-store.h"
#include "helper.h"
#include <gtksheet/gtksheet.h>
#include "data-editor.h"
#include "psppire-dialog.h"
#include "dialog-common.h"
#include "dict-display.h"
#include "widget-io.h"
#include "t-test-options.h"

#include <language/syntax-string-source.h>
#include "syntax-editor.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


struct tt_indep_samples_dialog
{
  GladeXML *xml;  /* The xml that generated the widgets */
  GtkWidget *dialog;
  PsppireDict *dict;
  gboolean groups_defined;

  struct tt_options_dialog *opts;
};


static gchar *
generate_syntax (const struct tt_indep_samples_dialog *d)
{
  gchar *text;
  GtkWidget *entry =
    get_widget_assert (d->xml, "indep-samples-t-test-entry");

  GtkWidget *tv =
    get_widget_assert (d->xml, "indep-samples-t-test-treeview2");

  GString *str = g_string_new ("T-TEST /VARIABLES=");

  append_variable_names (str, d->dict, GTK_TREE_VIEW (tv));

  g_string_append (str, "\n\t/GROUPS=");

  g_string_append (str, gtk_entry_get_text (GTK_ENTRY (entry)));

  if ( d->groups_defined )
    {
      GtkWidget *entry1 = get_widget_assert (d->xml, "group1-entry");
      GtkWidget *entry2 = get_widget_assert (d->xml, "group2-entry");

      g_string_append (str, "(");
      g_string_append (str, gtk_entry_get_text (GTK_ENTRY (entry1)));
      g_string_append (str, ",");
      g_string_append (str, gtk_entry_get_text (GTK_ENTRY (entry2)));
      g_string_append (str, ")");
    }


  tt_options_dialog_append_syntax (d->opts, str);

  g_string_append (str, ".\n");

  text = str->str;

  g_string_free (str, FALSE);

  return text;
}



static void
refresh (GladeXML *xml)
{
  GtkWidget *entry =
    get_widget_assert (xml, "indep-samples-t-test-entry");

  GtkWidget *tv =
    get_widget_assert (xml, "indep-samples-t-test-treeview2");

  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (tv));

  gtk_entry_set_text (GTK_ENTRY (entry), "");

  gtk_list_store_clear (GTK_LIST_STORE (model));
}


static gboolean
define_groups_state_valid (gpointer data)
{
  struct tt_indep_samples_dialog *d = data;

  GtkWidget *entry1 = get_widget_assert (d->xml, "group1-entry");
  GtkWidget *entry2 = get_widget_assert (d->xml, "group2-entry");

  if ( 0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (entry1))))
    return FALSE;

  if ( 0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (entry2))))
    return FALSE;

  return TRUE;
}

static void
run_define_groups (struct tt_indep_samples_dialog *ttd)
{
  gint response;
  GtkWidget *dialog =
    get_widget_assert (ttd->xml, "define-groups-dialog");


  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      define_groups_state_valid, ttd);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (ttd->dialog));

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  ttd->groups_defined = (response == PSPPIRE_RESPONSE_CONTINUE);
}



static gboolean
dialog_state_valid (gpointer data)
{
  struct tt_indep_samples_dialog *tt_d = data;

  GtkWidget *entry =
    get_widget_assert (tt_d->xml, "indep-samples-t-test-entry");

  GtkWidget *tv_vars =
    get_widget_assert (tt_d->xml, "indep-samples-t-test-treeview2");

  GtkTreeModel *vars = gtk_tree_view_get_model (GTK_TREE_VIEW (tv_vars));

  GtkTreeIter notused;

  if ( 0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (entry))))
    return FALSE;


  if ( 0 == gtk_tree_model_get_iter_first (vars, &notused))
    return FALSE;

  return TRUE;
}


/* Pops up the dialog box */
void
t_test_independent_samples_dialog (GObject *o, gpointer data)
{
  struct tt_indep_samples_dialog tt_d;
  gint response;
  struct data_editor *de = data;

  PsppireVarStore *vs;

  GladeXML *xml = XML_NEW ("t-test.glade");

  GtkSheet *var_sheet =
    GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

  GtkWidget *dict_view =
    get_widget_assert (xml, "indep-samples-t-test-treeview1");

  GtkWidget *test_variables_treeview =
    get_widget_assert (xml, "indep-samples-t-test-treeview2");

  GtkWidget *selector2 =
    get_widget_assert (xml, "indep-samples-t-test-selector2");

  GtkWidget *selector1 =
    get_widget_assert (xml, "indep-samples-t-test-selector1");

  GtkWidget *entry =
    get_widget_assert (xml, "indep-samples-t-test-entry");

  GtkWidget *define_groups_button =
    get_widget_assert (xml, "define-groups-button");

  GtkWidget *options_button =
    get_widget_assert (xml, "options-button");

  vs = PSPPIRE_VAR_STORE (gtk_sheet_get_model (var_sheet));

  tt_d.dialog = get_widget_assert (xml, "t-test-independent-samples-dialog");
  tt_d.xml = xml;
  tt_d.dict = vs->dict;
  tt_d.groups_defined = FALSE;
  tt_d.opts = tt_options_dialog_create (xml, de->parent.window);

  gtk_window_set_transient_for (GTK_WINDOW (tt_d.dialog), de->parent.window);

  attach_dictionary_to_treeview (GTK_TREE_VIEW (dict_view),
				 vs->dict,
				 GTK_SELECTION_MULTIPLE, NULL);

  set_dest_model (GTK_TREE_VIEW (test_variables_treeview), vs->dict);


  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector1),
				 dict_view, test_variables_treeview,
				 insert_source_row_into_tree_view,
				 NULL);


  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector2),
				 dict_view, entry,
				 insert_source_row_into_entry,
				 is_currently_in_entry);

  g_signal_connect_swapped (define_groups_button, "clicked",
			    G_CALLBACK (run_define_groups), &tt_d);


  g_signal_connect_swapped (options_button, "clicked",
			    G_CALLBACK (tt_options_dialog_run), tt_d.opts);


  g_signal_connect_swapped (tt_d.dialog, "refresh", G_CALLBACK (refresh),  xml);


  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (tt_d.dialog),
				      dialog_state_valid, &tt_d);

  response = psppire_dialog_run (PSPPIRE_DIALOG (tt_d.dialog));

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

	struct syntax_editor *se =
	  (struct syntax_editor *) window_create (WINDOW_SYNTAX, NULL);

	gtk_text_buffer_insert_at_cursor (se->buffer, syntax, -1);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  tt_options_dialog_destroy (tt_d.opts);
  g_object_unref (xml);
}


