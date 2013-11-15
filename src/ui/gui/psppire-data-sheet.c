/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include "ui/gui/psppire-data-sheet.h"

#include "data/case-map.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/data-out.h"
#include "data/datasheet.h"
#include "data/format.h"
#include "data/value-labels.h"
#include "libpspp/intern.h"
#include "libpspp/range-set.h"
#include "ui/gui/executor.h"
#include "ui/gui/find-dialog.h"
#include "ui/gui/goto-case-dialog.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/helper.h"
#include "ui/gui/pspp-sheet-selection.h"
#include "ui/gui/psppire-cell-renderer-button.h"
#include "ui/gui/psppire-data-store.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dialog-action-var-info.h"
#include "ui/gui/psppire-empty-list-store.h"
#include "ui/gui/psppire-marshal.h"

#include "gl/intprops.h"
#include "gl/xalloc.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_data_sheet_dispose (GObject *);
static void psppire_data_sheet_unset_data_store (PsppireDataSheet *);

static void psppire_data_sheet_update_clip_actions (PsppireDataSheet *);
static void psppire_data_sheet_update_primary_selection (PsppireDataSheet *,
                                                         gboolean should_own);
static void psppire_data_sheet_set_clip (PsppireDataSheet *, gboolean cut);

static void on_selection_changed (PsppSheetSelection *, gpointer);
static void on_owner_change (GtkClipboard *, GdkEventOwnerChange *, gpointer);
static void psppire_data_sheet_clip_received_cb (GtkClipboard *,
                                                 GtkSelectionData *, gpointer);

G_DEFINE_TYPE (PsppireDataSheet, psppire_data_sheet, PSPP_TYPE_SHEET_VIEW);

static gboolean
get_tooltip_location (GtkWidget *widget, GtkTooltip *tooltip,
                      gint wx, gint wy,
                      size_t *row, PsppSheetViewColumn **columnp)
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

  *columnp = tree_column;

  pspp_sheet_view_set_tooltip_cell (tree_view, tooltip, path, tree_column,
                                    NULL);

  tree_model = pspp_sheet_view_get_model (tree_view);
  ok = gtk_tree_model_get_iter (tree_model, &iter, path);
  gtk_tree_path_free (path);
  if (!ok)
    return FALSE;

  *row = GPOINTER_TO_INT (iter.user_data);
  return TRUE;
}

static gboolean
on_query_tooltip (GtkWidget *widget, gint wx, gint wy,
                  gboolean keyboard_mode UNUSED,
                  GtkTooltip *tooltip, gpointer data UNUSED)
{
  PsppireDataSheet *data_sheet = PSPPIRE_DATA_SHEET (widget);
  PsppireDataStore *data_store = psppire_data_sheet_get_data_store (data_sheet);
  PsppSheetViewColumn *column;
  struct variable *var;
  const char *label;
  union value v;
  size_t row;
  int width;

  g_return_val_if_fail (data_store != NULL, FALSE);
  g_return_val_if_fail (data_store->datasheet != NULL, FALSE);

  if (!get_tooltip_location (widget, tooltip, wx, wy, &row, &column))
    return FALSE;

  var = g_object_get_data (G_OBJECT (column), "variable");
  if (var == NULL)
    {
      if (g_object_get_data (G_OBJECT (column), "new-var-column") == NULL)
        return FALSE;

      gtk_tooltip_set_text (tooltip,
                            _("Enter a number to add a new variable."));
      return TRUE;
    }
  else if (row >= datasheet_get_n_rows (data_store->datasheet))
    {
      gtk_tooltip_set_text (tooltip, _("Enter a number to add a new case."));
      return TRUE;
    }

  width = var_get_width (var);

  value_init (&v, width);
  datasheet_get_value (data_store->datasheet, row, var_get_case_index (var),
                       &v);

  label = var_lookup_value_label (var, &v);
  if (label != NULL)
    {
      if (data_sheet->show_value_labels)
        {
          char *s = value_to_text (v, var);
          gtk_tooltip_set_text (tooltip, s);
          free (s);
        }
      else
        gtk_tooltip_set_text (tooltip, label);
    }
  value_destroy (&v, width);

  return label != NULL;
}

static void
render_row_number_cell (PsppSheetViewColumn *tree_column,
                        GtkCellRenderer *cell,
                        GtkTreeModel *model,
                        GtkTreeIter *iter,
                        gpointer store_)
{
  PsppireDataStore *store = store_;
  GValue gvalue = { 0, };
  gint row = GPOINTER_TO_INT (iter->user_data);

  g_return_if_fail (store->datasheet);

  g_value_init (&gvalue, G_TYPE_INT);
  g_value_set_int (&gvalue, row + 1);
  g_object_set_property (G_OBJECT (cell), "label", &gvalue);
  g_value_unset (&gvalue);

  if (row < datasheet_get_n_rows (store->datasheet))
    g_object_set (cell, "editable", TRUE, NULL);
  else
    g_object_set (cell, "editable", FALSE, NULL);

  g_object_set (cell,
                "slash", psppire_data_store_filtered (store, row),
                NULL);
}

static void
on_row_number_clicked (PsppireCellRendererButton *button,
                       gchar *path_string,
                       PsppSheetView *sheet_view)
{
  PsppSheetSelection *selection;
  GtkTreePath *path;

  path = gtk_tree_path_new_from_string (path_string);

  selection = pspp_sheet_view_get_selection (sheet_view);
  pspp_sheet_selection_unselect_all (selection);
  pspp_sheet_selection_select_path (selection, path);
  pspp_sheet_selection_select_all_columns (selection);

  gtk_tree_path_free (path);
}

static void
make_row_number_column (PsppireDataSheet *data_sheet,
                        PsppireDataStore *ds)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppSheetViewColumn *column;
  GtkCellRenderer *renderer;

  renderer = psppire_cell_renderer_button_new ();
  g_object_set (renderer, "xalign", 1.0, NULL);
  g_signal_connect (renderer, "clicked", G_CALLBACK (on_row_number_clicked),
                    sheet_view);

  column = pspp_sheet_view_column_new_with_attributes (_("Case"),
                                                       renderer, NULL);
  pspp_sheet_view_column_set_selectable (column, TRUE);
  pspp_sheet_view_column_set_row_head (column, TRUE);
  pspp_sheet_view_column_set_tabbable (column, FALSE);
  pspp_sheet_view_column_set_clickable (column, TRUE);
  pspp_sheet_view_column_set_cell_data_func (
    column, renderer, render_row_number_cell, ds, NULL);
  pspp_sheet_view_column_set_fixed_width (column, 50);
  pspp_sheet_view_column_set_visible (column, data_sheet->show_case_numbers);
  pspp_sheet_view_append_column (sheet_view, column);
}

static void
render_data_cell (PsppSheetViewColumn *tree_column,
                  GtkCellRenderer *cell,
                  GtkTreeModel *model,
                  GtkTreeIter *iter,
                  gpointer data_sheet_)
{
  PsppireDataSheet *data_sheet = data_sheet_;
  PsppireDataStore *store = psppire_data_sheet_get_data_store (data_sheet);
  struct variable *var;
  gchar *string;
  gint row;

  double xalign;

  row = GPOINTER_TO_INT (iter->user_data);
  var = g_object_get_data (G_OBJECT (tree_column), "variable");

  string = psppire_data_store_get_string (store, row, var,
                                          data_sheet->show_value_labels);
  if (string != NULL)
    {
      GValue gvalue = { 0 };

      g_value_init (&gvalue, G_TYPE_STRING);
      g_value_take_string (&gvalue, string);
      g_object_set_property (G_OBJECT (cell), "text", &gvalue);
      g_value_unset (&gvalue);
    }
  else
    g_object_set (G_OBJECT (cell), "text", "", NULL);

  switch (var_get_alignment (var))
    {
    case ALIGN_LEFT: xalign = 0.0; break;
    case ALIGN_RIGHT: xalign = 1.0; break;
    case ALIGN_CENTRE: xalign = 0.5; break;
    default: xalign = 0.0; break;
    }
  g_object_set (cell,
                "xalign", xalign,
                "editable", TRUE,
                NULL);
}

static gint
get_string_width (PsppSheetView *treeview, GtkCellRenderer *renderer,
                  const char *string)
{
  gint width;
  g_object_set (G_OBJECT (renderer), "text", string, (void *) NULL);
  gtk_cell_renderer_get_size (renderer, GTK_WIDGET (treeview),
                              NULL, NULL, NULL, &width, NULL);
  return width;
}

static gint
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

static void
on_data_column_editing_started (GtkCellRenderer *cell,
                                GtkCellEditable *editable,
                                const gchar     *path,
                                gpointer         user_data)
{
  PsppSheetViewColumn *column = g_object_get_data (G_OBJECT (cell), "column");
  PsppireDataSheet *data_sheet = g_object_get_data (G_OBJECT (cell), "data-sheet");
  PsppireDataStore *data_store = psppire_data_sheet_get_data_store (data_sheet);
  struct variable *var;

  g_return_if_fail (column);
  g_return_if_fail (data_sheet);
  g_return_if_fail (data_store);


  g_object_ref (editable);
  g_object_set_data_full (G_OBJECT (cell), "data-sheet-editable",
                          editable, g_object_unref);

  var = g_object_get_data (G_OBJECT (column), "variable");
  g_return_if_fail (var);

  if (var_has_value_labels (var) && GTK_IS_COMBO_BOX (editable))
    {
      const struct val_labs *labels = var_get_value_labels (var);
      const struct val_lab **vls = val_labs_sorted (labels);
      size_t n_vls = val_labs_count (labels);
      GtkListStore *list_store;
      int i;

      list_store = gtk_list_store_new (1, G_TYPE_STRING);
      for (i = 0; i < n_vls; ++i)
        {
          const struct val_lab *vl = vls[i];
          GtkTreeIter iter;

          gtk_list_store_append (list_store, &iter);
          gtk_list_store_set (list_store, &iter,
                              0, val_lab_get_label (vl),
                              -1);
        }
      free (vls);

      gtk_combo_box_set_model (GTK_COMBO_BOX (editable),
                               GTK_TREE_MODEL (list_store));
      g_object_unref (list_store);
    }
}

