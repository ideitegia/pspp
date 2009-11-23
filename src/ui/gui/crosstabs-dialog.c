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
#include "crosstabs-dialog.h"
#include "psppire-var-view.h"

#include <gtk/gtk.h>
#include <stdlib.h>

#include <language/syntax-string-source.h>
#include <ui/gui/psppire-data-window.h>
#include <ui/gui/dialog-common.h>
#include <ui/gui/dict-display.h>
#include "executor.h"
#include <ui/gui/psppire-dialog.h>
#include <ui/gui/psppire-var-store.h>
#include <ui/gui/helper.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


#define CROSSTABS_STATS                       \
  CS (CHISQ, N_("Chisq"))                         \
  CS (PHI, N_("Phi"))         \
  CS (CC, N_("CC"))                   \
  CS (LAMBDA, N_("Lambda"))                   \
  CS (UC, N_("UC")) \
  CS (BTAU, N_("BTau"))                 \
  CS (CTAU, N_("CTau"))                 \
  CS (RISK, N_("Risk"))  \
  CS (GAMMA, N_("Gamma"))                       \
  CS (D, N_("D"))                         \
  CS (KAPPA, N_("Kappa"))                 \
  CS (ETA, N_("Eta"))  \
  CS (CORR, N_("Corr")) \
  CS (STATS_NONE, N_("None"))

#define CROSSTABS_CELLS \
  CS (COUNT, N_("Count"))   \
  CS (ROW, N_("Row"))   \
  CS (COLUMN, N_("Column"))   \
  CS (TOTAL, N_("Total"))   \
  CS (EXPECTED, N_("Expected"))   \
  CS (RESIDUAL, N_("Residual"))   \
  CS (SRESIDUAL, N_("Std. Residual"))   \
  CS (ASRESIDUAL, N_("Adjusted Std. Residual"))   \
  CS (CELLS_NONE, N_("None"))

enum
  {
#define CS(NAME, LABEL) CS_##NAME,
    CROSSTABS_STATS
#undef CS
    N_CROSSTABS_STATS
  };

enum
  {
#define CS(NAME, LABEL) CS_##NAME,
    CROSSTABS_CELLS
#undef CS
    N_CROSSTABS_CELLS
  };

enum
  {
#define CS(NAME, LABEL) B_CS_##NAME = 1u << CS_##NAME,
    CROSSTABS_STATS
    CROSSTABS_CELLS
#undef CS
    B_CS_STATS_ALL = (1u << N_CROSSTABS_STATS) - 1,
    B_CS_CELLS_ALL = (1u << N_CROSSTABS_CELLS) - 1,
    B_CS_STATS_DEFAULT = B_CS_CHISQ,
    B_CS_CELL_DEFAULT = B_CS_COUNT | B_CS_ROW | B_CS_COLUMN | B_CS_TOTAL,
    B_CS_NONE
  };

static const struct checkbox_entry_item stats[] =
  {
#define CS(NAME, LABEL) {#NAME, LABEL},
    CROSSTABS_STATS \
    CS(NONE, N_("None"))
#undef CS
  };

static const struct checkbox_entry_item cells[] =
  {
#define CS(NAME, LABEL) {#NAME, LABEL},
    CROSSTABS_CELLS \
    CS(NONE, N_("None"))
#undef CS
  };

enum
  {
    LABEL,
    NO_LABEL,
    NO_VAL_LABEL,
  };
struct format_options
{
  gboolean avalue;
  gboolean pivot;
  gboolean table;
};

struct crosstabs_dialog
{
  GtkTreeView *row_vars;
  GtkTreeView *col_vars;
  PsppireDict *dict;

  GtkToggleButton *table_button;
  GtkToggleButton *pivot_button;

  GtkWidget *format_dialog;
  GtkWidget *cell_dialog;
  GtkWidget *stat_dialog;

  GtkToggleButton  *avalue;
  GtkTreeModel *stat;
  GtkTreeModel *cell;

  GtkWidget *stat_view;
  GtkWidget *cell_view;
  GtkToggleButton *label;
  GtkToggleButton *no_label;
  GtkToggleButton *no_val_label;
  struct format_options current_opts;
};

