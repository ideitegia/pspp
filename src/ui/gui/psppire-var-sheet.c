/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include "ui/gui/psppire-var-sheet.h"

#include "data/format.h"
#include "data/value-labels.h"
#include "libpspp/range-set.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/helper.h"
#include "ui/gui/missing-val-dialog.h"
#include "ui/gui/pspp-sheet-selection.h"
#include "ui/gui/psppire-cell-renderer-button.h"
#include "ui/gui/psppire-data-editor.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dialog-action-var-info.h"
#include "ui/gui/psppire-dictview.h"
#include "ui/gui/psppire-empty-list-store.h"
#include "ui/gui/psppire-marshal.h"
#include "ui/gui/val-labs-dialog.h"
#include "ui/gui/var-type-dialog.h"
#include "ui/gui/var-display.h"
#include "ui/gui/var-type-dialog.h"

#include "gl/intprops.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum vs_column
  {
    VS_NAME,
    VS_TYPE,
    VS_WIDTH,
    VS_DECIMALS,
    VS_LABEL,
    VS_VALUES,
    VS_MISSING,
    VS_COLUMNS,
    VS_ALIGN,
    VS_MEASURE,
    VS_ROLE
  };

G_DEFINE_TYPE (PsppireVarSheet, psppire_var_sheet, PSPP_TYPE_SHEET_VIEW);

static void
set_spin_cell (GtkCellRenderer *cell, int value, int min, int max, int step)
{
  char text[INT_BUFSIZE_BOUND (int)];
  GtkAdjustment *adjust;

  if (max > min)
    adjust = GTK_ADJUSTMENT (gtk_adjustment_new (value, min, max,
                                                 step, step, 0.0));
  else
    adjust = NULL;

  sprintf (text, "%d", value);
  g_object_set (cell,
                "text", text,
                "adjustment", adjust,
                "editable", TRUE,
                NULL);
}

static void
error_dialog (GtkWindow *w, gchar *primary_text, gchar *secondary_text)
{
  GtkWidget *dialog =
    gtk_message_dialog_new (w,
			    GTK_DIALOG_DESTROY_WITH_PARENT,
			    GTK_MESSAGE_ERROR,
			    GTK_BUTTONS_CLOSE, "%s", primary_text);

  g_object_set (dialog, "icon-name", "psppicon", NULL);

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
					    "%s", secondary_text);

  gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);
}

static void
on_name_column_editing_started (GtkCellRenderer *cell,
                                GtkCellEditable *editable,
                                const gchar     *path,
                                gpointer         user_data)
{
  PsppireVarSheet *var_sheet = g_object_get_data (G_OBJECT (cell), "var-sheet");
  PsppireDict *dict = psppire_var_sheet_get_dictionary (var_sheet);

  g_return_if_fail (var_sheet);
          g_return_if_fail (dict);

  if (GTK_IS_ENTRY (editable))
    {
      GtkEntry *entry = GTK_ENTRY (editable);
      if (gtk_entry_get_text (entry)[0] == '\0')
        {
          gchar name[64];
          if (psppire_dict_generate_name (dict, name, sizeof name))
            gtk_entry_set_text (entry, name);
        }
    }
}

static void
scroll_to_bottom (GtkWidget      *widget,
                  GtkRequisition *requisition,
                  gpointer        unused UNUSED)
{
  PsppireVarSheet *var_sheet = PSPPIRE_VAR_SHEET (widget);
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (widget);
  GtkAdjustment *vadjust;

  vadjust = pspp_sheet_view_get_vadjustment (sheet_view);
  gtk_adjustment_set_value (vadjust, gtk_adjustment_get_upper (vadjust));

  if (var_sheet->scroll_to_bottom_signal)
    {
      g_signal_handler_disconnect (var_sheet,
                                   var_sheet->scroll_to_bottom_signal);
      var_sheet->scroll_to_bottom_signal = 0;
    }
}