static void
scroll_to_bottom (GtkWidget      *widget,
                  GtkRequisition *requisition,
                  gpointer        unused UNUSED)
{
  PsppireDataSheet *data_sheet = PSPPIRE_DATA_SHEET (widget);
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (widget);
  GtkAdjustment *vadjust;

  vadjust = pspp_sheet_view_get_vadjustment (sheet_view);
  gtk_adjustment_set_value (vadjust, gtk_adjustment_get_upper (vadjust));

  if (data_sheet->scroll_to_bottom_signal)
    {
      g_signal_handler_disconnect (data_sheet,
                                   data_sheet->scroll_to_bottom_signal);
      data_sheet->scroll_to_bottom_signal = 0;
    }
}

static void
on_data_column_edited (GtkCellRendererText *cell,
                       gchar               *path_string,
                       gchar               *new_text,
                       gpointer             user_data)
{
  PsppSheetViewColumn *column = g_object_get_data (G_OBJECT (cell), "column");
  PsppireDataSheet *data_sheet = g_object_get_data (G_OBJECT (cell), "data-sheet");
  PsppireDataStore *data_store = psppire_data_sheet_get_data_store (data_sheet);
  GtkEditable *editable;
  struct variable *var;
  GtkTreePath *path;
  gboolean is_val_lab;
  gboolean new_row;
  gint row;

  path = gtk_tree_path_new_from_string (path_string);
  row = gtk_tree_path_get_indices (path)[0];
  gtk_tree_path_free (path);

  var = g_object_get_data (G_OBJECT (column), "variable");

  new_row = row == psppire_data_store_get_case_count (data_store);
  if (new_row && new_text[0] == '\0')
    return;

  editable = g_object_steal_data (G_OBJECT (cell), "data-sheet-editable");
  g_return_if_fail (editable != NULL);
  is_val_lab = (GTK_IS_COMBO_BOX (editable)
                && gtk_combo_box_get_active (GTK_COMBO_BOX (editable)) >= 0);
  g_object_unref (editable);

  psppire_data_store_set_string (data_store, new_text, row, var, is_val_lab);

  if (new_row && !data_sheet->scroll_to_bottom_signal)
    {
      gtk_widget_queue_resize (GTK_WIDGET (data_sheet));
      data_sheet->scroll_to_bottom_signal =
        g_signal_connect (data_sheet, "size-request",
                          G_CALLBACK (scroll_to_bottom), NULL);
    }
  else
    {
      /* We could be more specific about what to redraw, if it seems
         important for performance. */
      gtk_widget_queue_draw (GTK_WIDGET (data_sheet));
    }
}

static void
scroll_to_right (GtkWidget      *widget,
                 PsppireDataSheet  *data_sheet)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppSheetViewColumn *column, *prev;
  GList *columns, *iter;

  column = NULL;
  prev = NULL;
  columns = pspp_sheet_view_get_columns (sheet_view);
  for (iter = columns; iter; iter = iter->next)
    {
      PsppSheetViewColumn *c = iter->data;
      if (g_object_get_data (G_OBJECT (c), "new-var-column"))
        {
          column = c;
          break;
        }
      prev = c;
    }
  g_list_free (columns);

  if (column == NULL)
    return;

  pspp_sheet_view_scroll_to_cell (sheet_view, NULL, column, FALSE, 0, 0);

  if (prev)
    {
      GtkTreePath *path;

      pspp_sheet_view_get_cursor (sheet_view, &path, NULL);
      if (path)
        {
          pspp_sheet_view_set_cursor (sheet_view, path, prev, TRUE);
          gtk_tree_path_free (path);
        }
    }

  if (data_sheet->scroll_to_right_signal)
    {
      g_signal_handler_disconnect (widget, data_sheet->scroll_to_right_signal);
      data_sheet->scroll_to_right_signal = 0;
    }
}

static void
on_new_variable_column_edited (GtkCellRendererText *cell,
                               gchar               *path_string,
                               gchar               *new_text,
                               gpointer             user_data)
{
  PsppireDataSheet *data_sheet = g_object_get_data (G_OBJECT (cell), "data-sheet");
  PsppireDataStore *data_store = psppire_data_sheet_get_data_store (data_sheet);
  PsppireDict *dict = data_store->dict;
  struct variable *var;
  GtkTreePath *path;
  char name[64];
  gint row;

  if (new_text[0] == '\0')
    {
      /* User didn't enter anything so don't create a variable. */
      return;
    }

  path = gtk_tree_path_new_from_string (path_string);
  row = gtk_tree_path_get_indices (path)[0];
  gtk_tree_path_free (path);

  if (!psppire_dict_generate_name (dict, name, sizeof name))
    return;

  var = psppire_dict_insert_variable (dict, psppire_dict_get_var_cnt (dict),
                                      name);
  g_return_if_fail (var != NULL);

  psppire_data_store_set_string (data_store, new_text, row, var, FALSE);

  if (!data_sheet->scroll_to_right_signal)
    {
      gtk_widget_queue_resize (GTK_WIDGET (data_sheet));
      data_sheet->scroll_to_right_signal =
        g_signal_connect_after (gtk_widget_get_toplevel (GTK_WIDGET (data_sheet)), "check-resize",
                                G_CALLBACK (scroll_to_right), data_sheet);
    }
  else
    {
      /* We could be more specific about what to redraw, if it seems
         important for performance. */
      gtk_widget_queue_draw (GTK_WIDGET (data_sheet));
    }
}

static void
calc_width_conversion (PsppireDataSheet *data_sheet,
                       gint *base_width, gint *incr_width)
{
  GtkCellRenderer *cell;
  gint w1, w10;

  cell = gtk_cell_renderer_text_new ();
  w1 = get_monospace_width (PSPP_SHEET_VIEW (data_sheet), cell, 1);
  w10 = get_monospace_width (PSPP_SHEET_VIEW (data_sheet), cell, 10);
  *incr_width = MAX (1, (w10 - w1) / 9);
  *base_width = MAX (0, w10 - *incr_width * 10);
  g_object_ref_sink (cell);
  g_object_unref (cell);
}

static gint
display_width_from_pixel_width (PsppireDataSheet *data_sheet,
                                gint pixel_width)
{
  gint base_width, incr_width;

  calc_width_conversion (data_sheet, &base_width, &incr_width);
  return MAX ((pixel_width - base_width + incr_width / 2) / incr_width, 1);
}

static gint
display_width_to_pixel_width (PsppireDataSheet *data_sheet,
                              gint display_width,
                              gint base_width,
                              gint incr_width)
{
  return base_width + incr_width * display_width;
}

static void
on_data_column_resized (GObject    *gobject,
                        GParamSpec *pspec,
                        gpointer    user_data)
{
  PsppireDataSheet *data_sheet = user_data;
  PsppireDataStore *data_store = psppire_data_sheet_get_data_store (data_sheet);
  PsppSheetViewColumn *column = PSPP_SHEET_VIEW_COLUMN (gobject);
  struct variable *var;
  gint pixel_width;
  int display_width;

  if (data_store == NULL)
    return;

  pixel_width = pspp_sheet_view_column_get_width (column);
  if (pixel_width == pspp_sheet_view_column_get_fixed_width (column))
    {
      /* Short-circuit the expensive display_width_from_pixel_width()
         calculation, to make loading .sav files with 2000 columns visibly
         faster. */
      return;
    }

  var = g_object_get_data (G_OBJECT (column), "variable");
  display_width = display_width_from_pixel_width (data_sheet, pixel_width);
  var_set_display_width (var, display_width);
}

static void
do_data_column_popup_menu (PsppSheetViewColumn *column,
                           guint button, guint32 time)
{
  GtkWidget *sheet_view = pspp_sheet_view_column_get_tree_view (column);
  PsppireDataSheet *data_sheet = PSPPIRE_DATA_SHEET (sheet_view);
  GtkWidget *menu;

  menu = get_widget_assert (data_sheet->builder, "datasheet-variable-popup");
  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, button, time);
}

static void
on_data_column_popup_menu (PsppSheetViewColumn *column,
                           gpointer user_data UNUSED)
{
  do_data_column_popup_menu (column, 0, gtk_get_current_event_time ());
}

static gboolean
on_column_button_press_event (PsppSheetViewColumn *column,
                              GdkEventButton *event,
                              gpointer user_data UNUSED)
{
  PsppSheetSelection *selection;
  PsppSheetView *sheet_view;

  sheet_view = PSPP_SHEET_VIEW (pspp_sheet_view_column_get_tree_view (
                                  column));
  g_return_val_if_fail (sheet_view != NULL, FALSE);

  selection = pspp_sheet_view_get_selection (sheet_view);
  g_return_val_if_fail (selection != NULL, FALSE);

  if (event->type == GDK_BUTTON_PRESS && event->button == 3)
    {
      do_data_column_popup_menu (column, event->button, event->time);
      return TRUE;
    }
  else if (event->type == GDK_2BUTTON_PRESS && event->button == 1)
    {
      PsppireDataSheet *data_sheet = PSPPIRE_DATA_SHEET (sheet_view);
      struct variable *var;

      var = g_object_get_data (G_OBJECT (column), "variable");
      if (var != NULL)
        {
          gboolean handled;

          g_signal_emit_by_name (data_sheet, "var-double-clicked",
                                 var_get_dict_index (var), &handled);
          return handled;
        }
    }

  return FALSE;
}

static gboolean
on_data_column_query_tooltip (PsppSheetViewColumn *column,
                              GtkTooltip *tooltip,
                              gpointer user_data UNUSED)
{
  struct variable *var;
  const char *text;

  var = g_object_get_data (G_OBJECT (column), "variable");
  g_return_val_if_fail (var != NULL, FALSE);

  text = var_has_label (var) ? var_get_label (var) : var_get_name (var);
  gtk_tooltip_set_text (tooltip, text);

  return TRUE;
}

