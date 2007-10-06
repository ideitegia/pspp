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

#include "descriptives-dialog.h"

#include <gtk/gtk.h>
#include <gtksheet/gtksheet.h>
#include <stdlib.h>

#include <language/syntax-string-source.h>
#include <ui/gui/data-editor.h>
#include <ui/gui/dialog-common.h>
#include <ui/gui/dict-display.h>
#include <ui/gui/helper.h>
#include <ui/gui/psppire-dialog.h>
#include <ui/gui/psppire-var-store.h>
#include <ui/gui/syntax-editor.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum
  {
    COLUMN_LABEL,
    COLUMN_SELECTED,
    N_STAT_COLUMNS
  };

struct descriptive_stat
  {
    const char *name;
    const char *label;
  };

#define DESCRIPTIVE_STATS                       \
  DS (MEAN, N_("Mean"))                         \
  DS (STDDEV, N_("Standard deviation"))         \
  DS (MINIMUM, N_("Minimum"))                   \
  DS (MAXIMUM, N_("Maximum"))                   \
  DS (RANGE, N_("Range"))                       \
  DS (SUM, N_("Sum"))                           \
  DS (SEMEAN, N_("Standard error"))             \
  DS (VARIANCE, N_("Variance"))                 \
  DS (KURTOSIS, N_("Kurtosis"))                 \
  DS (SKEWNESS, N_("Skewness"))

enum
  {
#define DS(NAME, LABEL) DS_##NAME,
    DESCRIPTIVE_STATS
#undef DS
    N_DESCRIPTIVE_STATS
  };

enum
  {
#define DS(NAME, LABEL) B_DS_##NAME = 1u << DS_##NAME,
    DESCRIPTIVE_STATS
#undef DS
    B_DS_ALL = (1u << N_DESCRIPTIVE_STATS) - 1,
    B_DS_DEFAULT = B_DS_MEAN | B_DS_STDDEV | B_DS_MINIMUM | B_DS_MAXIMUM
  };

static const struct descriptive_stat stats[] =
  {
#define DS(NAME, LABEL) {#NAME, LABEL},
    DESCRIPTIVE_STATS
#undef DS
  };

struct descriptives_dialog
{
  GtkTreeView *stat_vars;
  GtkTreeModel *stats;
  PsppireDict *dict;
  GtkToggleButton *exclude_missing_listwise;
  GtkToggleButton *include_user_missing;
  GtkToggleButton *save_z_scores;
};

static void
refresh (PsppireDialog *dialog, struct descriptives_dialog *scd)
{
  GtkTreeModel *liststore;
  GtkTreeIter iter;
  size_t i;
  bool ok;

  liststore = gtk_tree_view_get_model (scd->stat_vars);
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  for (i = 0, ok = gtk_tree_model_get_iter_first (scd->stats, &iter); ok;
       i++, ok = gtk_tree_model_iter_next (scd->stats, &iter))
    gtk_list_store_set (GTK_LIST_STORE (scd->stats), &iter, COLUMN_SELECTED,
                        (B_DS_DEFAULT & (1u << i)) ? true : false, -1);

  gtk_toggle_button_set_active (scd->exclude_missing_listwise, false);
  gtk_toggle_button_set_active (scd->include_user_missing, false);
  gtk_toggle_button_set_active (scd->save_z_scores, false);
}

static char *
generate_syntax (const struct descriptives_dialog *scd)
{
  gchar *text;
  GString *string;
  GtkTreeIter iter;
  unsigned int selected;
  size_t i;
  bool listwise, include;
  bool ok;

  string = g_string_new ("DESCRIPTIVES");
  g_string_append (string, "\n    /VARIABLES=");
  append_variable_names (string, scd->dict, GTK_TREE_VIEW (scd->stat_vars));

  listwise = gtk_toggle_button_get_active (scd->exclude_missing_listwise);
  include = gtk_toggle_button_get_active (scd->include_user_missing);
  if (listwise || include)
    {
      g_string_append (string, "\n    /MISSING=");
      if (listwise)
        {
          g_string_append (string, "LISTWISE");
          if (include)
            g_string_append (string, " ");
        }
      if (include)
        g_string_append (string, "INCLUDE");
    }

  selected = 0;
  for (i = 0, ok = gtk_tree_model_get_iter_first (scd->stats, &iter); ok;
       i++, ok = gtk_tree_model_iter_next (scd->stats, &iter))
    {
      gboolean toggled;
      gtk_tree_model_get (scd->stats, &iter, COLUMN_SELECTED, &toggled, -1);
      if (toggled)
        selected |= 1u << i;
    }

  if (selected != B_DS_DEFAULT)
    {
      g_string_append (string, "\n    /STATISTICS=");
      if (selected == B_DS_ALL)
        g_string_append (string, "ALL");
      else if (selected == 0)
        g_string_append (string, "NONE");
      else
        {
          int n = 0;
          if ((selected & B_DS_DEFAULT) == B_DS_DEFAULT)
            {
              g_string_append (string, "DEFAULT");
              selected &= ~B_DS_DEFAULT;
              n++;
            }
          for (i = 0; i < N_DESCRIPTIVE_STATS; i++)
            if (selected & (1u << i))
              {
                if (n++)
                  g_string_append (string, " ");
                g_string_append (string, stats[i].name);
              }
        }
    }

  if (gtk_toggle_button_get_active (scd->save_z_scores))
    g_string_append (string, "\n    /SAVE");

  g_string_append (string, ".");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}

