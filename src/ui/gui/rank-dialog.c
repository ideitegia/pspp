/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007  Free Software Foundation

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

#include "rank-dialog.h"

#include <gtk/gtk.h>
#include <stdlib.h>

#include <language/syntax-string-source.h>
#include <ui/gui/psppire-data-window.h>
#include <ui/gui/dialog-common.h>
#include <ui/gui/dict-display.h>
#include <ui/gui/helper.h>
#include <ui/gui/psppire-dialog.h>
#include <ui/gui/psppire-var-store.h>
#include <ui/gui/psppire-syntax-window.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


enum RANK_FUNC
  {
    RANK,
    NORMAL,
    PERCENT,
    RFRACTION,
    PROPORTION,
    N,
    NTILES,
    SAVAGE,
    n_RANK_FUNCS
  };




struct rank_dialog
{
  PsppireDict *dict;
  GtkWidget *rank_vars;
  GtkWidget *group_vars;
  GtkWidget *dialog;

  GtkToggleButton *ascending_togglebutton;
  GtkToggleButton *summary_togglebutton;


  /* Types subdialog widgets */

  GtkWidget *types_dialog;
  GtkWidget *ntiles_entry;

  GtkToggleButton *func_button[n_RANK_FUNCS];
  GtkWidget *formula_box;

  GtkToggleButton *blom;
  GtkToggleButton *tukey;
  GtkToggleButton *rankit;
  GtkToggleButton *vw;

  /* Ties subdialog widgets */

  PsppireDialog *ties_dialog;
  GtkToggleButton *mean;
  GtkToggleButton *low;
  GtkToggleButton *high;
  GtkToggleButton *condense;
};

static void
refresh (PsppireDialog *dialog, struct rank_dialog *rd)
{
  GtkTreeModel *liststore;

  liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->rank_vars));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->group_vars));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_toggle_button_set_active (rd->ascending_togglebutton, TRUE);
  gtk_toggle_button_set_active (rd->summary_togglebutton, FALSE);
}

static char *
generate_syntax (const struct rank_dialog *rd)
{
  gchar *text;

  GtkTreeModel *gs = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->group_vars));

  GtkTreeIter notused;

  GString *str = g_string_new ("RANK VARIABLES=");

  append_variable_names (str, rd->dict, GTK_TREE_VIEW (rd->rank_vars), 0);

  g_string_append_printf (str, " (%c)",
		   gtk_toggle_button_get_active (rd->ascending_togglebutton)
		   ?'A':'D');

  if (  gtk_tree_model_get_iter_first (gs, &notused) )
    {
      g_string_append (str, "\n\tBY ");

      append_variable_names (str, rd->dict, GTK_TREE_VIEW (rd->group_vars), 0);
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

/* Dialog is valid iff at least one variable has been selected */
static gboolean
dialog_state_valid (gpointer data)
{
  struct rank_dialog *rd = data;

  GtkTreeModel *vars = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->rank_vars));

  GtkTreeIter notused;

  return gtk_tree_model_get_iter_first (vars, &notused);
}

static void on_ntiles_toggle (GtkToggleButton *, gpointer);
static void run_types_dialog (GtkButton *, gpointer);
static void run_ties_dialog (GtkButton *, gpointer );

static void
set_sensitivity (struct rank_dialog *rd)
{
  gboolean sens = gtk_toggle_button_get_active
    (GTK_TOGGLE_BUTTON (rd->func_button[PROPORTION]))
    ||
    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->func_button[NORMAL]));

  gtk_widget_set_sensitive (rd->formula_box, sens);
}