static void
add_data_column_cell_renderer (PsppireDataSheet *data_sheet,
                               PsppSheetViewColumn *column)
{
  GtkCellRenderer *cell;
  struct variable *var;

  var = g_object_get_data (G_OBJECT (column), "variable");
  g_return_if_fail (var != NULL);

  if (data_sheet->show_value_labels && var_has_value_labels (var))
    {
      cell = gtk_cell_renderer_combo_new ();
      g_object_set (G_OBJECT (cell),
                    "has-entry", TRUE,
                    "text-column", 0,
                    NULL);
    }
  else
    cell = gtk_cell_renderer_text_new ();

  g_signal_connect (cell, "editing-started",
                    G_CALLBACK (on_data_column_editing_started), NULL);
  g_signal_connect (cell, "edited", G_CALLBACK (on_data_column_edited), NULL);

  g_object_set_data (G_OBJECT (cell), "column", column);
  g_object_set_data (G_OBJECT (cell), "data-sheet", data_sheet);

  pspp_sheet_view_column_clear (column);
  pspp_sheet_view_column_pack_start (column, cell, TRUE);

  pspp_sheet_view_column_set_cell_data_func (
    column, cell, render_data_cell, data_sheet, NULL);
}

static PsppSheetViewColumn *
make_data_column (PsppireDataSheet *data_sheet, gint dict_idx,
                  gint base_width, gint incr_width)
{
  PsppireDataStore *data_store = psppire_data_sheet_get_data_store (data_sheet);
  struct variable *var;
  PsppSheetViewColumn *column;
  char *name;
  int width;

  var = psppire_dict_get_variable (data_store->dict, dict_idx);

  column = pspp_sheet_view_column_new ();

  name = escape_underscores (var_get_name (var));
  pspp_sheet_view_column_set_title (column, name);
  free (name);

  g_object_set_data (G_OBJECT (column), "variable", var);

  width = display_width_to_pixel_width (data_sheet,
                                        var_get_display_width (var),
                                        base_width, incr_width);
  pspp_sheet_view_column_set_min_width (column, 10);
  pspp_sheet_view_column_set_fixed_width (column, width);
  pspp_sheet_view_column_set_resizable (column, TRUE);

  pspp_sheet_view_column_set_clickable (column, TRUE);
  g_signal_connect (column, "notify::width",
                    G_CALLBACK (on_data_column_resized), data_sheet);

  g_signal_connect (column, "button-press-event",
                    G_CALLBACK (on_column_button_press_event),
                    data_sheet);
  g_signal_connect (column, "query-tooltip",
                    G_CALLBACK (on_data_column_query_tooltip), NULL);
  g_signal_connect (column, "popup-menu",
                    G_CALLBACK (on_data_column_popup_menu), data_sheet);

  add_data_column_cell_renderer (data_sheet, column);

  return column;
}

static void
make_new_variable_column (PsppireDataSheet *data_sheet,
                          gint base_width, gint incr_width)
{
  PsppSheetViewColumn *column;
  GtkCellRenderer *cell;
  int width;

  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell, "editable", TRUE, NULL);

  g_signal_connect (cell, "edited", G_CALLBACK (on_new_variable_column_edited),
                    NULL);

  column = pspp_sheet_view_column_new_with_attributes ("", cell, NULL);
  g_object_set_data (G_OBJECT (column), "new-var-column", column);

  width = display_width_to_pixel_width (data_sheet, 8, base_width, incr_width);
  pspp_sheet_view_column_set_min_width (column, 10);
  pspp_sheet_view_column_set_fixed_width (column, width);
  pspp_sheet_view_column_set_tabbable (column, FALSE);

  g_object_set_data (G_OBJECT (cell), "data-sheet", data_sheet);
  g_signal_connect (column, "button-press-event",
                    G_CALLBACK (on_column_button_press_event),
                    data_sheet);
  g_signal_connect (column, "popup-menu",
                    G_CALLBACK (on_data_column_popup_menu), data_sheet);

  pspp_sheet_view_column_set_visible (column, data_sheet->may_create_vars);

  pspp_sheet_view_append_column (PSPP_SHEET_VIEW (data_sheet), column);
  data_sheet->new_variable_column = column;
}

static void
psppire_data_sheet_model_changed (GObject    *gobject,
                                  GParamSpec *pspec,
                                  gpointer    user_data)
{
  PsppireDataSheet *data_sheet = PSPPIRE_DATA_SHEET (gobject);
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppireDataStore *data_store;

  /* Remove old columns. */
  for (;;)
    {
      PsppSheetViewColumn *column = pspp_sheet_view_get_column (sheet_view, 0);
      if (column == NULL)
        break;

      pspp_sheet_view_remove_column (sheet_view, column);
    }
  data_sheet->new_variable_column = NULL;

  if (pspp_sheet_view_get_model (sheet_view) == NULL)
    {
      /* Don't create any columns at all if there's no model.  Otherwise we'll
         create some columns as part of the "dispose" callback for the sheet
         view, which sets the model to NULL.  That causes warnings to be
         logged and is obviously undesirable in any case. */
      return;
    }

  /* Add new columns. */
  data_store = psppire_data_sheet_get_data_store (data_sheet);
  if (data_store != NULL)
    {
      gint base_width, incr_width;
      int i;

      calc_width_conversion (data_sheet, &base_width, &incr_width);

      make_row_number_column (data_sheet, data_store);
      for (i = 0; i < psppire_dict_get_var_cnt (data_store->dict); i++)
        {
          PsppSheetViewColumn *column;

          column = make_data_column (data_sheet, i, base_width, incr_width);
          pspp_sheet_view_append_column (sheet_view, column);
        }
      make_new_variable_column (data_sheet, base_width, incr_width);
    }
}

enum
  {
    PROP_0,
    PROP_DATA_STORE,
    PROP_VALUE_LABELS,
    PROP_CASE_NUMBERS,
    PROP_CURRENT_CASE,
    PROP_MAY_CREATE_VARS,
    PROP_MAY_DELETE_VARS,
    PROP_UI_MANAGER
  };

