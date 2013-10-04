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

#include "psppire-dialog-action-1sks.h"

#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

static void psppire_dialog_action_1sks_init            (PsppireDialogAction1sks      *act);
static void psppire_dialog_action_1sks_class_init      (PsppireDialogAction1sksClass *class);

G_DEFINE_TYPE (PsppireDialogAction1sks, psppire_dialog_action_1sks, PSPPIRE_TYPE_DIALOG_ACTION);


enum
  {
    CB_NORMAL,
    CB_POISSON,
    CB_UNIFORM,
    CB_EXPONENTIAL
  };

static void
append_fragment (GString *string, const gchar *dist, PsppireVarView *vv)
{
  g_string_append (string, "\n\t/KOLMOGOROV-SMIRNOV");

  g_string_append (string, " ( ");
  g_string_append (string, dist);
  g_string_append (string, " ) = ");

  psppire_var_view_append_names (vv, 0, string);
}


static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogAction1sks *rd = PSPPIRE_DIALOG_ACTION_1SKS (act);
  gchar *text;

  GString *string = g_string_new ("NPAR TEST");

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->cb[CB_NORMAL])))
    append_fragment (string, "NORMAL", PSPPIRE_VAR_VIEW (rd->variables));

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->cb[CB_UNIFORM])))
    append_fragment (string, "UNIFORM", PSPPIRE_VAR_VIEW (rd->variables));

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->cb[CB_POISSON])))
    append_fragment (string, "POISSON", PSPPIRE_VAR_VIEW (rd->variables));

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->cb[CB_EXPONENTIAL])))
    append_fragment (string, "EXPONENTIAL", PSPPIRE_VAR_VIEW (rd->variables));

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}


static gboolean
dialog_state_valid (gpointer data)
{
  int i;
  PsppireDialogAction1sks *fd = PSPPIRE_DIALOG_ACTION_1SKS (data);

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

  return TRUE;
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogAction1sks *fd = PSPPIRE_DIALOG_ACTION_1SKS (rd_);
  int i;
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (fd->variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  for (i = 0; i < 4; ++i)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->cb[i]), FALSE);
}

static void
psppire_dialog_action_1sks_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogAction1sks *act = PSPPIRE_DIALOG_ACTION_1SKS (a);

  GtkBuilder *xml = builder_new ("ks-one-sample.ui");
  pda->dialog = get_widget_assert   (xml, "ks-one-sample-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");

  act->variables = get_widget_assert   (xml, "psppire-var-view1");

  act->cb[CB_NORMAL] = get_widget_assert (xml, "checkbutton-normal");
  act->cb[CB_POISSON] = get_widget_assert (xml, "checkbutton-poisson");
  act->cb[CB_UNIFORM] = get_widget_assert (xml, "checkbutton-uniform");
  act->cb[CB_EXPONENTIAL] = get_widget_assert (xml, "checkbutton-exp");

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

  g_object_unref (xml);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_1sks_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_1sks_parent_class)->activate (pda);
}

static void
psppire_dialog_action_1sks_class_init (PsppireDialogAction1sksClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_1sks_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_1sks_init (PsppireDialogAction1sks *act)
{
}

