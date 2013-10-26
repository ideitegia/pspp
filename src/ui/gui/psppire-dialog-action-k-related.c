/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2010, 2011, 2012, 2013  Free Software Foundation

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

#include "psppire-dialog-action-k-related.h"

#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

static void psppire_dialog_action_k_related_init            (PsppireDialogActionKRelated      *act);
static void psppire_dialog_action_k_related_class_init      (PsppireDialogActionKRelatedClass *class);

G_DEFINE_TYPE (PsppireDialogActionKRelated, psppire_dialog_action_k_related, PSPPIRE_TYPE_DIALOG_ACTION);

static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionKRelated *krd = PSPPIRE_DIALOG_ACTION_K_RELATED (act);
  gchar *text;

  GString *string = g_string_new ("NPAR TEST");

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (krd->friedman)))
    {
      g_string_append (string, "\n\t/FRIEDMAN = ");
      psppire_var_view_append_names (PSPPIRE_VAR_VIEW (krd->var_view), 0, string);
    }

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (krd->kendal)))
    {
      g_string_append (string, "\n\t/KENDALL = ");
      psppire_var_view_append_names (PSPPIRE_VAR_VIEW (krd->var_view), 0, string);
    }

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (krd->cochran)))
    {
      g_string_append (string, "\n\t/COCHRAN = ");
      psppire_var_view_append_names (PSPPIRE_VAR_VIEW (krd->var_view), 0, string);
    }

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}


static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionKRelated *krd = PSPPIRE_DIALOG_ACTION_K_RELATED (data);

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (krd->var_view));

  /* Tests using less than 3 variables are not useful */
  if (gtk_tree_model_iter_n_children (vars, NULL) < 3)
    return FALSE;

  /* At least one checkbutton must be active */
  if ( 
      ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (krd->friedman))
      && 
      ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (krd->kendal))
      && 
      ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (krd->cochran))
       )
    return FALSE;

  return TRUE;
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionKRelated *krd = PSPPIRE_DIALOG_ACTION_K_RELATED (rd_);
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (krd->var_view));

  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (krd->friedman), TRUE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (krd->kendal), FALSE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (krd->cochran), FALSE);
}

static void
psppire_dialog_action_k_related_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionKRelated *act = PSPPIRE_DIALOG_ACTION_K_RELATED (a);

  GtkBuilder *xml = builder_new ("k-related.ui");
  pda->dialog = get_widget_assert   (xml, "k-related-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");

  act->var_view  = get_widget_assert (xml, "variables-treeview");
  act->friedman =  get_widget_assert (xml, "friedman-checkbutton");
  act->kendal =  get_widget_assert (xml, "kendal-checkbutton");
  act->cochran =  get_widget_assert (xml, "cochran-checkbutton");

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

  g_object_set (pda->source,
		"predicate", var_is_numeric,
		NULL);

  g_object_unref (xml);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_k_related_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_k_related_parent_class)->activate (pda);
}

static void
psppire_dialog_action_k_related_class_init (PsppireDialogActionKRelatedClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_k_related_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_k_related_init (PsppireDialogActionKRelated *act)
{
}

