/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2008, 2009, 2010, 2011  Free Software Foundation

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

#include "examine-dialog.h"
#include "psppire-var-view.h"

#include <gtk/gtk.h>
#include <stdlib.h>

#include <ui/gui/psppire-data-window.h>
#include <ui/gui/dialog-common.h>
#include <ui/gui/dict-display.h>
#include <ui/gui/helper.h>
#include <ui/gui/psppire-dialog.h>
#include <ui/gui/psppire-var-store.h>
#include "executor.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum opts
  {
    OPT_LISTWISE,
    OPT_PAIRWISE,
    OPT_REPORT
  };


#define     STAT_DESCRIPTIVES  0x01
#define     STAT_EXTREMES      0x02
#define     STAT_PERCENTILES   0x04


struct examine_dialog
{
  PsppireDict *dict;

  GtkWidget *dep_list ;
  GtkWidget *fct_list ;
  GtkWidget *id_entry ;

  GtkWidget *stats_dialog;
  GtkWidget *opts_dialog;

  /* Options */
  enum opts opts;
  guint stats;
  GtkWidget *listwise;
  GtkWidget *pairwise;
  GtkWidget *report;

  GtkToggleButton *descriptives_button;
  GtkToggleButton *extremes_button;
  GtkToggleButton *percentiles_button;
};

static void
refresh (PsppireDialog *dialog, struct examine_dialog *ex_d)
{
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (ex_d->dep_list));

  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (ex_d->fct_list));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));


  gtk_entry_set_text (GTK_ENTRY (ex_d->id_entry), "");

  ex_d->opts = OPT_LISTWISE;
  ex_d->stats = 0x00;
}

static char *
generate_syntax (const struct examine_dialog *ed)
{
  const char *label;
  gchar *text = NULL;
  GString *str = g_string_new ("EXAMINE ");

  g_string_append (str, "\n\t/VARIABLES=");
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (ed->dep_list), 0, str);

  if ( 0  < gtk_tree_model_iter_n_children
       (gtk_tree_view_get_model (GTK_TREE_VIEW (ed->fct_list)), NULL))
    {
      g_string_append (str, "\n\tBY ");
      psppire_var_view_append_names (PSPPIRE_VAR_VIEW (ed->fct_list), 0, str);
    }

  label = gtk_entry_get_text (GTK_ENTRY (ed->id_entry));
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

/* Dialog is valid iff at least one variable has been selected */
static gboolean
dialog_state_valid (gpointer data)
{
  struct examine_dialog *ex_d = data;

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (ex_d->dep_list));

  GtkTreeIter notused;

  return gtk_tree_model_get_iter_first (vars, &notused);
}


static void
run_stats_dialog (struct examine_dialog *ed)
{
  gint response;

  gtk_toggle_button_set_active (ed->descriptives_button,
				ed->stats & STAT_DESCRIPTIVES);

  gtk_toggle_button_set_active (ed->extremes_button,
				ed->stats & STAT_EXTREMES);

  gtk_toggle_button_set_active (ed->percentiles_button,
				ed->stats & STAT_PERCENTILES);

  response = psppire_dialog_run (PSPPIRE_DIALOG (ed->stats_dialog));

  if ( response == PSPPIRE_RESPONSE_CONTINUE )
    {
      ed->stats = 0;
      if ( gtk_toggle_button_get_active (ed->descriptives_button) )
	ed->stats |= STAT_DESCRIPTIVES;

      if ( gtk_toggle_button_get_active (ed->extremes_button) )
	ed->stats |= STAT_EXTREMES;

      if ( gtk_toggle_button_get_active (ed->percentiles_button) )
	ed->stats |= STAT_PERCENTILES;
    }
}

static void
run_opts_dialog (struct examine_dialog *ed)
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



/* Pops up the Examine dialog box */
void
examine_dialog (PsppireDataWindow *de)
{
  gint response;

  struct examine_dialog ex_d;

  GtkBuilder *xml = builder_new ("examine.ui");

  GtkWidget *dialog = get_widget_assert   (xml, "examine-dialog");
  GtkWidget *source = get_widget_assert   (xml, "treeview1");

  GtkWidget *stats_button = get_widget_assert   (xml, "stats-button");
  GtkWidget *opts_button = get_widget_assert   (xml, "opts-button");


  GtkWidget *dep_selector = get_widget_assert (xml, "psppire-selector1");

  PsppireVarStore *vs = NULL;

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  ex_d.dep_list =   get_widget_assert   (xml, "treeview2");
  ex_d.fct_list =   get_widget_assert   (xml, "treeview3");
  ex_d.id_entry =   get_widget_assert   (xml, "entry1");
  ex_d.stats_dialog =   get_widget_assert   (xml, "statistics-dialog");
  ex_d.opts_dialog =   get_widget_assert   (xml, "options-dialog");
  ex_d.listwise = get_widget_assert (xml, "radiobutton1");
  ex_d.pairwise = get_widget_assert (xml, "radiobutton2");
  ex_d.report   = get_widget_assert (xml, "radiobutton3");

  ex_d.descriptives_button = GTK_TOGGLE_BUTTON
    (get_widget_assert (xml, "descriptives-button"));

  ex_d.extremes_button    = GTK_TOGGLE_BUTTON
    (get_widget_assert (xml, "extremes-button"));

  ex_d.percentiles_button = GTK_TOGGLE_BUTTON
    (get_widget_assert (xml, "percentiles-button"));

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));
  gtk_window_set_transient_for (GTK_WINDOW (ex_d.stats_dialog), GTK_WINDOW (de));
  gtk_window_set_transient_for (GTK_WINDOW (ex_d.opts_dialog), GTK_WINDOW (de));

  g_object_get (vs, "dictionary", &ex_d.dict, NULL);
  g_object_set (source, "model", ex_d.dict, NULL);

  psppire_selector_set_allow (PSPPIRE_SELECTOR (dep_selector),
			      numeric_only);

  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &ex_d);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &ex_d);


  g_signal_connect_swapped (stats_button, "clicked",
		    G_CALLBACK (run_stats_dialog), &ex_d);

  g_signal_connect_swapped (opts_button, "clicked",
		    G_CALLBACK (run_opts_dialog), &ex_d);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (de, generate_syntax (&ex_d)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (&ex_d)));
      break;
    default:
      break;
    }

  g_object_unref (xml);
}