static void
on_var_column_edited (GtkCellRendererText *cell,
                      gchar               *path_string,
                      gchar               *new_text,
                      gpointer             user_data)
{
  PsppireVarSheet *var_sheet = user_data;
  GtkWindow *window = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (var_sheet)));
  struct dictionary *dict = var_sheet->dict->dict;
  enum vs_column column_id;
  struct variable *var;
  int width, decimals;
  GtkTreePath *path;
  gint row;

  path = gtk_tree_path_new_from_string (path_string);
  row = gtk_tree_path_get_indices (path)[0];
  gtk_tree_path_free (path);

  column_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (cell),
                                                  "column-id"));

  var = psppire_dict_get_variable (var_sheet->dict, row);
  if (var == NULL)
    {
      g_return_if_fail (column_id == VS_NAME);

      if (!dict_id_is_valid (dict, new_text, false))
        error_dialog (window,
                      g_strdup (_("Cannot create variable.")),
                      g_strdup_printf (_("\"%s\" is not a valid variable "
                                         "name."), new_text));
      else if (dict_lookup_var (dict, new_text) != NULL)
        error_dialog (window,
                      g_strdup (_("Cannot create variable.")),
                      g_strdup_printf (_("This dictionary already contains "
                                         "a variable named \"%s\"."),
                                         new_text));
      else
        {
          dict_create_var (var_sheet->dict->dict, new_text, 0);
          if (!var_sheet->scroll_to_bottom_signal)
            {
              gtk_widget_queue_resize (GTK_WIDGET (var_sheet));
              var_sheet->scroll_to_bottom_signal =
                g_signal_connect (var_sheet, "size-request",
                                  G_CALLBACK (scroll_to_bottom), NULL);
            }
        }

      return;
    }

  switch (column_id)
    {
    case VS_NAME:
      if (!dict_id_is_valid (dict, new_text, false))
        error_dialog (window,
                      g_strdup (_("Cannot rename variable.")),
                      g_strdup_printf (_("\"%s\" is not a valid variable "
                                         "name."), new_text));
      else if (dict_lookup_var (dict, new_text) != NULL
               && dict_lookup_var (dict, new_text) != var)
        error_dialog (window,
                      g_strdup (_("Cannot rename variable.")),
                      g_strdup_printf (_("This dictionary already contains "
                                         "a variable named \"%s\"."),
                                         new_text));
      else
        dict_rename_var (dict, var, new_text);
      break;

    case VS_TYPE:
      /* Not reachable. */
      break;

    case VS_WIDTH:
      width = atoi (new_text);
      if (width > 0)
        {
          struct fmt_spec format;

          format = *var_get_print_format (var);
          fmt_change_width (&format, width, var_sheet->format_use);
          var_set_width (var, fmt_var_width (&format));
          var_set_both_formats (var, &format);
        }
      break;

    case VS_DECIMALS:
      decimals = atoi (new_text);
      if (decimals >= 0)
        {
          struct fmt_spec format;

          format = *var_get_print_format (var);
          fmt_change_decimals (&format, decimals, var_sheet->format_use);
          var_set_print_format (var, &format);
        }
      break;

    case VS_LABEL:
      var_set_label (var, new_text, false);
      break;

    case VS_VALUES:
    case VS_MISSING:
      break;

    case VS_COLUMNS:
      width = atoi (new_text);
      if (width > 0 && width < 2 * MAX_STRING)
        var_set_display_width (var, width);
      break;

    case VS_ALIGN:
      if (!strcmp (new_text, alignment_to_string (ALIGN_LEFT)))
        var_set_alignment (var, ALIGN_LEFT);
      else if (!strcmp (new_text, alignment_to_string (ALIGN_CENTRE)))
        var_set_alignment (var, ALIGN_CENTRE);
      else if (!strcmp (new_text, alignment_to_string (ALIGN_RIGHT)))
        var_set_alignment (var, ALIGN_RIGHT);
      break;

    case VS_MEASURE:
      if (!strcmp (new_text, measure_to_string (MEASURE_NOMINAL)))
        var_set_measure (var, MEASURE_NOMINAL);
      else if (!strcmp (new_text, measure_to_string (MEASURE_ORDINAL)))
        var_set_measure (var, MEASURE_ORDINAL);
      else if (!strcmp (new_text, measure_to_string (MEASURE_SCALE)))
        var_set_measure (var, MEASURE_SCALE);
      break;

    case VS_ROLE:
      if (!strcmp (new_text, var_role_to_string (ROLE_NONE)))
        var_set_role (var, ROLE_NONE);
      else if (!strcmp (new_text, var_role_to_string (ROLE_INPUT)))
        var_set_role (var, ROLE_INPUT);
      else if (!strcmp (new_text, var_role_to_string (ROLE_OUTPUT)))
        var_set_role (var, ROLE_OUTPUT);
      else if (!strcmp (new_text, var_role_to_string (ROLE_BOTH)))
        var_set_role (var, ROLE_BOTH);
      else if (!strcmp (new_text, var_role_to_string (ROLE_PARTITION)))
        var_set_role (var, ROLE_PARTITION);
      else if (!strcmp (new_text, var_role_to_string (ROLE_SPLIT)))
        var_set_role (var, ROLE_SPLIT);
      break;
    }
}

static void
render_popup_cell (PsppSheetViewColumn *tree_column,
                   GtkCellRenderer *cell,
                   GtkTreeModel *model,
                   GtkTreeIter *iter,
                   void *user_data)
{
  PsppireVarSheet *var_sheet = user_data;
  gint row;

  row = GPOINTER_TO_INT (iter->user_data);
  g_object_set (cell,
                "editable", row < psppire_dict_get_var_cnt (var_sheet->dict),
                NULL);
}

const char *
get_var_align_stock_id (const struct variable *var)
{
  switch (var_get_alignment (var))
    {
    case ALIGN_LEFT:
      return GTK_STOCK_JUSTIFY_LEFT;

    case ALIGN_CENTRE:
      return GTK_STOCK_JUSTIFY_CENTER;

    case ALIGN_RIGHT:
      return GTK_STOCK_JUSTIFY_RIGHT;

    default:
      g_return_val_if_reached ("");
    }
}

