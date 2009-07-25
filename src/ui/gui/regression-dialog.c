/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008  Free Software Foundation

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

#include "checkbox-treeview.h"
#include "regression-dialog.h"
#include "executor.h"

#include <gtk/gtk.h>
#include <stdlib.h>

#include <language/syntax-string-source.h>
#include <ui/gui/psppire-data-window.h>
#include <ui/gui/dialog-common.h>
#include <ui/gui/dict-display.h>
#include <ui/gui/helper.h>
#include <ui/gui/psppire-dialog.h>
#include <ui/gui/psppire-var-store.h>


#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


#define REGRESSION_STATS                       \
  RG (COEFF, N_("Coeff"))                         \
  RG (R, N_("R"))         \
  RG (ANOVA, N_("Anova"))                   \
  RG (BCOV, N_("Bcov"))
enum
  {
#define RG(NAME, LABEL) RG_##NAME,
    REGRESSION_STATS
#undef RG
    N_REGRESSION_STATS
  };

enum
  {
#define RG(NAME, LABEL) B_RG_##NAME = 1u << RG_##NAME,
    REGRESSION_STATS
#undef RG
    B_RG_STATS_ALL = (1u << N_REGRESSION_STATS) - 1,
    B_RG_STATS_DEFAULT = B_RG_ANOVA | B_RG_COEFF | B_RG_R
  };

static const struct checkbox_entry_item stats[] =
  {
#define RG(NAME, LABEL) {#NAME, LABEL},
    REGRESSION_STATS
#undef RG
  };

struct save_options
{
  gboolean pred;
  gboolean resid;
};
struct regression_dialog
{
  GtkTreeView *dep_vars;
  GtkTreeView *indep_vars;
  PsppireDict *dict;

  GtkToggleButton *resid_button;
  GtkToggleButton *pred_button;

  GtkWidget *stat_dialog;
  GtkWidget *save_dialog;

  GtkWidget *stat_view;
  GtkTreeModel *stat;
  struct save_options current_opts;
};

static void
refresh (PsppireDialog *dialog, struct regression_dialog *rd)
{
  GtkTreeModel *liststore = gtk_tree_view_get_model (rd->dep_vars);
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  liststore = gtk_tree_view_get_model (rd->indep_vars);
  gtk_list_store_clear (GTK_LIST_STORE (liststore));
}

static void
on_statistics_clicked (struct regression_dialog *rd)
{
  GtkListStore *liststore;
  int ret;

  liststore = clone_list_store (GTK_LIST_STORE (rd->stat));

  ret = psppire_dialog_run (PSPPIRE_DIALOG (rd->stat_dialog));

  if ( ret == PSPPIRE_RESPONSE_CONTINUE )
    {
      g_object_unref (liststore);
    }
  else
    {
      g_object_unref (rd->stat);
      gtk_tree_view_set_model (GTK_TREE_VIEW (rd->stat_view) , GTK_TREE_MODEL (liststore));
      rd->stat = GTK_TREE_MODEL (liststore);
    }
}

static void
on_save_clicked (struct regression_dialog *rd)
{
  int ret;
  if (rd->current_opts.pred)
    {
      gtk_toggle_button_set_active (rd->pred_button, TRUE);
    }
  if (rd->current_opts.resid)
    {
      gtk_toggle_button_set_active (rd->resid_button, TRUE);
    }

  ret = psppire_dialog_run (PSPPIRE_DIALOG (rd->save_dialog));

  if ( ret == PSPPIRE_RESPONSE_CONTINUE )
    {
      rd->current_opts.pred = (gtk_toggle_button_get_active (rd->pred_button) == TRUE)
	? TRUE : FALSE;
      rd->current_opts.resid = (gtk_toggle_button_get_active (rd->resid_button) == TRUE)
	? TRUE : FALSE;
    }
}

