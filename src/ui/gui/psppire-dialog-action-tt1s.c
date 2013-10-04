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

#include "psppire-dialog-action-tt1s.h"

#include "psppire-var-view.h"
#include "t-test-options.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

static void psppire_dialog_action_tt1s_init            (PsppireDialogActionTt1s      *act);
static void psppire_dialog_action_tt1s_class_init      (PsppireDialogActionTt1sClass *class);

G_DEFINE_TYPE (PsppireDialogActionTt1s, psppire_dialog_action_tt1s, PSPPIRE_TYPE_DIALOG_ACTION);


static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionTt1s *d = PSPPIRE_DIALOG_ACTION_TT1S (act);
  gchar *text;

  GString *str = g_string_new ("T-TEST ");

  g_string_append_printf (str, "/TESTVAL=%s",
			  gtk_entry_get_text (GTK_ENTRY (d->test_value_entry)));

  g_string_append (str, "\n\t/VARIABLES=");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (d->vars_treeview), 0, str);

  tt_options_dialog_append_syntax (d->opt, str);

  g_string_append (str, ".\n");

  text = str->str;

  g_string_free (str, FALSE);

  return text;
}


static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionTt1s *tt_d = PSPPIRE_DIALOG_ACTION_TT1S (data);
  gchar *s = NULL;
  const gchar *text;


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

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionTt1s *d = PSPPIRE_DIALOG_ACTION_TT1S (rd_);

  GtkTreeModel *model =
    gtk_tree_view_get_model (GTK_TREE_VIEW (d->vars_treeview));

  gtk_entry_set_text (GTK_ENTRY (d->test_value_entry), "");

  gtk_list_store_clear (GTK_LIST_STORE (model));
}

static void
psppire_dialog_action_tt1s_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionTt1s *act = PSPPIRE_DIALOG_ACTION_TT1S (a);

  GtkBuilder *xml = builder_new ("t-test.ui");
  GtkWidget *options_button = get_widget_assert (xml, "button1");

  pda->dialog = get_widget_assert (xml, "t-test-one-sample-dialog");
  pda->source = get_widget_assert (xml, "one-sample-t-test-treeview2");

  g_object_set (pda->source, 
		"predicate", var_is_numeric, NULL);

  act->vars_treeview = get_widget_assert (xml, "one-sample-t-test-treeview1");
  act->test_value_entry = get_widget_assert (xml, "test-value-entry");

  act->opt = tt_options_dialog_create (GTK_WINDOW (pda->toplevel));

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

  g_signal_connect_swapped (options_button, "clicked",
			    G_CALLBACK (tt_options_dialog_run), act->opt);


  g_object_unref (xml);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_tt1s_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_tt1s_parent_class)->activate (pda);
}

static void
psppire_dialog_action_tt1s_finalize (GObject *a)
{
  PsppireDialogActionTt1s *act = PSPPIRE_DIALOG_ACTION_TT1S (a);
  tt_options_dialog_destroy (act->opt);
}

static void
psppire_dialog_action_tt1s_class_init (PsppireDialogActionTt1sClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = psppire_dialog_action_tt1s_finalize;
  action_class->activate = psppire_dialog_action_tt1s_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_tt1s_init (PsppireDialogActionTt1s *act)
{
}