static void
render_var_cell (PsppSheetViewColumn *tree_column,
                 GtkCellRenderer *cell,
                 GtkTreeModel *model,
                 GtkTreeIter *iter,
                 void *user_data)
{
  PsppireVarSheet *var_sheet = user_data;
  const struct fmt_spec *print;
  enum vs_column column_id;
  struct variable *var;
  gint row;

  column_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_column),
                                                  "column-number")) - 1;
  row = GPOINTER_TO_INT (iter->user_data);

  if (row >= psppire_dict_get_var_cnt (var_sheet->dict))
    {
      if (GTK_IS_CELL_RENDERER_TEXT (cell))
        {
          g_object_set (cell,
                        "text", "",
                        "editable", column_id == VS_NAME,
                        NULL);
          if (column_id == VS_WIDTH
              || column_id == VS_DECIMALS
              || column_id == VS_COLUMNS)
            g_object_set (cell, "adjustment", NULL, NULL);
        }
      else
        g_object_set (cell, "stock-id", "", NULL);
      return;
    }

  var = psppire_dict_get_variable (var_sheet->dict, row);

  print = var_get_print_format (var);
  switch (column_id)
    {
    case VS_NAME:
      g_object_set (cell,
                    "text", var_get_name (var),
                    "editable", TRUE,
                    NULL);
      break;

    case VS_TYPE:
      g_object_set (cell,
                    "text", fmt_gui_name (print->type),
                    "editable", FALSE,
                    NULL);
      break;

    case VS_WIDTH:
      set_spin_cell (cell, print->w,
                     fmt_min_width (print->type, var_sheet->format_use),
                     fmt_max_width (print->type, var_sheet->format_use),
                     fmt_step_width (print->type));
      break;

    case VS_DECIMALS:
      if (fmt_takes_decimals (print->type))
        {
          int max_w = fmt_max_width (print->type, var_sheet->format_use);
          int max_d = fmt_max_decimals (print->type, max_w,
                                        var_sheet->format_use);
          set_spin_cell (cell, print->d, 0, max_d, 1);
        }
      else
        g_object_set (cell,
                      "text", "",
                      "editable", FALSE,
                      "adjustment", NULL,
                      NULL);
      break;

    case VS_LABEL:
      g_object_set (cell,
                    "text", var_has_label (var) ? var_get_label (var) : "",
                    "editable", TRUE,
                    NULL);
      break;

    case VS_VALUES:
      g_object_set (cell, "editable", FALSE, NULL);
      if ( ! var_has_value_labels (var))
        g_object_set (cell, "text", _("None"), NULL);
      else
        {
          const struct val_labs *vls = var_get_value_labels (var);
          const struct val_lab **labels = val_labs_sorted (vls);
          const struct val_lab *vl = labels[0];
          gchar *vstr = value_to_text (vl->value, var);
          char *text = xasprintf (_("{%s, %s}..."), vstr,
                                  val_lab_get_escaped_label (vl));
          free (vstr);

          g_object_set (cell, "text", text, NULL);
          free (text);
          free (labels);
        }
      break;

    case VS_MISSING:
      {
        char *text = missing_values_to_string (var_sheet->dict, var, NULL);
        g_object_set (cell,
                      "text", text,
                      "editable", FALSE,
                      NULL);
        free (text);
      }
      break;

    case VS_COLUMNS:
      set_spin_cell (cell, var_get_display_width (var), 1, 2 * MAX_STRING, 1);
      break;

    case VS_ALIGN:
      if (GTK_IS_CELL_RENDERER_TEXT (cell))
        g_object_set (cell,
                      "text", alignment_to_string (var_get_alignment (var)),
                      "editable", TRUE,
                      NULL);
      else
        g_object_set (cell, "stock-id", get_var_align_stock_id (var), NULL);
      break;

    case VS_MEASURE:
      if (GTK_IS_CELL_RENDERER_TEXT (cell))
        g_object_set (cell,
                      "text", measure_to_string (var_get_measure (var)),
                      "editable", TRUE,
                      NULL);
      else
        g_object_set (cell, "stock-id",
                      psppire_dict_view_get_var_measurement_stock_id (var),
                      NULL);
      break;

    case VS_ROLE:
      g_object_set (cell,
                    "text", var_role_to_string (var_get_role (var)),
                    "editable", TRUE,
                    NULL);
      break;
    }
}

static struct variable *
path_string_to_variable (PsppireVarSheet *var_sheet, gchar *path_string)
{
  PsppireDict *dict;
  GtkTreePath *path;
  gint row;

  path = gtk_tree_path_new_from_string (path_string);
  row = gtk_tree_path_get_indices (path)[0];
  gtk_tree_path_free (path);

  dict = psppire_var_sheet_get_dictionary (var_sheet);
  g_return_val_if_fail (dict != NULL, NULL);

  return psppire_dict_get_variable (dict, row);
}

static void
on_type_click (PsppireCellRendererButton *cell,
               gchar *path,
               PsppireVarSheet *var_sheet)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (var_sheet));
  struct fmt_spec format;
  struct variable *var;

  var = path_string_to_variable (var_sheet, path);
  g_return_if_fail (var != NULL);

  format = *var_get_print_format (var);
  psppire_var_type_dialog_run (GTK_WINDOW (toplevel), &format);

  var_set_width_and_formats (var, fmt_var_width (&format), &format, &format);
}

static void
on_value_labels_click (PsppireCellRendererButton *cell,
                       gchar *path,
                       PsppireVarSheet *var_sheet)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (var_sheet));
  struct val_labs *labels;
  struct variable *var;

  var = path_string_to_variable (var_sheet, path);
  g_return_if_fail (var != NULL);

  labels = psppire_val_labs_dialog_run (GTK_WINDOW (toplevel), var);
  if (labels)
    {
      var_set_value_labels (var, labels);
      val_labs_destroy (labels);
    }
}

static void
on_missing_values_click (PsppireCellRendererButton *cell,
                         gchar *path,
                         PsppireVarSheet *var_sheet)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (var_sheet));
  struct missing_values mv;
  struct variable *var;

  var = path_string_to_variable (var_sheet, path);
  g_return_if_fail (var != NULL);

  psppire_missing_val_dialog_run (GTK_WINDOW (toplevel), var, &mv);
  var_set_missing_values (var, &mv);
  mv_destroy (&mv);
}

static gint
get_string_width (PsppSheetView *treeview, GtkCellRenderer *renderer,
                  const char *string)
{
  gint width;
  g_object_set (G_OBJECT (renderer),
                PSPPIRE_IS_CELL_RENDERER_BUTTON (renderer) ? "label" : "text",
                string, (void *) NULL);
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

static PsppSheetViewColumn *
add_var_sheet_column (PsppireVarSheet *var_sheet, GtkCellRenderer *renderer,
                      enum vs_column column_id,
                      const char *title, int width)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (var_sheet);
  int title_width, content_width;
  PsppSheetViewColumn *column;

  column = pspp_sheet_view_column_new_with_attributes (title, renderer, NULL);
  g_object_set_data (G_OBJECT (column), "column-number",
                     GINT_TO_POINTER (column_id) + 1);

  pspp_sheet_view_column_set_cell_data_func (
    column, renderer, render_var_cell, var_sheet, NULL);

  title_width = get_string_width (sheet_view, renderer, title);
  content_width = get_monospace_width (sheet_view, renderer, width);
  g_object_set_data (G_OBJECT (column), "content-width",
                     GINT_TO_POINTER (content_width));

  pspp_sheet_view_column_set_fixed_width (column,
                                          MAX (title_width, content_width));
  pspp_sheet_view_column_set_resizable (column, TRUE);

  pspp_sheet_view_append_column (sheet_view, column);

  g_signal_connect (renderer, "edited",
                    G_CALLBACK (on_var_column_edited),
                    var_sheet);
  g_object_set_data (G_OBJECT (renderer), "column-id",
                     GINT_TO_POINTER (column_id));
  g_object_set_data (G_OBJECT (renderer), "var-sheet", var_sheet);

  return column;
}

