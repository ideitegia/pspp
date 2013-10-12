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

#include "psppire-dialog-action-two-sample.h"

#include "psppire-var-view.h"

#include "psppire-selector.h"
#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void psppire_dialog_action_two_sample_init            (PsppireDialogActionTwoSample      *act);
static void psppire_dialog_action_two_sample_class_init      (PsppireDialogActionTwoSampleClass *class);

G_DEFINE_TYPE (PsppireDialogActionTwoSample, psppire_dialog_action_two_sample, PSPPIRE_TYPE_DIALOG_ACTION);


static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionTwoSample *pd = PSPPIRE_DIALOG_ACTION_TWO_SAMPLE (data);
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


  /* Now check that at least one toggle button is selected */

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pd->nts[NT_WILCOXON].button)))
    return TRUE;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pd->nts[NT_SIGN].button)))
    return TRUE;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pd->nts[NT_MCNEMAR].button)))
    return TRUE;
    
  return FALSE;
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionTwoSample *pd = PSPPIRE_DIALOG_ACTION_TWO_SAMPLE (rd_);

  gtk_list_store_clear (GTK_LIST_STORE (pd->list_store));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pd->nts[NT_WILCOXON].button), FALSE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pd->nts[NT_SIGN].button), FALSE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pd->nts[NT_MCNEMAR].button), FALSE);
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
  PsppireDialogActionTwoSample *tt_d = PSPPIRE_DIALOG_ACTION_TWO_SAMPLE (data);


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
  gint i;

  PsppireDialogActionTwoSample *d = PSPPIRE_DIALOG_ACTION_TWO_SAMPLE (pda);
  gchar *text = NULL;

  GString *str = g_string_new ("NPAR TEST");

  for (i = 0 ; i < n_Tests; ++i)
  {
    if (! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->nts[i].button)))
      continue;

    g_string_append (str, "\n\t");
    g_string_append (str, d->nts[i].syntax);

    psppire_var_view_append_names (PSPPIRE_VAR_VIEW (d->pairs_treeview), 0, str);

    g_string_append (str, " WITH ");

    psppire_var_view_append_names (PSPPIRE_VAR_VIEW (d->pairs_treeview), 1, str);

    g_string_append (str, " (PAIRED)");
  }

  g_string_append (str, ".\n");

  text = str->str;
  g_string_free (str, FALSE);

  return text;
}

static void
psppire_dialog_action_two_sample_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionTwoSample *act = PSPPIRE_DIALOG_ACTION_TWO_SAMPLE (a);

  GtkBuilder *xml = builder_new ("paired-samples.ui");
  GtkWidget *selector = get_widget_assert (xml, "psppire-selector3");

  pda->dialog = get_widget_assert   (xml, "t-test-paired-samples-dialog");
  pda->source = get_widget_assert   (xml, "paired-samples-t-test-treeview1");

  gtk_window_set_title (GTK_WINDOW (pda->dialog), _("Two-Related-Samples Tests"));

  act->pairs_treeview = get_widget_assert (xml, "paired-samples-t-test-treeview2");
  act->list_store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (act->pairs_treeview)));

  {
    /* NPAR Specific options */
    GtkWidget *frame = gtk_frame_new (_("Test Type"));
    GtkWidget *bb = gtk_vbutton_box_new ();
    GtkWidget *box = get_widget_assert (xml, "vbox3");


    strcpy (act->nts[NT_WILCOXON].syntax, "/WILCOXON");
    strcpy (act->nts[NT_SIGN].syntax, "/SIGN");
    strcpy (act->nts[NT_MCNEMAR].syntax, "/MCNEMAR");

    act->nts[NT_WILCOXON].button = gtk_check_button_new_with_mnemonic (_("_Wilcoxon"));
    act->nts[NT_SIGN].button = gtk_check_button_new_with_mnemonic (_("_Sign"));
    act->nts[NT_MCNEMAR].button = gtk_check_button_new_with_mnemonic (_("_McNemar"));

    gtk_box_pack_start (GTK_BOX (bb), act->nts[NT_WILCOXON].button, FALSE, FALSE, 5);
    gtk_box_pack_start (GTK_BOX (bb), act->nts[NT_SIGN].button,     FALSE, FALSE, 5);
    gtk_box_pack_start (GTK_BOX (bb), act->nts[NT_MCNEMAR].button,  FALSE, FALSE, 5);

    gtk_container_add (GTK_CONTAINER (frame), bb);

    gtk_widget_show_all (frame);

    gtk_box_pack_start (GTK_BOX (box), frame, FALSE, FALSE,  5);
  }

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

  g_object_set (pda->source,
		"predicate", var_is_numeric,
		NULL);

  psppire_selector_set_select_func (PSPPIRE_SELECTOR (selector),
				    select_as_pair_member,
				    act);
  
  g_object_unref (xml);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_two_sample_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_two_sample_parent_class)->activate (pda);
}

static void
psppire_dialog_action_two_sample_class_init (PsppireDialogActionTwoSampleClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_two_sample_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_two_sample_init (PsppireDialogActionTwoSample *act)
{
}

