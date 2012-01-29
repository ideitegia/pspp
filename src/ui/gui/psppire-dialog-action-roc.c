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

#include "psppire-dialog-action-roc.h"

#include "dialog-common.h"
#include <ui/syntax-gen.h>
#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include "psppire-dict.h"
#include "libpspp/str.h"

static void
psppire_dialog_action_roc_class_init (PsppireDialogActionRocClass *class);

G_DEFINE_TYPE (PsppireDialogActionRoc, psppire_dialog_action_roc, PSPPIRE_TYPE_DIALOG_ACTION);

static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionRoc *rd = data;
  const gchar *text;

  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (rd->test_variables));

  if  (gtk_tree_model_iter_n_children (liststore, NULL) < 1)
    return FALSE;

  
  text = gtk_entry_get_text (GTK_ENTRY (rd->state_variable));
  if ( 0 == strcmp ("", text))
    return FALSE;


  text = gtk_entry_get_text (GTK_ENTRY (rd->state_value));
  if ( 0 == strcmp ("", text))
    return FALSE;


  return TRUE;
}

static void
on_curve_button_toggle (GtkCheckButton *curve, PsppireDialogActionRoc *rd)
{
  if ( !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (curve)))
    {
      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->reference)))
	g_object_set (rd->reference, "inconsistent", TRUE, NULL);
      g_object_set (rd->reference, "sensitive", FALSE, NULL);
    }
  else 
    {
      g_object_set (rd->reference, "inconsistent", FALSE, NULL);
      g_object_set (rd->reference, "sensitive", TRUE, NULL);
    }
}

static void
refresh (PsppireDialogActionRoc *rd)
{
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (rd->test_variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_entry_set_text (GTK_ENTRY (rd->state_variable), "");
  gtk_entry_set_text (GTK_ENTRY (rd->state_value), "");

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->curve),          TRUE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->reference),      FALSE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->standard_error), FALSE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->coordinates),    FALSE);
}

static void
psppire_dialog_action_roc_activate (GtkAction *a)
{
  PsppireDialogActionRoc *act = PSPPIRE_DIALOG_ACTION_ROC (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);

  GtkBuilder *xml = builder_new ("roc.ui");
  pda->dialog = get_widget_assert   (xml, "roc-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");
  pda->source = get_widget_assert   (xml, "dict-view");

  act->test_variables    = get_widget_assert   (xml, "psppire-var-view1");
  act->state_variable    = get_widget_assert   (xml, "entry1");
  act->state_value       = get_widget_assert   (xml, "entry2");

  act->curve          = get_widget_assert   (xml, "curve");
  act->reference      = get_widget_assert   (xml, "reference-line");
  act->standard_error = get_widget_assert   (xml, "standard-error");
  act->coordinates    = get_widget_assert   (xml, "co-ordinates");

  g_object_unref (xml);

  g_signal_connect (act->curve, "toggled",
		    G_CALLBACK (on_curve_button_toggle), act);

  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda,
					dialog_state_valid);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_roc_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_roc_parent_class)->activate (pda);
}



static char *
generate_syntax (PsppireDialogAction *a)
{
  PsppireDialogActionRoc *rd = PSPPIRE_DIALOG_ACTION_ROC (a);
  gchar *text;
  const gchar *var_name = gtk_entry_get_text (GTK_ENTRY (rd->state_variable));
  GString *string = g_string_new ("ROC");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (rd->test_variables), 0, string);

  g_string_append (string, " BY ");

  g_string_append (string, var_name);

  g_string_append (string, " (");
  {
    const gchar *value = gtk_entry_get_text (GTK_ENTRY (rd->state_value));

    const struct variable *var = psppire_dict_lookup_var (PSPPIRE_DIALOG_ACTION(rd)->dict, var_name);

    g_return_val_if_fail (var, NULL);

    if ( var_is_alpha (var))
      {
	struct string str;
	ds_init_empty (&str);
	syntax_gen_string (&str, ss_cstr (value));
	g_string_append (string, ds_cstr (&str));
	ds_destroy (&str);
      }
    else
      g_string_append (string, value);
  }
  g_string_append (string, ")");


  /* The /PLOT subcommand */
  g_string_append (string, "\n\t/PLOT ");
  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->curve)))
    {
      g_string_append (string, "CURVE");
      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->reference)))
	g_string_append (string, " (REFERENCE)");
    }
  else
    g_string_append (string, "NONE");


  /* The /PRINT subcommand */
  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->standard_error)) ||
       gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->coordinates)) )
    {
      g_string_append (string, "\n\t/PRINT");

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->standard_error)))
	g_string_append (string, " SE");

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->coordinates)))
	g_string_append (string, " COORDINATES");
    }

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}

static void
psppire_dialog_action_roc_class_init (PsppireDialogActionRocClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_roc_activate;

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_roc_init (PsppireDialogActionRoc *act)
{
}