static void
psppire_data_sheet_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PsppireDataSheet *obj = PSPPIRE_DATA_SHEET (object);

  switch (prop_id)
    {
    case PROP_DATA_STORE:
      psppire_data_sheet_set_data_store (
        obj, PSPPIRE_DATA_STORE (g_value_get_object (value)));
      break;

    case PROP_VALUE_LABELS:
      psppire_data_sheet_set_value_labels (obj, g_value_get_boolean (value));
      break;

    case PROP_CASE_NUMBERS:
      psppire_data_sheet_set_case_numbers (obj, g_value_get_boolean (value));
      break;

    case PROP_CURRENT_CASE:
      psppire_data_sheet_goto_case (obj, g_value_get_long (value));
      break;

    case PROP_MAY_CREATE_VARS:
      psppire_data_sheet_set_may_create_vars (obj,
                                              g_value_get_boolean (value));
      break;

    case PROP_MAY_DELETE_VARS:
      psppire_data_sheet_set_may_delete_vars (obj,
                                              g_value_get_boolean (value));
      break;

    case PROP_UI_MANAGER:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_data_sheet_get_property (GObject      *object,
                                 guint         prop_id,
                                 GValue       *value,
                                 GParamSpec   *pspec)
{
  PsppireDataSheet *obj = PSPPIRE_DATA_SHEET (object);

  switch (prop_id)
    {
    case PROP_DATA_STORE:
      g_value_set_object (value, psppire_data_sheet_get_data_store (obj));
      break;

    case PROP_VALUE_LABELS:
      g_value_set_boolean (value, psppire_data_sheet_get_value_labels (obj));
      break;

    case PROP_CASE_NUMBERS:
      g_value_set_boolean (value, psppire_data_sheet_get_case_numbers (obj));
      break;

    case PROP_CURRENT_CASE:
      g_value_set_long (value, psppire_data_sheet_get_selected_case (obj));
      break;

    case PROP_MAY_CREATE_VARS:
      g_value_set_boolean (value, obj->may_create_vars);
      break;

    case PROP_MAY_DELETE_VARS:
      g_value_set_boolean (value, obj->may_delete_vars);
      break;

    case PROP_UI_MANAGER:
      g_value_set_object (value, psppire_data_sheet_get_ui_manager (obj));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

gboolean
psppire_data_sheet_get_value_labels (const PsppireDataSheet *ds)
{
  return ds->show_value_labels;
}

void
psppire_data_sheet_set_value_labels (PsppireDataSheet *ds,
                                  gboolean show_value_labels)
{
  show_value_labels = !!show_value_labels;
  if (show_value_labels != ds->show_value_labels)
    {
      ds->show_value_labels = show_value_labels;
      g_object_notify (G_OBJECT (ds), "value-labels");

      /* Pretend the model changed, to force the columns to be rebuilt.
         Otherwise cell renderers won't get changed from combo boxes to text
         entries or vice versa. */
      g_object_notify (G_OBJECT (ds), "model");
    }
}

gboolean
psppire_data_sheet_get_case_numbers (const PsppireDataSheet *ds)
{
  return ds->show_case_numbers;
}

void
psppire_data_sheet_set_case_numbers (PsppireDataSheet *ds,
                                     gboolean show_case_numbers)
{
  show_case_numbers = !!show_case_numbers;
  if (show_case_numbers != ds->show_case_numbers)
    {
      PsppSheetViewColumn *column;

      ds->show_case_numbers = show_case_numbers;
      column = pspp_sheet_view_get_column (PSPP_SHEET_VIEW (ds), 0);
      if (column)
        pspp_sheet_view_column_set_visible (column, show_case_numbers);

      g_object_notify (G_OBJECT (ds), "case-numbers");
      gtk_widget_queue_draw (GTK_WIDGET (ds));
    }
}

gboolean
psppire_data_sheet_get_may_create_vars (PsppireDataSheet *data_sheet)
{
  return data_sheet->may_create_vars;
}

void
psppire_data_sheet_set_may_create_vars (PsppireDataSheet *data_sheet,
                                       gboolean may_create_vars)
{
  if (data_sheet->may_create_vars != may_create_vars)
    {
      data_sheet->may_create_vars = may_create_vars;
      if (data_sheet->new_variable_column)
        pspp_sheet_view_column_set_visible (data_sheet->new_variable_column,
                                            may_create_vars);

      on_selection_changed (pspp_sheet_view_get_selection (
                              PSPP_SHEET_VIEW (data_sheet)), NULL);
    }
}

gboolean
psppire_data_sheet_get_may_delete_vars (PsppireDataSheet *data_sheet)
{
  return data_sheet->may_delete_vars;
}

void
psppire_data_sheet_set_may_delete_vars (PsppireDataSheet *data_sheet,
                                       gboolean may_delete_vars)
{
  if (data_sheet->may_delete_vars != may_delete_vars)
    {
      data_sheet->may_delete_vars = may_delete_vars;
      on_selection_changed (pspp_sheet_view_get_selection (
                              PSPP_SHEET_VIEW (data_sheet)), NULL);
    }
}

static PsppSheetViewColumn *
psppire_data_sheet_find_column_for_variable (PsppireDataSheet *data_sheet,
                                             gint dict_index)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppireDataStore *data_store;
  PsppSheetViewColumn *column;
  struct variable *var;
  GList *list, *iter;

  data_store = psppire_data_sheet_get_data_store (data_sheet);
  g_return_val_if_fail (data_store != NULL, NULL);
  g_return_val_if_fail (data_store->dict != NULL, NULL);

  var = psppire_dict_get_variable (data_store->dict, dict_index);
  g_return_val_if_fail (var != NULL, NULL);

  column = NULL;
  list = pspp_sheet_view_get_columns (sheet_view);
  for (iter = list; iter != NULL; iter = iter->next)
    {
      PsppSheetViewColumn *c = iter->data;
      struct variable *v;

      v = g_object_get_data (G_OBJECT (c), "variable");
      if (v == var)
        {
          column = c;
          break;
        }
    }
  g_list_free (list);

  return column;
}

void
psppire_data_sheet_goto_variable (PsppireDataSheet *data_sheet,
                                  gint dict_index)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppSheetViewColumn *column;

  column = psppire_data_sheet_find_column_for_variable (data_sheet,
                                                        dict_index);
  if (column != NULL)
    {
      GtkTreePath *path;

      gint row = psppire_data_sheet_get_current_case (data_sheet);
      path = gtk_tree_path_new_from_indices (row >= 0 ? row : 0, -1);

      pspp_sheet_view_scroll_to_cell (sheet_view, path, column,
                                      FALSE, 0.0, 0.0);
      pspp_sheet_view_set_cursor (sheet_view, path, column, FALSE);
      gtk_tree_path_free (path);
    }
}

struct variable *
psppire_data_sheet_get_current_variable (const PsppireDataSheet *data_sheet)
{
  PsppSheetSelection *selection;
  struct variable *var;
  GList *selected_columns;
  GList *iter;

  selection = pspp_sheet_view_get_selection (PSPP_SHEET_VIEW (data_sheet));
  selected_columns = pspp_sheet_selection_get_selected_columns (selection);

  var = NULL;
  for (iter = selected_columns; iter != NULL; iter = iter->next)
    {
      PsppSheetViewColumn *column = iter->data;
      struct variable *v = g_object_get_data (G_OBJECT (column), "variable");
      if (v != NULL)
        {
          if (var)
            {
              var = NULL;
              break;
            }
          else
            var = v;
        }
    }

  g_list_free (selected_columns);

  return var;

}
void
psppire_data_sheet_goto_case (PsppireDataSheet *data_sheet, gint case_index)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppireDataStore *store = data_sheet->data_store;
  PsppSheetSelection *selection;
  GtkTreePath *path;

  g_return_if_fail (case_index >= 0);
  g_return_if_fail (case_index < psppire_data_store_get_case_count (store));

  path = gtk_tree_path_new_from_indices (case_index, -1);

  /* Select the case. */
  selection = pspp_sheet_view_get_selection (sheet_view);
  pspp_sheet_selection_unselect_all (selection);
  pspp_sheet_selection_select_path (selection, path);
  pspp_sheet_selection_select_all_columns (selection);

  /* Scroll so that the case is visible. */
  pspp_sheet_view_scroll_to_cell (sheet_view, path, NULL, FALSE, 0.0, 0.0);

  gtk_tree_path_free (path);
}

/* Returns the 0-based index of a selected case, if there is at least one, and
   -1 otherwise.

   If more than one case is selected, returns the one with the smallest index,
   that is, the index of the case closest to the beginning of the file.  The
   row that can be used to insert a new case is not considered a case. */
gint
psppire_data_sheet_get_selected_case (const PsppireDataSheet *data_sheet)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppireDataStore *store = data_sheet->data_store;
  const struct range_set_node *node;
  PsppSheetSelection *selection;
  struct range_set *rows;
  gint row;

  selection = pspp_sheet_view_get_selection (sheet_view);
  rows = pspp_sheet_selection_get_range_set (selection);
  node = range_set_first (rows);
  row = (node && node->start < psppire_data_store_get_case_count (store)
         ? node->start
         : -1);
  range_set_destroy (rows);

  return row;
}

/* Returns the 0-based index of a selected case, if exactly one case is
   selected, and -1 otherwise.  Returns -1 if the row that can be used to
   insert a new case is selected. */
gint
psppire_data_sheet_get_current_case (const PsppireDataSheet *data_sheet)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppireDataStore *store = data_sheet->data_store;
  const struct range_set_node *node;
  PsppSheetSelection *selection;
  struct range_set *rows;
  gint row;

  selection = pspp_sheet_view_get_selection (sheet_view);
  if (pspp_sheet_selection_count_selected_rows (selection) != 1)
    return -1;

  rows = pspp_sheet_selection_get_range_set (selection);
  node = range_set_first (rows);
  row = (node && node->start < psppire_data_store_get_case_count (store)
         ? node->start
         : -1);
  range_set_destroy (rows);

  return row;
}

GtkUIManager *
psppire_data_sheet_get_ui_manager (PsppireDataSheet *data_sheet)
{
  if (data_sheet->uim == NULL)
    {
      data_sheet->uim = 
	GTK_UI_MANAGER (get_object_assert (data_sheet->builder,
					   "data_sheet_uim",
					   GTK_TYPE_UI_MANAGER));
      g_object_ref (data_sheet->uim);
    }

  return data_sheet->uim;
}

static void
psppire_data_sheet_dispose (GObject *object)
{
  PsppireDataSheet *data_sheet = PSPPIRE_DATA_SHEET (object);

  if (data_sheet->clip != NULL && data_sheet->on_owner_change_signal != 0)
    {
      g_signal_handler_disconnect (data_sheet->clip,
                                   data_sheet->on_owner_change_signal);
      data_sheet->on_owner_change_signal = 0;
    }

  if (data_sheet->dispose_has_run)
    return;

  data_sheet->dispose_has_run = TRUE;

  psppire_data_sheet_unset_data_store (data_sheet);

  g_object_unref (data_sheet->builder);

  if (data_sheet->uim)
    g_object_unref (data_sheet->uim);

  G_OBJECT_CLASS (psppire_data_sheet_parent_class)->dispose (object);
}

static void
psppire_data_sheet_map (GtkWidget *widget)
{
  PsppireDataSheet *data_sheet = PSPPIRE_DATA_SHEET (widget);

  GTK_WIDGET_CLASS (psppire_data_sheet_parent_class)->map (widget);

  data_sheet->clip = gtk_widget_get_clipboard (widget,
                                               GDK_SELECTION_CLIPBOARD);
  if (data_sheet->on_owner_change_signal)
    g_signal_handler_disconnect (data_sheet->clip,
                                 data_sheet->on_owner_change_signal);
  data_sheet->on_owner_change_signal
    = g_signal_connect (data_sheet->clip, "owner-change",
                        G_CALLBACK (on_owner_change), widget);
  on_owner_change (data_sheet->clip, NULL, widget);
}

static void
psppire_data_sheet_class_init (PsppireDataSheetClass *class)
{
  GObjectClass *gobject_class;
  GtkWidgetClass *widget_class;

  gobject_class = G_OBJECT_CLASS (class);
  gobject_class->set_property = psppire_data_sheet_set_property;
  gobject_class->get_property = psppire_data_sheet_get_property;
  gobject_class->dispose = psppire_data_sheet_dispose;

  widget_class = GTK_WIDGET_CLASS (class);
  widget_class->map = psppire_data_sheet_map;

  g_signal_new ("var-double-clicked",
                G_OBJECT_CLASS_TYPE (gobject_class),
                G_SIGNAL_RUN_LAST,
                0,
                g_signal_accumulator_true_handled, NULL,
                psppire_marshal_BOOLEAN__INT,
                G_TYPE_BOOLEAN, 1, G_TYPE_INT);

  g_object_class_install_property (
    gobject_class, PROP_DATA_STORE,
    g_param_spec_object ("data-store",
                         "Data Store",
                         "The data store for the data sheet to display.",
                         PSPPIRE_TYPE_DATA_STORE,
                         G_PARAM_WRITABLE | G_PARAM_READABLE));

  g_object_class_install_property (
    gobject_class, PROP_VALUE_LABELS,
    g_param_spec_boolean ("value-labels",
                          "Value Labels",
                          "Whether or not the data sheet should display labels instead of values",
			  FALSE,
                          G_PARAM_WRITABLE | G_PARAM_READABLE));

  g_object_class_install_property (
    gobject_class, PROP_CASE_NUMBERS,
    g_param_spec_boolean ("case-numbers",
                          "Case Numbers",
                          "Whether or not the data sheet should display case numbers",
			  FALSE,
                          G_PARAM_WRITABLE | G_PARAM_READABLE));

  g_object_class_install_property (
    gobject_class,
    PROP_CURRENT_CASE,
    g_param_spec_long ("current-case",
		       "Current Case",
		       "Zero based number of the selected case",
		       0, CASENUMBER_MAX,
		       0,
		       G_PARAM_WRITABLE | G_PARAM_READABLE));

  g_object_class_install_property (
    gobject_class,
    PROP_MAY_CREATE_VARS,
    g_param_spec_boolean ("may-create-vars",
                          "May create variables",
                          "Whether the user may create more variables",
                          TRUE,
                          G_PARAM_READWRITE));

  g_object_class_install_property (
    gobject_class,
    PROP_MAY_DELETE_VARS,
    g_param_spec_boolean ("may-delete-vars",
                          "May delete variables",
                          "Whether the user may delete variables",
                          TRUE,
                          G_PARAM_READWRITE));

  g_object_class_install_property (
    gobject_class,
    PROP_UI_MANAGER,
    g_param_spec_object ("ui-manager",
                         "UI Manager",
                         "UI manager for the data sheet.  The client should merge this UI manager with the active UI manager to obtain data sheet specific menu items and tool bar items.",
                         GTK_TYPE_UI_MANAGER,
                         G_PARAM_READABLE));
}

