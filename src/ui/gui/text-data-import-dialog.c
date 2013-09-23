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

#include "ui/gui/text-data-import-dialog.h"

#include "page-intro.h"
#include "page-sheet-spec.h"
#include "page-first-line.h"
#include "page-separators.h"
#include "page-formats.h"

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
#include "ui/gui/pspp-sheet-view.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dialog.h"
#include "ui/gui/psppire-encoding-selector.h"
#include "ui/gui/psppire-empty-list-store.h"
#include "ui/gui/psppire-var-sheet.h"
#include "ui/gui/psppire-scanf.h"
#include "ui/syntax-gen.h"

#include "gl/intprops.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

struct import_assistant;

static void apply_dict (const struct dictionary *, struct string *);
static char *generate_syntax (const struct import_assistant *);

static void add_line_number_column (const struct import_assistant *,
                                    PsppSheetView *);

/* Pops up the Text Data Import assistant. */
void
text_data_import_assistant (PsppireDataWindow *dw)
{
  GtkWindow *parent_window = GTK_WINDOW (dw);
  struct import_assistant *ia = init_assistant (parent_window);
  struct sheet_spec_page *ssp ;

  if (!init_file (ia, parent_window))
    {
      free (ia);
      return;
    }

  ssp = ia->sheet_spec;

  if (ia->spreadsheet)
    {
      ia->sheet_spec = sheet_spec_page_create (ia);
    }
  else
    {
      ia->intro = intro_page_create (ia);
      ia->first_line = first_line_page_create (ia);
      ia->separators = separators_page_create (ia);
    }
  ia->formats = formats_page_create (ia);

  gtk_widget_show_all (GTK_WIDGET (ia->asst.assistant));

  ia->asst.main_loop = g_main_loop_new (NULL, false);

  {  
  /*
    Instead of this block,
    A simple     g_main_loop_run (ia->asst.main_loop);  should work here.  But it seems to crash.
    I have no idea why.
  */
    GMainContext *ctx = g_main_loop_get_context (ia->asst.main_loop);
    ia->asst.loop_done = false;
    while (! ia->asst.loop_done)
      {
	g_main_context_iteration (ctx, TRUE);
      }
  }
  g_main_loop_unref (ia->asst.main_loop);

  switch (ia->asst.response)
    {
    case GTK_RESPONSE_APPLY:
      {
	gchar *fn = g_path_get_basename (ia->file.file_name);
	open_data_window (PSPPIRE_WINDOW (dw), fn, generate_syntax (ia));
	g_free (fn);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      free (paste_syntax_to_window (generate_syntax (ia)));
      break;
    default:
      break;
    }

  if (ssp) 
    {
      destroy_formats_page (ia);
      destroy_separators_page (ia);
    }

  destroy_assistant (ia);
  destroy_file (ia);
  free (ia);
}

/* Emits PSPP syntax to S that applies the dictionary attributes
   (such as missing values and value labels) of the variables in
   DICT.  */
static void
apply_dict (const struct dictionary *dict, struct string *s)
{
  size_t var_cnt = dict_get_var_cnt (dict);
  size_t i;

  for (i = 0; i < var_cnt; i++)
    {
      struct variable *var = dict_get_var (dict, i);
      const char *name = var_get_name (var);
      enum val_type type = var_get_type (var);
      int width = var_get_width (var);
      enum measure measure = var_get_measure (var);
      enum var_role role = var_get_role (var);
      enum alignment alignment = var_get_alignment (var);
      const struct fmt_spec *format = var_get_print_format (var);

      if (var_has_missing_values (var))
        {
          const struct missing_values *mv = var_get_missing_values (var);
          size_t j;

          syntax_gen_pspp (s, "MISSING VALUES %ss (", name);
          for (j = 0; j < mv_n_values (mv); j++)
            {
              if (j)
                ds_put_cstr (s, ", ");
              syntax_gen_value (s, mv_get_value (mv, j), width, format);
            }

          if (mv_has_range (mv))
            {
              double low, high;
              if (mv_has_value (mv))
                ds_put_cstr (s, ", ");
              mv_get_range (mv, &low, &high);
              syntax_gen_num_range (s, low, high, format);
            }
          ds_put_cstr (s, ").\n");
        }
      if (var_has_value_labels (var))
        {
          const struct val_labs *vls = var_get_value_labels (var);
          const struct val_lab **labels = val_labs_sorted (vls);
          size_t n_labels = val_labs_count (vls);
          size_t i;

          syntax_gen_pspp (s, "VALUE LABELS %ss", name);
          for (i = 0; i < n_labels; i++)
            {
              const struct val_lab *vl = labels[i];
              ds_put_cstr (s, "\n  ");
              syntax_gen_value (s, &vl->value, width, format);
              ds_put_byte (s, ' ');
              syntax_gen_string (s, ss_cstr (val_lab_get_escaped_label (vl)));
            }
          free (labels);
          ds_put_cstr (s, ".\n");
        }
      if (var_has_label (var))
        syntax_gen_pspp (s, "VARIABLE LABELS %ss %sq.\n",
                         name, var_get_label (var));
      if (measure != var_default_measure (type))
        syntax_gen_pspp (s, "VARIABLE LEVEL %ss (%ss).\n",
                         name, measure_to_syntax (measure));
      if (role != ROLE_INPUT)
        syntax_gen_pspp (s, "VARIABLE ROLE /%ss %ss.\n",
                         var_role_to_syntax (role), name);
      if (alignment != var_default_alignment (type))
        syntax_gen_pspp (s, "VARIABLE ALIGNMENT %ss (%ss).\n",
                         name, alignment_to_syntax (alignment));
      if (var_get_display_width (var) != var_default_display_width (width))
        syntax_gen_pspp (s, "VARIABLE WIDTH %ss (%d).\n",
                         name, var_get_display_width (var));
    }
}

/* Generates and returns PSPP syntax to execute the import
   operation described by IA.  The caller must free the syntax
   with free(). */
static char *
generate_syntax (const struct import_assistant *ia)
{
  struct string s = DS_EMPTY_INITIALIZER;

  if (ia->spreadsheet == NULL)
    {
      syntax_gen_pspp (&s,
		       "GET DATA"
		       "\n  /TYPE=TXT"
		       "\n  /FILE=%sq\n",
		       ia->file.file_name);
      if (ia->file.encoding && strcmp (ia->file.encoding, "Auto"))
	syntax_gen_pspp (&s, "  /ENCODING=%sq\n", ia->file.encoding);

      intro_append_syntax (ia->intro, &s);


      ds_put_cstr (&s,
		   "  /ARRANGEMENT=DELIMITED\n"
		   "  /DELCASE=LINE\n");

      first_line_append_syntax (ia, &s);
      separators_append_syntax (ia, &s);
      formats_append_syntax (ia, &s);
      apply_dict (ia->dict, &s);
    }
  else
    {
      return sheet_spec_gen_syntax (ia);
    }
  
  return ds_cstr (&s);
}



static void render_input_cell (PsppSheetViewColumn *tree_column,
                               GtkCellRenderer *cell,
                               GtkTreeModel *model, GtkTreeIter *iter,
                               gpointer ia);

static gboolean on_query_input_tooltip (GtkWidget *widget, gint wx, gint wy,
                                        gboolean keyboard_mode UNUSED,
                                        GtkTooltip *tooltip,
                                        struct import_assistant *);



/* Called to render one of the cells in the fields preview tree
   view. */
static void
render_input_cell (PsppSheetViewColumn *tree_column, GtkCellRenderer *cell,
                   GtkTreeModel *model, GtkTreeIter *iter,
                   gpointer ia_)
{
  struct import_assistant *ia = ia_;
  struct substring field;
  size_t row;
  gint column;

  column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_column),
                                               "column-number"));
  row = empty_list_store_iter_to_row (iter) + ia->skip_lines;
  field = ia->columns[column].contents[row];
  if (field.string != NULL)
    {
      GValue text = {0, };
      g_value_init (&text, G_TYPE_STRING);
      g_value_take_string (&text, ss_xstrdup (field));
      g_object_set_property (G_OBJECT (cell), "text", &text);
      g_value_unset (&text);
      g_object_set (cell, "background-set", FALSE, (void *) NULL);
    }
  else
    g_object_set (cell,
                  "text", "",
                  "background", "red",
                  "background-set", TRUE,
                  (void *) NULL);
}

