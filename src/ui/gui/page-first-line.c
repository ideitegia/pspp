/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2012, 2013  Free Software Foundation

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

#include "page-first-line.h"

#include "ui/gui/text-data-import-dialog.h"

#include <errno.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "data/data-in.h"
#include "data/data-out.h"
#include "data/format-guesser.h"
#include "data/value-labels.h"
#include "language/data-io/data-parser.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/line-reader.h"
#include "libpspp/message.h"
#include "ui/gui/checkbox-treeview.h"
#include "ui/gui/dialog-common.h"
#include "ui/gui/executor.h"
#include "ui/gui/helper.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/pspp-sheet-selection.h"
#include "ui/gui/pspp-sheet-view-column.h"
#include "ui/gui/pspp-sheet-view.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dialog.h"
#include "ui/gui/psppire-encoding-selector.h"
#include "ui/gui/psppire-empty-list-store.h"
#include "ui/gui/psppire-var-sheet.h"
#include "ui/gui/psppire-scanf.h"

#include "gl/intprops.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid



/* The "first line" page of the assistant. */

/* Page where the user chooses the first line of data. */
struct first_line_page
  {
    GtkWidget *page;
    PsppSheetView *tree_view;
    GtkWidget *variable_names_cb;
  };

static PsppSheetView *create_lines_tree_view (GtkContainer *parent_window,
                                              struct import_assistant *);
static void on_first_line_change (GtkTreeSelection *,
                                  struct import_assistant *);
static void on_variable_names_cb_toggle (GtkToggleButton *,
                                         struct import_assistant *);
static void set_first_line (struct import_assistant *);
static void get_first_line (struct import_assistant *);

/* Initializes IA's first_line substructure. */
struct first_line_page *
first_line_page_create (struct import_assistant *ia)
{
  struct first_line_page *p = xzalloc (sizeof *p);

  GtkBuilder *builder = ia->asst.builder;

  p->page = add_page_to_assistant (ia, get_widget_assert (builder, "FirstLine"),
                                   GTK_ASSISTANT_PAGE_CONTENT);

  gtk_widget_destroy (get_widget_assert (builder, "first-line"));
  p->tree_view = create_lines_tree_view (
    GTK_CONTAINER (get_widget_assert (builder, "first-line-scroller")), ia);
  p->variable_names_cb = get_widget_assert (builder, "variable-names");
  pspp_sheet_selection_set_mode (
    pspp_sheet_view_get_selection (PSPP_SHEET_VIEW (p->tree_view)),
    PSPP_SHEET_SELECTION_BROWSE);
  pspp_sheet_view_set_rubber_banding (PSPP_SHEET_VIEW (p->tree_view), TRUE);
  g_signal_connect (pspp_sheet_view_get_selection (PSPP_SHEET_VIEW (p->tree_view)),
                    "changed", G_CALLBACK (on_first_line_change), ia);
  g_signal_connect (p->variable_names_cb, "toggled",
                    G_CALLBACK (on_variable_names_cb_toggle), ia);
  return p;
}

/* Resets the first_line page to its initial content. */
void
reset_first_line_page (struct import_assistant *ia)
{
  ia->skip_lines = 0;
  ia->variable_names = false;
  set_first_line (ia);
}

static void
render_line (PsppSheetViewColumn *tree_column,
             GtkCellRenderer *cell,
             GtkTreeModel *tree_model,
             GtkTreeIter *iter,
             gpointer data)
{
  gint row = empty_list_store_iter_to_row (iter);
  struct string *lines;

  lines = g_object_get_data (G_OBJECT (tree_model), "lines");
  g_return_if_fail (lines != NULL);

  g_object_set (cell, "text", ds_cstr (&lines[row]), NULL);
}


/* Creates and returns a tree view that contains each of the
   lines in IA's file as a row. */
static PsppSheetView *
create_lines_tree_view (GtkContainer *parent, struct import_assistant *ia)
{
  PsppSheetView *tree_view = NULL;
  PsppSheetViewColumn *column;
  size_t max_line_length;
  gint content_width, header_width;
  size_t i;
  const gchar *title = _("Text");

  make_tree_view (ia, 0, &tree_view);

  column = pspp_sheet_view_column_new_with_attributes (
     title, ia->asst.fixed_renderer, (void *) NULL);
  pspp_sheet_view_column_set_cell_data_func (column, ia->asst.fixed_renderer,
                                           render_line, NULL, NULL);
  pspp_sheet_view_column_set_resizable (column, TRUE);

  max_line_length = 0;
  for (i = 0; i < ia->file.line_cnt; i++)
    {
      size_t w = ds_length (&ia->file.lines[i]);
      max_line_length = MAX (max_line_length, w);
    }

  content_width = get_monospace_width (tree_view, ia->asst.fixed_renderer,
                                       max_line_length);
  header_width = get_string_width (tree_view, ia->asst.prop_renderer, title);
  pspp_sheet_view_column_set_fixed_width (column, MAX (content_width,
                                                     header_width));
  pspp_sheet_view_append_column (tree_view, column);

  gtk_container_add (parent, GTK_WIDGET (tree_view));
  gtk_widget_show (GTK_WIDGET (tree_view));

  return tree_view;
}

/* Called when the line selected in the first_line tree view
   changes. */
static void
on_first_line_change (GtkTreeSelection *selection UNUSED,
                      struct import_assistant *ia)
{
  get_first_line (ia);
}

/* Called when the checkbox that indicates whether variable
   names are in the row above the first line is toggled. */
static void
on_variable_names_cb_toggle (GtkToggleButton *variable_names_cb UNUSED,
                             struct import_assistant *ia)
{
  get_first_line (ia);
}

/* Sets the widgets to match IA's first_line substructure. */
static void
set_first_line (struct import_assistant *ia)
{
  GtkTreePath *path;

  path = gtk_tree_path_new_from_indices (ia->skip_lines, -1);
  pspp_sheet_view_set_cursor (PSPP_SHEET_VIEW (ia->first_line->tree_view),
                            path, NULL, false);
  gtk_tree_path_free (path);

  gtk_toggle_button_set_active (
    GTK_TOGGLE_BUTTON (ia->first_line->variable_names_cb),
    ia->variable_names);
  gtk_widget_set_sensitive (ia->first_line->variable_names_cb,
                            ia->skip_lines > 0);
}

/* Sets IA's first_line substructure to match the widgets. */
static void
get_first_line (struct import_assistant *ia)
{
  PsppSheetSelection *selection;
  GtkTreeIter iter;
  GtkTreeModel *model;

  selection = pspp_sheet_view_get_selection (ia->first_line->tree_view);
  if (pspp_sheet_selection_get_selected (selection, &model, &iter))
    {
      GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
      int row = gtk_tree_path_get_indices (path)[0];
      gtk_tree_path_free (path);

      ia->skip_lines = row;
      ia->variable_names =
        (ia->skip_lines > 0
         && gtk_toggle_button_get_active (
           GTK_TOGGLE_BUTTON (ia->first_line->variable_names_cb)));
    }
  gtk_widget_set_sensitive (ia->first_line->variable_names_cb,
                            ia->skip_lines > 0);
}



void
first_line_append_syntax (const struct import_assistant *ia, struct string *s)
{
  if (ia->skip_lines > 0)
    ds_put_format (s, "  /FIRSTCASE=%d\n", ia->skip_lines + 1);
}