static PsppSheetViewColumn *
add_text_column (PsppireVarSheet *var_sheet, enum vs_column column_id,
                 const char *title, int width)
{
  return add_var_sheet_column (var_sheet, gtk_cell_renderer_text_new (),
                               column_id, title, width);
}

static PsppSheetViewColumn *
add_spin_column (PsppireVarSheet *var_sheet, enum vs_column column_id,
                 const char *title, int width)
{
  return add_var_sheet_column (var_sheet, gtk_cell_renderer_spin_new (),
                               column_id, title, width);
}

static PsppSheetViewColumn *
add_combo_column (PsppireVarSheet *var_sheet, enum vs_column column_id,
                  const char *title, int width,
                  ...)
{
  GtkCellRenderer *cell;
  GtkListStore *store;
  const char *name;
  va_list args;

  store = gtk_list_store_new (2, G_TYPE_INT, G_TYPE_STRING);
  va_start (args, width);
  while ((name = va_arg (args, const char *)) != NULL)
    {
      int value = va_arg (args, int);
      gtk_list_store_insert_with_values (store, NULL, G_MAXINT,
                                         0, value,
                                         1, name,
                                         -1);
    }
  va_end (args);

  cell = gtk_cell_renderer_combo_new ();
  g_object_set (cell,
                "has-entry", FALSE,
                "model", GTK_TREE_MODEL (store),
                "text-column", 1,
                NULL);

  return add_var_sheet_column (var_sheet, cell, column_id, title, width);

}

static void
add_popup_menu (PsppireVarSheet *var_sheet,
                PsppSheetViewColumn *column,
                void (*on_click) (PsppireCellRendererButton *,
                                  gchar *path,
                                  PsppireVarSheet *var_sheet))
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (var_sheet);
  const char *button_label = "...";
  GtkCellRenderer *button_renderer;
  gint content_width;

  button_renderer = psppire_cell_renderer_button_new ();
  g_object_set (button_renderer,
                "label", button_label,
                "editable", TRUE,
                NULL);
  g_signal_connect (button_renderer, "clicked", G_CALLBACK (on_click),
                    var_sheet);
  pspp_sheet_view_column_pack_start (column, button_renderer, FALSE);
  pspp_sheet_view_column_set_cell_data_func (
    column, button_renderer, render_popup_cell, var_sheet, NULL);

  content_width = GPOINTER_TO_INT (g_object_get_data (
                                     G_OBJECT (column), "content-width"));
  content_width += get_string_width (sheet_view, button_renderer,
                                     button_label);
  if (content_width > pspp_sheet_view_column_get_fixed_width (column))
    pspp_sheet_view_column_set_fixed_width (column, content_width);
}

static gboolean
get_tooltip_location (GtkWidget *widget, GtkTooltip *tooltip,
                      gint wx, gint wy, size_t *row, size_t *column)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  gint bx, by;
  GtkTreePath *path;
  GtkTreeIter iter;
  PsppSheetViewColumn *tree_column;
  GtkTreeModel *tree_model;
  gpointer column_ptr;
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

  column_ptr = g_object_get_data (G_OBJECT (tree_column), "column-number");
  if (column_ptr == NULL)
    return FALSE;
  *column = GPOINTER_TO_INT (column_ptr) - 1;

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
on_query_var_tooltip (GtkWidget *widget, gint wx, gint wy,
                      gboolean keyboard_mode UNUSED,
                      GtkTooltip *tooltip, gpointer *user_data UNUSED)
{
  PsppireVarSheet *var_sheet = PSPPIRE_VAR_SHEET (widget);
  PsppireDict *dict;
  struct variable *var;
  size_t row, column;

  if (!get_tooltip_location (widget, tooltip, wx, wy, &row, &column))
    return FALSE;

  dict = psppire_var_sheet_get_dictionary (var_sheet);
  g_return_val_if_fail (dict != NULL, FALSE);

  if (row >= psppire_dict_get_var_cnt (dict))
    {
      gtk_tooltip_set_text (tooltip, _("Enter a variable name to add a "
                                       "new variable."));
      return TRUE;
    }

  var = psppire_dict_get_variable (dict, row);
  g_return_val_if_fail (var != NULL, FALSE);

  switch (column)
    {
    case VS_TYPE:
      {
        char text[FMT_STRING_LEN_MAX + 1];

        fmt_to_string (var_get_print_format (var), text);
        gtk_tooltip_set_text (tooltip, text);
        return TRUE;
      }

    case VS_VALUES:
      if (var_has_value_labels (var))
        {
          const struct val_labs *vls = var_get_value_labels (var);
          const struct val_lab **labels = val_labs_sorted (vls);
          struct string s;
          size_t i;

          ds_init_empty (&s);
          for (i = 0; i < val_labs_count (vls); i++)
            {
              const struct val_lab *vl = labels[i];
              gchar *vstr;

              if (i >= 10 || ds_length (&s) > 500)
                {
                  ds_put_cstr (&s, "...");
                  break;
                }

              vstr = value_to_text (vl->value, var);
              ds_put_format (&s, _("{%s, %s}\n"), vstr,
                             val_lab_get_escaped_label (vl));
              free (vstr);

            }
          ds_chomp_byte (&s, '\n');

          gtk_tooltip_set_text (tooltip, ds_cstr (&s));
          ds_destroy (&s);
          free (labels);

          return TRUE;
        }
    }

  return FALSE;
}

