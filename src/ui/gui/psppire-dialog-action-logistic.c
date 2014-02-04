/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2010, 2011, 2012, 2014  Free Software Foundation

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

#include <float.h>

#include "dialog-common.h"
#include "helper.h"
#include <ui/syntax-gen.h>
#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"
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
on_opts_clicked (PsppireDialogActionLogistic *act)
{
  int ret;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(act->conf_checkbox), act->conf);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (act->conf_entry), act->conf_level);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(act->const_checkbox), act->constant);

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (act->cut_point_entry), act->cut_point);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (act->iterations_entry), act->max_iterations);

  
  ret = psppire_dialog_run (PSPPIRE_DIALOG (act->opts_dialog));

  if ( ret == PSPPIRE_RESPONSE_CONTINUE )
    {
      act->conf = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(act->conf_checkbox));
      act->conf_level = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->conf_entry));
      
      act->constant = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(act->const_checkbox));

      act->cut_point = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->cut_point_entry));
      act->max_iterations = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->iterations_entry));
    }
}


static void
psppire_dialog_action_logistic_activate (GtkAction *a)
{
  PsppireDialogActionLogistic *act = PSPPIRE_DIALOG_ACTION_LOGISTIC (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  GtkWidget *opts_button;

  GtkBuilder *xml = builder_new ("logistic.ui");

  pda->dialog = get_widget_assert   (xml, "logistic-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");
  act->cut_point = 0.5;
  act->max_iterations = 20;
  act->constant = true;
  act->conf = false;
  act->conf_level = 95;

  act->dep_var  = get_widget_assert   (xml, "dependent-entry");
  act->indep_vars  = get_widget_assert   (xml, "indep-view");
  act->opts_dialog = get_widget_assert (xml, "options-dialog");
  act->conf_checkbox = get_widget_assert (xml, "checkbutton2");
  act->conf_entry = get_widget_assert (xml, "spinbutton1");
  act->const_checkbox = get_widget_assert (xml, "checkbutton1");

  act->iterations_entry = get_widget_assert (xml, "spinbutton3");
  act->cut_point_entry = get_widget_assert (xml, "spinbutton2");

  opts_button = get_widget_assert (xml, "options-button");

  g_signal_connect_swapped (opts_button, "clicked",
			    G_CALLBACK (on_opts_clicked),  act);

  g_signal_connect (act->conf_checkbox, "toggled",
		    G_CALLBACK (set_sensitivity_from_toggle),  
		    act->conf_entry);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(act->conf_checkbox), TRUE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(act->conf_checkbox), FALSE);

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
  struct string str;
  const gchar *dep = gtk_entry_get_text (GTK_ENTRY (rd->dep_var));

  ds_init_cstr (&str, "LOGISTIC REGRESSION ");

  ds_put_cstr (&str, dep);

  ds_put_cstr (&str, " WITH ");

  psppire_var_view_append_names_str (PSPPIRE_VAR_VIEW (rd->indep_vars), 0, &str);

  ds_put_cstr (&str, "\n\t/CRITERIA =");

  syntax_gen_pspp (&str, " CUT(%.*g)", DBL_DIG + 1, rd->cut_point);

  syntax_gen_pspp (&str, " ITERATE(%d)", rd->max_iterations);

  if (rd->conf)
    {
      syntax_gen_pspp (&str, "\n\t/PRINT = CI(%.*g)",
                       DBL_DIG + 1, rd->conf_level);
    }

  if (rd->constant) 
    ds_put_cstr (&str, "\n\t/NOORIGIN");
  else
    ds_put_cstr (&str, "\n\t/ORIGIN");

  ds_put_cstr (&str, ".\n");

  text = ds_steal_cstr (&str);

  ds_destroy (&str);

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