static void
do_popup_menu (GtkWidget *widget, guint button, guint32 time)
{
  PsppireDataSheet *data_sheet = PSPPIRE_DATA_SHEET (widget);
  GtkWidget *menu;

  menu = get_widget_assert (data_sheet->builder, "datasheet-cases-popup");
  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, button, time);
}

static void
on_popup_menu (GtkWidget *widget, gpointer user_data UNUSED)
{
  do_popup_menu (widget, 0, gtk_get_current_event_time ());
}

static gboolean
on_button_pressed (GtkWidget *widget, GdkEventButton *event,
                   gpointer user_data UNUSED)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (widget);

  if (event->type == GDK_BUTTON_PRESS && event->button == 3)
    {
      PsppSheetSelection *selection;

      selection = pspp_sheet_view_get_selection (sheet_view);
      if (pspp_sheet_selection_count_selected_rows (selection) <= 1)
        {
          GtkTreePath *path;

          if (pspp_sheet_view_get_path_at_pos (sheet_view, event->x, event->y,
                                               &path, NULL, NULL, NULL))
            {
              pspp_sheet_selection_unselect_all (selection);
              pspp_sheet_selection_select_path (selection, path);
              pspp_sheet_selection_select_all_columns (selection);
              gtk_tree_path_free (path);
            }
        }

      do_popup_menu (widget, event->button, event->time);

      return TRUE;
    }

  return FALSE;
}

static void
on_edit_clear_cases (GtkAction *action, PsppireDataSheet *data_sheet)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppSheetSelection *selection = pspp_sheet_view_get_selection (sheet_view);
  const struct range_set_node *node;
  struct range_set *selected;

  selected = pspp_sheet_selection_get_range_set (selection);
  for (node = range_set_last (selected); node != NULL;
       node = range_set_prev (selected, node))
    {
      unsigned long int start = range_set_node_get_start (node);
      unsigned long int count = range_set_node_get_width (node);

      psppire_data_store_delete_cases (data_sheet->data_store, start, count);
    }
  range_set_destroy (selected);
}

static void
on_selection_changed (PsppSheetSelection *selection,
                      gpointer user_data UNUSED)
{
  PsppSheetView *sheet_view = pspp_sheet_selection_get_tree_view (selection);
  PsppireDataSheet *data_sheet = PSPPIRE_DATA_SHEET (sheet_view);
  gint n_selected_rows;
  gboolean any_variables_selected;
  gboolean may_delete_cases, may_delete_vars, may_insert_vars;
  GList *list, *iter;
  GtkTreePath *path;
  GtkAction *action;

  n_selected_rows = pspp_sheet_selection_count_selected_rows (selection);

  action = get_action_assert (data_sheet->builder, "edit_insert-case");
  gtk_action_set_sensitive (action, n_selected_rows > 0);

  switch (n_selected_rows)
    {
    case 0:
      may_delete_cases = FALSE;
      break;

    case 1:
      /* The row used for inserting new cases cannot be deleted. */
      path = gtk_tree_path_new_from_indices (
        psppire_data_store_get_case_count (data_sheet->data_store), -1);
      may_delete_cases = !pspp_sheet_selection_path_is_selected (selection,
                                                                 path);
      gtk_tree_path_free (path);
      break;

    default:
      may_delete_cases = TRUE;
      break;
    }
  action = get_action_assert (data_sheet->builder, "edit_clear-cases");
  gtk_action_set_sensitive (action, may_delete_cases);

  any_variables_selected = FALSE;
  may_delete_vars = may_insert_vars = FALSE;
  list = pspp_sheet_selection_get_selected_columns (selection);
  for (iter = list; iter != NULL; iter = iter->next)
    {
      PsppSheetViewColumn *column = iter->data;
      struct variable *var = g_object_get_data (G_OBJECT (column), "variable");

      if (var != NULL)
        {
          may_delete_vars = may_insert_vars = TRUE;
          any_variables_selected = TRUE;
          break;
        }
      if (g_object_get_data (G_OBJECT (column), "new-var-column") != NULL)
        may_insert_vars = TRUE;
    }
  g_list_free (list);

  may_insert_vars = may_insert_vars && data_sheet->may_create_vars;
  may_delete_vars = may_delete_vars && data_sheet->may_delete_vars;

  action = get_action_assert (data_sheet->builder, "edit_insert-variable");
  gtk_action_set_sensitive (action, may_insert_vars);

  action = get_action_assert (data_sheet->builder, "edit_clear-variables");
  gtk_action_set_sensitive (action, may_delete_vars);

  action = get_action_assert (data_sheet->builder, "sort-up");
  gtk_action_set_sensitive (action, may_delete_vars);

  action = get_action_assert (data_sheet->builder, "sort-down");
  gtk_action_set_sensitive (action, may_delete_vars);

  psppire_data_sheet_update_clip_actions (data_sheet);
  psppire_data_sheet_update_primary_selection (data_sheet,
                                               (n_selected_rows > 0
                                                && any_variables_selected));
}

static gboolean
psppire_data_sheet_get_selected_range (PsppireDataSheet *data_sheet,
                                    struct range_set **rowsp,
                                    struct range_set **colsp)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppireDataStore *data_store = data_sheet->data_store;
  PsppSheetSelection *selection = pspp_sheet_view_get_selection (sheet_view);
  unsigned long n_cases;
  struct range_set *rows, *cols;
  GList *list, *iter;

  if (data_store == NULL)
    return FALSE;
  n_cases = psppire_data_store_get_case_count (data_store);

  rows = pspp_sheet_selection_get_range_set (selection);
  range_set_set0 (rows, n_cases, ULONG_MAX - n_cases);
  if (range_set_is_empty (rows))
    {
      range_set_destroy (rows);
      return FALSE;
    }

  cols = range_set_create ();
  list = pspp_sheet_selection_get_selected_columns (selection);
  for (iter = list; iter != NULL; iter = iter->next)
    {
      PsppSheetViewColumn *column = iter->data;
      struct variable *var = g_object_get_data (G_OBJECT (column), "variable");

      if (var != NULL)
        range_set_set1 (cols, var_get_dict_index (var), 1);
    }
  g_list_free (list);
  if (range_set_is_empty (cols))
    {
      range_set_destroy (rows);
      range_set_destroy (cols);
      return FALSE;
    }

  *rowsp = rows;
  *colsp = cols;
  return TRUE;
}

static void
on_edit_insert_case (GtkAction *action, PsppireDataSheet *data_sheet)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppSheetSelection *selection = pspp_sheet_view_get_selection (sheet_view);
  PsppireDataStore *data_store = data_sheet->data_store;
  struct range_set *selected;
  unsigned long row;

  selected = pspp_sheet_selection_get_range_set (selection);
  row = range_set_scan (selected, 0);
  range_set_destroy (selected);

  if (row <= psppire_data_store_get_case_count (data_store))
    psppire_data_store_insert_new_case (data_store, row);
}

static void
on_edit_insert_variable (GtkAction *action, PsppireDataSheet *data_sheet)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppSheetSelection *selection = pspp_sheet_view_get_selection (sheet_view);
  PsppireDict *dict = data_sheet->data_store->dict;
  PsppSheetViewColumn *column;
  struct variable *var;
  gchar name[64];
  GList *list;
  gint index;

  list = pspp_sheet_selection_get_selected_columns (selection);
  if (list == NULL)
    return;
  column = list->data;
  g_list_free (list);

  var = g_object_get_data (G_OBJECT (column), "variable");
  index = var ? var_get_dict_index (var) : psppire_dict_get_var_cnt (dict);
  if (psppire_dict_generate_name (dict, name, sizeof name))
    psppire_dict_insert_variable (dict, index, name);
}

static void
on_edit_clear_variables (GtkAction *action, PsppireDataSheet *data_sheet)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppSheetSelection *selection = pspp_sheet_view_get_selection (sheet_view);
  PsppireDict *dict = data_sheet->data_store->dict;
  GList *list, *iter;

  list = pspp_sheet_selection_get_selected_columns (selection);
  if (list == NULL)
    return;
  list = g_list_reverse (list);
  for (iter = list; iter; iter = iter->next)
    {
      PsppSheetViewColumn *column = iter->data;
      struct variable *var;

      var = g_object_get_data (G_OBJECT (column), "variable");
      if (var != NULL)
        psppire_dict_delete_variables (dict, var_get_dict_index (var), 1);
    }
  g_list_free (list);
}

enum sort_order
  {
    SORT_ASCEND,
    SORT_DESCEND
  };

