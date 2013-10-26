/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2013  Free Software Foundation

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

#include "psppire-dialog-action-flip.h"

#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

static void psppire_dialog_action_flip_init            (PsppireDialogActionFlip      *act);
static void psppire_dialog_action_flip_class_init      (PsppireDialogActionFlipClass *class);

G_DEFINE_TYPE (PsppireDialogActionFlip, psppire_dialog_action_flip, PSPPIRE_TYPE_DIALOG_ACTION);


/*
     FLIP /VARIABLES=var_list /NEWNAMES=var_name.
*/
static char *
generate_syntax (PsppireDialogAction *act)
{
  const gchar *text;
  PsppireDialogActionFlip *rd = PSPPIRE_DIALOG_ACTION_FLIP (act);

  GString *string = g_string_new ("FLIP");
  gchar *syntax ;

  g_string_append (string, " /VARIABLES = ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (rd->dest), 0, string);

  text = gtk_entry_get_text (GTK_ENTRY (rd->entry));

  if ( text)
    g_string_append_printf (string, " /NEWNAME = %s", text);

  g_string_append (string, ".\n");

  syntax = string->str;

  g_string_free (string, FALSE);

  return syntax;
}


static gboolean
dialog_state_valid (gpointer a)
{
  PsppireDialogActionFlip *act = PSPPIRE_DIALOG_ACTION_FLIP (a);

  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (act->dest));

  gint n_rows = gtk_tree_model_iter_n_children  (model, NULL);

  if ( n_rows == 0 )
    return FALSE;

  if ( 0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (act->entry))))
    return FALSE;

  return TRUE;
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionFlip *rd = PSPPIRE_DIALOG_ACTION_FLIP (rd_);
  GtkTreeModel *dmodel = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->dest));

  gtk_list_store_clear (GTK_LIST_STORE (dmodel));
  gtk_entry_set_text (GTK_ENTRY (rd->entry), "");
}

static void
psppire_dialog_action_flip_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionFlip *act = PSPPIRE_DIALOG_ACTION_FLIP (a);

  GtkBuilder *xml = builder_new ("psppire.ui");
  pda->dialog = get_widget_assert   (xml, "transpose-dialog");
  pda->source = get_widget_assert   (xml, "source-treeview");

  act->dest = get_widget_assert (xml, "variables-treeview");
  act->entry = get_widget_assert (xml, "new-name-entry");

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

  g_object_unref (xml);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_flip_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_flip_parent_class)->activate (pda);
}

static void
psppire_dialog_action_flip_class_init (PsppireDialogActionFlipClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_flip_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_flip_init (PsppireDialogActionFlip *act)
{
}

