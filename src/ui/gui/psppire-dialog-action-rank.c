/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010, 2011, 2012  Free Software Foundation

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

#include "psppire-dialog-action-rank.h"

#include "psppire-var-view.h"
#include "dialog-common.h"
#include "psppire-selector.h"
#include "psppire-dict.h"
#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_dialog_action_rank_class_init      (PsppireDialogActionRankClass *class);

G_DEFINE_TYPE (PsppireDialogActionRank, psppire_dialog_action_rank, PSPPIRE_TYPE_DIALOG_ACTION);


static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionRank *rd  = PSPPIRE_DIALOG_ACTION_RANK (act);

  gchar *text = NULL;
  GtkTreeModel *gs = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->group_vars));

  GtkTreeIter notused;

  GString *str = g_string_new ("RANK VARIABLES=");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (rd->rank_vars), 0, str);

  g_string_append_printf (str, " (%c)",
		   gtk_toggle_button_get_active (rd->ascending_togglebutton)
		   ?'A':'D');

  if (  gtk_tree_model_get_iter_first (gs, &notused) )
    {
      g_string_append (str, "\n\tBY ");

      psppire_var_view_append_names (PSPPIRE_VAR_VIEW (rd->group_vars),  0, str);
    }

  g_string_append (str, "\n\t/PRINT = ");
  if (gtk_toggle_button_get_active (rd->summary_togglebutton))
    g_string_append (str, "YES");
  else
    g_string_append (str, "NO");


  if (gtk_toggle_button_get_active (rd->func_button [RANK]))
    g_string_append (str, "\n\t/RANK");
  if (gtk_toggle_button_get_active (rd->func_button [NORMAL]))
    g_string_append (str, "\n\t/NORMAL");
  if (gtk_toggle_button_get_active (rd->func_button [PROPORTION]))
    g_string_append (str, "\n\t/PROPORTION");
  if (gtk_toggle_button_get_active (rd->func_button [PERCENT]))
    g_string_append (str, "\n\t/PERCENT");
  if (gtk_toggle_button_get_active (rd->func_button [RFRACTION]))
    g_string_append (str, "\n\t/RFRACTION");
  if (gtk_toggle_button_get_active (rd->func_button [N]))
    g_string_append (str, "\n\t/N");
  if (gtk_toggle_button_get_active (rd->func_button [SAVAGE]))
    g_string_append (str, "\n\t/SAVAGE");
  if (gtk_toggle_button_get_active (rd->func_button [NTILES]))
    {
      gint n = gtk_spin_button_get_value (GTK_SPIN_BUTTON (rd->ntiles_entry));
      g_string_append_printf (str, "\n\t/NTILES(%d)", n);
    }


  if (gtk_toggle_button_get_active (rd->func_button [NORMAL])
      ||
      gtk_toggle_button_get_active (rd->func_button [PROPORTION]))
    {
      g_string_append (str, "\n\t/FRACTION=");

      if ( gtk_toggle_button_get_active (rd->blom))
	g_string_append (str, "BLOM");
      else if ( gtk_toggle_button_get_active (rd->tukey))
	g_string_append (str, "TUKEY");
      else if ( gtk_toggle_button_get_active (rd->rankit))
	g_string_append (str, "RANKIT");
      else if ( gtk_toggle_button_get_active (rd->vw))
	g_string_append (str, "VW");
    }

  g_string_append (str, "\n\t/TIES=");
  if ( gtk_toggle_button_get_active (rd->mean))
    g_string_append (str, "MEAN");
  else if ( gtk_toggle_button_get_active (rd->low))
    g_string_append (str, "LOW");
  else if ( gtk_toggle_button_get_active (rd->high))
    g_string_append (str, "HIGH");
  else if ( gtk_toggle_button_get_active (rd->condense))
    g_string_append (str, "CONDENSE");


  g_string_append (str, ".");
  text = str->str;

  g_string_free (str, FALSE);

  return text;
}

static gboolean
dialog_state_valid (PsppireDialogAction *da)
{
  PsppireDialogActionRank *dar  = PSPPIRE_DIALOG_ACTION_RANK (da);
  GtkTreeIter notused;
  GtkTreeModel *vars = gtk_tree_view_get_model (GTK_TREE_VIEW (dar->rank_vars));

  return gtk_tree_model_get_iter_first (vars, &notused);
}

static void
dialog_refresh (PsppireDialogAction *act)
{
  PsppireDialogActionRank *dar  = PSPPIRE_DIALOG_ACTION_RANK (act);

  GtkTreeModel *liststore;

  liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (dar->rank_vars));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (dar->group_vars));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_toggle_button_set_active (dar->ascending_togglebutton, TRUE);
  gtk_toggle_button_set_active (dar->summary_togglebutton, FALSE);
}