static gboolean
get_tooltip_location (GtkWidget *widget, gint wx, gint wy,
                      const struct import_assistant *ia,
                      size_t *row, size_t *column);


/* Called to render a tooltip on one of the cells in the fields
   preview tree view. */
static gboolean
on_query_input_tooltip (GtkWidget *widget, gint wx, gint wy,
                        gboolean keyboard_mode UNUSED,
                        GtkTooltip *tooltip, struct import_assistant *ia)
{
  size_t row, column;

  if (!get_tooltip_location (widget, wx, wy, ia, &row, &column))
    return FALSE;

  if (ia->columns[column].contents[row].string != NULL)
    return FALSE;

  gtk_tooltip_set_text (tooltip,
                        _("This input line has too few separators "
                          "to fill in this field."));
  return TRUE;
}


/* Parses the contents of the field at (ROW,COLUMN) according to
   its variable format.  If OUTPUTP is non-null, then *OUTPUTP
   receives the formatted output for that field (which must be
   freed with free).  If TOOLTIPP is non-null, then *TOOLTIPP
   receives a message suitable for use in a tooltip, if one is
   needed, or a null pointer otherwise.  Returns true if a
   tooltip message is needed, otherwise false. */
static bool
parse_field (struct import_assistant *ia,
             size_t row, size_t column,
             char **outputp, char **tooltipp)
{
  const struct fmt_spec *in;
  struct fmt_spec out;
  char *tooltip;
  bool ok;

  struct substring field = ia->columns[column].contents[row];
  struct variable *var = dict_get_var (ia->dict, column);
  union value val;

  value_init (&val, var_get_width (var));
  in = var_get_print_format (var);
  out = fmt_for_output_from_input (in);
  tooltip = NULL;
  if (field.string != NULL)
    {
      char *error;

      error = data_in (field, "UTF-8", in->type, &val, var_get_width (var),
                       dict_get_encoding (ia->dict));
      if (error != NULL)
        {
          tooltip = xasprintf (_("Cannot parse field content `%.*s' as "
                                 "format %s: %s"),
                               (int) field.length, field.string,
                               fmt_name (in->type), error);
          free (error);
        }
    }
  else
    {
      tooltip = xstrdup (_("This input line has too few separators "
                           "to fill in this field."));
      value_set_missing (&val, var_get_width (var));
    }
  if (outputp != NULL)
    {
      *outputp = data_out (&val, dict_get_encoding (ia->dict),  &out);
    }
  value_destroy (&val, var_get_width (var));

  ok = tooltip == NULL;
  if (tooltipp != NULL)
    *tooltipp = tooltip;
  else
    free (tooltip);
  return ok;
}