static void
do_popup_menu (GtkWidget *widget, guint button, guint32 time)
{
  PsppireVarSheet *var_sheet = PSPPIRE_VAR_SHEET (widget);
  GtkWidget *menu;

  menu = get_widget_assert (var_sheet->builder, "varsheet-variable-popup");
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
              gtk_tree_path_free (path);
            }
        }

      do_popup_menu (widget, event->button, event->time);
      return TRUE;
    }

  return FALSE;
}

GType
psppire_fmt_use_get_type (void)
{
  static GType etype = 0;
  if (etype == 0)
    {
      static const GEnumValue values[] =
	{
	  { FMT_FOR_INPUT, "FMT_FOR_INPUT", "input" },
	  { FMT_FOR_OUTPUT, "FMT_FOR_OUTPUT", "output" },
	  { 0, NULL, NULL }
	};

      etype = g_enum_register_static
	(g_intern_static_string ("PsppireFmtUse"), values);
    }
  return etype;
}

enum
  {
    PROP_0,
    PROP_DICTIONARY,
    PROP_MAY_CREATE_VARS,
    PROP_MAY_DELETE_VARS,
    PROP_FORMAT_TYPE,
    PROP_UI_MANAGER
  };

static void
psppire_var_sheet_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  PsppireVarSheet *obj = PSPPIRE_VAR_SHEET (object);

  switch (prop_id)
    {
    case PROP_DICTIONARY:
      psppire_var_sheet_set_dictionary (obj,
                                        PSPPIRE_DICT (g_value_get_object (
                                                        value)));
      break;

    case PROP_MAY_CREATE_VARS:
      psppire_var_sheet_set_may_create_vars (obj,
                                             g_value_get_boolean (value));
      break;

    case PROP_MAY_DELETE_VARS:
      psppire_var_sheet_set_may_delete_vars (obj,
                                             g_value_get_boolean (value));
      break;

    case PROP_FORMAT_TYPE:
      obj->format_use = g_value_get_enum (value);
      break;

    case PROP_UI_MANAGER:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_var_sheet_get_property (GObject      *object,
                                guint         prop_id,
                                GValue       *value,
                                GParamSpec   *pspec)
{
  PsppireVarSheet *obj = PSPPIRE_VAR_SHEET (object);

  switch (prop_id)
    {
    case PROP_DICTIONARY:
      g_value_set_object (value,
                          G_OBJECT (psppire_var_sheet_get_dictionary (obj)));
      break;

    case PROP_MAY_CREATE_VARS:
      g_value_set_boolean (value, obj->may_create_vars);
      break;

    case PROP_MAY_DELETE_VARS:
      g_value_set_boolean (value, obj->may_delete_vars);
      break;

    case PROP_FORMAT_TYPE:
      g_value_set_enum (value, obj->format_use);
      break;

    case PROP_UI_MANAGER:
      g_value_set_object (value, psppire_var_sheet_get_ui_manager (obj));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_var_sheet_dispose (GObject *obj)
{
  PsppireVarSheet *var_sheet = PSPPIRE_VAR_SHEET (obj);
  int i;

  if (var_sheet->dispose_has_run)
    return;

  var_sheet->dispose_has_run = TRUE;

  for (i = 0; i < PSPPIRE_VAR_SHEET_N_SIGNALS; i++)
    if ( var_sheet->dict_signals[i])
      g_signal_handler_disconnect (var_sheet->dict,
				   var_sheet->dict_signals[i]);

  if (var_sheet->dict)
    g_object_unref (var_sheet->dict);
  
  if (var_sheet->uim)
    g_object_unref (var_sheet->uim);

  /* These dialogs are not GObjects (although they should be!)
    But for now, unreffing them only causes a GCritical Error
    so comment them out for now. (and accept the memory leakage)

  g_object_unref (var_sheet->val_labs_dialog);
  g_object_unref (var_sheet->missing_val_dialog);
  g_object_unref (var_sheet->var_type_dialog);
  */

  G_OBJECT_CLASS (psppire_var_sheet_parent_class)->dispose (obj);
}

static void
psppire_var_sheet_class_init (PsppireVarSheetClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);
  GParamSpec *pspec;

  gobject_class->set_property = psppire_var_sheet_set_property;
  gobject_class->get_property = psppire_var_sheet_get_property;
  gobject_class->dispose = psppire_var_sheet_dispose;

  g_signal_new ("var-double-clicked",
                G_OBJECT_CLASS_TYPE (gobject_class),
                G_SIGNAL_RUN_LAST,
                0,
                g_signal_accumulator_true_handled, NULL,
                psppire_marshal_BOOLEAN__INT,
                G_TYPE_BOOLEAN, 1, G_TYPE_INT);

  pspec = g_param_spec_object ("dictionary",
                               "Dictionary displayed by the sheet",
                               "The PsppireDict that the sheet displays "
                               "may allow the user to edit",
                               PSPPIRE_TYPE_DICT,
                               G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_DICTIONARY, pspec);

  pspec = g_param_spec_boolean ("may-create-vars",
                                "May create variables",
                                "Whether the user may create more variables",
                                TRUE,
                                G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_MAY_CREATE_VARS, pspec);

  pspec = g_param_spec_boolean ("may-delete-vars",
                                "May delete variables",
                                "Whether the user may delete variables",
                                TRUE,
                                G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_MAY_DELETE_VARS, pspec);

  pspec = g_param_spec_enum ("format-use",
                             "Use of variable format",
                             ("Whether variables have input or output "
                              "formats"),
                             PSPPIRE_TYPE_FMT_USE,
                             FMT_FOR_OUTPUT,
                             G_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_FORMAT_TYPE, pspec);

  pspec = g_param_spec_object ("ui-manager",
                               "UI Manager",
                               "UI manager for the variable sheet.  The client should merge this UI manager with the active UI manager to obtain variable sheet specific menu items and tool bar items.",
                               GTK_TYPE_UI_MANAGER,
                               G_PARAM_READABLE);
  g_object_class_install_property (gobject_class, PROP_UI_MANAGER, pspec);
}

static void
render_row_number_cell (PsppSheetViewColumn *tree_column,
                        GtkCellRenderer *cell,
                        GtkTreeModel *model,
                        GtkTreeIter *iter,
                        gpointer user_data)
{
  PsppireVarSheet *var_sheet = user_data;
  GValue gvalue = { 0, };
  gint row;

  row = GPOINTER_TO_INT (iter->user_data);

  g_value_init (&gvalue, G_TYPE_INT);
  g_value_set_int (&gvalue, row + 1);
  g_object_set_property (G_OBJECT (cell), "label", &gvalue);
  g_value_unset (&gvalue);

  if (!var_sheet->dict || row < psppire_dict_get_var_cnt (var_sheet->dict))
    g_object_set (cell, "editable", TRUE, NULL);
  else
    g_object_set (cell, "editable", FALSE, NULL);
}

static void
psppire_var_sheet_row_number_double_clicked (PsppireCellRendererButton *button,
                                             gchar *path_string,
                                             PsppireVarSheet *var_sheet)
{
  GtkTreePath *path;

  g_return_if_fail (var_sheet->dict != NULL);

  path = gtk_tree_path_new_from_string (path_string);
  if (gtk_tree_path_get_depth (path) == 1)
    {
      gint *indices = gtk_tree_path_get_indices (path);
      if (indices[0] < psppire_dict_get_var_cnt (var_sheet->dict))
        {
          gboolean handled;
          g_signal_emit_by_name (var_sheet, "var-double-clicked",
                                 indices[0], &handled);
        }
    }
  gtk_tree_path_free (path);
}

static void
psppire_var_sheet_variables_column_clicked (PsppSheetViewColumn *column,
                                            PsppireVarSheet *var_sheet)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (var_sheet);
  PsppSheetSelection *selection = pspp_sheet_view_get_selection (sheet_view);

  pspp_sheet_selection_select_all (selection);
}

static PsppSheetViewColumn *
make_row_number_column (PsppireVarSheet *var_sheet)
{
  PsppSheetViewColumn *column;
  GtkCellRenderer *renderer;

  renderer = psppire_cell_renderer_button_new ();
  g_object_set (renderer, "xalign", 1.0, NULL);
  g_signal_connect (renderer, "double-clicked",
                    G_CALLBACK (psppire_var_sheet_row_number_double_clicked),
                    var_sheet);

  column = pspp_sheet_view_column_new_with_attributes (_("Variable"),
                                                       renderer, NULL);
  pspp_sheet_view_column_set_clickable (column, TRUE);
  pspp_sheet_view_column_set_cell_data_func (
    column, renderer, render_row_number_cell, var_sheet, NULL);
  pspp_sheet_view_column_set_fixed_width (column, 50);
  g_signal_connect (column, "clicked",
                    G_CALLBACK (psppire_var_sheet_variables_column_clicked),
                    var_sheet);

  return column;
}

static void
on_edit_clear_variables (GtkAction *action, PsppireVarSheet *var_sheet)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (var_sheet);
  PsppSheetSelection *selection = pspp_sheet_view_get_selection (sheet_view);
  PsppireDict *dict = var_sheet->dict;
  const struct range_set_node *node;
  struct range_set *selected;

  selected = pspp_sheet_selection_get_range_set (selection);
  for (node = range_set_last (selected); node != NULL;
       node = range_set_prev (selected, node))
    {
      int i;

      for (i = 1; i <= range_set_node_get_width (node); i++)
        {
          unsigned long row = range_set_node_get_end (node) - i;
          if (row >= 0 && row < psppire_dict_get_var_cnt (dict))
            psppire_dict_delete_variables (dict, row, 1);
        }
    }
  range_set_destroy (selected);
}