/* A GtkTreeCellDataFunc which renders a checkbox that determines
   whether to calculate the statistic. */
static void
stat_calculate_cell_data_func (GtkTreeViewColumn *col,
                               GtkCellRenderer *cell,
                               GtkTreeModel *model,
                               GtkTreeIter *iter,
                               gpointer data)
{
  gboolean selected;

  gtk_tree_model_get (model, iter, COLUMN_SELECTED, &selected, -1);
  g_object_set (cell, "active", selected, NULL);
}

/* Callback for checkbox cells in the statistics tree view.
   Toggles the checkbox. */
static void
toggle (GtkCellRendererToggle *cell_renderer, gchar *path_str, gpointer data)
{
  GtkTreeModel *model = (GtkTreeModel *)data;
  GtkTreeIter iter;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
  gboolean selected;

  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter, COLUMN_SELECTED, &selected, -1);
  gtk_list_store_set (GTK_LIST_STORE (model), &iter, COLUMN_SELECTED,
                      !selected, -1);
  gtk_tree_path_free (path);
}

/* A GtkTreeCellDataFunc which renders the label of the statistic. */
static void
stat_label_cell_data_func (GtkTreeViewColumn *col,
                           GtkCellRenderer *cell,
                           GtkTreeModel *model,
                           GtkTreeIter *iter,
                           gpointer statistic)
{
  gchar *label = NULL;
  gtk_tree_model_get (model, iter, COLUMN_LABEL, &label, -1);
  g_object_set (cell, "text", gettext (label), NULL);
  g_free (label);
}

static void
put_statistics_in_treeview (GtkTreeView *treeview)
{
  GtkTreeViewColumn *col;
  GtkCellRenderer *renderer;
  GtkListStore *list;
  size_t i;

  list = gtk_list_store_new (N_STAT_COLUMNS, G_TYPE_STRING, G_TYPE_BOOLEAN);
  gtk_tree_view_set_model (GTK_TREE_VIEW (treeview), GTK_TREE_MODEL (list));

  for (i = 0; i < sizeof stats / sizeof *stats; i++)
    {
      GtkTreeIter iter;
      gtk_list_store_append (list, &iter);
      gtk_list_store_set (list, &iter,
                          COLUMN_LABEL, stats[i].label,
                          COLUMN_SELECTED, (B_DS_DEFAULT & (1u << i)) != 0,
                          -1);
    }

  /* Calculate column. */
  col = gtk_tree_view_column_new ();
  renderer = gtk_cell_renderer_toggle_new ();
  g_signal_connect (GTK_CELL_RENDERER_TOGGLE (renderer),
                    "toggled", G_CALLBACK (toggle), GTK_TREE_MODEL (list));
  gtk_tree_view_column_pack_start (col, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (col, renderer,
					   stat_calculate_cell_data_func,
					   NULL, NULL);
  gtk_tree_view_append_column (treeview, col);

  /* Statistic column. */
  col = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (col, _("Statistic"));
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (col, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (col, renderer,
					   stat_label_cell_data_func,
					   NULL, NULL);
  g_object_set (renderer, "ellipsize-set", TRUE, NULL);
  g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  gtk_tree_view_column_set_min_width (col, 150);
  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_resizable (col, true);
  gtk_tree_view_append_column (treeview, col);
}

/* Pops up the Descriptives dialog box */
void
descriptives_dialog (GObject *o, gpointer data)
{
  gint response;
  struct data_editor *de = data;

  struct descriptives_dialog scd;

  GladeXML *xml = XML_NEW ("descriptives-dialog.glade");

  GtkWidget *dialog = get_widget_assert   (xml, "descriptives-dialog");


  GtkWidget *source = get_widget_assert   (xml, "all-variables");
  GtkWidget *selector = get_widget_assert (xml, "stat-var-selector");
  GtkWidget *dest =   get_widget_assert   (xml, "stat-variables");

  GtkWidget *stats = get_widget_assert    (xml, "statistics");

  GtkSheet *var_sheet =
    GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

  PsppireVarStore *vs = PSPPIRE_VAR_STORE (gtk_sheet_get_model (var_sheet));

  gtk_window_set_transient_for (GTK_WINDOW (dialog), de->parent.window);

  attach_dictionary_to_treeview (GTK_TREE_VIEW (source),
				 vs->dict,
				 GTK_SELECTION_MULTIPLE, var_is_numeric);

  set_dest_model (GTK_TREE_VIEW (dest), vs->dict);

  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector),
				 source,
				 dest,
				 insert_source_row_into_tree_view,
				 NULL);

  put_statistics_in_treeview (GTK_TREE_VIEW (stats));

  scd.stat_vars = GTK_TREE_VIEW (dest);
  scd.stats = gtk_tree_view_get_model (GTK_TREE_VIEW (stats));
  scd.dict = vs->dict;
  scd.include_user_missing =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "include_user_missing"));
  scd.exclude_missing_listwise =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "exclude_missing_listwise"));
  scd.save_z_scores =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "save_z_scores"));

  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &scd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (&scd);
	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (&scd);

	struct syntax_editor *se =
	  (struct syntax_editor *) window_create (WINDOW_SYNTAX, NULL);

	gtk_text_buffer_insert_at_cursor (se->buffer, syntax, -1);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (xml);
}
