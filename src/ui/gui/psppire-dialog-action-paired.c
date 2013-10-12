/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012, 2013  Free Software Foundation

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

#include "psppire-dialog-action-paired.h"

#include "psppire-var-view.h"

#include "psppire-selector.h"
#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include "t-test-options.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void psppire_dialog_action_paired_init            (PsppireDialogActionPaired      *act);
static void psppire_dialog_action_paired_class_init      (PsppireDialogActionPairedClass *class);

G_DEFINE_TYPE (PsppireDialogActionPaired, psppire_dialog_action_paired, PSPPIRE_TYPE_DIALOG_ACTION);


static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionPaired *pd = PSPPIRE_DIALOG_ACTION_PAIRED (data);
  gint n_rows = gtk_tree_model_iter_n_children  (GTK_TREE_MODEL (pd->list_store), NULL);
  struct variable *v = NULL;
  GtkTreeIter dest_iter;

  if ( n_rows == 0 )
    return FALSE;

  /* Get the last row */
  gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (pd->list_store), &dest_iter,
				 NULL, n_rows - 1);

  /* Get the last (2nd) column */
  gtk_tree_model_get (GTK_TREE_MODEL (pd->list_store), &dest_iter, 1, &v, -1);

  if (v == NULL)
    return FALSE;
    
  /* if ( NULL == pd->valid) */
  /*   return TRUE; */

  return TRUE;
  //  return pd->valid (pd->aux);
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionPaired *pd = PSPPIRE_DIALOG_ACTION_PAIRED (rd_);

  gtk_list_store_clear (GTK_LIST_STORE (pd->list_store));
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
  PsppireDialogActionPaired *tt_d = PSPPIRE_DIALOG_ACTION_PAIRED (data);


  gtk_tree_model_get (source_model, &source_iter,
		      DICT_TVM_COL_VAR, &v, -1);

  n_rows = gtk_tree_model_iter_n_children  (GTK_TREE_MODEL (tt_d->list_store), NULL);

  if ( n_rows > 0 )
    {

      gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (tt_d->list_store),
				     &dest_iter, NULL, n_rows - 1);

      gtk_tree_model_get (GTK_TREE_MODEL (tt_d->list_store), &dest_iter, 1, &v1, -1);
    }
  else
    v1 = NULL;

  if ( n_rows == 0 || v1 != NULL)
    {
      gtk_list_store_append (tt_d->list_store, &dest_iter);

      gtk_list_store_set (tt_d->list_store, &dest_iter,
			  0, v,
			  1, NULL,
			  -1);
    }
  else
    {
      gtk_list_store_set (tt_d->list_store, &dest_iter,
			  1, v,
			  -1);
    }
}



static gchar *
generate_syntax (PsppireDialogAction *pda)
{
  PsppireDialogActionPaired *d = PSPPIRE_DIALOG_ACTION_PAIRED (pda);
  gchar *text = NULL;
  GString *str =   g_string_new ("T-TEST \n\tPAIRS = ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (d->pairs_treeview), 0, str);

  g_string_append (str, " WITH ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (d->pairs_treeview), 1, str);

  g_string_append (str, " (PAIRED)");
  g_string_append (str, "\n");

  tt_options_dialog_append_syntax (d->opt, str);

  g_string_append (str, ".\n");

  text = str->str;
  g_string_free (str, FALSE);

  return text;
}

static void
psppire_dialog_action_paired_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionPaired *act = PSPPIRE_DIALOG_ACTION_PAIRED (a);

  GtkBuilder *xml = builder_new ("paired-samples.ui");
  GtkWidget *selector = get_widget_assert (xml, "psppire-selector3");
  GtkWidget *bb = gtk_hbutton_box_new ();
  GtkWidget *button = gtk_button_new_with_mnemonic (_("O_ptions..."));
  GtkWidget *box = get_widget_assert (xml, "vbox3");


  pda->dialog = get_widget_assert   (xml, "t-test-paired-samples-dialog");
  pda->source = get_widget_assert   (xml, "paired-samples-t-test-treeview1");

  gtk_window_set_title (GTK_WINDOW (pda->dialog), _("Paired Samples T Test"));

  act->pairs_treeview = get_widget_assert (xml, "paired-samples-t-test-treeview2");
  act->list_store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (act->pairs_treeview)));

  act->opt = tt_options_dialog_create (GTK_WINDOW (pda->toplevel));


  g_signal_connect_swapped (button, "clicked", G_CALLBACK (tt_options_dialog_run), act->opt);


  gtk_box_pack_start (GTK_BOX (bb), button, TRUE, TRUE, 5);
  gtk_box_pack_start (GTK_BOX (box), bb, FALSE, FALSE, 5);
  gtk_widget_show_all (box);
 

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

  g_object_set (pda->source,
		"predicate", var_is_numeric,
		NULL);

  psppire_selector_set_select_func (PSPPIRE_SELECTOR (selector),
				    select_as_pair_member,
				    act);
  
  g_object_unref (xml);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_paired_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_paired_parent_class)->activate (pda);
}

static void
psppire_dialog_action_paired_finalize (GObject *o)
{
  PsppireDialogActionPaired *act = PSPPIRE_DIALOG_ACTION_PAIRED (o);
  tt_options_dialog_destroy (act->opt);
}

static void
psppire_dialog_action_paired_class_init (PsppireDialogActionPairedClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  action_class->activate = psppire_dialog_action_paired_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;

   object_class->finalize = psppire_dialog_action_paired_finalize;
}


static void
psppire_dialog_action_paired_init (PsppireDialogActionPaired *act)
{
  act->opt = NULL;
}