static void
on_selection_changed (PsppSheetSelection *selection,
                      gpointer user_data UNUSED)
{
  PsppSheetView *sheet_view = pspp_sheet_selection_get_tree_view (selection);
  PsppireVarSheet *var_sheet = PSPPIRE_VAR_SHEET (sheet_view);
  gint n_selected_rows;
  gboolean may_delete;
  GtkTreePath *path;
  GtkAction *action;

  n_selected_rows = pspp_sheet_selection_count_selected_rows (selection);

  action = get_action_assert (var_sheet->builder, "edit_insert-variable");
  gtk_action_set_sensitive (action, (var_sheet->may_create_vars
                                     && n_selected_rows > 0));

  switch (n_selected_rows)
    {
    case 0:
      may_delete = FALSE;
      break;

    case 1:
      /* The row used for inserting new variables cannot be deleted. */
      path = gtk_tree_path_new_from_indices (
        psppire_dict_get_var_cnt (var_sheet->dict), -1);
      may_delete = !pspp_sheet_selection_path_is_selected (selection, path);
      gtk_tree_path_free (path);
      break;

    default:
      may_delete = TRUE;
      break;
    }
  action = get_action_assert (var_sheet->builder, "edit_clear-variables");
  gtk_action_set_sensitive (action, var_sheet->may_delete_vars && may_delete);
}

static void
on_edit_insert_variable (GtkAction *action, PsppireVarSheet *var_sheet)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (var_sheet);
  PsppSheetSelection *selection = pspp_sheet_view_get_selection (sheet_view);
  PsppireDict *dict = var_sheet->dict;
  struct range_set *selected;
  unsigned long row;

  selected = pspp_sheet_selection_get_range_set (selection);
  row = range_set_scan (selected, 0);
  range_set_destroy (selected);

  if (row <= psppire_dict_get_var_cnt (dict))
    {
      gchar name[64];;
      if (psppire_dict_generate_name (dict, name, sizeof name))
        psppire_dict_insert_variable (dict, row, name);
    }
}