static char *
generate_syntax (const struct regression_dialog *rd)
{
  gint i;
  int n;
  guint selected;
  GtkTreeIter iter;
  gboolean ok;

  gchar *text;
  GString *string = g_string_new ("REGRESSION");

  g_string_append (string, "\n\t/VARIABLES=");
  append_variable_names (string, rd->dict, GTK_TREE_VIEW (rd->indep_vars), 0);
  g_string_append (string, "\n\t/DEPENDENT=\t");
  append_variable_names (string, rd->dict, GTK_TREE_VIEW (rd->dep_vars), 0);

  selected = 0;
  for (i = 0, ok = gtk_tree_model_get_iter_first (rd->stat, &iter); ok; 
       i++, ok = gtk_tree_model_iter_next (rd->stat, &iter))
    {
      gboolean toggled;
      gtk_tree_model_get (rd->stat, &iter,
			  CHECKBOX_COLUMN_SELECTED, &toggled, -1); 
      if (toggled) 
	selected |= 1u << i; 
      else 
	selected &= ~(1u << i);
    }

  if (selected)
    {
      g_string_append (string, "\n\t/STATISTICS=");
      n = 0;
      for (i = 0; i < N_REGRESSION_STATS; i++)
	if (selected & (1u << i))
	  {
	    if (n++)
	      g_string_append (string, " ");
	    g_string_append (string, stats[i].name);
	  }
    }
  if (rd->current_opts.pred || rd->current_opts.resid)
    {
      g_string_append (string, "\n\t/SAVE=");
      if (rd->current_opts.pred)
	g_string_append (string, " PRED");
      if (rd->current_opts.resid)
	g_string_append (string, " RESID");
    }
  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}

/* Dialog is valid iff at least one dependent and one independent variable have
   been selected. */
static gboolean
dialog_state_valid (gpointer data)
{
  struct regression_dialog *rd = data;

  GtkTreeModel *dep_vars = gtk_tree_view_get_model (rd->dep_vars);
  GtkTreeModel *indep_vars = gtk_tree_view_get_model (rd->indep_vars);

  GtkTreeIter notused;

  return (gtk_tree_model_get_iter_first (dep_vars, &notused)
    && gtk_tree_model_get_iter_first (indep_vars, &notused));
}

/* Pops up the Regression dialog box */
void
regression_dialog (GObject *o, gpointer data)
{
  gint response;
  struct regression_dialog rd;

  GtkBuilder *xml = builder_new ("regression.ui");
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);
  PsppireVarStore *vs;

  GtkWidget *dialog = get_widget_assert   (xml, "regression-dialog");
  GtkWidget *source = get_widget_assert   (xml, "dict-view");
  GtkWidget *dest_dep =   get_widget_assert   (xml, "dep-view");
  GtkWidget *dest_indep =   get_widget_assert   (xml, "indep-view");
  GtkWidget *dep_selector = get_widget_assert (xml, "dep-selector");
  GtkWidget *indep_selector = get_widget_assert (xml, "indep-selector");
  GtkWidget *stat_button = get_widget_assert (xml, "stat-button");
  GtkWidget *save_button = get_widget_assert (xml, "save-button");


  rd.stat_view = get_widget_assert (xml, "stat-view");

  g_object_get (de->data_editor, "var-store", &vs, NULL);


  put_checkbox_items_in_treeview (GTK_TREE_VIEW(rd.stat_view),
				  B_RG_STATS_DEFAULT,
				  N_REGRESSION_STATS,
				  stats
				  );

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  g_object_get (vs, "dictionary", &rd.dict, NULL);
  g_object_set (source, "dictionary", rd.dict, NULL);

  set_dest_model (GTK_TREE_VIEW (dest_dep), rd.dict);
  set_dest_model (GTK_TREE_VIEW (dest_indep), rd.dict);

  psppire_selector_set_subjects (PSPPIRE_SELECTOR (dep_selector),
				 source,
				 dest_dep,
				 insert_source_row_into_tree_view,
				 NULL,
				 NULL);

  psppire_selector_set_subjects (PSPPIRE_SELECTOR (indep_selector),
				 source,
				 dest_indep,
				 insert_source_row_into_tree_view,
				 NULL,
				 NULL);

  rd.dep_vars = GTK_TREE_VIEW (dest_dep);
  rd.indep_vars = GTK_TREE_VIEW (dest_indep);


  rd.save_dialog = get_widget_assert (xml, "save-dialog");
  rd.pred_button = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "pred-button"));
  rd.resid_button = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "resid-button"));
  rd.stat_dialog = get_widget_assert (xml, "statistics-dialog");

  rd.stat = gtk_tree_view_get_model (GTK_TREE_VIEW (rd.stat_view));
  rd.current_opts.pred = FALSE;
  rd.current_opts.resid = FALSE;

  gtk_window_set_transient_for (GTK_WINDOW (rd.save_dialog), GTK_WINDOW (de));
  gtk_window_set_transient_for (GTK_WINDOW (rd.stat_dialog), GTK_WINDOW (de));

  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &rd);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &rd);

  g_signal_connect_swapped (stat_button, "clicked",
			    G_CALLBACK (on_statistics_clicked),  &rd);
  g_signal_connect_swapped (save_button, "clicked",
			    G_CALLBACK (on_save_clicked),  &rd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


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
        paste_syntax_in_new_window (syntax);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (xml);
}