/* Called to render one of the cells in the data preview tree
   view. */
static void
render_output_cell (PsppSheetViewColumn *tree_column,
                    GtkCellRenderer *cell,
                    GtkTreeModel *model,
                    GtkTreeIter *iter,
                    gpointer ia_)
{
  struct import_assistant *ia = ia_;
  char *output;
  GValue gvalue = { 0, };
  bool ok;

  ok = parse_field (ia,
                    (empty_list_store_iter_to_row (iter)
                     + ia->skip_lines),
                    GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_column),
                                                        "column-number")),
                    &output, NULL);

  g_value_init (&gvalue, G_TYPE_STRING);
  g_value_take_string (&gvalue, output);
  g_object_set_property (G_OBJECT (cell), "text", &gvalue);
  g_value_unset (&gvalue);

  if (ok)
    g_object_set (cell, "background-set", FALSE, (void *) NULL);
  else
    g_object_set (cell,
                  "background", "red",
                  "background-set", TRUE,
                  (void *) NULL);
}

/* Called to render a tooltip for one of the cells in the data
   preview tree view. */
static gboolean
on_query_output_tooltip (GtkWidget *widget, gint wx, gint wy,
			 gboolean keyboard_mode UNUSED,
			 GtkTooltip *tooltip, struct import_assistant *ia)
{
  size_t row, column;
  char *text;

  if (!get_tooltip_location (widget, wx, wy, ia, &row, &column))
    return FALSE;

  if (parse_field (ia, row, column, NULL, &text))
    return FALSE;

  gtk_tooltip_set_text (tooltip, text);
  free (text);
  return TRUE;
}