static void
types_dialog_reset (PsppireDialogActionRank *rd)
{
  gint i;

  for (i = 0 ; i < n_RANK_FUNCS ; ++i )
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->func_button [i]),
				  FALSE);

  gtk_widget_set_sensitive (rd->ntiles_entry, FALSE);

  gtk_widget_set_sensitive (rd->formula_box, FALSE);
}

static void
run_types_dialog (GtkButton *b, PsppireDialogActionRank *dar)
{
  PsppireDialogAction *act  = PSPPIRE_DIALOG_ACTION (dar);

  gtk_window_set_transient_for (GTK_WINDOW (dar->types_dialog),
				GTK_WINDOW (act->dialog));

  types_dialog_reset (dar);

  psppire_dialog_run (PSPPIRE_DIALOG (dar->types_dialog));
}

static void
run_ties_dialog (GtkButton *b,  PsppireDialogActionRank *dar)
{
  PsppireDialogAction *act  = PSPPIRE_DIALOG_ACTION (dar);

  gtk_window_set_transient_for (GTK_WINDOW (dar->ties_dialog),
				GTK_WINDOW (act->dialog));

  psppire_dialog_run (PSPPIRE_DIALOG (dar->ties_dialog));
}

static void
on_ntiles_toggle (GtkToggleButton *toggle_button, PsppireDialogActionRank *dar)
{
  gboolean active = gtk_toggle_button_get_active (toggle_button);
  gtk_widget_set_sensitive (GTK_WIDGET (dar), active);
}

static void
set_sensitivity (PsppireDialogActionRank *dar)
{
  gboolean sens = gtk_toggle_button_get_active
    (GTK_TOGGLE_BUTTON (dar->func_button[PROPORTION]))
    ||
    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dar->func_button[NORMAL]));

  gtk_widget_set_sensitive (dar->formula_box, sens);
}

static void
psppire_dialog_action_rank_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionRank *act = PSPPIRE_DIALOG_ACTION_RANK (a);

  GtkBuilder *xml = builder_new ("rank.ui");

  GtkWidget *types_button = get_widget_assert (xml, "button1");
  GtkWidget *ties_button = get_widget_assert (xml, "button2");

  pda->dialog    = get_widget_assert   (xml, "rank-dialog");
  pda->source    = get_widget_assert   (xml, "dict-treeview");
  act->rank_vars = get_widget_assert   (xml, "variables-treeview");
  act->group_vars =  get_widget_assert (xml, "group-vars-treeview");
  act->ascending_togglebutton =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "radiobutton1"));

  act->summary_togglebutton =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "summary-checkbutton"));

  act->types_dialog = get_widget_assert (xml, "rank-types-dialog");


  act->ntiles_entry  = get_widget_assert (xml, "ntiles-entry");

  act->func_button[RANK]    =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "rank-checkbutton"));

  act->func_button[SAVAGE]  =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "savage-checkbutton"));

  act->func_button[RFRACTION] =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "rfrac-checkbutton"));

  act->func_button[PERCENT] =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "percent-checkbutton"));

  act->func_button[N]       =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "sum-checkbutton"));

  act->func_button[NTILES] =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "ntiles-checkbutton"));

  act->func_button[PROPORTION] =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "prop-checkbutton"));

  act->func_button[NORMAL] =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "normal-checkbutton"));

  act->formula_box = get_widget_assert (xml, "formula-frame");

  act->blom = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "blom-button"));
  act->tukey = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "tukey-button"));
  act->rankit = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "rankit-button"));
  act->vw = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "vw-button"));

  /* Ties dialog */
  act->ties_dialog = PSPPIRE_DIALOG (get_widget_assert (xml, "ties-dialog"));

  act->mean = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "mean-button"));
  act->low = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "low-button"));
  act->high = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "high-button"));
  act->condense = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "condense-button"));

  g_signal_connect_swapped (act->func_button[PROPORTION], "toggled",
			    G_CALLBACK (set_sensitivity),
			    act);

  g_signal_connect_swapped (act->func_button[NORMAL], "toggled",
			    G_CALLBACK (set_sensitivity),
			    act);

  g_signal_connect (types_button, "clicked",
		    G_CALLBACK (run_types_dialog),  act);

  g_signal_connect (ties_button, "clicked",
		    G_CALLBACK (run_ties_dialog),  act);

  g_signal_connect (act->func_button[NTILES], "toggled",
		    G_CALLBACK (on_ntiles_toggle),
		    act->ntiles_entry);

  psppire_dialog_action_set_valid_predicate (pda, (void *) dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, dialog_refresh);

  PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_rank_parent_class)->activate (pda);
  
  g_object_unref (xml);
}

static void
psppire_dialog_action_rank_class_init (PsppireDialogActionRankClass *class)
{
  GTK_ACTION_CLASS (class)->activate = psppire_dialog_action_rank_activate;

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}

static void
psppire_dialog_action_rank_init (PsppireDialogActionRank *act)
{
}
