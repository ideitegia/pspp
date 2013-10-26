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

#include "psppire-dialog-action-runs.h"

#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

static void psppire_dialog_action_runs_init            (PsppireDialogActionRuns      *act);
static void psppire_dialog_action_runs_class_init      (PsppireDialogActionRunsClass *class);

G_DEFINE_TYPE (PsppireDialogActionRuns, psppire_dialog_action_runs, PSPPIRE_TYPE_DIALOG_ACTION);

enum
  {
    CB_MEDIAN,
    CB_MEAN,
    CB_MODE,
    CB_CUSTOM
  };


static void
append_fragment (GString *string, const gchar *cut, PsppireVarView *vv)
{
  g_string_append (string, "\n\t/RUNS");

  g_string_append (string, " ( ");
  g_string_append (string, cut);
  g_string_append (string, " ) = ");

  psppire_var_view_append_names (vv, 0, string);
}

static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionRuns *rd = PSPPIRE_DIALOG_ACTION_RUNS (act);
  gchar *text;

  GString *string = g_string_new ("NPAR TEST");

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->cb[CB_MEAN])))
    append_fragment (string, "MEAN", PSPPIRE_VAR_VIEW (rd->variables));

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->cb[CB_MEDIAN])))
    append_fragment (string, "MEDIAN", PSPPIRE_VAR_VIEW (rd->variables));

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->cb[CB_MODE])))
    append_fragment (string, "MODE", PSPPIRE_VAR_VIEW (rd->variables));

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->cb[CB_CUSTOM])))
    {
      const char *text = gtk_entry_get_text (GTK_ENTRY (rd->entry));
      append_fragment (string, text, PSPPIRE_VAR_VIEW (rd->variables));
    }

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}


static gboolean
dialog_state_valid (gpointer data)
{
  int i;
  PsppireDialogActionRuns *fd = PSPPIRE_DIALOG_ACTION_RUNS (data);

  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (fd->variables));

  if  (gtk_tree_model_iter_n_children (liststore, NULL) < 1)
    return FALSE;

  for (i = 0; i < 4; ++i)
    {
      if ( TRUE == gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->cb[i])))
	break;
    }
  if ( i >= 4)
    return FALSE;


  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->cb[CB_CUSTOM])))
    {
      if (0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (fd->entry))))
	return FALSE;
    }

  return TRUE;
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionRuns *rd = PSPPIRE_DIALOG_ACTION_RUNS (rd_);
  int i;
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_entry_set_text (GTK_ENTRY (rd->entry), "");

  for (i = 0; i < 4; ++i)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->cb[i]), FALSE);
}

static void
psppire_dialog_action_runs_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionRuns *act = PSPPIRE_DIALOG_ACTION_RUNS (a);

  GtkBuilder *xml = builder_new ("runs.ui");
  pda->dialog = get_widget_assert   (xml, "runs-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");

  act->entry = get_widget_assert   (xml, "entry1");
  act->cb[CB_MEDIAN] = get_widget_assert (xml, "checkbutton1");
  act->cb[CB_MEAN] = get_widget_assert (xml, "checkbutton2");
  act->cb[CB_MODE] = get_widget_assert (xml, "checkbutton4");
  act->cb[CB_CUSTOM] = get_widget_assert (xml, "checkbutton3");
  act->variables = get_widget_assert   (xml, "psppire-var-view1");


  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

  g_object_unref (xml);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_runs_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_runs_parent_class)->activate (pda);
}

static void
psppire_dialog_action_runs_class_init (PsppireDialogActionRunsClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_runs_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_runs_init (PsppireDialogActionRuns *act)
{
}