static void
do_sort (PsppireDataSheet *data_sheet, enum sort_order order)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  PsppSheetSelection *selection = pspp_sheet_view_get_selection (sheet_view);
  PsppireDataWindow *pdw;
  GList *list, *iter;
  GString *syntax;
  int n_vars;

  pdw = psppire_data_window_for_data_store (data_sheet->data_store);
  g_return_if_fail (pdw != NULL);

  list = pspp_sheet_selection_get_selected_columns (selection);

  syntax = g_string_new ("SORT CASES BY");
  n_vars = 0;
  for (iter = list; iter; iter = iter->next)
    {
      PsppSheetViewColumn *column = iter->data;
      struct variable *var;

      var = g_object_get_data (G_OBJECT (column), "variable");
      if (var != NULL)
        {
          g_string_append_printf (syntax, " %s", var_get_name (var));
          n_vars++;
        }
    }
  if (n_vars > 0)
    {
      if (order == SORT_DESCEND)
        g_string_append (syntax, " (DOWN)");
      g_string_append_c (syntax, '.');
      execute_const_syntax_string (pdw, syntax->str);
    }
  g_string_free (syntax, TRUE);
}

void
on_sort_up (GtkAction *action, PsppireDataSheet *data_sheet)
{
  do_sort (data_sheet, SORT_ASCEND);
}

void
on_sort_down (GtkAction *action, PsppireDataSheet *data_sheet)
{
  do_sort (data_sheet, SORT_DESCEND);
}

void
on_edit_goto_case (GtkAction *action, PsppireDataSheet *data_sheet)
{
  goto_case_dialog (data_sheet);
}

void
on_edit_find (GtkAction *action, PsppireDataSheet *data_sheet)
{
  PsppireDataWindow *pdw;

  pdw = psppire_data_window_for_data_store (data_sheet->data_store);
  g_return_if_fail (pdw != NULL);

  find_dialog (pdw);
}

void
on_edit_copy (GtkAction *action, PsppireDataSheet *data_sheet)
{
  psppire_data_sheet_set_clip (data_sheet, FALSE);
}

void
on_edit_cut (GtkAction *action, PsppireDataSheet *data_sheet)
{
  psppire_data_sheet_set_clip (data_sheet, TRUE);
}

void
on_edit_paste (GtkAction *action, PsppireDataSheet *data_sheet)
{
  GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (data_sheet));
  GtkClipboard *clipboard =
    gtk_clipboard_get_for_display (display, GDK_SELECTION_CLIPBOARD);

  gtk_clipboard_request_contents (clipboard,
                                  gdk_atom_intern ("UTF8_STRING", TRUE),
                                  psppire_data_sheet_clip_received_cb,
                                  data_sheet);
}

static void
psppire_data_sheet_init (PsppireDataSheet *obj)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (obj);
  GtkAction *action;

  obj->show_value_labels = FALSE;
  obj->show_case_numbers = TRUE;
  obj->may_create_vars = TRUE;
  obj->may_delete_vars = TRUE;

  obj->owns_primary_selection = FALSE;

  obj->scroll_to_bottom_signal = 0;
  obj->scroll_to_right_signal = 0;
  obj->on_owner_change_signal = 0;
  obj->new_variable_column = NULL;
  obj->container = NULL;

  obj->uim = NULL;
  obj->dispose_has_run = FALSE;

  pspp_sheet_view_set_special_cells (sheet_view, PSPP_SHEET_VIEW_SPECIAL_CELLS_YES);

  g_signal_connect (obj, "notify::model",
                    G_CALLBACK (psppire_data_sheet_model_changed), NULL);

  pspp_sheet_view_set_rubber_banding (sheet_view, TRUE);
  pspp_sheet_selection_set_mode (pspp_sheet_view_get_selection (sheet_view),
                                 PSPP_SHEET_SELECTION_RECTANGLE);

  g_object_set (G_OBJECT (obj), "has-tooltip", TRUE, (void *) NULL);
  g_signal_connect (obj, "query-tooltip",
                    G_CALLBACK (on_query_tooltip), NULL);
  g_signal_connect (obj, "button-press-event",
                    G_CALLBACK (on_button_pressed), NULL);
  g_signal_connect (obj, "popup-menu", G_CALLBACK (on_popup_menu), NULL);

  obj->builder = builder_new ("data-sheet.ui");

  action = get_action_assert (obj->builder, "edit_clear-cases");
  g_signal_connect (action, "activate", G_CALLBACK (on_edit_clear_cases),
                    obj);
  gtk_action_set_sensitive (action, FALSE);
  g_signal_connect (pspp_sheet_view_get_selection (sheet_view),
                    "changed", G_CALLBACK (on_selection_changed), NULL);

  action = get_action_assert (obj->builder, "edit_insert-case");
  g_signal_connect (action, "activate", G_CALLBACK (on_edit_insert_case),
                    obj);

  action = get_action_assert (obj->builder, "edit_insert-variable");
  g_signal_connect (action, "activate", G_CALLBACK (on_edit_insert_variable),
                    obj);

  action = get_action_assert (obj->builder, "edit_goto-case");
  g_signal_connect (action, "activate", G_CALLBACK (on_edit_goto_case),
                    obj);

  action = get_action_assert (obj->builder, "edit_copy");
  g_signal_connect (action, "activate", G_CALLBACK (on_edit_copy), obj);

  action = get_action_assert (obj->builder, "edit_cut");
  g_signal_connect (action, "activate", G_CALLBACK (on_edit_cut), obj);

  action = get_action_assert (obj->builder, "edit_paste");
  g_signal_connect (action, "activate", G_CALLBACK (on_edit_paste), obj);

  action = get_action_assert (obj->builder, "edit_clear-variables");
  g_signal_connect (action, "activate", G_CALLBACK (on_edit_clear_variables),
                    obj);

  action = get_action_assert (obj->builder, "edit_find");
  g_signal_connect (action, "activate", G_CALLBACK (on_edit_find), obj);

  action = get_action_assert (obj->builder, "sort-up");
  g_signal_connect (action, "activate", G_CALLBACK (on_sort_up), obj);

  action = get_action_assert (obj->builder, "sort-down");
  g_signal_connect (action, "activate", G_CALLBACK (on_sort_down), obj);

}

GtkWidget *
psppire_data_sheet_new (void)
{
  return g_object_new (PSPP_TYPE_DATA_SHEET, NULL);
}

PsppireDataStore *
psppire_data_sheet_get_data_store (PsppireDataSheet *data_sheet)
{
  return data_sheet->data_store;
}

static void
refresh_model (PsppireDataSheet *data_sheet)
{
  pspp_sheet_view_set_model (PSPP_SHEET_VIEW (data_sheet), NULL);

  if (data_sheet->data_store != NULL)
    {
      PsppireEmptyListStore *model;
      GtkAction *action;
      int n_rows;

      n_rows = psppire_data_store_get_case_count (data_sheet->data_store) + 1;
      model = psppire_empty_list_store_new (n_rows);
      pspp_sheet_view_set_model (PSPP_SHEET_VIEW (data_sheet),
                                 GTK_TREE_MODEL (model));
      g_object_unref (model);

      action = get_action_assert (data_sheet->builder, "edit_copy");
      g_signal_connect (action, "activate", G_CALLBACK (on_edit_copy),
                        data_sheet);
    }
}

static void
on_case_inserted (PsppireDataStore *data_store, gint row,
                  PsppireDataSheet *data_sheet)
{
  PsppireEmptyListStore *empty_list_store;
  GtkTreeModel *tree_model;
  gint n_rows;

  g_return_if_fail (data_store == data_sheet->data_store);

  n_rows = psppire_data_store_get_case_count (data_store) + 1;
  if (row == n_rows - 1)
    row++;

  tree_model = pspp_sheet_view_get_model (PSPP_SHEET_VIEW (data_sheet));
  empty_list_store = PSPPIRE_EMPTY_LIST_STORE (tree_model);
  psppire_empty_list_store_set_n_rows (empty_list_store, n_rows);
  psppire_empty_list_store_row_inserted (empty_list_store, row);
}

static void
on_cases_deleted (PsppireDataStore *data_store, gint first, gint n_cases,
                  PsppireDataSheet *data_sheet)
{

  g_return_if_fail (data_store == data_sheet->data_store);

  if (n_cases > 1)
    {
      /* This is a bit of a cop-out.  We could do better, if it ever turns out
         that this performs too poorly. */
      refresh_model (data_sheet);
    }
  else
    {
      PsppireEmptyListStore *empty_list_store;
      GtkTreeModel *tree_model;
      gint n_rows = psppire_data_store_get_case_count (data_store) + 1;

      tree_model = pspp_sheet_view_get_model (PSPP_SHEET_VIEW (data_sheet));
      empty_list_store = PSPPIRE_EMPTY_LIST_STORE (tree_model);
      psppire_empty_list_store_set_n_rows (empty_list_store, n_rows);
      psppire_empty_list_store_row_deleted (empty_list_store, first);
    }
}

static void
on_case_change (PsppireDataStore *data_store, gint row,
                PsppireDataSheet *data_sheet)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);

  pspp_sheet_view_stop_editing (sheet_view, TRUE);
  gtk_widget_queue_draw (GTK_WIDGET (data_sheet));
}

static void
on_backend_changed (PsppireDataStore *data_store,
                    PsppireDataSheet *data_sheet)
{
  g_return_if_fail (data_store == data_sheet->data_store);
  refresh_model (data_sheet);
}

