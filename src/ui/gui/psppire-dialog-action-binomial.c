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

#include "psppire-dialog-action-binomial.h"
#include "psppire-value-entry.h"

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
psppire_dialog_action_binomial_class_init (PsppireDialogActionBinomialClass *class);

G_DEFINE_TYPE (PsppireDialogActionBinomial, psppire_dialog_action_binomial, PSPPIRE_TYPE_DIALOG_ACTION);


static gboolean
get_proportion (PsppireDialogActionBinomial *act, double *prop)
{
    const gchar *text = gtk_entry_get_text (GTK_ENTRY (act->prop_entry));
    gchar *endptr = NULL;
     *prop = g_strtod (text, &endptr);

    if (endptr == text)
      return FALSE;

    return TRUE; 
}

static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionBinomial *act = PSPPIRE_DIALOG_ACTION_BINOMIAL (data);
  double prop;

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (act->var_view));

  GtkTreeIter notused;

  if ( !gtk_tree_model_get_iter_first (vars, &notused) )
    return FALSE;

  if ( ! get_proportion (act, &prop))
    return FALSE;

  if (prop < 0 || prop > 1.0)
    return FALSE;

  return TRUE;
}

static void
refresh (PsppireDialogAction *da)
{
  PsppireDialogActionBinomial *act = PSPPIRE_DIALOG_ACTION_BINOMIAL (da);
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (act->var_view));

  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (act->button1), TRUE);

  gtk_entry_set_text (GTK_ENTRY (act->prop_entry), "0.5");

  gtk_entry_set_text (GTK_ENTRY (act->cutpoint_entry), "");
}


static void
psppire_dialog_action_binomial_activate (GtkAction *a)
{
  PsppireDialogActionBinomial *act = PSPPIRE_DIALOG_ACTION_BINOMIAL (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);

  GtkBuilder *xml = builder_new ("binomial.ui");

  pda->dialog = get_widget_assert   (xml, "binomial-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");

  act->var_view   = get_widget_assert (xml, "variables-treeview");
  act->button1   = get_widget_assert (xml, "radiobutton3");
  act->prop_entry = get_widget_assert (xml, "proportion-entry");

  act->cutpoint_entry =     get_widget_assert   (xml, "cutpoint-entry");
  act->cutpoint_button =    get_widget_assert   (xml, "radiobutton4");

  g_object_unref (xml);


  g_signal_connect (act->cutpoint_button, "toggled", G_CALLBACK (set_sensitivity_from_toggle),
		    act->cutpoint_entry);

  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda,
					dialog_state_valid);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_binomial_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_binomial_parent_class)->activate (pda);
}



static char *
generate_syntax (PsppireDialogAction *a)
{
  PsppireDialogActionBinomial *scd = PSPPIRE_DIALOG_ACTION_BINOMIAL (a);
  gchar *text = NULL;

  double prop;
  struct string str;

  ds_init_cstr (&str, "NPAR TEST\n\t/BINOMIAL");

  if ( get_proportion (scd, &prop))
    ds_put_c_format (&str, "(%g)", prop);

  ds_put_cstr (&str, " =");

  psppire_var_view_append_names_str (PSPPIRE_VAR_VIEW (scd->var_view), 0, &str);

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scd->cutpoint_button)))
    {
      const gchar *cutpoint = gtk_entry_get_text (GTK_ENTRY (scd->cutpoint_entry));
      ds_put_c_format  (&str, "(%s)", cutpoint);
    }

  ds_put_cstr (&str, ".\n");

  text = ds_steal_cstr (&str);

  ds_destroy (&str);

  return text;
}

static void
psppire_dialog_action_binomial_class_init (PsppireDialogActionBinomialClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_binomial_activate;

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_binomial_init (PsppireDialogActionBinomial *act)
{
}