/* Utility functions used by multiple pages of the assistant. */

static gboolean
get_tooltip_location (GtkWidget *widget, gint wx, gint wy,
                      const struct import_assistant *ia,
                      size_t *row, size_t *column)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  gint bx, by;
  GtkTreePath *path;
  GtkTreeIter iter;
  PsppSheetViewColumn *tree_column;
  GtkTreeModel *tree_model;
  bool ok;

  /* Check that WIDGET is really visible on the screen before we
     do anything else.  This is a bug fix for a sticky situation:
     when text_data_import_assistant() returns, it frees the data
     necessary to compose the tool tip message, but there may be
     a tool tip under preparation at that point (even if there is
     no visible tool tip) that will call back into us a little
     bit later.  Perhaps the correct solution to this problem is
     to make the data related to the tool tips part of a GObject
     that only gets destroyed when all references are released,
     but this solution appears to be effective too. */
  if (!gtk_widget_get_mapped (widget))
    return FALSE;

  pspp_sheet_view_convert_widget_to_bin_window_coords (tree_view,
                                                       wx, wy, &bx, &by);
  if (!pspp_sheet_view_get_path_at_pos (tree_view, bx, by,
                                      &path, &tree_column, NULL, NULL))
    return FALSE;

  *column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_column),
                                                "column-number"));

  tree_model = pspp_sheet_view_get_model (tree_view);
  ok = gtk_tree_model_get_iter (tree_model, &iter, path);
  gtk_tree_path_free (path);
  if (!ok)
    return FALSE;

  *row = empty_list_store_iter_to_row (&iter) + ia->skip_lines;
  return TRUE;
}

void
make_tree_view (const struct import_assistant *ia,
                size_t first_line,
                PsppSheetView **tree_view)
{
  GtkTreeModel *model;

  *tree_view = PSPP_SHEET_VIEW (pspp_sheet_view_new ());
  pspp_sheet_view_set_grid_lines (*tree_view, PSPP_SHEET_VIEW_GRID_LINES_BOTH);
  model = GTK_TREE_MODEL (psppire_empty_list_store_new (
							ia->file.line_cnt - first_line));
  g_object_set_data (G_OBJECT (model), "lines", ia->file.lines + first_line);
  g_object_set_data (G_OBJECT (model), "first-line",
                     GINT_TO_POINTER (first_line));
  pspp_sheet_view_set_model (*tree_view, model);
  g_object_unref (model);

  add_line_number_column (ia, *tree_view);
}

static void
render_line_number (PsppSheetViewColumn *tree_column,
                    GtkCellRenderer *cell,
                    GtkTreeModel *tree_model,
                    GtkTreeIter *iter,
                    gpointer data)
{
  gint row = empty_list_store_iter_to_row (iter);
  char s[INT_BUFSIZE_BOUND (int)];
  int first_line;

  first_line = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_model),
                                                   "first-line"));
  sprintf (s, "%d", first_line + row);
  g_object_set (cell, "text", s, NULL);
}

static void
add_line_number_column (const struct import_assistant *ia,
                        PsppSheetView *treeview)
{
  PsppSheetViewColumn *column;

  column = pspp_sheet_view_column_new_with_attributes (
    _("Line"), ia->asst.prop_renderer, (void *) NULL);
  pspp_sheet_view_column_set_fixed_width (
    column, get_monospace_width (treeview, ia->asst.prop_renderer, 5));
  pspp_sheet_view_column_set_resizable (column, TRUE);
  pspp_sheet_view_column_set_cell_data_func (column, ia->asst.prop_renderer,
                                             render_line_number, NULL, NULL);
  pspp_sheet_view_append_column (treeview, column);
}