static void
on_variable_display_width_changed (PsppireDict *dict, int dict_index,
                                   PsppireDataSheet *data_sheet)
{
  PsppireDataStore *data_store = psppire_data_sheet_get_data_store (data_sheet);
  PsppSheetViewColumn *column;
  struct variable *var;
  int display_width;
  gint pixel_width;

  g_return_if_fail (data_sheet->data_store != NULL);
  g_return_if_fail (dict == data_sheet->data_store->dict);

  column = psppire_data_sheet_find_column_for_variable (data_sheet,
                                                        dict_index);
  if (column == NULL)
    return;

  var = psppire_dict_get_variable (data_store->dict, dict_index);
  g_return_if_fail (var != NULL);

  pixel_width = pspp_sheet_view_column_get_fixed_width (column);
  display_width = display_width_from_pixel_width (data_sheet, pixel_width);
  if (display_width != var_get_display_width (var))
    {
      gint base_width, incr_width;

      display_width = var_get_display_width (var);
      calc_width_conversion (data_sheet, &base_width, &incr_width);
      pixel_width = display_width_to_pixel_width (data_sheet, display_width,
                                                  base_width, incr_width);
      pspp_sheet_view_column_set_fixed_width (column, pixel_width);
    }
}

static void
on_variable_changed (PsppireDict *dict, int dict_index,
		     guint what, const struct variable *oldvar,
                     PsppireDataSheet *data_sheet)
{
  PsppireDataStore *data_store = psppire_data_sheet_get_data_store (data_sheet);
  PsppSheetViewColumn *column;
  GtkCellRenderer *cell;
  struct variable *var;
  GList *cells;
  char *name;

  g_return_if_fail (data_sheet->data_store != NULL);
  g_return_if_fail (dict == data_sheet->data_store->dict);


  if (what & VAR_TRAIT_DISPLAY_WIDTH)
    on_variable_display_width_changed (dict, dict_index, data_sheet);

  column = psppire_data_sheet_find_column_for_variable (data_sheet,
                                                        dict_index);
  if (column == NULL)
    return;


  var = psppire_dict_get_variable (data_store->dict, dict_index);
  g_return_if_fail (var != NULL);

  name = escape_underscores (var_get_name (var));
  if (strcmp (name, pspp_sheet_view_column_get_title (column)))
    pspp_sheet_view_column_set_title (column, name);
  free (name);

  cells = pspp_sheet_view_column_get_cell_renderers (column);
  g_return_if_fail (cells);
  cell = cells->data;
  g_list_free (cells);

  if (var_has_value_labels (var) != GTK_IS_CELL_RENDERER_COMBO (cell))
    {
      /* Stop editing before we delete and replace the cell renderers.
         Otherwise if this column is currently being edited, an eventual call
         to pspp_sheet_view_stop_editing() will obtain a NULL cell and pass
         that to gtk_cell_renderer_stop_editing(), which causes a critical.

         It's possible that this is a bug in PsppSheetView, and it's possible
         that PsppSheetView inherits that from GtkTreeView, but I haven't
         investigated yet. */
      pspp_sheet_view_stop_editing (PSPP_SHEET_VIEW (data_sheet), TRUE);

      add_data_column_cell_renderer (data_sheet, column);
    }
}

static void
on_variable_inserted (PsppireDict *dict, int var_index,
                      PsppireDataSheet *data_sheet)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  gint base_width, incr_width;
  PsppSheetViewColumn *column;

  calc_width_conversion (data_sheet, &base_width, &incr_width);
  column = make_data_column (data_sheet, var_index, base_width, incr_width);
  pspp_sheet_view_insert_column (sheet_view, column, var_index + 1);
}

static void
on_variable_deleted (PsppireDict *dict,
                     const struct variable *var, int case_idx, int width,
                     PsppireDataSheet *data_sheet)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (data_sheet);
  GList *columns, *iter;

  columns = pspp_sheet_view_get_columns (sheet_view);
  for (iter = columns; iter != NULL; iter = iter->next)
    {
      PsppSheetViewColumn *column = iter->data;
      const struct variable *column_var;

      column_var = g_object_get_data (G_OBJECT (column), "variable");
      if (column_var == var)
        pspp_sheet_view_remove_column (sheet_view, column);
    }
  g_list_free (columns);
}

static void
psppire_data_sheet_unset_data_store (PsppireDataSheet *data_sheet)
{
  PsppireDataStore *store = data_sheet->data_store;

  if (store == NULL)
    return;

  data_sheet->data_store = NULL;

  g_signal_handlers_disconnect_by_func (
    store, G_CALLBACK (on_backend_changed), data_sheet);
  g_signal_handlers_disconnect_by_func (
    store, G_CALLBACK (on_case_inserted), data_sheet);
  g_signal_handlers_disconnect_by_func (
    store, G_CALLBACK (on_cases_deleted), data_sheet);
  g_signal_handlers_disconnect_by_func (
    store, G_CALLBACK (on_case_change), data_sheet);

  g_signal_handlers_disconnect_by_func (
    store->dict, G_CALLBACK (on_variable_changed), data_sheet);
  g_signal_handlers_disconnect_by_func (
    store->dict, G_CALLBACK (on_variable_display_width_changed), data_sheet);
  g_signal_handlers_disconnect_by_func (
    store->dict, G_CALLBACK (on_variable_inserted), data_sheet);
  g_signal_handlers_disconnect_by_func (
    store->dict, G_CALLBACK (on_variable_deleted), data_sheet);

  g_object_unref (store);
}

void
psppire_data_sheet_set_data_store (PsppireDataSheet *data_sheet,
                                PsppireDataStore *data_store)
{
  psppire_data_sheet_unset_data_store (data_sheet);

  data_sheet->data_store = data_store;
  if (data_store != NULL)
    {
      g_object_ref (data_store);
      g_signal_connect (data_store, "backend-changed",
                        G_CALLBACK (on_backend_changed), data_sheet);
      g_signal_connect (data_store, "case-inserted",
                        G_CALLBACK (on_case_inserted), data_sheet);
      g_signal_connect (data_store, "cases-deleted",
                        G_CALLBACK (on_cases_deleted), data_sheet);
      g_signal_connect (data_store, "case-changed",
                        G_CALLBACK (on_case_change), data_sheet);

      /* XXX it's unclean to hook into the dict this way--what if the dict
         changes?  As of this writing, though, nothing ever changes the
         data_store's dict. */
      g_signal_connect (data_store->dict, "variable-changed",
                        G_CALLBACK (on_variable_changed),
                        data_sheet);
      g_signal_connect (data_store->dict, "variable-inserted",
                        G_CALLBACK (on_variable_inserted), data_sheet);
      g_signal_connect (data_store->dict, "variable-deleted",
                        G_CALLBACK (on_variable_deleted), data_sheet);
    }
  refresh_model (data_sheet);
}

/* Clipboard stuff */

/* A casereader and dictionary holding the data currently in the clip */
static struct casereader *clip_datasheet = NULL;
static struct dictionary *clip_dict = NULL;


static void psppire_data_sheet_update_clipboard (PsppireDataSheet *);

static gboolean
psppire_data_sheet_fetch_clip (PsppireDataSheet *data_sheet, gboolean cut,
                               struct casereader **readerp,
                               struct dictionary **dictp)
{
  struct casewriter *writer ;
  PsppireDataStore *ds = psppire_data_sheet_get_data_store (data_sheet);
  struct case_map *map = NULL;
  struct range_set *rows, *cols;
  const struct range_set_node *node;
  struct dictionary *dict;

  if (!psppire_data_sheet_get_selected_range (data_sheet, &rows, &cols))
    {
      *readerp = NULL;
      *dictp = NULL;
      return FALSE;
    }

  /* Construct clip dictionary. */
  *dictp = dict = dict_create (dict_get_encoding (ds->dict->dict));
  RANGE_SET_FOR_EACH (node, cols)
    {
      int dict_index;

      for (dict_index = node->start; dict_index < node->end; dict_index++)
        {
          struct variable *var = dict_get_var (ds->dict->dict, dict_index);
          dict_clone_var_assert (dict, var);
        }
    }

  /* Construct clip data. */
  map = case_map_by_name (ds->dict->dict, dict);
  writer = autopaging_writer_create (dict_get_proto (dict));
  RANGE_SET_FOR_EACH (node, rows)
    {
      unsigned long int row;

      for (row = node->start; row < node->end; row++)
        {
          struct ccase *old = psppire_data_store_get_case (ds, row);
          if (old != NULL)
            casewriter_write (writer, case_map_execute (map, old));
          else
            casewriter_force_error (writer);
        }
    }
  case_map_destroy (map);

  /* Clear data that we copied out, if we're doing a "cut". */
  if (cut && !casewriter_error (writer))
    {
      RANGE_SET_FOR_EACH (node, rows)
        {
          unsigned long int row;

          for (row = node->start; row < node->end; row++)
            {
              const struct range_set_node *node2;

              RANGE_SET_FOR_EACH (node2, cols)
                {
                  int dict_index;

                  for (dict_index = node2->start; dict_index < node2->end;
                       dict_index++)
                    {
                      struct variable *var;

                      var = dict_get_var (ds->dict->dict, dict_index);
                      psppire_data_store_set_string (ds, "", row,
                                                     var, false);
                    }
                }
            }
        }
    }

  range_set_destroy (rows);
  range_set_destroy (cols);

  *readerp = casewriter_make_reader (writer);

  return TRUE;
}

/* Set the clip from the currently selected range in DATA_SHEET.  If CUT is
   true, clears the original data from DATA_SHEET, otherwise leaves the
   original data in-place. */
static void
psppire_data_sheet_set_clip (PsppireDataSheet *data_sheet,
                             gboolean cut)
{
  struct casereader *reader;
  struct dictionary *dict;

  if (psppire_data_sheet_fetch_clip (data_sheet, cut, &reader, &dict))
    {
      casereader_destroy (clip_datasheet);
      dict_destroy (clip_dict);

      clip_datasheet = reader;
      clip_dict = dict;

      psppire_data_sheet_update_clipboard (data_sheet);
    }
}

enum {
  SELECT_FMT_NULL,
  SELECT_FMT_TEXT,
  SELECT_FMT_HTML
};