static void
psppire_var_sheet_init (PsppireVarSheet *obj)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (obj);
  PsppSheetViewColumn *column;
  GtkCellRenderer *cell;
  GtkAction *action;
  GList *list;

  obj->dict = NULL;
  obj->format_use = FMT_FOR_OUTPUT;
  obj->may_create_vars = TRUE;
  obj->may_delete_vars = TRUE;

  obj->scroll_to_bottom_signal = 0;

  obj->container = NULL;
  obj->dispose_has_run = FALSE;
  obj->uim = NULL;

  pspp_sheet_view_append_column (sheet_view, make_row_number_column (obj));

  column = add_text_column (obj, VS_NAME, _("Name"), 12);
  list = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
  g_signal_connect (list->data, "editing-started",
                    G_CALLBACK (on_name_column_editing_started), NULL);
  g_list_free (list);

  column = add_text_column (obj, VS_TYPE, _("Type"), 8);
  add_popup_menu (obj, column, on_type_click);

  add_spin_column (obj, VS_WIDTH, _("Width"), 5);

  add_spin_column (obj, VS_DECIMALS, _("Decimals"), 2);

  add_text_column (obj, VS_LABEL, _("Label"), 20);

  column = add_text_column (obj, VS_VALUES, _("Value Labels"), 20);
  add_popup_menu (obj, column, on_value_labels_click);

  column = add_text_column (obj, VS_MISSING, _("Missing Values"), 20);
  add_popup_menu (obj, column, on_missing_values_click);

  add_spin_column (obj, VS_COLUMNS, _("Columns"), 3);

  column
   = add_combo_column (obj, VS_ALIGN, _("Align"), 8,
                       alignment_to_string (ALIGN_LEFT), ALIGN_LEFT,
                       alignment_to_string (ALIGN_CENTRE), ALIGN_CENTRE,
                       alignment_to_string (ALIGN_RIGHT), ALIGN_RIGHT,
                       NULL);
  cell = gtk_cell_renderer_pixbuf_new ();
  g_object_set (cell, "width", 16, "height", 16, NULL);
  pspp_sheet_view_column_pack_end (column, cell, FALSE);
  pspp_sheet_view_column_set_cell_data_func (
    column, cell, render_var_cell, obj, NULL);

  column
    = add_combo_column (obj, VS_MEASURE, _("Measure"), 12,
                        measure_to_string (MEASURE_NOMINAL), MEASURE_NOMINAL,
                        measure_to_string (MEASURE_ORDINAL), MEASURE_ORDINAL,
                        measure_to_string (MEASURE_SCALE), MEASURE_SCALE,
                        NULL);
  cell = gtk_cell_renderer_pixbuf_new ();
  g_object_set (cell, "width", 16, "height", 16, NULL);
  pspp_sheet_view_column_pack_end (column, cell, FALSE);
  pspp_sheet_view_column_set_cell_data_func (
    column, cell, render_var_cell, obj, NULL);

  add_combo_column (obj, VS_ROLE, _("Role"), 12,
                    var_role_to_string (ROLE_NONE), ROLE_NONE,
                    var_role_to_string (ROLE_INPUT), ROLE_INPUT,
                    var_role_to_string (ROLE_OUTPUT), ROLE_OUTPUT,
                    var_role_to_string (ROLE_BOTH), ROLE_BOTH,
                    var_role_to_string (ROLE_PARTITION), ROLE_PARTITION,
                    var_role_to_string (ROLE_SPLIT), ROLE_SPLIT,
                    NULL);

  pspp_sheet_view_set_rubber_banding (sheet_view, TRUE);
  pspp_sheet_selection_set_mode (pspp_sheet_view_get_selection (sheet_view),
                                 PSPP_SHEET_SELECTION_MULTIPLE);

  g_object_set (G_OBJECT (obj), "has-tooltip", TRUE, NULL);
  g_signal_connect (obj, "query-tooltip",
                    G_CALLBACK (on_query_var_tooltip), NULL);
  g_signal_connect (obj, "button-press-event",
                    G_CALLBACK (on_button_pressed), NULL);
  g_signal_connect (obj, "popup-menu", G_CALLBACK (on_popup_menu), NULL);

  obj->builder = builder_new ("var-sheet.ui");

  action = get_action_assert (obj->builder, "edit_clear-variables");
  g_signal_connect (action, "activate", G_CALLBACK (on_edit_clear_variables),
                    obj);
  gtk_action_set_sensitive (action, FALSE);
  g_signal_connect (pspp_sheet_view_get_selection (sheet_view),
                    "changed", G_CALLBACK (on_selection_changed), NULL);

  action = get_action_assert (obj->builder, "edit_insert-variable");
  gtk_action_set_sensitive (action, FALSE);
  g_signal_connect (action, "activate", G_CALLBACK (on_edit_insert_variable),
                    obj);
}

GtkWidget *
psppire_var_sheet_new (void)
{
  return g_object_new (PSPPIRE_VAR_SHEET_TYPE, NULL);
}

PsppireDict *
psppire_var_sheet_get_dictionary (PsppireVarSheet *var_sheet)
{
  return var_sheet->dict;
}

static void
refresh_model (PsppireVarSheet *var_sheet)
{
  pspp_sheet_view_set_model (PSPP_SHEET_VIEW (var_sheet), NULL);

  if (var_sheet->dict != NULL)
    {
      PsppireEmptyListStore *store;
      int n_rows;

      n_rows = (psppire_dict_get_var_cnt (var_sheet->dict)
                + var_sheet->may_create_vars);
      store = psppire_empty_list_store_new (n_rows);
      pspp_sheet_view_set_model (PSPP_SHEET_VIEW (var_sheet),
                                 GTK_TREE_MODEL (store));
      g_object_unref (store);
    }
}

static void
on_var_changed (PsppireDict *dict, glong row,
		guint what, const struct variable *oldvar,
		PsppireVarSheet *var_sheet)
{
  PsppireEmptyListStore *store;

  g_return_if_fail (dict == var_sheet->dict);

  store = PSPPIRE_EMPTY_LIST_STORE (pspp_sheet_view_get_model (
                                      PSPP_SHEET_VIEW (var_sheet)));
  g_return_if_fail (store != NULL);

  psppire_empty_list_store_row_changed (store, row);
}

