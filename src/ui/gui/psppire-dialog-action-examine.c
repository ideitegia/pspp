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

#include "psppire-dialog-action-examine.h"

#include "psppire-var-view.h"
#include "dialog-common.h"
#include "psppire-selector.h"
#include "psppire-dict.h"
#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_dialog_action_examine_class_init      (PsppireDialogActionExamineClass *class);

G_DEFINE_TYPE (PsppireDialogActionExamine, psppire_dialog_action_examine, PSPPIRE_TYPE_DIALOG_ACTION);


#define     STAT_DESCRIPTIVES  0x01
#define     STAT_EXTREMES      0x02
#define     STAT_PERCENTILES   0x04

static void
run_stats_dialog (PsppireDialogActionExamine *ed)
{
  gint response;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->descriptives_button),
				ed->stats & STAT_DESCRIPTIVES);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->extremes_button),
				ed->stats & STAT_EXTREMES);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->percentiles_button),
				ed->stats & STAT_PERCENTILES);

  response = psppire_dialog_run (PSPPIRE_DIALOG (ed->stats_dialog));

  if ( response == PSPPIRE_RESPONSE_CONTINUE )
    {
      ed->stats = 0;
      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->descriptives_button) ))
	ed->stats |= STAT_DESCRIPTIVES;

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->extremes_button) ))
	ed->stats |= STAT_EXTREMES;

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->percentiles_button) ))
	ed->stats |= STAT_PERCENTILES;
    }
}

static void
run_opts_dialog (PsppireDialogActionExamine *ed)
{
  gint response;

  switch (ed->opts)
    {
    case OPT_LISTWISE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->listwise), TRUE);
      break;
    case OPT_PAIRWISE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->pairwise), TRUE);
      break;
    case OPT_REPORT:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->report), TRUE);
      break;
    default:
      g_assert_not_reached ();
      break;
    };

  response = psppire_dialog_run (PSPPIRE_DIALOG (ed->opts_dialog));

  if ( response == PSPPIRE_RESPONSE_CONTINUE )
    {
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->listwise)))
	ed->opts = OPT_LISTWISE;
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->pairwise)))
	ed->opts = OPT_PAIRWISE;
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->report)))
	ed->opts = OPT_REPORT;
  }
}




static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionExamine *ed  = PSPPIRE_DIALOG_ACTION_EXAMINE (act);

  const char *label;
  gchar *text = NULL;
  GString *str = g_string_new ("EXAMINE ");

  g_string_append (str, "\n\t/VARIABLES=");
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (ed->variables), 0, str);

  if ( 0  < gtk_tree_model_iter_n_children
       (gtk_tree_view_get_model (GTK_TREE_VIEW (ed->factors)), NULL))
    {
      g_string_append (str, "\n\tBY ");
      psppire_var_view_append_names (PSPPIRE_VAR_VIEW (ed->factors), 0, str);
    }

  label = gtk_entry_get_text (GTK_ENTRY (ed->id_var));
  if ( 0 != strcmp (label, "") )
    {
      g_string_append (str, "\n\t/ID = ");
      g_string_append (str, label);
    }

  if ( ed->stats & (STAT_DESCRIPTIVES | STAT_EXTREMES))
    {
      g_string_append (str, "\n\t/STATISTICS =");

      if ( ed->stats & STAT_DESCRIPTIVES)
	g_string_append (str, " DESCRIPTIVES");

      if ( ed->stats & STAT_EXTREMES)
	g_string_append (str, " EXTREME");
    }

  if ( ed->stats & STAT_PERCENTILES)
    g_string_append (str, "\n\t/PERCENTILES");


  g_string_append (str, "\n\t/MISSING=");
  switch (ed->opts)
    {
    case OPT_REPORT:
      g_string_append (str, "REPORT");
      break;
    case OPT_PAIRWISE:
      g_string_append (str, "PAIRWISE");
      break;
    default:
      g_string_append (str, "LISTWISE");
      break;
    };

  g_string_append (str, ".");
  text = str->str;

  g_string_free (str, FALSE);

  return text;
}

static gboolean
dialog_state_valid (PsppireDialogAction *da)
{
  PsppireDialogActionExamine *pae  = PSPPIRE_DIALOG_ACTION_EXAMINE (da);
  GtkTreeIter notused;
  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (pae->variables));

  return gtk_tree_model_get_iter_first (vars, &notused);
}

static void
dialog_refresh (PsppireDialogAction *da)
{
  PsppireDialogActionExamine *dae  = PSPPIRE_DIALOG_ACTION_EXAMINE (da);
  GtkTreeModel *liststore = NULL;

  liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (dae->variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (dae->factors));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_entry_set_text (GTK_ENTRY (dae->id_var), "");
  dae->stats = 0x00;
  dae->opts = OPT_LISTWISE;
}

static void
psppire_dialog_action_examine_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionExamine *act = PSPPIRE_DIALOG_ACTION_EXAMINE (a);

  GtkBuilder *xml = builder_new ("examine.ui");

  GtkWidget *stats_button = get_widget_assert (xml, "stats-button");
  GtkWidget *opts_button = get_widget_assert (xml, "opts-button");

  GtkWidget *dep_sel = get_widget_assert (xml, "psppire-selector1");

  pda->dialog    = get_widget_assert   (xml, "examine-dialog");
  pda->source    = get_widget_assert   (xml, "treeview1");
  act->variables = get_widget_assert   (xml, "treeview2");
  act->factors   = get_widget_assert   (xml, "treeview3");
  act->id_var    = get_widget_assert   (xml, "entry1");

  act->stats_dialog        = get_widget_assert (xml, "statistics-dialog");
  act->descriptives_button = get_widget_assert (xml, "descriptives-button");
  act->extremes_button     = get_widget_assert (xml, "extremes-button"); 
  act->percentiles_button  = get_widget_assert (xml, "percentiles-button");

  act->opts_dialog = get_widget_assert (xml, "options-dialog");
  act->listwise    = get_widget_assert (xml, "radiobutton1");
  act->pairwise    = get_widget_assert (xml, "radiobutton2");
  act->report      = get_widget_assert (xml, "radiobutton3");

  psppire_selector_set_allow (PSPPIRE_SELECTOR (dep_sel), numeric_only);

  psppire_dialog_action_set_valid_predicate (pda, (void *) dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, dialog_refresh);

  g_signal_connect_swapped (stats_button, "clicked",
		    G_CALLBACK (run_stats_dialog), act);

  g_signal_connect_swapped (opts_button, "clicked",
			    G_CALLBACK (run_opts_dialog), act);

  PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_examine_parent_class)->activate (pda);
  
  g_object_unref (xml);
}

static void
psppire_dialog_action_examine_class_init (PsppireDialogActionExamineClass *class)
{
  GTK_ACTION_CLASS (class)->activate = psppire_dialog_action_examine_activate;

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}

static void
psppire_dialog_action_examine_init (PsppireDialogActionExamine *act)
{
}