static void
refresh (PsppireDialog *dialog, struct crosstabs_dialog *cd)
{
  GtkTreeModel *liststore = gtk_tree_view_get_model (cd->row_vars);
  gtk_list_store_clear (GTK_LIST_STORE (liststore));
  
  liststore = gtk_tree_view_get_model (cd->col_vars);
  gtk_list_store_clear (GTK_LIST_STORE (liststore));
}
static void
on_format_clicked (struct crosstabs_dialog *cd)
{
  int ret;
  gboolean lab;
  gboolean no_lab;
  gboolean no_val_lab;

  if (cd->current_opts.avalue)
    {
      gtk_toggle_button_set_active (cd->avalue, TRUE);
    }
  if (cd->current_opts.table)
    {
      gtk_toggle_button_set_active (cd->table_button, TRUE);
    }
  if (cd->current_opts.pivot)
    {
      gtk_toggle_button_set_active (cd->pivot_button, TRUE);
    }
  lab = gtk_toggle_button_get_active (cd->label);
  no_lab = gtk_toggle_button_get_active (cd->no_label);
  no_val_lab = gtk_toggle_button_get_active (cd->no_val_label);
  if (!lab)
    if (!no_lab)
      if (!no_val_lab)
	gtk_toggle_button_set_active (cd->label, TRUE);


  ret = psppire_dialog_run (PSPPIRE_DIALOG (cd->format_dialog));

  if ( ret == PSPPIRE_RESPONSE_CONTINUE )
    {
      cd->current_opts.avalue = (gtk_toggle_button_get_active (cd->avalue) == TRUE ) 
	? TRUE : FALSE;
      cd->current_opts.table = (gtk_toggle_button_get_active (cd->table_button) == TRUE)
	? TRUE : FALSE;
      cd->current_opts.pivot = (gtk_toggle_button_get_active (cd->pivot_button) == TRUE)
	? TRUE : FALSE;
    }
  else
    {
      gtk_toggle_button_set_active (cd->label, lab);
      gtk_toggle_button_set_active (cd->no_label, no_lab);
      gtk_toggle_button_set_active (cd->no_val_label, no_val_lab);
    }
}

static void
on_statistics_clicked (struct crosstabs_dialog *cd)
{
  GtkListStore *liststore;
  int ret;

  liststore = clone_list_store (GTK_LIST_STORE (cd->stat));

  ret = psppire_dialog_run (PSPPIRE_DIALOG (cd->stat_dialog));

  if ( ret == PSPPIRE_RESPONSE_CONTINUE )
    {
      g_object_unref (liststore);
    }
  else
    {
      g_object_unref (cd->stat);
      gtk_tree_view_set_model (GTK_TREE_VIEW (cd->stat_view) , GTK_TREE_MODEL (liststore));
      cd->stat = GTK_TREE_MODEL (liststore);
    }
}
static void
on_cell_clicked (struct crosstabs_dialog *cd)
{
  GtkListStore *liststore;
  int ret;

  liststore = clone_list_store (GTK_LIST_STORE (cd->cell));

  ret = psppire_dialog_run (PSPPIRE_DIALOG (cd->cell_dialog));

  if ( ret == PSPPIRE_RESPONSE_CONTINUE )
    {
      g_object_unref (liststore);
    }
  else
    {
      g_object_unref (cd->cell);
      gtk_tree_view_set_model (GTK_TREE_VIEW (cd->cell_view) , GTK_TREE_MODEL (liststore));
      cd->cell = GTK_TREE_MODEL (liststore);
    }
}

static char *
generate_syntax (const struct crosstabs_dialog *cd)
{
  gint i;
  int n;
  guint selected;
  GtkTreeIter iter;
  gboolean ok;

  gchar *text;
  GString *string = g_string_new ("CROSSTABS");

  g_string_append (string, "\n\t/TABLES=");
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (cd->row_vars), 0, string);
  g_string_append (string, "\tBY\t");
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (cd->col_vars), 0, string);

  g_string_append (string, "\n\t/FORMAT=");

  if (cd->current_opts.avalue)
    {
      g_string_append (string, "AVALUE");
    }
  else 
    {
      g_string_append (string, "DVALUE");
    }
  g_string_append (string, " ");
  if (gtk_toggle_button_get_active (cd->label))
    {
      g_string_append (string, "LABELS");
    }
  else if (gtk_toggle_button_get_active (cd->no_label))
    {
      g_string_append (string, "NOLABELS");
    }
  else if (gtk_toggle_button_get_active (cd->no_val_label))
    {
      g_string_append (string, "NOVALLABS");
    }
  g_string_append (string, " ");
  if (cd->current_opts.table)
    g_string_append (string, "TABLES");
  else
    g_string_append (string, "NOTABLES");
  g_string_append (string, " ");

  if (cd->current_opts.pivot)
    g_string_append (string, "PIVOT");
  else 
    g_string_append (string, "NOPIVOT");

  selected = 0;
  for (i = 0, ok = gtk_tree_model_get_iter_first (cd->stat, &iter); ok; 
       i++, ok = gtk_tree_model_iter_next (cd->stat, &iter))
    {
      gboolean toggled;
      gtk_tree_model_get (cd->stat, &iter,
			  CHECKBOX_COLUMN_SELECTED, &toggled, -1); 
      if (toggled) 
	selected |= 1u << i; 
      else 
	selected &= ~(1u << i);
    }

  if (!(selected & (1u << CS_STATS_NONE)))
    {
      if (selected)
	{
	  g_string_append (string, "\n\t/STATISTICS=");
	  n = 0;
	  for (i = 0; i < N_CROSSTABS_STATS; i++)
	    if (selected & (1u << i))
	      {
		if (n++)
		  g_string_append (string, " ");
		g_string_append (string, stats[i].name);
	      }
	}
    }

  selected = 0;
  for (i = 0, ok = gtk_tree_model_get_iter_first (cd->cell, &iter); ok; 
       i++, ok = gtk_tree_model_iter_next (cd->cell, &iter))
    {
      gboolean toggled;
      gtk_tree_model_get (cd->cell, &iter,
			  CHECKBOX_COLUMN_SELECTED, &toggled, -1); 
      if (toggled) 
	selected |= 1u << i; 
      else 
	selected &= ~(1u << i);
    }

  g_string_append (string, "\n\t/CELLS=");
  if (selected & (1u << CS_CELLS_NONE))
    g_string_append (string, "NONE");
  else
    {
      n = 0;
      for (i = 0; i < N_CROSSTABS_CELLS; i++)
	if (selected & (1u << i))
	  {
	    if (n++)
	      g_string_append (string, " ");
	    g_string_append (string, cells[i].name);
	  }
    }
  
  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}