static void
on_var_inserted (PsppireDict *dict, glong row, PsppireVarSheet *var_sheet)
{
  PsppireEmptyListStore *store;
  int n_rows;

  g_return_if_fail (dict == var_sheet->dict);

  store = PSPPIRE_EMPTY_LIST_STORE (pspp_sheet_view_get_model (
                                      PSPP_SHEET_VIEW (var_sheet)));
  g_return_if_fail (store != NULL);

  n_rows = (psppire_dict_get_var_cnt (var_sheet->dict)
            + var_sheet->may_create_vars);
  psppire_empty_list_store_set_n_rows (store, n_rows);
  psppire_empty_list_store_row_inserted (store, row);
}

static void
on_var_deleted (PsppireDict *dict,
                const struct variable *var, int dict_idx, int case_idx,
                PsppireVarSheet *var_sheet)
{
  PsppireEmptyListStore *store;
  int n_rows;

  g_return_if_fail (dict == var_sheet->dict);

  store = PSPPIRE_EMPTY_LIST_STORE (pspp_sheet_view_get_model (
                                      PSPP_SHEET_VIEW (var_sheet)));
  g_return_if_fail (store != NULL);

  n_rows = (psppire_dict_get_var_cnt (var_sheet->dict)
            + var_sheet->may_create_vars);
  psppire_empty_list_store_set_n_rows (store, n_rows);
  psppire_empty_list_store_row_deleted (store, dict_idx);
}

static void
on_backend_changed (PsppireDict *dict, PsppireVarSheet *var_sheet)
{
  g_return_if_fail (dict == var_sheet->dict);
  refresh_model (var_sheet);
}

void
psppire_var_sheet_set_dictionary (PsppireVarSheet *var_sheet,
                                  PsppireDict *dict)
{
  if (var_sheet->dict != NULL)
    {
      int i;
      
      for (i = 0; i < PSPPIRE_VAR_SHEET_N_SIGNALS; i++)
	{
	  if (var_sheet->dict_signals[i])
	    g_signal_handler_disconnect (var_sheet->dict,
					 var_sheet->dict_signals[i]);
	  
	  var_sheet->dict_signals[i] = 0;
	}

      g_object_unref (var_sheet->dict);
    }

  var_sheet->dict = dict;

  if (dict != NULL)
    {
      g_object_ref (dict);

      var_sheet->dict_signals[PSPPIRE_VAR_SHEET_BACKEND_CHANGED]
        = g_signal_connect (dict, "backend-changed",
                            G_CALLBACK (on_backend_changed), var_sheet);

      var_sheet->dict_signals[PSPPIRE_VAR_SHEET_VARIABLE_CHANGED]
        = g_signal_connect (dict, "variable-changed",
                            G_CALLBACK (on_var_changed), var_sheet);

      var_sheet->dict_signals[PSPPIRE_VAR_SHEET_VARIABLE_DELETED]
        = g_signal_connect (dict, "variable-inserted",
                            G_CALLBACK (on_var_inserted), var_sheet);

      var_sheet->dict_signals[PSPPIRE_VAR_SHEET_VARIABLE_INSERTED]
        = g_signal_connect (dict, "variable-deleted",
                            G_CALLBACK (on_var_deleted), var_sheet);
    }

  refresh_model (var_sheet);
}

gboolean
psppire_var_sheet_get_may_create_vars (PsppireVarSheet *var_sheet)
{
  return var_sheet->may_create_vars;
}

void
psppire_var_sheet_set_may_create_vars (PsppireVarSheet *var_sheet,
                                       gboolean may_create_vars)
{
  if (var_sheet->may_create_vars != may_create_vars)
    {
      PsppireEmptyListStore *store;
      gint n_rows;

      var_sheet->may_create_vars = may_create_vars;

      store = PSPPIRE_EMPTY_LIST_STORE (pspp_sheet_view_get_model (
                                          PSPP_SHEET_VIEW (var_sheet)));
      g_return_if_fail (store != NULL);

      n_rows = (psppire_dict_get_var_cnt (var_sheet->dict)
                + var_sheet->may_create_vars);
      psppire_empty_list_store_set_n_rows (store, n_rows);

      if (may_create_vars)
        psppire_empty_list_store_row_inserted (store, n_rows - 1);
      else
        psppire_empty_list_store_row_deleted (store, n_rows);

      on_selection_changed (pspp_sheet_view_get_selection (
                              PSPP_SHEET_VIEW (var_sheet)), NULL);
    }
}

gboolean
psppire_var_sheet_get_may_delete_vars (PsppireVarSheet *var_sheet)
{
  return var_sheet->may_delete_vars;
}

void
psppire_var_sheet_set_may_delete_vars (PsppireVarSheet *var_sheet,
                                       gboolean may_delete_vars)
{
  if (var_sheet->may_delete_vars != may_delete_vars)
    {
      var_sheet->may_delete_vars = may_delete_vars;
      on_selection_changed (pspp_sheet_view_get_selection (
                              PSPP_SHEET_VIEW (var_sheet)), NULL);
    }
}

void
psppire_var_sheet_goto_variable (PsppireVarSheet *var_sheet, int dict_index)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (var_sheet);
  GtkTreePath *path;

  path = gtk_tree_path_new_from_indices (dict_index, -1);
  pspp_sheet_view_scroll_to_cell (sheet_view, path, NULL, FALSE, 0.0, 0.0);
  pspp_sheet_view_set_cursor (sheet_view, path, NULL, FALSE);
  gtk_tree_path_free (path);
}

GtkUIManager *
psppire_var_sheet_get_ui_manager (PsppireVarSheet *var_sheet)
{
  if (var_sheet->uim == NULL)
    {
      var_sheet->uim = GTK_UI_MANAGER (get_object_assert (var_sheet->builder,
							  "var_sheet_uim",
							  GTK_TYPE_UI_MANAGER));
      g_object_ref (var_sheet->uim);
    }

  return var_sheet->uim;
}

