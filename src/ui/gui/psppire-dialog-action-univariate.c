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

#include "psppire-dialog-action-univariate.h"

#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

static void psppire_dialog_action_univariate_init            (PsppireDialogActionUnivariate      *act);
static void psppire_dialog_action_univariate_class_init      (PsppireDialogActionUnivariateClass *class);

G_DEFINE_TYPE (PsppireDialogActionUnivariate, psppire_dialog_action_univariate, PSPPIRE_TYPE_DIALOG_ACTION);

static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionUnivariate *uvd = PSPPIRE_DIALOG_ACTION_UNIVARIATE (act);

  gchar *text = NULL;
  GString *str = g_string_new ("GLM ");

  g_string_append (str, gtk_entry_get_text (GTK_ENTRY (uvd->dep_entry)));
  
  g_string_append (str, " BY ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (uvd->factor_list), 0, str);

  g_string_append (str, ".\n");

  text = str->str;

  g_string_free (str, FALSE);

  return text;
}


static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionUnivariate *ud = PSPPIRE_DIALOG_ACTION_UNIVARIATE (data);
  GtkTreeModel *vars;
  GtkTreeIter notused;

  if ( 0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (ud->dep_entry))))
    return false;

  vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (ud->factor_list));

  return gtk_tree_model_get_iter_first (vars, &notused);
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionUnivariate *uv = PSPPIRE_DIALOG_ACTION_UNIVARIATE (rd_);
  GtkTreeModel *liststore ;

  gtk_entry_set_text (GTK_ENTRY (uv->dep_entry), "");

  liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (uv->factor_list));

  gtk_list_store_clear (GTK_LIST_STORE (liststore));
}

static void
psppire_dialog_action_univariate_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionUnivariate *act = PSPPIRE_DIALOG_ACTION_UNIVARIATE (a);

  GtkBuilder *xml = builder_new ("univariate.ui");
  pda->dialog = get_widget_assert   (xml, "univariate-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");

  act->dep_entry = get_widget_assert (xml, "dep-entry");
  act->factor_list = get_widget_assert (xml, "factors-view");

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

  g_object_unref (xml);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_univariate_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_univariate_parent_class)->activate (pda);
}

static void
psppire_dialog_action_univariate_class_init (PsppireDialogActionUnivariateClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_univariate_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_univariate_init (PsppireDialogActionUnivariate *act)
{
}

