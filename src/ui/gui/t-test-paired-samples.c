/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008  Free Software Foundation

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
#include <glade/glade.h>
#include <language/syntax-string-source.h>

#include "psppire-data-window.h"

#include "psppire-dict.h"
#include "psppire-var-store.h"
#include "t-test-paired-samples.h"
#include "t-test-options.h"

#include "dict-display.h"
#include "dialog-common.h"
#include "psppire-dialog.h"

#include "psppire-syntax-window.h"

#include "helper.h"

#include "psppire-var-ptr.h"


#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


struct tt_paired_samples_dialog
{
  PsppireDict *dict;
  GtkWidget *pairs_treeview;
  GtkTreeModel *list_store;
  struct tt_options_dialog *opt;
};

static gchar *
generate_syntax (const struct tt_paired_samples_dialog *d)
{
  gchar *text = NULL;
  GString *str =   g_string_new ("T-TEST \n\tPAIRS = ");

  append_variable_names (str, d->dict, GTK_TREE_VIEW (d->pairs_treeview), 0);

  g_string_append (str, " WITH ");

  append_variable_names (str, d->dict, GTK_TREE_VIEW (d->pairs_treeview), 1);

  g_string_append (str, " (PAIRED)");
  g_string_append (str, "\n");

  tt_options_dialog_append_syntax (d->opt, str);

  g_string_append (str, ".\n");

  text = str->str;
  g_string_free (str, FALSE);

  return text;
}

static void
refresh (struct tt_paired_samples_dialog *tt_d)
{
  gtk_list_store_clear (GTK_LIST_STORE (tt_d->list_store));
}

static gboolean
dialog_state_valid (gpointer data)
{
  struct variable *v = NULL;
  struct tt_paired_samples_dialog *tt_d = data;
  GtkTreeIter dest_iter;

  gint n_rows = gtk_tree_model_iter_n_children  (tt_d->list_store, NULL);

  if ( n_rows == 0 )
    return FALSE;

  /* Get the last row */
  gtk_tree_model_iter_nth_child (tt_d->list_store, &dest_iter,
				 NULL, n_rows - 1);

  /* Get the last (2nd) column */
  gtk_tree_model_get (tt_d->list_store, &dest_iter, 1, &v, -1);


  return (v != NULL);
}



static void
select_as_pair_member (GtkTreeIter source_iter,
		       GtkWidget *dest,
		       GtkTreeModel *source_model,
		       gpointer data)
{
  struct variable *v;
  struct variable *v1;
  gint n_rows;
  GtkTreeIter dest_iter;
  struct tt_paired_samples_dialog *tt_d = data;


  gtk_tree_model_get (source_model, &source_iter,
		      DICT_TVM_COL_VAR, &v, -1);

  n_rows = gtk_tree_model_iter_n_children  (tt_d->list_store, NULL);

  if ( n_rows > 0 )
    {

      gtk_tree_model_iter_nth_child (tt_d->list_store,
				     &dest_iter, NULL, n_rows - 1);

      gtk_tree_model_get (tt_d->list_store, &dest_iter, 1, &v1, -1);
    }

  if ( n_rows == 0 || v1 != NULL)
    {
      gtk_list_store_append (GTK_LIST_STORE (tt_d->list_store), &dest_iter);

      gtk_list_store_set (GTK_LIST_STORE (tt_d->list_store), &dest_iter,
			  0, v,
			  1, NULL,
			  -1);
    }
  else
    {
      gtk_list_store_set (GTK_LIST_STORE (tt_d->list_store), &dest_iter,
			  1, v,
			  -1);

    }
}


/* Append a new column to TV at position C, and heading TITLE */
static void
add_new_column (GtkTreeView *tv, const gchar *title, gint c)
{
  GtkTreeViewColumn *col = gtk_tree_view_column_new ();
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

  gtk_tree_view_column_set_min_width (col, 100);
  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_resizable (col, TRUE);


  gtk_tree_view_column_set_title (col, title);

  gtk_tree_view_column_pack_start (col, renderer, TRUE);

  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);

  gtk_tree_view_append_column (tv, col);

  gtk_tree_view_column_add_attribute  (col, renderer, "text", c);
}


/* Pops up the dialog box */
void
t_test_paired_samples_dialog (GObject *o, gpointer data)
{
  struct tt_paired_samples_dialog tt_d;
  gint response;
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);

  PsppireVarStore *vs = NULL;

  GladeXML *xml = XML_NEW ("t-test.glade");

  GtkWidget *dict_view =
    get_widget_assert (xml, "paired-samples-t-test-treeview1");

  GtkWidget *options_button = get_widget_assert (xml, "paired-samples-t-test-options-button");

  GtkWidget *selector = get_widget_assert (xml, "psppire-selector3");

  GtkWidget *dialog = get_widget_assert (xml, "t-test-paired-samples-dialog");

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  tt_d.dict = vs->dict;
  tt_d.pairs_treeview =
   get_widget_assert (xml, "paired-samples-t-test-treeview2");
  tt_d.opt = tt_options_dialog_create (xml, GTK_WINDOW (de));

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));


  attach_dictionary_to_treeview (GTK_TREE_VIEW (dict_view),
				 vs->dict,
				 GTK_SELECTION_MULTIPLE,
				 var_is_numeric);

  {
    tt_d.list_store =
      GTK_TREE_MODEL (
		      gtk_list_store_new (2,
					  PSPPIRE_VAR_PTR_TYPE,
					  PSPPIRE_VAR_PTR_TYPE));


    gtk_tree_view_set_model (GTK_TREE_VIEW (tt_d.pairs_treeview),
			     GTK_TREE_MODEL (tt_d.list_store));


    add_new_column (GTK_TREE_VIEW (tt_d.pairs_treeview), _("Var 1"), 0);
    add_new_column (GTK_TREE_VIEW (tt_d.pairs_treeview), _("Var 2"), 1);
  }


  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector),
				 dict_view,
				 tt_d.pairs_treeview,
				 select_as_pair_member,
				 NULL,
				 &tt_d);


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

        GtkWidget *se = psppire_syntax_window_new ();

	gtk_text_buffer_insert_at_cursor (PSPPIRE_SYNTAX_WINDOW (se)->buffer, syntax, -1);

	gtk_widget_show (se);

	g_free (syntax);
      }
      break;
    default:
      break;
    }


  g_object_unref (xml);

  tt_options_dialog_destroy (tt_d.opt);
}


