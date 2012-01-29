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

#include "psppire-dialog-action-kmeans.h"

#include "psppire-var-view.h"
#include <stdlib.h>
#include "psppire-dialog.h"
#include "builder-wrapper.h"

static void psppire_dialog_action_kmeans_init            (PsppireDialogActionKmeans      *act);
static void psppire_dialog_action_kmeans_class_init      (PsppireDialogActionKmeansClass *class);


G_DEFINE_TYPE (PsppireDialogActionKmeans, psppire_dialog_action_kmeans, PSPPIRE_TYPE_DIALOG_ACTION);

static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionKmeans *km = PSPPIRE_DIALOG_ACTION_KMEANS (act);
  gchar *text;

  GString *string = g_string_new ("QUICK CLUSTER ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (km->variables), 0, string);

  g_string_append_printf (string, "\n\t/CRITERIA=CLUSTERS(%d)",
			  atoi (gtk_entry_get_text (GTK_ENTRY (km->entry))));

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}



static gboolean
dialog_state_valid (gpointer user_data)
{
  PsppireDialogActionKmeans *fd = user_data;

  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (fd->variables));

  if  (gtk_tree_model_iter_n_children (liststore, NULL) < 2)
    return FALSE;

  if (atoi (gtk_entry_get_text (GTK_ENTRY (fd->entry))) < 2)
    return FALSE;

  return TRUE;
}

static void
refresh (PsppireDialogActionKmeans *fd)
{
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (fd->variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_entry_set_text (GTK_ENTRY (fd->entry), "");
}

static void
psppire_dialog_action_kmeans_activate (GtkAction *a)
{
  PsppireDialogActionKmeans *act = PSPPIRE_DIALOG_ACTION_KMEANS (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  GtkBuilder *xml = builder_new ("k-means.ui");

  pda->dialog = get_widget_assert   (xml, "k-means-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");

  act->entry = get_widget_assert   (xml, "entry1");
  act->variables = get_widget_assert (xml, "psppire-var-view1");

  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);

  g_object_unref (xml);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_kmeans_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_kmeans_parent_class)->activate (pda);
}

static void
psppire_dialog_action_kmeans_class_init (PsppireDialogActionKmeansClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_kmeans_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_kmeans_init (PsppireDialogActionKmeans *act)
{
}