/* Pops up the Rank dialog box */
void
rank_dialog (GObject *o, gpointer data)
{
  gint response;
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);

  struct rank_dialog rd;

  GladeXML *xml = XML_NEW ("rank.glade");

  GtkWidget *vars = get_widget_assert   (xml, "dict-treeview");
  GtkWidget *selector1 = get_widget_assert (xml, "psppire-selector1");
  GtkWidget *selector2 = get_widget_assert (xml, "psppire-selector2");


  GtkWidget *types_button = get_widget_assert (xml, "button1");
  GtkWidget *ties_button = get_widget_assert (xml, "button2");

  PsppireVarStore *vs = NULL;

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  rd.dict = vs->dict;
  rd.rank_vars =   get_widget_assert (xml, "variables-treeview");
  rd.group_vars =  get_widget_assert (xml, "group-vars-treeview");
  rd.dialog = get_widget_assert   (xml, "rank-dialog");
  rd.ascending_togglebutton =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "radiobutton1"));

  rd.summary_togglebutton =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "summary-checkbutton"));

  rd.types_dialog = get_widget_assert (xml, "rank-types-dialog");


  rd.ntiles_entry  = get_widget_assert (xml, "ntiles-entry");

  rd.func_button[RANK]    =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "rank-checkbutton"));

  rd.func_button[SAVAGE]  =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "savage-checkbutton"));

  rd.func_button[RFRACTION] =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "rfrac-checkbutton"));

  rd.func_button[PERCENT] =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "percent-checkbutton"));

  rd.func_button[N]       =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "sum-checkbutton"));

  rd.func_button[NTILES] =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "ntiles-checkbutton"));

  rd.func_button[PROPORTION] =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "prop-checkbutton"));

  rd.func_button[NORMAL] =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "normal-checkbutton"));

  rd.formula_box = get_widget_assert (xml, "formula-frame");

  rd.blom = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "blom-button"));
  rd.tukey = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "tukey-button"));
  rd.rankit = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "rankit-button"));
  rd.vw = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "vw-button"));

  /* Ties dialog */
  rd.ties_dialog = PSPPIRE_DIALOG (get_widget_assert (xml, "ties-dialog"));

  rd.mean = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "mean-button"));
  rd.low = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "low-button"));
  rd.high = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "high-button"));
  rd.condense = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "condense-button"));

  g_signal_connect_swapped (rd.func_button[PROPORTION], "toggled",
			    G_CALLBACK (set_sensitivity),
			    &rd);

  g_signal_connect_swapped (rd.func_button[NORMAL], "toggled",
			    G_CALLBACK (set_sensitivity),
			    &rd);

  g_signal_connect (rd.func_button[NTILES], "toggled",
		    G_CALLBACK (on_ntiles_toggle),
		    rd.ntiles_entry);

  gtk_window_set_transient_for (GTK_WINDOW (rd.dialog), GTK_WINDOW (de));

  attach_dictionary_to_treeview (GTK_TREE_VIEW (vars),
				 vs->dict,
				 GTK_SELECTION_MULTIPLE, NULL);


  set_dest_model (GTK_TREE_VIEW (rd.rank_vars), vs->dict);

  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector1),
				 vars,
				 rd.rank_vars,
				 insert_source_row_into_tree_view,
				 NULL,
				 NULL);

  set_dest_model (GTK_TREE_VIEW (rd.group_vars), vs->dict);

  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector2),
				 vars,
				 rd.group_vars,
				 insert_source_row_into_tree_view,
				 NULL,
				 NULL);


  g_signal_connect (types_button, "clicked",
		    G_CALLBACK (run_types_dialog),  &rd);

  g_signal_connect (ties_button, "clicked",
		    G_CALLBACK (run_ties_dialog),  &rd);

  g_signal_connect (rd.dialog, "refresh", G_CALLBACK (refresh),  &rd);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (rd.dialog),
				      dialog_state_valid, &rd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (rd.dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (&rd);
	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (&rd);

        GtkWidget *se = psppire_syntax_window_new ();

	gtk_text_buffer_insert_at_cursor (PSPPIRE_SYNTAX_WINDOW (se)->buffer, syntax, -1);

	gtk_widget_show (se);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (xml);
}


static void
types_dialog_reset (struct rank_dialog *rd)
{
  gint i;

  for (i = 0 ; i < n_RANK_FUNCS ; ++i )
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->func_button [i]),
				  FALSE);

  gtk_widget_set_sensitive (rd->ntiles_entry, FALSE);

  gtk_widget_set_sensitive (rd->formula_box, FALSE);
}



static void
run_types_dialog (GtkButton *b, gpointer data)
{
  struct rank_dialog *rd = data;
  gint response;

  gtk_window_set_transient_for (GTK_WINDOW (rd->types_dialog),
				GTK_WINDOW (rd->dialog));

  types_dialog_reset (rd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (rd->types_dialog));
}

static void
run_ties_dialog (GtkButton *b, gpointer data)
{
  struct rank_dialog *rd = data;
  gint response;

  gtk_window_set_transient_for (GTK_WINDOW (rd->ties_dialog),
				GTK_WINDOW (rd->dialog));


  response = psppire_dialog_run (PSPPIRE_DIALOG (rd->ties_dialog));
}


static void
on_ntiles_toggle (GtkToggleButton *toggle_button, gpointer data)
{
  GtkWidget *w = data;
  gboolean active = gtk_toggle_button_get_active (toggle_button);
  gtk_widget_set_sensitive (w, active);
}
