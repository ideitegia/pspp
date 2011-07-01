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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#include <gtk/gtk.h>


#include "paired-dialog.h"

#include "psppire-data-window.h"
#include "psppire-selector.h"
#include "psppire-var-view.h"

#include "psppire-dict.h"
#include "psppire-var-store.h"

#include "dialog-common.h"
#include "psppire-dialog.h"

#include "psppire-var-ptr.h"


#include "helper.h"



static void
refresh (struct paired_samples_dialog *tt_d)
{
  gtk_list_store_clear (GTK_LIST_STORE (tt_d->list_store));

  if (tt_d->refresh)
    tt_d->refresh (tt_d->aux);
}

static gboolean
dialog_state_valid (gpointer data)
{
  struct variable *v = NULL;
  struct paired_samples_dialog *tt_d = data;
  GtkTreeIter dest_iter;

  gint n_rows = gtk_tree_model_iter_n_children  (tt_d->list_store, NULL);

  if ( n_rows == 0 )
    return FALSE;

  /* Get the last row */
  gtk_tree_model_iter_nth_child (tt_d->list_store, &dest_iter,
				 NULL, n_rows - 1);

  /* Get the last (2nd) column */
  gtk_tree_model_get (tt_d->list_store, &dest_iter, 1, &v, -1);


  if (v == NULL)
    return FALSE;
    
  if ( NULL == tt_d->valid)
    return TRUE;

  return tt_d->valid (tt_d->aux);
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
  struct paired_samples_dialog *tt_d = data;


  gtk_tree_model_get (source_model, &source_iter,
		      DICT_TVM_COL_VAR, &v, -1);

  n_rows = gtk_tree_model_iter_n_children  (tt_d->list_store, NULL);

  if ( n_rows > 0 )
    {

      gtk_tree_model_iter_nth_child (tt_d->list_store,
				     &dest_iter, NULL, n_rows - 1);

      gtk_tree_model_get (tt_d->list_store, &dest_iter, 1, &v1, -1);
    }
  else
    v1 = NULL;

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

void
two_sample_dialog_add_widget (struct paired_samples_dialog *psd, GtkWidget *w)
{
  GtkWidget *box = get_widget_assert (psd->xml, "vbox3");
  gtk_box_pack_start (GTK_BOX (box), w, FALSE, FALSE,  5);
}

void
two_sample_dialog_destroy (struct paired_samples_dialog *psd)
{
  g_object_unref (psd->xml);
  free (psd);
}

struct paired_samples_dialog *
two_sample_dialog_create (PsppireDataWindow *de)
{
  struct paired_samples_dialog *tt_d = g_malloc (sizeof *tt_d);

  PsppireVarStore *vs = NULL;

  tt_d->xml = builder_new ("paired-samples.ui");

  GtkWidget *dict_view =
    get_widget_assert (tt_d->xml, "paired-samples-t-test-treeview1");

  GtkWidget *options_button = get_widget_assert (tt_d->xml, "paired-samples-t-test-options-button");

  GtkWidget *selector = get_widget_assert (tt_d->xml, "psppire-selector3");

  tt_d->dialog = get_widget_assert (tt_d->xml, "t-test-paired-samples-dialog");

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  g_object_get (vs, "dictionary", &tt_d->dict, NULL);
  tt_d->pairs_treeview =
   get_widget_assert (tt_d->xml, "paired-samples-t-test-treeview2");

  gtk_window_set_transient_for (GTK_WINDOW (tt_d->dialog), GTK_WINDOW (de));


  g_object_set (dict_view, "model", tt_d->dict,
		"predicate",
		var_is_numeric, NULL);

  
  tt_d->list_store = gtk_tree_view_get_model (GTK_TREE_VIEW (tt_d->pairs_treeview));

  psppire_selector_set_select_func (PSPPIRE_SELECTOR (selector),
				    select_as_pair_member,
				    tt_d);

  g_signal_connect_swapped (tt_d->dialog, "refresh",
			    G_CALLBACK (refresh),  tt_d);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (tt_d->dialog),
				      dialog_state_valid, tt_d);

  return tt_d;
}