gint
get_monospace_width (PsppSheetView *treeview, GtkCellRenderer *renderer,
                     size_t char_cnt)
{
  struct string s;
  gint width;

  ds_init_empty (&s);
  ds_put_byte_multiple (&s, '0', char_cnt);
  ds_put_byte (&s, ' ');
  width = get_string_width (treeview, renderer, ds_cstr (&s));
  ds_destroy (&s);

  return width;
}

gint
get_string_width (PsppSheetView *treeview, GtkCellRenderer *renderer,
                  const char *string)
{
  gint width;
  g_object_set (G_OBJECT (renderer), "text", string, (void *) NULL);
  gtk_cell_renderer_get_size (renderer, GTK_WIDGET (treeview),
                              NULL, NULL, NULL, &width, NULL);
  return width;
}

PsppSheetViewColumn *
make_data_column (struct import_assistant *ia, PsppSheetView *tree_view,
                  bool input, gint dict_idx)
{
  struct variable *var = NULL;
  struct column *column = NULL;
  size_t char_cnt;
  gint content_width, header_width;
  PsppSheetViewColumn *tree_column;
  char *name;

  if (input)
    column = &ia->columns[dict_idx];
  else
    var = dict_get_var (ia->dict, dict_idx);

  name = escape_underscores (input ? column->name : var_get_name (var));
  char_cnt = input ? column->width : var_get_print_format (var)->w;
  content_width = get_monospace_width (tree_view, ia->asst.fixed_renderer,
                                       char_cnt);
  header_width = get_string_width (tree_view, ia->asst.prop_renderer,
                                   name);

  tree_column = pspp_sheet_view_column_new ();
  g_object_set_data (G_OBJECT (tree_column), "column-number",
                     GINT_TO_POINTER (dict_idx));
  pspp_sheet_view_column_set_title (tree_column, name);
  pspp_sheet_view_column_pack_start (tree_column, ia->asst.fixed_renderer,
                                     FALSE);
  pspp_sheet_view_column_set_cell_data_func (
    tree_column, ia->asst.fixed_renderer,
    input ? render_input_cell : render_output_cell, ia, NULL);
  pspp_sheet_view_column_set_fixed_width (tree_column, MAX (content_width,
                                                            header_width));

  free (name);

  return tree_column;
}

PsppSheetView *
create_data_tree_view (bool input, GtkContainer *parent,
                       struct import_assistant *ia)
{
  PsppSheetView *tree_view;
  gint i;

  make_tree_view (ia, ia->skip_lines, &tree_view);
  pspp_sheet_selection_set_mode (pspp_sheet_view_get_selection (tree_view),
                                 PSPP_SHEET_SELECTION_NONE);

  for (i = 0; i < ia->column_cnt; i++)
    pspp_sheet_view_append_column (tree_view,
                                   make_data_column (ia, tree_view, input, i));

  g_object_set (G_OBJECT (tree_view), "has-tooltip", TRUE, (void *) NULL);
  g_signal_connect (tree_view, "query-tooltip",
                    G_CALLBACK (input ? on_query_input_tooltip
                                : on_query_output_tooltip), ia);


  gtk_container_add (parent, GTK_WIDGET (tree_view));
  gtk_widget_show (GTK_WIDGET (tree_view));

  return tree_view;
}

/* Increments the "watch cursor" level, setting the cursor for
   the assistant window to a watch face to indicate to the user
   that the ongoing operation may take some time. */
void
push_watch_cursor (struct import_assistant *ia)
{
  if (++ia->asst.watch_cursor == 1)
    {
      GtkWidget *widget = GTK_WIDGET (ia->asst.assistant);
      GdkDisplay *display = gtk_widget_get_display (widget);
      GdkCursor *cursor = gdk_cursor_new_for_display (display, GDK_WATCH);
      gdk_window_set_cursor (widget->window, cursor);
      gdk_cursor_unref (cursor);
      gdk_display_flush (display);
    }
}

/* Decrements the "watch cursor" level.  If the level reaches
   zero, the cursor is reset to its default shape. */
void
pop_watch_cursor (struct import_assistant *ia)
{
  if (--ia->asst.watch_cursor == 0)
    {
      GtkWidget *widget = GTK_WIDGET (ia->asst.assistant);
      gdk_window_set_cursor (widget->window, NULL);
    }
}