/* Dialog is valid iff at least one row and one column variable has
   been selected. */
static gboolean
dialog_state_valid (gpointer data)
{
  struct crosstabs_dialog *cd = data;

  GtkTreeModel *row_vars = gtk_tree_view_get_model (cd->row_vars);
  GtkTreeModel *col_vars = gtk_tree_view_get_model (cd->col_vars);

  GtkTreeIter notused;

  return (gtk_tree_model_get_iter_first (row_vars, &notused) 
    && gtk_tree_model_get_iter_first (col_vars, &notused));
}

/* Pops up the Crosstabs dialog box */
void
crosstabs_dialog (GObject *o, gpointer data)
{
  gint response;
  struct crosstabs_dialog cd;

  GtkBuilder *xml = builder_new ("crosstabs.ui");
  PsppireVarStore *vs = NULL;
  PsppireDict *dict = NULL;

  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);


  GtkWidget *dialog = get_widget_assert   (xml, "crosstabs-dialog");
  GtkWidget *source = get_widget_assert   (xml, "dict-treeview");
  GtkWidget *dest_rows =   get_widget_assert   (xml, "rows");
  GtkWidget *dest_cols =   get_widget_assert   (xml, "cols");
  GtkWidget *format_button = get_widget_assert (xml, "format-button");
  GtkWidget *stat_button = get_widget_assert (xml, "stats-button");
  GtkWidget *cell_button = get_widget_assert (xml, "cell-button");


  cd.stat_view = get_widget_assert (xml, "stats-view");
  cd.cell_view = get_widget_assert (xml, "cell-view");

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  put_checkbox_items_in_treeview (GTK_TREE_VIEW(cd.stat_view),
				  B_CS_STATS_DEFAULT,
				  N_CROSSTABS_STATS,
				  stats
				  );
  put_checkbox_items_in_treeview (GTK_TREE_VIEW(cd.cell_view),
				  B_CS_CELL_DEFAULT,
				  N_CROSSTABS_CELLS,
				  cells
				  );

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  g_object_get (vs, "dictionary", &dict, NULL);
  g_object_set (source, "model", dict, NULL);

  cd.row_vars = GTK_TREE_VIEW (dest_rows);
  cd.col_vars = GTK_TREE_VIEW (dest_cols);
  g_object_get (vs, "dictionary", &cd.dict, NULL);
  cd.format_dialog = get_widget_assert (xml, "format-dialog");
  cd.table_button = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "print-tables"));
  cd.pivot_button = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "pivot"));
  cd.label = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "radiobutton1"));
  cd.no_label = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "radiobutton2"));
  cd.no_val_label = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "radiobutton3"));
  cd.stat_dialog = get_widget_assert (xml, "stat-dialog");
  cd.cell_dialog = get_widget_assert (xml, "cell-dialog");

  cd.stat = gtk_tree_view_get_model (GTK_TREE_VIEW (cd.stat_view));
  cd.cell = gtk_tree_view_get_model (GTK_TREE_VIEW (cd.cell_view));
  cd.avalue = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "ascending"));
  cd.current_opts.avalue = TRUE;
  cd.current_opts.table = TRUE;
  cd.current_opts.pivot = TRUE;

  gtk_window_set_transient_for (GTK_WINDOW (cd.format_dialog), GTK_WINDOW (de));
  gtk_window_set_transient_for (GTK_WINDOW (cd.cell_dialog), GTK_WINDOW (de));
  gtk_window_set_transient_for (GTK_WINDOW (cd.stat_dialog), GTK_WINDOW (de));

  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &cd);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &cd);

  g_signal_connect_swapped (format_button, "clicked",
			    G_CALLBACK (on_format_clicked),  &cd);
  g_signal_connect_swapped (stat_button, "clicked",
			    G_CALLBACK (on_statistics_clicked),  &cd);
  g_signal_connect_swapped (cell_button, "clicked",
			    G_CALLBACK (on_cell_clicked),  &cd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (&cd);

	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (&cd);

	paste_syntax_in_new_window (syntax);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (xml);
}
