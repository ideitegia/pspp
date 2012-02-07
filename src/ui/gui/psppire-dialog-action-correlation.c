/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012  Free Software Foundation

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

#include "psppire-dialog-action-correlation.h"

#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

static void psppire_dialog_action_correlation_init            (PsppireDialogActionCorrelation      *act);
static void psppire_dialog_action_correlation_class_init      (PsppireDialogActionCorrelationClass *class);

G_DEFINE_TYPE (PsppireDialogActionCorrelation, psppire_dialog_action_correlation, PSPPIRE_TYPE_DIALOG_ACTION);

static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionCorrelation *rd = PSPPIRE_DIALOG_ACTION_CORRELATION (act);
  gchar *text;
  GString *string = g_string_new ("CORRELATION");
  g_string_append (string, "\n\t/VARIABLES = ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (rd->variables), 0, string);


  g_string_append (string, "\n\t/PRINT =");

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->two_tailed)))
    g_string_append (string, " TWOTAIL");
  else
    g_string_append (string, " ONETAIL");


  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->significant)))
    g_string_append (string, " NOSIG");
  else
    g_string_append (string, " SIG");


  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}


static gboolean
dialog_state_valid (gpointer user_data)
{
  PsppireDialogActionCorrelation *corr = user_data;
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (corr->variables));

  if  (gtk_tree_model_iter_n_children (liststore, NULL) > 1)
    return TRUE;

  return FALSE;
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionCorrelation *rd =
    PSPPIRE_DIALOG_ACTION_CORRELATION (rd_);
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->significant), FALSE);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->two_tailed), TRUE);
}

static void
psppire_dialog_action_correlation_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionCorrelation *act = PSPPIRE_DIALOG_ACTION_CORRELATION (a);

  GtkBuilder *xml = builder_new ("correlation.ui");
  pda->dialog = get_widget_assert   (xml, "correlation-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");

  act->variables = get_widget_assert (xml, "psppire-var-view1");
  act->significant = get_widget_assert (xml, "button-flag-significants");
  act->two_tailed = get_widget_assert (xml, "button-two-tailed");

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

  g_object_unref (xml);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_correlation_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_correlation_parent_class)->activate (pda);
}

static void
psppire_dialog_action_correlation_class_init (PsppireDialogActionCorrelationClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_correlation_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_correlation_init (PsppireDialogActionCorrelation *act)
{
}