/* Perform data_out for case CC, variable V, appending to STRING */
static void
data_out_g_string (GString *string, const struct variable *v,
		   const struct ccase *cc)
{
  const struct fmt_spec *fs = var_get_print_format (v);
  const union value *val = case_data (cc, v);

  char *s = data_out (val, var_get_encoding (v), fs);

  g_string_append (string, s);

  g_free (s);
}

static GString *
clip_to_text (struct casereader *datasheet, struct dictionary *dict)
{
  casenumber r;
  GString *string;

  const size_t val_cnt = caseproto_get_n_widths (casereader_get_proto (datasheet));
  const casenumber case_cnt = casereader_get_case_cnt (datasheet);
  const size_t var_cnt = dict_get_var_cnt (dict);

  string = g_string_sized_new (10 * val_cnt * case_cnt);

  for (r = 0 ; r < case_cnt ; ++r )
    {
      int c;
      struct ccase *cc;

      cc = casereader_peek (datasheet, r);
      if (cc == NULL)
	{
	  g_warning ("Clipboard seems to have inexplicably shrunk");
	  break;
	}

      for (c = 0 ; c < var_cnt ; ++c)
	{
	  const struct variable *v = dict_get_var (dict, c);
	  data_out_g_string (string, v, cc);
	  if ( c < val_cnt - 1 )
	    g_string_append (string, "\t");
	}

      if ( r < case_cnt)
	g_string_append (string, "\n");

      case_unref (cc);
    }

  return string;
}


static GString *
clip_to_html (struct casereader *datasheet, struct dictionary *dict)
{
  casenumber r;
  GString *string;

  const size_t val_cnt = caseproto_get_n_widths (casereader_get_proto (datasheet));
  const casenumber case_cnt = casereader_get_case_cnt (datasheet);
  const size_t var_cnt = dict_get_var_cnt (dict);

  /* Guestimate the size needed */
  string = g_string_sized_new (80 + 20 * val_cnt * case_cnt);

  g_string_append (string,
		   "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\">\n");

  g_string_append (string, "<table>\n");
  for (r = 0 ; r < case_cnt ; ++r )
    {
      int c;
      struct ccase *cc = casereader_peek (datasheet, r);
      if (cc == NULL)
	{
	  g_warning ("Clipboard seems to have inexplicably shrunk");
	  break;
	}
      g_string_append (string, "<tr>\n");

      for (c = 0 ; c < var_cnt ; ++c)
	{
	  const struct variable *v = dict_get_var (dict, c);
	  g_string_append (string, "<td>");
	  data_out_g_string (string, v, cc);
	  g_string_append (string, "</td>\n");
	}

      g_string_append (string, "</tr>\n");

      case_unref (cc);
    }
  g_string_append (string, "</table>\n");

  return string;
}



static void
psppire_data_sheet_clipboard_set (GtkSelectionData *selection_data,
                                  guint             info,
                                  struct casereader *reader,
                                  struct dictionary *dict)
{
  GString *string = NULL;

  switch (info)
    {
    case SELECT_FMT_TEXT:
      string = clip_to_text (reader, dict);
      break;
    case SELECT_FMT_HTML:
      string = clip_to_html (reader, dict);
      break;
    default:
      g_assert_not_reached ();
    }

  gtk_selection_data_set (selection_data, selection_data->target,
			  8,
			  (const guchar *) string->str, string->len);

  g_string_free (string, TRUE);
}

static void
psppire_data_sheet_clipboard_get_cb (GtkClipboard     *clipboard,
                                     GtkSelectionData *selection_data,
                                     guint             info,
                                     gpointer          data)
{
  psppire_data_sheet_clipboard_set (selection_data, info,
                                    clip_datasheet, clip_dict);
}

static void
psppire_data_sheet_clipboard_clear_cb (GtkClipboard *clipboard,
                                       gpointer data)
{
  dict_destroy (clip_dict);
  clip_dict = NULL;

  casereader_destroy (clip_datasheet);
  clip_datasheet = NULL;
}


static const GtkTargetEntry targets[] = {
  { "UTF8_STRING",   0, SELECT_FMT_TEXT },
  { "STRING",        0, SELECT_FMT_TEXT },
  { "TEXT",          0, SELECT_FMT_TEXT },
  { "COMPOUND_TEXT", 0, SELECT_FMT_TEXT },
  { "text/plain;charset=utf-8", 0, SELECT_FMT_TEXT },
  { "text/plain",    0, SELECT_FMT_TEXT },
  { "text/html",     0, SELECT_FMT_HTML }
};



static void
psppire_data_sheet_update_clipboard (PsppireDataSheet *sheet)
{
  GtkClipboard *clipboard =
    gtk_widget_get_clipboard (GTK_WIDGET (sheet),
			      GDK_SELECTION_CLIPBOARD);

  if (!gtk_clipboard_set_with_owner (clipboard, targets,
				     G_N_ELEMENTS (targets),
				     psppire_data_sheet_clipboard_get_cb,
                                     psppire_data_sheet_clipboard_clear_cb,
				     G_OBJECT (sheet)))
    psppire_data_sheet_clipboard_clear_cb (clipboard, sheet);
}

static void
psppire_data_sheet_update_clip_actions (PsppireDataSheet *data_sheet)
{
  struct range_set *rows, *cols;
  GtkAction *action;
  gboolean enable;

  enable = psppire_data_sheet_get_selected_range (data_sheet, &rows, &cols);
  if (enable)
    {
      range_set_destroy (rows);
      range_set_destroy (cols);
    }

  action = get_action_assert (data_sheet->builder, "edit_copy");
  gtk_action_set_sensitive (action, enable);

  action = get_action_assert (data_sheet->builder, "edit_cut");
  gtk_action_set_sensitive (action, enable);
}

static void
psppire_data_sheet_primary_get_cb (GtkClipboard     *clipboard,
                                   GtkSelectionData *selection_data,
                                   guint             info,
                                   gpointer          data)
{
  PsppireDataSheet *data_sheet = PSPPIRE_DATA_SHEET (data);
  struct casereader *reader;
  struct dictionary *dict;

  if (psppire_data_sheet_fetch_clip (data_sheet, FALSE, &reader, &dict))
    {
      psppire_data_sheet_clipboard_set (selection_data, info,
                                        reader, dict);
      casereader_destroy (reader);
      dict_destroy (dict);
    }
}

static void
psppire_data_sheet_update_primary_selection (PsppireDataSheet *data_sheet,
                                             gboolean should_own)
{
  GtkClipboard *clipboard;
  GdkDisplay *display;

  display = gtk_widget_get_display (GTK_WIDGET (data_sheet));
  clipboard = gtk_clipboard_get_for_display (display, GDK_SELECTION_PRIMARY);
  g_return_if_fail (clipboard != NULL);

  if (data_sheet->owns_primary_selection && !should_own)
    {
      data_sheet->owns_primary_selection = FALSE;
      gtk_clipboard_clear (clipboard);
    }
  else if (should_own)
    data_sheet->owns_primary_selection =
      gtk_clipboard_set_with_owner (clipboard, targets, G_N_ELEMENTS (targets),
                                    psppire_data_sheet_primary_get_cb,
                                    NULL, G_OBJECT (data_sheet));
}

/* A callback for when the clipboard contents have been received. */
static void
psppire_data_sheet_clip_received_cb (GtkClipboard *clipboard,
                                     GtkSelectionData *sd,
                                     gpointer data)
{
  PsppireDataSheet *data_sheet = data;
  PsppireDataStore *store = data_sheet->data_store;
  struct range_set *rows, *cols;
  gint count = 0;
  gint next_row, next_column;
  gint first_column;
  char *c;

  if ( sd->length < 0 )
    return;

  if ( sd->type != gdk_atom_intern ("UTF8_STRING", FALSE))
    return;

  c = (char *) sd->data;

  /* Get the starting selected position in the data sheet.  (Possibly we should
     only paste into the selected range if it's larger than one cell?) */
  if (!psppire_data_sheet_get_selected_range (data_sheet, &rows, &cols))
    return;
  next_row = range_set_first (rows)->start;
  first_column = next_column = range_set_first (cols)->start;
  range_set_destroy (rows);
  range_set_destroy (cols);

  g_return_if_fail (next_row >= 0);
  g_return_if_fail (next_column >= 0);

  while (count < sd->length)
    {
      gint row = next_row;
      gint column = next_column;
      struct variable *var;
      char *s = c;

      while (*c != '\t' && *c != '\n' && count < sd->length)
        {
          c++;
          count++;
        }
      if ( *c == '\t')
        {
          next_row = row ;
          next_column = column + 1;
        }
      else if ( *c == '\n')
        {
          next_row = row + 1;
          next_column = first_column;
        }
      *c++ = '\0';
      count++;

      var = psppire_dict_get_variable (store->dict, column);
      if (var != NULL)
        psppire_data_store_set_string (store, s, row, var, FALSE);
    }
}

static void
psppire_data_sheet_targets_received_cb (GtkClipboard *clipboard,
                                        GdkAtom *atoms,
                                        gint n_atoms,
                                        gpointer data)
{
  GtkAction *action = GTK_ACTION (data);
  gboolean compatible_target;
  gint i;

  compatible_target = FALSE;
  for (i = 0; i < G_N_ELEMENTS (targets); i++)
    {
      GdkAtom target = gdk_atom_intern (targets[i].target, TRUE);
      gint j;

      for (j = 0; j < n_atoms; j++)
        if (target == atoms[j])
          {
            compatible_target = TRUE;
            break;
          }
    }

  gtk_action_set_sensitive (action, compatible_target);
  g_object_unref (action);
}

static void
on_owner_change (GtkClipboard *clip, GdkEventOwnerChange *event, gpointer data)
{
  PsppireDataSheet *data_sheet = PSPPIRE_DATA_SHEET (data);
  GtkAction *action = get_action_assert (data_sheet->builder, "edit_paste");

  g_object_ref (action);
  gtk_clipboard_request_targets (clip, psppire_data_sheet_targets_received_cb,
                                 action);
}
