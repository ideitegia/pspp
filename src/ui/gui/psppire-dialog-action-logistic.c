/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2010, 2011, 2012  Free Software Foundation

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

#include "psppire-dialog-action-logistic.h"
#include "psppire-value-entry.h"

#include "dialog-common.h"
#include "helper.h"
#include <ui/syntax-gen.h>
#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"
#include "checkbox-treeview.h"
#include "psppire-dict.h"
#include "libpspp/str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void
psppire_dialog_action_logistic_class_init (PsppireDialogActionLogisticClass *class);

G_DEFINE_TYPE (PsppireDialogActionLogistic, psppire_dialog_action_logistic, PSPPIRE_TYPE_DIALOG_ACTION);

static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionLogistic *rd = PSPPIRE_DIALOG_ACTION_LOGISTIC (data);

  const gchar *text = gtk_entry_get_text (GTK_ENTRY (rd->dep_var));

  GtkTreeModel *indep_vars = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->indep_vars));

  GtkTreeIter notused;

  return 0 != strcmp ("", text) &&
    gtk_tree_model_get_iter_first (indep_vars, &notused);
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionLogistic *rd = PSPPIRE_DIALOG_ACTION_LOGISTIC (rd_);

  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->indep_vars));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_entry_set_text (GTK_ENTRY (rd->dep_var), "");
}


static void
psppire_dialog_action_logistic_activate (GtkAction *a)
{
  PsppireDialogActionLogistic *act = PSPPIRE_DIALOG_ACTION_LOGISTIC (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);

  GtkBuilder *xml = builder_new ("logistic.ui");

  pda->dialog = get_widget_assert   (xml, "logistic-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");

  act->dep_var  = get_widget_assert   (xml, "dependent-entry");
  act->indep_vars  = get_widget_assert   (xml, "indep-view");

  g_object_unref (xml);

  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda,
					dialog_state_valid);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_logistic_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_logistic_parent_class)->activate (pda);
}



static char *
generate_syntax (PsppireDialogAction *a)
{
  PsppireDialogActionLogistic *rd = PSPPIRE_DIALOG_ACTION_LOGISTIC (a);
  gchar *text = NULL;

  GString *string = g_string_new ("LOGISTIC REGRESSION ");

  const gchar *dep = gtk_entry_get_text (GTK_ENTRY (rd->dep_var));

  g_string_append (string, dep);

  g_string_append (string, " WITH ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (rd->indep_vars), 0, string);

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}

static void
psppire_dialog_action_logistic_class_init (PsppireDialogActionLogisticClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_logistic_activate;

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_logistic_init (PsppireDialogActionLogistic *act)
{
}

