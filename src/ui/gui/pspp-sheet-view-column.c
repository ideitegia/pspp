/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011, 2012 Free Software Foundation, Inc.

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

/* gtktreeviewcolumn.c
 * Copyright (C) 2000  Red Hat, Inc.,  Jonathan Blandford <jrb@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include "ui/gui/pspp-sheet-private.h"

#include <errno.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

#include "ui/gui/psppire-marshal.h"
#include "ui/gui/pspp-sheet-selection.h"
#include "ui/gui/pspp-widget-facade.h"

#define P_(STRING) STRING
#define GTK_PARAM_READABLE G_PARAM_READABLE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB
#define GTK_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

enum
{
  PROP_0,
  PROP_VISIBLE,
  PROP_RESIZABLE,
  PROP_WIDTH,
  PROP_SPACING,
  PROP_FIXED_WIDTH,
  PROP_MIN_WIDTH,
  PROP_MAX_WIDTH,
  PROP_TITLE,
  PROP_EXPAND,
  PROP_CLICKABLE,
  PROP_WIDGET,
  PROP_ALIGNMENT,
  PROP_REORDERABLE,
  PROP_SORT_INDICATOR,
  PROP_SORT_ORDER,
  PROP_SORT_COLUMN_ID,
  PROP_QUICK_EDIT,
  PROP_SELECTED,
  PROP_SELECTABLE,
  PROP_ROW_HEAD,
  PROP_TABBABLE
};

enum
{
  CLICKED,
  QUERY_TOOLTIP,
  POPUP_MENU,
  BUTTON_PRESS_EVENT,
  LAST_SIGNAL
};

typedef struct _PsppSheetViewColumnCellInfo PsppSheetViewColumnCellInfo;
struct _PsppSheetViewColumnCellInfo
{
  GtkCellRenderer *cell;
  GSList *attributes;
  PsppSheetCellDataFunc func;
  gpointer func_data;
  GDestroyNotify destroy;
  gint requested_width;
  gint real_width;
  guint expand : 1;
  guint pack : 1;
  guint has_focus : 1;
  guint in_editing_mode : 1;
};

/* Type methods */
static void pspp_sheet_view_column_cell_layout_init              (GtkCellLayoutIface      *iface);

/* GObject methods */
static void pspp_sheet_view_column_set_property                  (GObject                 *object,
								guint                    prop_id,
								const GValue            *value,
								GParamSpec              *pspec);
static void pspp_sheet_view_column_get_property                  (GObject                 *object,
								guint                    prop_id,
								GValue                  *value,
								GParamSpec              *pspec);
static void pspp_sheet_view_column_finalize                      (GObject                 *object);

/* GtkCellLayout implementation */
static void pspp_sheet_view_column_cell_layout_pack_start         (GtkCellLayout         *cell_layout,
                                                                 GtkCellRenderer       *cell,
                                                                 gboolean               expand);
static void pspp_sheet_view_column_cell_layout_pack_end           (GtkCellLayout         *cell_layout,
                                                                 GtkCellRenderer       *cell,
                                                                 gboolean               expand);
static void pspp_sheet_view_column_cell_layout_clear              (GtkCellLayout         *cell_layout);
static void pspp_sheet_view_column_cell_layout_add_attribute      (GtkCellLayout         *cell_layout,
                                                                 GtkCellRenderer       *cell,
                                                                 const gchar           *attribute,
                                                                 gint                   column);
static void pspp_sheet_view_column_cell_layout_set_cell_data_func (GtkCellLayout         *cell_layout,
                                                                 GtkCellRenderer       *cell,
                                                                 GtkCellLayoutDataFunc  func,
                                                                 gpointer               func_data,
                                                                 GDestroyNotify         destroy);
static void pspp_sheet_view_column_cell_layout_clear_attributes   (GtkCellLayout         *cell_layout,
                                                                 GtkCellRenderer       *cell);
static void pspp_sheet_view_column_cell_layout_reorder            (GtkCellLayout         *cell_layout,
                                                                 GtkCellRenderer       *cell,
                                                                 gint                   position);
static GList *pspp_sheet_view_column_cell_layout_get_cells        (GtkCellLayout         *cell_layout);

/* Button handling code */
static void pspp_sheet_view_column_create_button                 (PsppSheetViewColumn       *tree_column);
void pspp_sheet_view_column_update_button                 (PsppSheetViewColumn       *tree_column);

/* Button signal handlers */
static gint pspp_sheet_view_column_button_event                  (GtkWidget               *widget,
								GdkEvent                *event,
								gpointer                 data);
static void pspp_sheet_view_column_button_clicked                (GtkWidget               *widget,
								gpointer                 data);
static void pspp_sheet_view_column_button_popup_menu (GtkWidget *widget,
                                                      gpointer data);
static gboolean pspp_sheet_view_column_mnemonic_activate         (GtkWidget *widget,
					                        gboolean   group_cycling,
								gpointer   data);
static gboolean on_pspp_sheet_view_column_button_clicked (PsppSheetViewColumn *);
static gboolean on_pspp_sheet_view_column_button_press_event (PsppSheetViewColumn *,
                                                              GdkEventButton *);

/* Property handlers */
static void pspp_sheet_view_model_sort_column_changed            (GtkTreeSortable         *sortable,
								PsppSheetViewColumn       *tree_column);

/* Internal functions */
static void pspp_sheet_view_column_sort                          (PsppSheetViewColumn       *tree_column,
								gpointer                 data);
static void pspp_sheet_view_column_setup_sort_column_id_callback (PsppSheetViewColumn       *tree_column);
static void pspp_sheet_view_column_set_attributesv               (PsppSheetViewColumn       *tree_column,
								GtkCellRenderer         *cell_renderer,
								va_list                  args);
static PsppSheetViewColumnCellInfo *pspp_sheet_view_column_get_cell_info (PsppSheetViewColumn *tree_column,
								      GtkCellRenderer   *cell_renderer);

/* cell list manipulation */
static GList *pspp_sheet_view_column_cell_first                  (PsppSheetViewColumn      *tree_column);
static GList *pspp_sheet_view_column_cell_last                   (PsppSheetViewColumn      *tree_column);
static GList *pspp_sheet_view_column_cell_next                   (PsppSheetViewColumn      *tree_column,
								GList                  *current);
static GList *pspp_sheet_view_column_cell_prev                   (PsppSheetViewColumn      *tree_column,
								GList                  *current);
static void pspp_sheet_view_column_clear_attributes_by_info      (PsppSheetViewColumn      *tree_column,
					                        PsppSheetViewColumnCellInfo *info);
/* GtkBuildable implementation */
static void pspp_sheet_view_column_buildable_init                 (GtkBuildableIface     *iface);

static guint tree_column_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_CODE (PsppSheetViewColumn, pspp_sheet_view_column, GTK_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (GTK_TYPE_CELL_LAYOUT,
						pspp_sheet_view_column_cell_layout_init)
			 G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE,
						pspp_sheet_view_column_buildable_init))


static void
pspp_sheet_view_column_class_init (PsppSheetViewColumnClass *class)
{
  GObjectClass *object_class;

  object_class = (GObjectClass*) class;

  class->clicked = on_pspp_sheet_view_column_button_clicked;
  class->button_press_event = on_pspp_sheet_view_column_button_press_event;

  object_class->finalize = pspp_sheet_view_column_finalize;
  object_class->set_property = pspp_sheet_view_column_set_property;
  object_class->get_property = pspp_sheet_view_column_get_property;
  
  tree_column_signals[CLICKED] =
    g_signal_new ("clicked",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (PsppSheetViewColumnClass, clicked),
                  g_signal_accumulator_true_handled, NULL,
                  psppire_marshal_BOOLEAN__VOID,
                  G_TYPE_BOOLEAN, 0);

  tree_column_signals[POPUP_MENU] =
    g_signal_new ("popup-menu",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  tree_column_signals[QUERY_TOOLTIP] =
    g_signal_new ("query-tooltip",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  g_signal_accumulator_true_handled, NULL,
                  psppire_marshal_BOOLEAN__OBJECT,
                  G_TYPE_BOOLEAN, 1,
                  GTK_TYPE_TOOLTIP);

  tree_column_signals[BUTTON_PRESS_EVENT] =
    g_signal_new ("button-press-event",
                  G_OBJECT_CLASS_TYPE (object_class),
                  G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (PsppSheetViewColumnClass, button_press_event),
                  g_signal_accumulator_true_handled, NULL,
                  psppire_marshal_BOOLEAN__BOXED,
                  G_TYPE_BOOLEAN, 1,
                  GDK_TYPE_EVENT | G_SIGNAL_TYPE_STATIC_SCOPE);

  g_object_class_install_property (object_class,
                                   PROP_VISIBLE,
                                   g_param_spec_boolean ("visible",
                                                        P_("Visible"),
                                                        P_("Whether to display the column"),
                                                         TRUE,
                                                         GTK_PARAM_READWRITE));
  
  g_object_class_install_property (object_class,
                                   PROP_RESIZABLE,
                                   g_param_spec_boolean ("resizable",
							 P_("Resizable"),
							 P_("Column is user-resizable"),
                                                         FALSE,
                                                         GTK_PARAM_READWRITE));
  
  g_object_class_install_property (object_class,
                                   PROP_WIDTH,
                                   g_param_spec_int ("width",
						     P_("Width"),
						     P_("Current width of the column"),
						     0,
						     G_MAXINT,
						     0,
						     GTK_PARAM_READABLE));
  g_object_class_install_property (object_class,
                                   PROP_SPACING,
                                   g_param_spec_int ("spacing",
						     P_("Spacing"),
						     P_("Space which is inserted between cells"),
						     0,
						     G_MAXINT,
						     0,
						     GTK_PARAM_READWRITE));
  
  g_object_class_install_property (object_class,
                                   PROP_FIXED_WIDTH,
                                   g_param_spec_int ("fixed-width",
                                                     P_("Fixed Width"),
                                                     P_("Current fixed width of the column"),
                                                     1,
                                                     G_MAXINT,
                                                     100,
                                                     GTK_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_MIN_WIDTH,
                                   g_param_spec_int ("min-width",
                                                     P_("Minimum Width"),
                                                     P_("Minimum allowed width of the column"),
                                                     -1,
                                                     G_MAXINT,
                                                     -1,
                                                     GTK_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_MAX_WIDTH,
                                   g_param_spec_int ("max-width",
                                                     P_("Maximum Width"),
                                                     P_("Maximum allowed width of the column"),
                                                     -1,
                                                     G_MAXINT,
                                                     -1,
                                                     GTK_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_TITLE,
                                   g_param_spec_string ("title",
                                                        P_("Title"),
                                                        P_("Title to appear in column header"),
                                                        "",
                                                        GTK_PARAM_READWRITE));
  
  g_object_class_install_property (object_class,
                                   PROP_EXPAND,
                                   g_param_spec_boolean ("expand",
							 P_("Expand"),
							 P_("Column gets share of extra width allocated to the widget"),
							 FALSE,
							 GTK_PARAM_READWRITE));
  
  g_object_class_install_property (object_class,
                                   PROP_CLICKABLE,
                                   g_param_spec_boolean ("clickable",
                                                        P_("Clickable"),
                                                        P_("Whether the header can be clicked"),
                                                         FALSE,
                                                         GTK_PARAM_READWRITE));
  

  g_object_class_install_property (object_class,
                                   PROP_WIDGET,
                                   g_param_spec_object ("widget",
                                                        P_("Widget"),
                                                        P_("Widget to put in column header button instead of column title"),
                                                        GTK_TYPE_WIDGET,
                                                        GTK_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_ALIGNMENT,
                                   g_param_spec_float ("alignment",
                                                       P_("Alignment"),
                                                       P_("X Alignment of the column header text or widget"),
                                                       0.0,
                                                       1.0,
                                                       0.0,
                                                       GTK_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_REORDERABLE,
                                   g_param_spec_boolean ("reorderable",
							 P_("Reorderable"),
							 P_("Whether the column can be reordered around the headers"),
							 FALSE,
							 GTK_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_SORT_INDICATOR,
                                   g_param_spec_boolean ("sort-indicator",
                                                        P_("Sort indicator"),
                                                        P_("Whether to show a sort indicator"),
                                                         FALSE,
                                                         GTK_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_SORT_ORDER,
                                   g_param_spec_enum ("sort-order",
                                                      P_("Sort order"),
                                                      P_("Sort direction the sort indicator should indicate"),
                                                      GTK_TYPE_SORT_TYPE,
                                                      GTK_SORT_ASCENDING,
                                                      GTK_PARAM_READWRITE));

  /**
   * PsppSheetViewColumn:sort-column-id:
   *
   * Logical sort column ID this column sorts on when selected for sorting. Setting the sort column ID makes the column header
   * clickable. Set to %-1 to make the column unsortable.
   *
   * Since: 2.18
   **/
  g_object_class_install_property (object_class,
                                   PROP_SORT_COLUMN_ID,
                                   g_param_spec_int ("sort-column-id",
                                                     P_("Sort column ID"),
                                                     P_("Logical sort column ID this column sorts on when selected for sorting"),
                                                     -1,
                                                     G_MAXINT,
                                                     -1,
                                                     GTK_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_QUICK_EDIT,
                                   g_param_spec_boolean ("quick-edit",
                                                         P_("Quick edit"),
                                                         P_("If true, editing starts upon the first click in the column.  If false, the first click selects the column and a second click is needed to begin editing.  This has no effect on cells that are not editable."),
                                                         TRUE,
                                                         GTK_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_SELECTED,
                                   g_param_spec_boolean ("selected",
                                                         P_("Selected"),
                                                         P_("If true, this column is selected as part of a rectangular selection."),
                                                         FALSE,
                                                         GTK_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_SELECTABLE,
                                   g_param_spec_boolean ("selectable",
                                                         P_("Selectable"),
                                                         P_("If true, this column may be selected as part of a rectangular selection."),
                                                         TRUE,
                                                         GTK_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_ROW_HEAD,
                                   g_param_spec_boolean ("row-head",
                                                         P_("Row head"),
                                                         P_("If true, this column is a \"row head\", equivalent to a column head.  If rectangular selection is enabled, then shift+click and control+click in the column select row ranges and toggle row selection, respectively.  The column should ordinarily include a button cell; clicking on the button will select the row (and deselect all other rows)."),
                                                         FALSE,
                                                         GTK_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_TABBABLE,
                                   g_param_spec_boolean ("tabbable",
                                                         P_("Tabbable"),
                                                         P_("If true, Tab and Shift+Tab visit this column.  If false, Tab and Shift+Tab skip this column."),
                                                         TRUE,
                                                         GTK_PARAM_READWRITE));
}

static void
pspp_sheet_view_column_buildable_init (GtkBuildableIface *iface)
{
  iface->add_child = _gtk_cell_layout_buildable_add_child;
  iface->custom_tag_start = _gtk_cell_layout_buildable_custom_tag_start;
  iface->custom_tag_end = _gtk_cell_layout_buildable_custom_tag_end;
}

static void
pspp_sheet_view_column_cell_layout_init (GtkCellLayoutIface *iface)
{
  iface->pack_start = pspp_sheet_view_column_cell_layout_pack_start;
  iface->pack_end = pspp_sheet_view_column_cell_layout_pack_end;
  iface->clear = pspp_sheet_view_column_cell_layout_clear;
  iface->add_attribute = pspp_sheet_view_column_cell_layout_add_attribute;
  iface->set_cell_data_func = pspp_sheet_view_column_cell_layout_set_cell_data_func;
  iface->clear_attributes = pspp_sheet_view_column_cell_layout_clear_attributes;
  iface->reorder = pspp_sheet_view_column_cell_layout_reorder;
  iface->get_cells = pspp_sheet_view_column_cell_layout_get_cells;
}

static void
pspp_sheet_view_column_init (PsppSheetViewColumn *tree_column)
{
  tree_column->button = NULL;
  tree_column->xalign = 0.0;
  tree_column->width = 0;
  tree_column->spacing = 0;
  tree_column->requested_width = -1;
  tree_column->min_width = -1;
  tree_column->max_width = -1;
  tree_column->resized_width = 0;
  tree_column->visible = TRUE;
  tree_column->resizable = FALSE;
  tree_column->expand = FALSE;
  tree_column->clickable = FALSE;
  tree_column->dirty = TRUE;
  tree_column->selected = FALSE;
  tree_column->selectable = TRUE;
  tree_column->row_head = FALSE;
  tree_column->tabbable = TRUE;
  tree_column->sort_order = GTK_SORT_ASCENDING;
  tree_column->show_sort_indicator = FALSE;
  tree_column->property_changed_signal = 0;
  tree_column->sort_clicked_signal = 0;
  tree_column->sort_column_changed_signal = 0;
  tree_column->sort_column_id = -1;
  tree_column->reorderable = FALSE;
  tree_column->maybe_reordered = FALSE;
  tree_column->fixed_width = 1;
  tree_column->use_resized_width = FALSE;
  tree_column->title = g_strdup ("");
  tree_column->quick_edit = TRUE;
  tree_column->need_button = FALSE;
}

static void
pspp_sheet_view_column_finalize (GObject *object)
{
  PsppSheetViewColumn *tree_column = (PsppSheetViewColumn *) object;
  GList *list;

  for (list = tree_column->cell_list; list; list = list->next)
    {
      PsppSheetViewColumnCellInfo *info = (PsppSheetViewColumnCellInfo *) list->data;

      if (info->destroy)
	{
	  GDestroyNotify d = info->destroy;

	  info->destroy = NULL;
	  d (info->func_data);
	}
      pspp_sheet_view_column_clear_attributes_by_info (tree_column, info);
      g_object_unref (info->cell);
      g_free (info);
    }

  g_free (tree_column->title);
  g_list_free (tree_column->cell_list);

  if (tree_column->child)
    g_object_unref (tree_column->child);

  G_OBJECT_CLASS (pspp_sheet_view_column_parent_class)->finalize (object);
}

static void
pspp_sheet_view_column_set_property (GObject         *object,
                                   guint            prop_id,
                                   const GValue    *value,
                                   GParamSpec      *pspec)
{
  PsppSheetViewColumn *tree_column;

  tree_column = PSPP_SHEET_VIEW_COLUMN (object);

  switch (prop_id)
    {
    case PROP_VISIBLE:
      pspp_sheet_view_column_set_visible (tree_column,
                                        g_value_get_boolean (value));
      break;

    case PROP_RESIZABLE:
      pspp_sheet_view_column_set_resizable (tree_column,
					  g_value_get_boolean (value));
      break;

    case PROP_FIXED_WIDTH:
      pspp_sheet_view_column_set_fixed_width (tree_column,
					    g_value_get_int (value));
      break;

    case PROP_MIN_WIDTH:
      pspp_sheet_view_column_set_min_width (tree_column,
                                          g_value_get_int (value));
      break;

    case PROP_MAX_WIDTH:
      pspp_sheet_view_column_set_max_width (tree_column,
                                          g_value_get_int (value));
      break;

    case PROP_SPACING:
      pspp_sheet_view_column_set_spacing (tree_column,
					g_value_get_int (value));
      break;

    case PROP_TITLE:
      pspp_sheet_view_column_set_title (tree_column,
                                      g_value_get_string (value));
      break;

    case PROP_EXPAND:
      pspp_sheet_view_column_set_expand (tree_column,
				       g_value_get_boolean (value));
      break;

    case PROP_CLICKABLE:
      pspp_sheet_view_column_set_clickable (tree_column,
                                          g_value_get_boolean (value));
      break;

    case PROP_WIDGET:
      pspp_sheet_view_column_set_widget (tree_column,
                                       (GtkWidget*) g_value_get_object (value));
      break;

    case PROP_ALIGNMENT:
      pspp_sheet_view_column_set_alignment (tree_column,
                                          g_value_get_float (value));
      break;

    case PROP_REORDERABLE:
      pspp_sheet_view_column_set_reorderable (tree_column,
					    g_value_get_boolean (value));
      break;

    case PROP_SORT_INDICATOR:
      pspp_sheet_view_column_set_sort_indicator (tree_column,
                                               g_value_get_boolean (value));
      break;

    case PROP_SORT_ORDER:
      pspp_sheet_view_column_set_sort_order (tree_column,
                                           g_value_get_enum (value));
      break;
      
    case PROP_SORT_COLUMN_ID:
      pspp_sheet_view_column_set_sort_column_id (tree_column,
                                               g_value_get_int (value));
      break;

    case PROP_QUICK_EDIT:
      pspp_sheet_view_column_set_quick_edit (tree_column,
                                             g_value_get_boolean (value));
      break;

    case PROP_SELECTED:
      pspp_sheet_view_column_set_selected (tree_column,
                                             g_value_get_boolean (value));
      break;

    case PROP_SELECTABLE:
      pspp_sheet_view_column_set_selectable (tree_column,
                                             g_value_get_boolean (value));
      break;

    case PROP_ROW_HEAD:
      pspp_sheet_view_column_set_row_head (tree_column,
                                             g_value_get_boolean (value));
      break;

    case PROP_TABBABLE:
      pspp_sheet_view_column_set_tabbable (tree_column,
                                           g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
pspp_sheet_view_column_get_property (GObject         *object,
                                   guint            prop_id,
                                   GValue          *value,
                                   GParamSpec      *pspec)
{
  PsppSheetViewColumn *tree_column;

  tree_column = PSPP_SHEET_VIEW_COLUMN (object);

  switch (prop_id)
    {
    case PROP_VISIBLE:
      g_value_set_boolean (value,
                           pspp_sheet_view_column_get_visible (tree_column));
      break;

    case PROP_RESIZABLE:
      g_value_set_boolean (value,
                           pspp_sheet_view_column_get_resizable (tree_column));
      break;

    case PROP_WIDTH:
      g_value_set_int (value,
                       pspp_sheet_view_column_get_width (tree_column));
      break;

    case PROP_SPACING:
      g_value_set_int (value,
                       pspp_sheet_view_column_get_spacing (tree_column));
      break;

    case PROP_FIXED_WIDTH:
      g_value_set_int (value,
                       pspp_sheet_view_column_get_fixed_width (tree_column));
      break;

    case PROP_MIN_WIDTH:
      g_value_set_int (value,
                       pspp_sheet_view_column_get_min_width (tree_column));
      break;

    case PROP_MAX_WIDTH:
      g_value_set_int (value,
                       pspp_sheet_view_column_get_max_width (tree_column));
      break;

    case PROP_TITLE:
      g_value_set_string (value,
                          pspp_sheet_view_column_get_title (tree_column));
      break;

    case PROP_EXPAND:
      g_value_set_boolean (value,
                          pspp_sheet_view_column_get_expand (tree_column));
      break;

    case PROP_CLICKABLE:
      g_value_set_boolean (value,
                           pspp_sheet_view_column_get_clickable (tree_column));
      break;

    case PROP_WIDGET:
      g_value_set_object (value,
                          (GObject*) pspp_sheet_view_column_get_widget (tree_column));
      break;

    case PROP_ALIGNMENT:
      g_value_set_float (value,
                         pspp_sheet_view_column_get_alignment (tree_column));
      break;

    case PROP_REORDERABLE:
      g_value_set_boolean (value,
			   pspp_sheet_view_column_get_reorderable (tree_column));
      break;

    case PROP_SORT_INDICATOR:
      g_value_set_boolean (value,
                           pspp_sheet_view_column_get_sort_indicator (tree_column));
      break;

    case PROP_SORT_ORDER:
      g_value_set_enum (value,
                        pspp_sheet_view_column_get_sort_order (tree_column));
      break;
      
    case PROP_SORT_COLUMN_ID:
      g_value_set_int (value,
                       pspp_sheet_view_column_get_sort_column_id (tree_column));
      break;

    case PROP_QUICK_EDIT:
      g_value_set_boolean (value,
                           pspp_sheet_view_column_get_quick_edit (tree_column));
      break;

    case PROP_SELECTED:
      g_value_set_boolean (value,
                           pspp_sheet_view_column_get_selected (tree_column));
      break;

    case PROP_SELECTABLE:
      g_value_set_boolean (value,
                           pspp_sheet_view_column_get_selectable (tree_column));
      break;

    case PROP_ROW_HEAD:
      g_value_set_boolean (value,
                           pspp_sheet_view_column_get_row_head (tree_column));
      break;

    case PROP_TABBABLE:
      g_value_set_boolean (value,
                           pspp_sheet_view_column_get_tabbable (tree_column));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* Implementation of GtkCellLayout interface
 */

static void
pspp_sheet_view_column_cell_layout_pack_start (GtkCellLayout   *cell_layout,
                                             GtkCellRenderer *cell,
                                             gboolean         expand)
{
  PsppSheetViewColumn *column;
  PsppSheetViewColumnCellInfo *cell_info;

  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (cell_layout));
  column = PSPP_SHEET_VIEW_COLUMN (cell_layout);
  g_return_if_fail (! pspp_sheet_view_column_get_cell_info (column, cell));

  g_object_ref_sink (cell);

  cell_info = g_new0 (PsppSheetViewColumnCellInfo, 1);
  cell_info->cell = cell;
  cell_info->expand = expand ? TRUE : FALSE;
  cell_info->pack = GTK_PACK_START;
  cell_info->has_focus = 0;
  cell_info->attributes = NULL;

  column->cell_list = g_list_append (column->cell_list, cell_info);
}

static void
pspp_sheet_view_column_cell_layout_pack_end (GtkCellLayout   *cell_layout,
                                           GtkCellRenderer *cell,
                                           gboolean         expand)
{
  PsppSheetViewColumn *column;
  PsppSheetViewColumnCellInfo *cell_info;

  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (cell_layout));
  column = PSPP_SHEET_VIEW_COLUMN (cell_layout);
  g_return_if_fail (! pspp_sheet_view_column_get_cell_info (column, cell));

  g_object_ref_sink (cell);

  cell_info = g_new0 (PsppSheetViewColumnCellInfo, 1);
  cell_info->cell = cell;
  cell_info->expand = expand ? TRUE : FALSE;
  cell_info->pack = GTK_PACK_END;
  cell_info->has_focus = 0;
  cell_info->attributes = NULL;

  column->cell_list = g_list_append (column->cell_list, cell_info);
}

static void
pspp_sheet_view_column_cell_layout_clear (GtkCellLayout *cell_layout)
{
  PsppSheetViewColumn *column;

  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (cell_layout));
  column = PSPP_SHEET_VIEW_COLUMN (cell_layout);

  while (column->cell_list)
    {
      PsppSheetViewColumnCellInfo *info = (PsppSheetViewColumnCellInfo *)column->cell_list->data;

      pspp_sheet_view_column_cell_layout_clear_attributes (cell_layout, info->cell);
      g_object_unref (info->cell);
      g_free (info);
      column->cell_list = g_list_delete_link (column->cell_list, 
					      column->cell_list);
    }
}

static void
pspp_sheet_view_column_cell_layout_add_attribute (GtkCellLayout   *cell_layout,
                                                GtkCellRenderer *cell,
                                                const gchar     *attribute,
                                                gint             column)
{
  PsppSheetViewColumn *tree_column;
  PsppSheetViewColumnCellInfo *info;

  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (cell_layout));
  tree_column = PSPP_SHEET_VIEW_COLUMN (cell_layout);

  info = pspp_sheet_view_column_get_cell_info (tree_column, cell);
  g_return_if_fail (info != NULL);

  info->attributes = g_slist_prepend (info->attributes, GINT_TO_POINTER (column));
  info->attributes = g_slist_prepend (info->attributes, g_strdup (attribute));

  if (tree_column->tree_view)
    _pspp_sheet_view_column_cell_set_dirty (tree_column);
}

static void
pspp_sheet_view_column_cell_layout_set_cell_data_func (GtkCellLayout         *cell_layout,
                                                     GtkCellRenderer       *cell,
                                                     GtkCellLayoutDataFunc  func,
                                                     gpointer               func_data,
                                                     GDestroyNotify         destroy)
{
  PsppSheetViewColumn *column;
  PsppSheetViewColumnCellInfo *info;

  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (cell_layout));
  column = PSPP_SHEET_VIEW_COLUMN (cell_layout);

  info = pspp_sheet_view_column_get_cell_info (column, cell);
  g_return_if_fail (info != NULL);

  if (info->destroy)
    {
      GDestroyNotify d = info->destroy;

      info->destroy = NULL;
      d (info->func_data);
    }

  info->func = (PsppSheetCellDataFunc)func;
  info->func_data = func_data;
  info->destroy = destroy;

  if (column->tree_view)
    _pspp_sheet_view_column_cell_set_dirty (column);
}

static void
pspp_sheet_view_column_cell_layout_clear_attributes (GtkCellLayout    *cell_layout,
                                                   GtkCellRenderer  *cell_renderer)
{
  PsppSheetViewColumn *column;
  PsppSheetViewColumnCellInfo *info;

  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (cell_layout));
  column = PSPP_SHEET_VIEW_COLUMN (cell_layout);

  info = pspp_sheet_view_column_get_cell_info (column, cell_renderer);
  if (info)
    pspp_sheet_view_column_clear_attributes_by_info (column, info);
}

static void
pspp_sheet_view_column_cell_layout_reorder (GtkCellLayout   *cell_layout,
                                          GtkCellRenderer *cell,
                                          gint             position)
{
  GList *link;
  PsppSheetViewColumn *column;
  PsppSheetViewColumnCellInfo *info;

  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (cell_layout));
  column = PSPP_SHEET_VIEW_COLUMN (cell_layout);

  info = pspp_sheet_view_column_get_cell_info (column, cell);

  g_return_if_fail (info != NULL);
  g_return_if_fail (position >= 0);

  link = g_list_find (column->cell_list, info);

  g_return_if_fail (link != NULL);

  column->cell_list = g_list_delete_link (column->cell_list, link);
  column->cell_list = g_list_insert (column->cell_list, info, position);

  if (column->tree_view)
    gtk_widget_queue_draw (column->tree_view);
}

static void
pspp_sheet_view_column_clear_attributes_by_info (PsppSheetViewColumn *tree_column,
					       PsppSheetViewColumnCellInfo *info)
{
  GSList *list;

  list = info->attributes;

  while (list && list->next)
    {
      g_free (list->data);
      list = list->next->next;
    }
  g_slist_free (info->attributes);
  info->attributes = NULL;

  if (tree_column->tree_view)
    _pspp_sheet_view_column_cell_set_dirty (tree_column);
}

static gboolean
on_query_tooltip (GtkWidget  *widget,
                  gint        x,
                  gint        y,
                  gboolean    keyboard_mode,
                  GtkTooltip *tooltip,
                  gpointer    user_data)
{
  PsppSheetViewColumn *tree_column = user_data;
  gboolean handled;

  g_signal_emit (tree_column, tree_column_signals[QUERY_TOOLTIP], 0,
                 tooltip, &handled);
  return handled;
}

static gboolean
on_button_pressed (GtkWidget *widget, GdkEventButton *event,
                   gpointer user_data)
{
  PsppSheetViewColumn *tree_column = user_data;
  gboolean handled;

  /* XXX See "Implement GtkWidget::popup_menu" in GTK+ reference manual. */
  g_signal_emit (tree_column, tree_column_signals[BUTTON_PRESS_EVENT],
                 0, event, &handled);
  return handled;
}

/* Helper functions
 */

/* Button handling code
 */
static void
pspp_sheet_view_column_create_button (PsppSheetViewColumn *tree_column)
{
  PsppSheetView *tree_view;
  GtkWidget *child;
  GtkWidget *hbox;

  tree_view = (PsppSheetView *) tree_column->tree_view;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (tree_column->button == NULL);

  gtk_widget_push_composite_child ();
  tree_column->button = gtk_button_new ();
  gtk_widget_add_events (tree_column->button, GDK_POINTER_MOTION_MASK);
  gtk_widget_pop_composite_child ();

  /* make sure we own a reference to it as well. */
  if (tree_view->priv->header_window)
    gtk_widget_set_parent_window (tree_column->button, tree_view->priv->header_window);
  gtk_widget_set_parent (tree_column->button, GTK_WIDGET (tree_view));

  g_signal_connect (tree_column->button, "event",
		    G_CALLBACK (pspp_sheet_view_column_button_event),
		    tree_column);
  g_signal_connect (tree_column->button, "clicked",
		    G_CALLBACK (pspp_sheet_view_column_button_clicked),
		    tree_column);
  g_signal_connect (tree_column->button, "popup-menu",
		    G_CALLBACK (pspp_sheet_view_column_button_popup_menu),
		    tree_column);
  g_signal_connect (tree_column->button, "button-press-event",
                    G_CALLBACK (on_button_pressed), tree_column);

  g_signal_connect (tree_column->button, "query-tooltip",
                    G_CALLBACK (on_query_tooltip), tree_column);
  g_object_set (tree_column->button, "has-tooltip", TRUE, NULL);

  tree_column->alignment = gtk_alignment_new (tree_column->xalign, 0.5, 0.0, 0.0);

  hbox = gtk_hbox_new (FALSE, 2);
  tree_column->arrow = gtk_arrow_new (GTK_ARROW_DOWN, GTK_SHADOW_IN);

  if (tree_column->child)
    child = tree_column->child;
  else
    {
      child = gtk_label_new (tree_column->title);
      gtk_widget_show (child);
    }

  g_signal_connect (child, "mnemonic-activate",
		    G_CALLBACK (pspp_sheet_view_column_mnemonic_activate),
		    tree_column);

  if (tree_column->xalign <= 0.5)
    gtk_box_pack_end (GTK_BOX (hbox), tree_column->arrow, FALSE, FALSE, 0);
  else
    gtk_box_pack_start (GTK_BOX (hbox), tree_column->arrow, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (hbox), tree_column->alignment, TRUE, TRUE, 0);
        
  gtk_container_add (GTK_CONTAINER (tree_column->alignment), child);
  gtk_container_add (GTK_CONTAINER (tree_column->button), hbox);

  gtk_widget_show (hbox);
  gtk_widget_show (tree_column->alignment);
  pspp_sheet_view_column_update_button (tree_column);
}

void 
pspp_sheet_view_column_update_button (PsppSheetViewColumn *tree_column)
{
  gint sort_column_id = -1;
  GtkWidget *hbox;
  GtkWidget *alignment;
  GtkWidget *arrow;
  GtkWidget *current_child;
  GtkArrowType arrow_type = GTK_ARROW_NONE;
  GtkTreeModel *model;
  gboolean can_focus;

  if (tree_column->tree_view)
    model = pspp_sheet_view_get_model (PSPP_SHEET_VIEW (tree_column->tree_view));
  else
    model = NULL;

  /* Create a button if necessary */
  if (tree_column->need_button &&
      tree_column->visible &&
      tree_column->button == NULL &&
      tree_column->tree_view &&
      gtk_widget_get_realized (tree_column->tree_view))
    pspp_sheet_view_column_create_button (tree_column);
  
  if (! tree_column->button)
    return;

  hbox = gtk_bin_get_child (GTK_BIN (tree_column->button));
  alignment = tree_column->alignment;
  arrow = tree_column->arrow;
  current_child = gtk_bin_get_child (GTK_BIN (alignment));

  /* Set up the actual button */
  gtk_alignment_set (GTK_ALIGNMENT (alignment), tree_column->xalign,
		     0.5, 0.0, 0.0);
      
  if (tree_column->child)
    {
      if (current_child != tree_column->child)
	{
	  gtk_container_remove (GTK_CONTAINER (alignment),
				current_child);
	  gtk_container_add (GTK_CONTAINER (alignment),
			     tree_column->child);
	}
    }
  else 
    {
      if (current_child == NULL)
	{
	  current_child = gtk_label_new (NULL);
	  gtk_widget_show (current_child);
	  gtk_container_add (GTK_CONTAINER (alignment),
			     current_child);
	}

      g_return_if_fail (GTK_IS_LABEL (current_child));

      if (tree_column->title)
	gtk_label_set_text_with_mnemonic (GTK_LABEL (current_child),
					  tree_column->title);
      else
	gtk_label_set_text_with_mnemonic (GTK_LABEL (current_child),
					  "");
    }

  if (GTK_IS_TREE_SORTABLE (model))
    gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (model),
					  &sort_column_id,
					  NULL);

  if (tree_column->show_sort_indicator)
    {
      gboolean alternative;

      g_object_get (gtk_widget_get_settings (tree_column->tree_view),
		    "gtk-alternative-sort-arrows", &alternative,
		    NULL);

      switch (tree_column->sort_order)
        {
	  case GTK_SORT_ASCENDING:
	    arrow_type = alternative ? GTK_ARROW_UP : GTK_ARROW_DOWN;
	    break;

	  case GTK_SORT_DESCENDING:
	    arrow_type = alternative ? GTK_ARROW_DOWN : GTK_ARROW_UP;
	    break;

	  default:
	    g_warning (G_STRLOC": bad sort order");
	    break;
	}
    }

  gtk_arrow_set (GTK_ARROW (arrow),
		 arrow_type,
		 GTK_SHADOW_IN);

  /* Put arrow on the right if the text is left-or-center justified, and on the
   * left otherwise; do this by packing boxes, so flipping text direction will
   * reverse things
   */
  g_object_ref (arrow);
  gtk_container_remove (GTK_CONTAINER (hbox), arrow);

  if (tree_column->xalign <= 0.5)
    {
      gtk_box_pack_end (GTK_BOX (hbox), arrow, FALSE, FALSE, 0);
    }
  else
    {
      gtk_box_pack_start (GTK_BOX (hbox), arrow, FALSE, FALSE, 0);
      /* move it to the front */
      gtk_box_reorder_child (GTK_BOX (hbox), arrow, 0);
    }
  g_object_unref (arrow);

  if (tree_column->show_sort_indicator
      || (GTK_IS_TREE_SORTABLE (model) && tree_column->sort_column_id >= 0))
    gtk_widget_show (arrow);
  else
    gtk_widget_hide (arrow);

  /* It's always safe to hide the button.  It isn't always safe to show it, as
   * if you show it before it's realized, it'll get the wrong window. */
  if (tree_column->button &&
      tree_column->tree_view != NULL &&
      gtk_widget_get_realized (tree_column->tree_view))
    {
      if (tree_column->visible)
	{
	  gtk_widget_show_now (tree_column->button);
	  if (tree_column->window)
	    {
	      if (tree_column->resizable)
		{
		  gdk_window_show (tree_column->window);
		  gdk_window_raise (tree_column->window);
		}
	      else
		{
		  gdk_window_hide (tree_column->window);
		}
	    }
	}
      else
	{
	  gtk_widget_hide (tree_column->button);
	  if (tree_column->window)
	    gdk_window_hide (tree_column->window);
	}
    }

  can_focus = pspp_sheet_view_column_can_focus (tree_column);
  gtk_widget_set_can_focus (tree_column->button, can_focus);
  if (!can_focus && gtk_widget_has_focus (tree_column->button))
    {
      GtkWidget *toplevel = gtk_widget_get_toplevel (tree_column->tree_view);
      if (gtk_widget_is_toplevel (toplevel))
        {
          gtk_window_set_focus (GTK_WINDOW (toplevel), NULL);
        }
    }

  /* Queue a resize on the assumption that we always want to catch all changes
   * and columns don't change all that often.
   */
  if (gtk_widget_get_realized (tree_column->tree_view))
     gtk_widget_queue_resize (tree_column->tree_view);

}

/* Button signal handlers
 */

static gint
pspp_sheet_view_column_button_event (GtkWidget *widget,
				   GdkEvent  *event,
				   gpointer   data)
{
  PsppSheetViewColumn *column = (PsppSheetViewColumn *) data;

  g_return_val_if_fail (event != NULL, FALSE);

  if (event->type == GDK_BUTTON_PRESS &&
      column->reorderable &&
      ((GdkEventButton *)event)->button == 1)
    {
      column->maybe_reordered = TRUE;
      gdk_window_get_pointer (GTK_BUTTON (widget)->event_window,
			      &column->drag_x,
			      &column->drag_y,
			      NULL);
      gtk_widget_grab_focus (widget);
    }

  if (event->type == GDK_BUTTON_RELEASE ||
      event->type == GDK_LEAVE_NOTIFY)
    column->maybe_reordered = FALSE;
  
  if (event->type == GDK_MOTION_NOTIFY &&
      column->maybe_reordered &&
      (gtk_drag_check_threshold (widget,
				 column->drag_x,
				 column->drag_y,
				 (gint) ((GdkEventMotion *)event)->x,
				 (gint) ((GdkEventMotion *)event)->y)))
    {
      column->maybe_reordered = FALSE;
      _pspp_sheet_view_column_start_drag (PSPP_SHEET_VIEW (column->tree_view), column);
      return TRUE;
    }
  if (column->clickable == FALSE)
    {
      switch (event->type)
	{
	case GDK_MOTION_NOTIFY:
	case GDK_BUTTON_RELEASE:
	case GDK_ENTER_NOTIFY:
	case GDK_LEAVE_NOTIFY:
	  return TRUE;
	default:
	  return FALSE;
	}
    }
  return FALSE;
}

static gboolean
all_rows_selected (PsppSheetView *sheet_view)
{
  PsppSheetSelection *selection = sheet_view->priv->selection;
  gint n_rows, n_selected_rows;

  n_rows = sheet_view->priv->row_count;
  n_selected_rows = pspp_sheet_selection_count_selected_rows (selection);

  return n_rows > 0 && n_selected_rows >= n_rows;
}

static gboolean
on_pspp_sheet_view_column_button_press_event (PsppSheetViewColumn *column,
                                              GdkEventButton *event)
{
  PsppSheetView *sheet_view = PSPP_SHEET_VIEW (column->tree_view);
  PsppSheetSelection *selection;
  GSignalInvocationHint *hint;
  guint modifiers;

  /* We only want to run first, not last, but combining that with return type
     `gboolean' makes GObject warn, so just ignore the run_last call. */
  hint = g_signal_get_invocation_hint (column);
  g_return_val_if_fail (hint != NULL, FALSE);
  if (hint->run_type != G_SIGNAL_RUN_FIRST)
    return FALSE;

  g_return_val_if_fail (sheet_view != NULL, FALSE);

  selection = sheet_view->priv->selection;
  g_return_val_if_fail (selection != NULL, FALSE);

  if (pspp_sheet_selection_get_mode (selection) != PSPP_SHEET_SELECTION_RECTANGLE)
    return FALSE;

  modifiers = event->state & gtk_accelerator_get_default_mod_mask ();
  if (event->type == GDK_BUTTON_PRESS && event->button == 3)
    {
      if (pspp_sheet_selection_count_selected_columns (selection) <= 1
          || !all_rows_selected (sheet_view))
        {
          pspp_sheet_selection_select_all (selection);
          pspp_sheet_selection_unselect_all_columns (selection);
          pspp_sheet_selection_select_column (selection, column);
          sheet_view->priv->anchor_column = column;
        }
      return FALSE;
    }
  else if (event->type == GDK_BUTTON_PRESS && event->button == 1
           && modifiers == GDK_CONTROL_MASK)
    {
      gboolean is_selected;

      if (!all_rows_selected (sheet_view))
        {
          pspp_sheet_selection_select_all (selection);
          pspp_sheet_selection_unselect_all_columns (selection);
        }
      sheet_view->priv->anchor_column = column;

      is_selected = pspp_sheet_view_column_get_selected (column);
      pspp_sheet_view_column_set_selected (column, !is_selected);

      return TRUE;
    }
  else if (event->type == GDK_BUTTON_PRESS && event->button == 1
           && modifiers == GDK_SHIFT_MASK)
    {
      if (!all_rows_selected (sheet_view))
        {
          pspp_sheet_selection_select_all (selection);
          pspp_sheet_selection_unselect_all_columns (selection);
          sheet_view->priv->anchor_column = column;
        }
      else if (sheet_view->priv->anchor_column == NULL)
        sheet_view->priv->anchor_column = column;

      pspp_sheet_selection_unselect_all_columns (selection);
      pspp_sheet_selection_select_column_range (selection,
                                                sheet_view->priv->anchor_column,
                                                column);
      return TRUE;
    }

  return FALSE;
}

static gboolean
on_pspp_sheet_view_column_button_clicked (PsppSheetViewColumn *column)
{
  PsppSheetSelection *selection;
  PsppSheetView *sheet_view;

  sheet_view = PSPP_SHEET_VIEW (pspp_sheet_view_column_get_tree_view (column));
  selection = pspp_sheet_view_get_selection (sheet_view);
  if (pspp_sheet_selection_get_mode (selection) == PSPP_SHEET_SELECTION_RECTANGLE)
    {
      pspp_sheet_selection_select_all (selection);
      if (pspp_sheet_view_column_get_row_head (column))
        pspp_sheet_selection_select_all_columns (selection);
      else
        {
          pspp_sheet_selection_unselect_all_columns (selection);
          pspp_sheet_selection_select_column (selection, column);
        }
      sheet_view->priv->anchor_column = column;
      return TRUE;
    }
  return FALSE;
}

static void
pspp_sheet_view_column_button_clicked (GtkWidget *widget, gpointer data)
{
  PsppSheetViewColumn *column = data;
  gboolean handled;

  g_signal_emit (column, tree_column_signals[CLICKED], 0, &handled);
}

static void
pspp_sheet_view_column_button_popup_menu (GtkWidget *widget, gpointer data)
{
  g_signal_emit_by_name (data, "popup-menu");
}

static gboolean
pspp_sheet_view_column_mnemonic_activate (GtkWidget *widget,
					gboolean   group_cycling,
					gpointer   data)
{
  PsppSheetViewColumn *column = (PsppSheetViewColumn *)data;

  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (column), FALSE);

  PSPP_SHEET_VIEW (column->tree_view)->priv->focus_column = column;
  if (column->clickable)
    gtk_button_clicked (GTK_BUTTON (column->button));
  else if (gtk_widget_get_can_focus (column->button))
    gtk_widget_grab_focus (column->button);
  else
    gtk_widget_grab_focus (column->tree_view);

  return TRUE;
}

static void
pspp_sheet_view_model_sort_column_changed (GtkTreeSortable   *sortable,
					 PsppSheetViewColumn *column)
{
  gint sort_column_id;
  GtkSortType order;

  if (gtk_tree_sortable_get_sort_column_id (sortable,
					    &sort_column_id,
					    &order))
    {
      if (sort_column_id == column->sort_column_id)
	{
	  pspp_sheet_view_column_set_sort_indicator (column, TRUE);
	  pspp_sheet_view_column_set_sort_order (column, order);
	}
      else
	{
	  pspp_sheet_view_column_set_sort_indicator (column, FALSE);
	}
    }
  else
    {
      pspp_sheet_view_column_set_sort_indicator (column, FALSE);
    }
}

static void
pspp_sheet_view_column_sort (PsppSheetViewColumn *tree_column,
			   gpointer           data)
{
  gint sort_column_id;
  GtkSortType order;
  gboolean has_sort_column;
  gboolean has_default_sort_func;

  g_return_if_fail (tree_column->tree_view != NULL);

  has_sort_column =
    gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (PSPP_SHEET_VIEW (tree_column->tree_view)->priv->model),
					  &sort_column_id,
					  &order);
  has_default_sort_func =
    gtk_tree_sortable_has_default_sort_func (GTK_TREE_SORTABLE (PSPP_SHEET_VIEW (tree_column->tree_view)->priv->model));

  if (has_sort_column &&
      sort_column_id == tree_column->sort_column_id)
    {
      if (order == GTK_SORT_ASCENDING)
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (PSPP_SHEET_VIEW (tree_column->tree_view)->priv->model),
					      tree_column->sort_column_id,
					      GTK_SORT_DESCENDING);
      else if (order == GTK_SORT_DESCENDING && has_default_sort_func)
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (PSPP_SHEET_VIEW (tree_column->tree_view)->priv->model),
					      GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
					      GTK_SORT_ASCENDING);
      else
	gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (PSPP_SHEET_VIEW (tree_column->tree_view)->priv->model),
					      tree_column->sort_column_id,
					      GTK_SORT_ASCENDING);
    }
  else
    {
      gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (PSPP_SHEET_VIEW (tree_column->tree_view)->priv->model),
					    tree_column->sort_column_id,
					    GTK_SORT_ASCENDING);
    }
}


static void
pspp_sheet_view_column_setup_sort_column_id_callback (PsppSheetViewColumn *tree_column)
{
  GtkTreeModel *model;

  if (tree_column->tree_view == NULL)
    return;

  model = pspp_sheet_view_get_model (PSPP_SHEET_VIEW (tree_column->tree_view));

  if (model == NULL)
    return;

  if (GTK_IS_TREE_SORTABLE (model) &&
      tree_column->sort_column_id != -1)
    {
      gint real_sort_column_id;
      GtkSortType real_order;

      if (tree_column->sort_column_changed_signal == 0)
        tree_column->sort_column_changed_signal =
	  g_signal_connect (model, "sort-column-changed",
			    G_CALLBACK (pspp_sheet_view_model_sort_column_changed),
			    tree_column);
      
      if (gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE (model),
						&real_sort_column_id,
						&real_order) &&
	  (real_sort_column_id == tree_column->sort_column_id))
	{
	  pspp_sheet_view_column_set_sort_indicator (tree_column, TRUE);
	  pspp_sheet_view_column_set_sort_order (tree_column, real_order);
 	}
      else 
	{
	  pspp_sheet_view_column_set_sort_indicator (tree_column, FALSE);
	}
   }
}


/* Exported Private Functions.
 * These should only be called by gtktreeview.c or gtktreeviewcolumn.c
 */

void
_pspp_sheet_view_column_realize_button (PsppSheetViewColumn *column)
{
  GtkAllocation allocation;
  PsppSheetView *tree_view;
  GdkWindowAttr attr;
  guint attributes_mask;
  gboolean rtl;

  tree_view = (PsppSheetView *)column->tree_view;
  rtl = (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL);

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (gtk_widget_get_realized (GTK_WIDGET (tree_view)));
  g_return_if_fail (tree_view->priv->header_window != NULL);
  if (!column->need_button || !column->button)
    return;

  g_return_if_fail (column->button != NULL);

  gtk_widget_set_parent_window (column->button, tree_view->priv->header_window);

  if (column->visible)
    gtk_widget_show (column->button);

  attr.window_type = GDK_WINDOW_CHILD;
  attr.wclass = GDK_INPUT_ONLY;
  attr.visual = gtk_widget_get_visual (GTK_WIDGET (tree_view));
  attr.colormap = gtk_widget_get_colormap (GTK_WIDGET (tree_view));
  attr.event_mask = gtk_widget_get_events (GTK_WIDGET (tree_view)) |
                    (GDK_BUTTON_PRESS_MASK |
		     GDK_BUTTON_RELEASE_MASK |
		     GDK_POINTER_MOTION_MASK |
		     GDK_POINTER_MOTION_HINT_MASK |
		     GDK_KEY_PRESS_MASK);
  attributes_mask = GDK_WA_CURSOR | GDK_WA_X | GDK_WA_Y;
  attr.cursor = gdk_cursor_new_for_display (gdk_window_get_display (tree_view->priv->header_window),
					    GDK_SB_H_DOUBLE_ARROW);
  attr.y = 0;
  attr.width = TREE_VIEW_DRAG_WIDTH;
  attr.height = tree_view->priv->header_height;
  gtk_widget_get_allocation (column->button, &allocation);
  attr.x = (allocation.x + (rtl ? 0 : allocation.width)) - TREE_VIEW_DRAG_WIDTH / 2;
  column->window = gdk_window_new (tree_view->priv->header_window,
				   &attr, attributes_mask);
  gdk_window_set_user_data (column->window, tree_view);

  pspp_sheet_view_column_update_button (column);

  gdk_cursor_unref (attr.cursor);
}

void
_pspp_sheet_view_column_unrealize_button (PsppSheetViewColumn *column)
{
  g_return_if_fail (column != NULL);
  if (column->window != NULL)
    {
      gdk_window_set_user_data (column->window, NULL);
      gdk_window_destroy (column->window);
      column->window = NULL;
    }
}

void
_pspp_sheet_view_column_unset_model (PsppSheetViewColumn *column,
				   GtkTreeModel      *old_model)
{
  if (column->sort_column_changed_signal)
    {
      g_signal_handler_disconnect (old_model,
				   column->sort_column_changed_signal);
      column->sort_column_changed_signal = 0;
    }
  pspp_sheet_view_column_set_sort_indicator (column, FALSE);
}

void
_pspp_sheet_view_column_set_tree_view (PsppSheetViewColumn *column,
				     PsppSheetView       *tree_view)
{
  g_assert (column->tree_view == NULL);

  column->tree_view = GTK_WIDGET (tree_view);
  if (column->need_button)
    pspp_sheet_view_column_create_button (column);

  column->property_changed_signal =
	  g_signal_connect_swapped (tree_view,
				    "notify::model",
				    G_CALLBACK (pspp_sheet_view_column_setup_sort_column_id_callback),
				    column);

  pspp_sheet_view_column_setup_sort_column_id_callback (column);
}

void
_pspp_sheet_view_column_unset_tree_view (PsppSheetViewColumn *column)
{
  if (column->tree_view && column->button)
    {
      gtk_container_remove (GTK_CONTAINER (column->tree_view), column->button);
    }
  if (column->property_changed_signal)
    {
      g_signal_handler_disconnect (column->tree_view, column->property_changed_signal);
      column->property_changed_signal = 0;
    }

  if (column->sort_column_changed_signal)
    {
      g_signal_handler_disconnect (pspp_sheet_view_get_model (PSPP_SHEET_VIEW (column->tree_view)),
				   column->sort_column_changed_signal);
      column->sort_column_changed_signal = 0;
    }

  column->tree_view = NULL;
  column->button = NULL;
}

gboolean
_pspp_sheet_view_column_has_editable_cell (PsppSheetViewColumn *column)
{
  GList *list;

  for (list = column->cell_list; list; list = list->next)
    if (((PsppSheetViewColumnCellInfo *)list->data)->cell->mode ==
	GTK_CELL_RENDERER_MODE_EDITABLE)
      return TRUE;

  return FALSE;
}

/* gets cell being edited */
GtkCellRenderer *
_pspp_sheet_view_column_get_edited_cell (PsppSheetViewColumn *column)
{
  GList *list;

  for (list = column->cell_list; list; list = list->next)
    if (((PsppSheetViewColumnCellInfo *)list->data)->in_editing_mode)
      return ((PsppSheetViewColumnCellInfo *)list->data)->cell;

  return NULL;
}

gint
_pspp_sheet_view_column_count_special_cells (PsppSheetViewColumn *column)
{
  gint i = 0;
  GList *list;

  for (list = column->cell_list; list; list = list->next)
    {
      PsppSheetViewColumnCellInfo *cellinfo = list->data;

      if ((cellinfo->cell->mode == GTK_CELL_RENDERER_MODE_EDITABLE ||
	  cellinfo->cell->mode == GTK_CELL_RENDERER_MODE_ACTIVATABLE) &&
	  gtk_cell_renderer_get_visible (cellinfo->cell))
	i++;
    }

  return i;
}

GtkCellRenderer *
_pspp_sheet_view_column_get_cell_at_pos (PsppSheetViewColumn *column,
				       gint               x)
{
  GList *list;
  gint current_x = 0;

  list = pspp_sheet_view_column_cell_first (column);
  for (; list; list = pspp_sheet_view_column_cell_next (column, list))
   {
     PsppSheetViewColumnCellInfo *cellinfo = list->data;
     if (current_x <= x && x <= current_x + cellinfo->real_width)
       return cellinfo->cell;
     current_x += cellinfo->real_width;
   }

  return NULL;
}

/* Public Functions */


/**
 * pspp_sheet_view_column_new:
 * 
 * Creates a new #PsppSheetViewColumn.
 * 
 * Return value: A newly created #PsppSheetViewColumn.
 **/
PsppSheetViewColumn *
pspp_sheet_view_column_new (void)
{
  PsppSheetViewColumn *tree_column;

  tree_column = g_object_new (PSPP_TYPE_SHEET_VIEW_COLUMN, NULL);

  return tree_column;
}

/**
 * pspp_sheet_view_column_new_with_attributes:
 * @title: The title to set the header to.
 * @cell: The #GtkCellRenderer.
 * @Varargs: A %NULL-terminated list of attributes.
 * 
 * Creates a new #PsppSheetViewColumn with a number of default values.  This is
 * equivalent to calling pspp_sheet_view_column_set_title(),
 * pspp_sheet_view_column_pack_start(), and
 * pspp_sheet_view_column_set_attributes() on the newly created #PsppSheetViewColumn.
 *
 * Here's a simple example:
 * |[
 *  enum { TEXT_COLUMN, COLOR_COLUMN, N_COLUMNS };
 *  ...
 *  {
 *    PsppSheetViewColumn *column;
 *    GtkCellRenderer   *renderer = gtk_cell_renderer_text_new ();
 *  
 *    column = pspp_sheet_view_column_new_with_attributes ("Title",
 *                                                       renderer,
 *                                                       "text", TEXT_COLUMN,
 *                                                       "foreground", COLOR_COLUMN,
 *                                                       NULL);
 *  }
 * ]|
 * 
 * Return value: A newly created #PsppSheetViewColumn.
 **/
PsppSheetViewColumn *
pspp_sheet_view_column_new_with_attributes (const gchar     *title,
					  GtkCellRenderer *cell,
					  ...)
{
  PsppSheetViewColumn *retval;
  va_list args;

  retval = pspp_sheet_view_column_new ();

  pspp_sheet_view_column_set_title (retval, title);
  pspp_sheet_view_column_pack_start (retval, cell, TRUE);

  va_start (args, cell);
  pspp_sheet_view_column_set_attributesv (retval, cell, args);
  va_end (args);

  return retval;
}

static PsppSheetViewColumnCellInfo *
pspp_sheet_view_column_get_cell_info (PsppSheetViewColumn *tree_column,
				    GtkCellRenderer   *cell_renderer)
{
  GList *list;
  for (list = tree_column->cell_list; list; list = list->next)
    if (((PsppSheetViewColumnCellInfo *)list->data)->cell == cell_renderer)
      return (PsppSheetViewColumnCellInfo *) list->data;
  return NULL;
}


/**
 * pspp_sheet_view_column_pack_start:
 * @tree_column: A #PsppSheetViewColumn.
 * @cell: The #GtkCellRenderer. 
 * @expand: %TRUE if @cell is to be given extra space allocated to @tree_column.
 *
 * Packs the @cell into the beginning of the column. If @expand is %FALSE, then
 * the @cell is allocated no more space than it needs. Any unused space is divided
 * evenly between cells for which @expand is %TRUE.
 **/
void
pspp_sheet_view_column_pack_start (PsppSheetViewColumn *tree_column,
				 GtkCellRenderer   *cell,
				 gboolean           expand)
{
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (tree_column), cell, expand);
}

/**
 * pspp_sheet_view_column_pack_end:
 * @tree_column: A #PsppSheetViewColumn.
 * @cell: The #GtkCellRenderer. 
 * @expand: %TRUE if @cell is to be given extra space allocated to @tree_column.
 *
 * Adds the @cell to end of the column. If @expand is %FALSE, then the @cell
 * is allocated no more space than it needs. Any unused space is divided
 * evenly between cells for which @expand is %TRUE.
 **/
void
pspp_sheet_view_column_pack_end (PsppSheetViewColumn  *tree_column,
			       GtkCellRenderer    *cell,
			       gboolean            expand)
{
  gtk_cell_layout_pack_end (GTK_CELL_LAYOUT (tree_column), cell, expand);
}

/**
 * pspp_sheet_view_column_clear:
 * @tree_column: A #PsppSheetViewColumn
 * 
 * Unsets all the mappings on all renderers on the @tree_column.
 **/
void
pspp_sheet_view_column_clear (PsppSheetViewColumn *tree_column)
{
  gtk_cell_layout_clear (GTK_CELL_LAYOUT (tree_column));
}

static GList *
pspp_sheet_view_column_cell_layout_get_cells (GtkCellLayout *layout)
{
  PsppSheetViewColumn *tree_column = PSPP_SHEET_VIEW_COLUMN (layout);
  GList *retval = NULL, *list;

  g_return_val_if_fail (tree_column != NULL, NULL);

  for (list = tree_column->cell_list; list; list = list->next)
    {
      PsppSheetViewColumnCellInfo *info = (PsppSheetViewColumnCellInfo *)list->data;

      retval = g_list_append (retval, info->cell);
    }

  return retval;
}

/**
 * pspp_sheet_view_column_get_cell_renderers:
 * @tree_column: A #PsppSheetViewColumn
 *
 * Returns a newly-allocated #GList of all the cell renderers in the column,
 * in no particular order.  The list must be freed with g_list_free().
 *
 * Return value: A list of #GtkCellRenderers
 *
 * Deprecated: 2.18: use gtk_cell_layout_get_cells() instead.
 **/
GList *
pspp_sheet_view_column_get_cell_renderers (PsppSheetViewColumn *tree_column)
{
  return pspp_sheet_view_column_cell_layout_get_cells (GTK_CELL_LAYOUT (tree_column));
}

/**
 * pspp_sheet_view_column_add_attribute:
 * @tree_column: A #PsppSheetViewColumn.
 * @cell_renderer: the #GtkCellRenderer to set attributes on
 * @attribute: An attribute on the renderer
 * @column: The column position on the model to get the attribute from.
 * 
 * Adds an attribute mapping to the list in @tree_column.  The @column is the
 * column of the model to get a value from, and the @attribute is the
 * parameter on @cell_renderer to be set from the value. So for example
 * if column 2 of the model contains strings, you could have the
 * "text" attribute of a #GtkCellRendererText get its values from
 * column 2.
 **/
void
pspp_sheet_view_column_add_attribute (PsppSheetViewColumn *tree_column,
				    GtkCellRenderer   *cell_renderer,
				    const gchar       *attribute,
				    gint               column)
{
  gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (tree_column),
                                 cell_renderer, attribute, column);
}

static void
pspp_sheet_view_column_set_attributesv (PsppSheetViewColumn *tree_column,
				      GtkCellRenderer   *cell_renderer,
				      va_list            args)
{
  gchar *attribute;
  gint column;

  attribute = va_arg (args, gchar *);

  pspp_sheet_view_column_clear_attributes (tree_column, cell_renderer);
  
  while (attribute != NULL)
    {
      column = va_arg (args, gint);
      pspp_sheet_view_column_add_attribute (tree_column, cell_renderer, attribute, column);
      attribute = va_arg (args, gchar *);
    }
}

/**
 * pspp_sheet_view_column_set_attributes:
 * @tree_column: A #PsppSheetViewColumn.
 * @cell_renderer: the #GtkCellRenderer we're setting the attributes of
 * @Varargs: A %NULL-terminated list of attributes.
 * 
 * Sets the attributes in the list as the attributes of @tree_column.
 * The attributes should be in attribute/column order, as in
 * pspp_sheet_view_column_add_attribute(). All existing attributes
 * are removed, and replaced with the new attributes.
 **/
void
pspp_sheet_view_column_set_attributes (PsppSheetViewColumn *tree_column,
				     GtkCellRenderer   *cell_renderer,
				     ...)
{
  va_list args;

  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));
  g_return_if_fail (GTK_IS_CELL_RENDERER (cell_renderer));
  g_return_if_fail (pspp_sheet_view_column_get_cell_info (tree_column, cell_renderer));

  va_start (args, cell_renderer);
  pspp_sheet_view_column_set_attributesv (tree_column, cell_renderer, args);
  va_end (args);
}


/**
 * pspp_sheet_view_column_set_cell_data_func:
 * @tree_column: A #PsppSheetViewColumn
 * @cell_renderer: A #GtkCellRenderer
 * @func: The #PsppSheetViewColumnFunc to use. 
 * @func_data: The user data for @func.
 * @destroy: The destroy notification for @func_data
 * 
 * Sets the #PsppSheetViewColumnFunc to use for the column.  This
 * function is used instead of the standard attributes mapping for
 * setting the column value, and should set the value of @tree_column's
 * cell renderer as appropriate.  @func may be %NULL to remove an
 * older one.
 **/
void
pspp_sheet_view_column_set_cell_data_func (PsppSheetViewColumn   *tree_column,
					 GtkCellRenderer     *cell_renderer,
					 PsppSheetCellDataFunc  func,
					 gpointer             func_data,
					 GDestroyNotify       destroy)
{
  gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (tree_column),
                                      cell_renderer,
                                      (GtkCellLayoutDataFunc)func,
                                      func_data, destroy);
}


/**
 * pspp_sheet_view_column_clear_attributes:
 * @tree_column: a #PsppSheetViewColumn
 * @cell_renderer: a #GtkCellRenderer to clear the attribute mapping on.
 * 
 * Clears all existing attributes previously set with
 * pspp_sheet_view_column_set_attributes().
 **/
void
pspp_sheet_view_column_clear_attributes (PsppSheetViewColumn *tree_column,
				       GtkCellRenderer   *cell_renderer)
{
  gtk_cell_layout_clear_attributes (GTK_CELL_LAYOUT (tree_column),
                                    cell_renderer);
}

/**
 * pspp_sheet_view_column_set_spacing:
 * @tree_column: A #PsppSheetViewColumn.
 * @spacing: distance between cell renderers in pixels.
 * 
 * Sets the spacing field of @tree_column, which is the number of pixels to
 * place between cell renderers packed into it.
 **/
void
pspp_sheet_view_column_set_spacing (PsppSheetViewColumn *tree_column,
				  gint               spacing)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));
  g_return_if_fail (spacing >= 0);

  if (tree_column->spacing == spacing)
    return;

  tree_column->spacing = spacing;
  if (tree_column->tree_view)
    _pspp_sheet_view_column_cell_set_dirty (tree_column);
}

/**
 * pspp_sheet_view_column_get_spacing:
 * @tree_column: A #PsppSheetViewColumn.
 * 
 * Returns the spacing of @tree_column.
 * 
 * Return value: the spacing of @tree_column.
 **/
gint
pspp_sheet_view_column_get_spacing (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), 0);

  return tree_column->spacing;
}

/* Options for manipulating the columns */

/**
 * pspp_sheet_view_column_set_visible:
 * @tree_column: A #PsppSheetViewColumn.
 * @visible: %TRUE if the @tree_column is visible.
 * 
 * Sets the visibility of @tree_column.
 **/
void
pspp_sheet_view_column_set_visible (PsppSheetViewColumn *tree_column,
				  gboolean           visible)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  visible = !! visible;
  
  if (tree_column->visible == visible)
    return;

  tree_column->visible = visible;

  if (tree_column->visible)
    _pspp_sheet_view_column_cell_set_dirty (tree_column);

  pspp_sheet_view_column_update_button (tree_column);
  g_object_notify (G_OBJECT (tree_column), "visible");
}

/**
 * pspp_sheet_view_column_get_visible:
 * @tree_column: A #PsppSheetViewColumn.
 * 
 * Returns %TRUE if @tree_column is visible.
 * 
 * Return value: whether the column is visible or not.  If it is visible, then
 * the tree will show the column.
 **/
gboolean
pspp_sheet_view_column_get_visible (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), FALSE);

  return tree_column->visible;
}

/**
 * pspp_sheet_view_column_set_resizable:
 * @tree_column: A #PsppSheetViewColumn
 * @resizable: %TRUE, if the column can be resized
 * 
 * If @resizable is %TRUE, then the user can explicitly resize the column by
 * grabbing the outer edge of the column button.
 **/
void
pspp_sheet_view_column_set_resizable (PsppSheetViewColumn *tree_column,
				    gboolean           resizable)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  resizable = !! resizable;

  if (tree_column->resizable == resizable)
    return;

  tree_column->resizable = resizable;

  pspp_sheet_view_column_update_button (tree_column);

  g_object_notify (G_OBJECT (tree_column), "resizable");
}

/**
 * pspp_sheet_view_column_get_resizable:
 * @tree_column: A #PsppSheetViewColumn
 * 
 * Returns %TRUE if the @tree_column can be resized by the end user.
 * 
 * Return value: %TRUE, if the @tree_column can be resized.
 **/
gboolean
pspp_sheet_view_column_get_resizable (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), FALSE);

  return tree_column->resizable;
}


/**
 * pspp_sheet_view_column_get_width:
 * @tree_column: A #PsppSheetViewColumn.
 * 
 * Returns the current size of @tree_column in pixels.
 * 
 * Return value: The current width of @tree_column.
 **/
gint
pspp_sheet_view_column_get_width (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), 0);

  return tree_column->width;
}

/**
 * pspp_sheet_view_column_set_fixed_width:
 * @tree_column: A #PsppSheetViewColumn.
 * @fixed_width: The size to set @tree_column to. Must be greater than 0.
 * 
 * Sets the size of the column in pixels.  The size of the column is clamped to
 * the min/max width for the column.  Please note that the min/max width of the
 * column doesn't actually affect the "fixed_width" property of the widget, just
 * the actual size when displayed.
 **/
void
pspp_sheet_view_column_set_fixed_width (PsppSheetViewColumn *tree_column,
				      gint               fixed_width)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));
  g_return_if_fail (fixed_width > 0);

  tree_column->fixed_width = fixed_width;
  tree_column->use_resized_width = FALSE;

  if (tree_column->tree_view &&
      gtk_widget_get_realized (tree_column->tree_view))
    {
      gtk_widget_queue_resize (tree_column->tree_view);
    }

  g_object_notify (G_OBJECT (tree_column), "fixed-width");
}

/**
 * pspp_sheet_view_column_get_fixed_width:
 * @tree_column: a #PsppSheetViewColumn
 * 
 * Gets the fixed width of the column.  This value is only meaning may not be
 * the actual width of the column on the screen, just what is requested.
 * 
 * Return value: the fixed width of the column
 **/
gint
pspp_sheet_view_column_get_fixed_width (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), 0);

  return tree_column->fixed_width;
}

/**
 * pspp_sheet_view_column_set_min_width:
 * @tree_column: A #PsppSheetViewColumn.
 * @min_width: The minimum width of the column in pixels, or -1.
 * 
 * Sets the minimum width of the @tree_column.  If @min_width is -1, then the
 * minimum width is unset.
 **/
void
pspp_sheet_view_column_set_min_width (PsppSheetViewColumn *tree_column,
				    gint               min_width)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));
  g_return_if_fail (min_width >= -1);

  if (min_width == tree_column->min_width)
    return;

  if (tree_column->visible &&
      tree_column->tree_view != NULL &&
      gtk_widget_get_realized (tree_column->tree_view))
    {
      if (min_width > tree_column->width)
	gtk_widget_queue_resize (tree_column->tree_view);
    }

  tree_column->min_width = min_width;
  g_object_freeze_notify (G_OBJECT (tree_column));
  if (tree_column->max_width != -1 && tree_column->max_width < min_width)
    {
      tree_column->max_width = min_width;
      g_object_notify (G_OBJECT (tree_column), "max-width");
    }
  g_object_notify (G_OBJECT (tree_column), "min-width");
  g_object_thaw_notify (G_OBJECT (tree_column));
}

/**
 * pspp_sheet_view_column_get_min_width:
 * @tree_column: A #PsppSheetViewColumn.
 * 
 * Returns the minimum width in pixels of the @tree_column, or -1 if no minimum
 * width is set.
 * 
 * Return value: The minimum width of the @tree_column.
 **/
gint
pspp_sheet_view_column_get_min_width (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), -1);

  return tree_column->min_width;
}

/**
 * pspp_sheet_view_column_set_max_width:
 * @tree_column: A #PsppSheetViewColumn.
 * @max_width: The maximum width of the column in pixels, or -1.
 * 
 * Sets the maximum width of the @tree_column.  If @max_width is -1, then the
 * maximum width is unset.  Note, the column can actually be wider than max
 * width if it's the last column in a view.  In this case, the column expands to
 * fill any extra space.
 **/
void
pspp_sheet_view_column_set_max_width (PsppSheetViewColumn *tree_column,
				    gint               max_width)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));
  g_return_if_fail (max_width >= -1);

  if (max_width == tree_column->max_width)
    return;

  if (tree_column->visible &&
      tree_column->tree_view != NULL &&
      gtk_widget_get_realized (tree_column->tree_view))
    {
      if (max_width != -1 && max_width < tree_column->width)
	gtk_widget_queue_resize (tree_column->tree_view);
    }

  tree_column->max_width = max_width;
  g_object_freeze_notify (G_OBJECT (tree_column));
  if (max_width != -1 && max_width < tree_column->min_width)
    {
      tree_column->min_width = max_width;
      g_object_notify (G_OBJECT (tree_column), "min-width");
    }
  g_object_notify (G_OBJECT (tree_column), "max-width");
  g_object_thaw_notify (G_OBJECT (tree_column));
}

/**
 * pspp_sheet_view_column_get_max_width:
 * @tree_column: A #PsppSheetViewColumn.
 * 
 * Returns the maximum width in pixels of the @tree_column, or -1 if no maximum
 * width is set.
 * 
 * Return value: The maximum width of the @tree_column.
 **/
gint
pspp_sheet_view_column_get_max_width (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), -1);

  return tree_column->max_width;
}

/**
 * pspp_sheet_view_column_clicked:
 * @tree_column: a #PsppSheetViewColumn
 * 
 * Emits the "clicked" signal on the column.  This function will only work if
 * @tree_column is clickable.
 **/
void
pspp_sheet_view_column_clicked (PsppSheetViewColumn *tree_column)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  if (tree_column->visible &&
      tree_column->button &&
      tree_column->clickable)
    gtk_button_clicked (GTK_BUTTON (tree_column->button));
}

/**
 * pspp_sheet_view_column_set_title:
 * @tree_column: A #PsppSheetViewColumn.
 * @title: The title of the @tree_column.
 * 
 * Sets the title of the @tree_column.  If a custom widget has been set, then
 * this value is ignored.
 **/
void
pspp_sheet_view_column_set_title (PsppSheetViewColumn *tree_column,
				const gchar       *title)
{
  gchar *new_title;
  
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  new_title = g_strdup (title);
  g_free (tree_column->title);
  tree_column->title = new_title;

  pspp_sheet_view_column_update_button (tree_column);
  g_object_notify (G_OBJECT (tree_column), "title");
}

/**
 * pspp_sheet_view_column_get_title:
 * @tree_column: A #PsppSheetViewColumn.
 * 
 * Returns the title of the widget.
 * 
 * Return value: the title of the column. This string should not be
 * modified or freed.
 **/
const gchar *
pspp_sheet_view_column_get_title (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), NULL);

  return tree_column->title;
}

/**
 * pspp_sheet_view_column_set_expand:
 * @tree_column: A #PsppSheetViewColumn
 * @expand: %TRUE if the column should take available extra space, %FALSE if not
 * 
 * Sets the column to take available extra space.  This space is shared equally
 * amongst all columns that have the expand set to %TRUE.  If no column has this
 * option set, then the last column gets all extra space.  By default, every
 * column is created with this %FALSE.
 *
 * Since: 2.4
 **/
void
pspp_sheet_view_column_set_expand (PsppSheetViewColumn *tree_column,
				 gboolean           expand)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  expand = !!expand;
  if (tree_column->expand == expand)
    return;
  tree_column->expand = expand;

  if (tree_column->visible &&
      tree_column->tree_view != NULL &&
      gtk_widget_get_realized (tree_column->tree_view))
    {
      /* We want to continue using the original width of the
       * column that includes additional space added by the user
       * resizing the columns and possibly extra (expanded) space, which
       * are not included in the resized width.
       */
      tree_column->use_resized_width = FALSE;

      gtk_widget_queue_resize (tree_column->tree_view);
    }

  g_object_notify (G_OBJECT (tree_column), "expand");
}

/**
 * pspp_sheet_view_column_get_expand:
 * @tree_column: a #PsppSheetViewColumn
 * 
 * Return %TRUE if the column expands to take any available space.
 * 
 * Return value: %TRUE, if the column expands
 *
 * Since: 2.4
 **/
gboolean
pspp_sheet_view_column_get_expand (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), FALSE);

  return tree_column->expand;
}

/**
 * pspp_sheet_view_column_set_clickable:
 * @tree_column: A #PsppSheetViewColumn.
 * @clickable: %TRUE if the header is active.
 * 
 * Sets the header to be active if @active is %TRUE.  When the header is active,
 * then it can take keyboard focus, and can be clicked.
 **/
void
pspp_sheet_view_column_set_clickable (PsppSheetViewColumn *tree_column,
                                    gboolean           clickable)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  clickable = !! clickable;
  if (tree_column->clickable == clickable)
    return;

  tree_column->clickable = clickable;
  pspp_sheet_view_column_update_button (tree_column);
  g_object_notify (G_OBJECT (tree_column), "clickable");
}

/**
 * pspp_sheet_view_column_get_clickable:
 * @tree_column: a #PsppSheetViewColumn
 * 
 * Returns %TRUE if the user can click on the header for the column.
 * 
 * Return value: %TRUE if user can click the column header.
 **/
gboolean
pspp_sheet_view_column_get_clickable (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), FALSE);

  return tree_column->clickable;
}

/**
 * pspp_sheet_view_column_set_widget:
 * @tree_column: A #PsppSheetViewColumn.
 * @widget: (allow-none): A child #GtkWidget, or %NULL.
 *
 * Sets the widget in the header to be @widget.  If widget is %NULL, then the
 * header button is set with a #GtkLabel set to the title of @tree_column.
 **/
void
pspp_sheet_view_column_set_widget (PsppSheetViewColumn *tree_column,
				 GtkWidget         *widget)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));
  g_return_if_fail (widget == NULL || GTK_IS_WIDGET (widget));

  if (widget)
    g_object_ref_sink (widget);

  if (tree_column->child)      
    g_object_unref (tree_column->child);

  tree_column->child = widget;
  pspp_sheet_view_column_update_button (tree_column);
  g_object_notify (G_OBJECT (tree_column), "widget");
}

/**
 * pspp_sheet_view_column_get_widget:
 * @tree_column: A #PsppSheetViewColumn.
 * 
 * Returns the #GtkWidget in the button on the column header.  If a custom
 * widget has not been set then %NULL is returned.
 * 
 * Return value: The #GtkWidget in the column header, or %NULL
 **/
GtkWidget *
pspp_sheet_view_column_get_widget (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), NULL);

  return tree_column->child;
}

/**
 * pspp_sheet_view_column_set_alignment:
 * @tree_column: A #PsppSheetViewColumn.
 * @xalign: The alignment, which is between [0.0 and 1.0] inclusive.
 * 
 * Sets the alignment of the title or custom widget inside the column header.
 * The alignment determines its location inside the button -- 0.0 for left, 0.5
 * for center, 1.0 for right.
 **/
void
pspp_sheet_view_column_set_alignment (PsppSheetViewColumn *tree_column,
                                    gfloat             xalign)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  xalign = CLAMP (xalign, 0.0, 1.0);

  if (tree_column->xalign == xalign)
    return;

  tree_column->xalign = xalign;
  pspp_sheet_view_column_update_button (tree_column);
  g_object_notify (G_OBJECT (tree_column), "alignment");
}

/**
 * pspp_sheet_view_column_get_alignment:
 * @tree_column: A #PsppSheetViewColumn.
 * 
 * Returns the current x alignment of @tree_column.  This value can range
 * between 0.0 and 1.0.
 * 
 * Return value: The current alignent of @tree_column.
 **/
gfloat
pspp_sheet_view_column_get_alignment (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), 0.5);

  return tree_column->xalign;
}

/**
 * pspp_sheet_view_column_set_reorderable:
 * @tree_column: A #PsppSheetViewColumn
 * @reorderable: %TRUE, if the column can be reordered.
 * 
 * If @reorderable is %TRUE, then the column can be reordered by the end user
 * dragging the header.
 **/
void
pspp_sheet_view_column_set_reorderable (PsppSheetViewColumn *tree_column,
				      gboolean           reorderable)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  /*  if (reorderable)
      pspp_sheet_view_column_set_clickable (tree_column, TRUE);*/

  reorderable = !!reorderable;
  if (tree_column->reorderable == reorderable)
    return;

  tree_column->reorderable = reorderable;
  pspp_sheet_view_column_update_button (tree_column);
  g_object_notify (G_OBJECT (tree_column), "reorderable");
}

/**
 * pspp_sheet_view_column_get_reorderable:
 * @tree_column: A #PsppSheetViewColumn
 * 
 * Returns %TRUE if the @tree_column can be reordered by the user.
 * 
 * Return value: %TRUE if the @tree_column can be reordered by the user.
 **/
gboolean
pspp_sheet_view_column_get_reorderable (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), FALSE);

  return tree_column->reorderable;
}

/**
 * pspp_sheet_view_column_set_quick_edit:
 * @tree_column: A #PsppSheetViewColumn
 * @quick_edit: If true, editing starts upon the first click in the column.  If
 * false, the first click selects the column and a second click is needed to
 * begin editing.  This has no effect on cells that are not editable.
 **/
void
pspp_sheet_view_column_set_quick_edit (PsppSheetViewColumn *tree_column,
				      gboolean           quick_edit)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  quick_edit = !!quick_edit;
  if (tree_column->quick_edit != quick_edit)
    {
      tree_column->quick_edit = quick_edit;
      g_object_notify (G_OBJECT (tree_column), "quick-edit");
    }
}

/**
 * pspp_sheet_view_column_get_quick_edit:
 * @tree_column: A #PsppSheetViewColumn
 *
 * Returns %TRUE if editing starts upon the first click in the column.  Returns
 * %FALSE, the first click selects the column and a second click is needed to
 * begin editing.  This is not meaningful for cells that are not editable.
 *
 * Return value: %TRUE if editing starts upon the first click.
 **/
gboolean
pspp_sheet_view_column_get_quick_edit (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), FALSE);

  return tree_column->quick_edit;
}


/**
 * pspp_sheet_view_column_set_selected:
 * @tree_column: A #PsppSheetViewColumn
 * @selected: If true, the column is selected as part of a rectangular
 * selection.
 **/
void
pspp_sheet_view_column_set_selected (PsppSheetViewColumn *tree_column,
				      gboolean           selected)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  selected = !!selected;
  if (tree_column->selected != selected)
    {
      PsppSheetSelection *selection;
      PsppSheetView *sheet_view;

      if (tree_column->tree_view != NULL)
        gtk_widget_queue_draw (GTK_WIDGET (tree_column->tree_view));
      tree_column->selected = selected;
      g_object_notify (G_OBJECT (tree_column), "selected");

      sheet_view = PSPP_SHEET_VIEW (pspp_sheet_view_column_get_tree_view (
                                      tree_column));
      selection = pspp_sheet_view_get_selection (sheet_view);
      _pspp_sheet_selection_emit_changed (selection);
    }
}

/**
 * pspp_sheet_view_column_get_selected:
 * @tree_column: A #PsppSheetViewColumn
 *
 * Returns %TRUE if the column is selected as part of a rectangular
 * selection.
 *
 * Return value: %TRUE if the column is selected as part of a rectangular
 * selection.
 **/
gboolean
pspp_sheet_view_column_get_selected (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), FALSE);

  return tree_column->selected;
}

/**
 * pspp_sheet_view_column_set_selectable:
 * @tree_column: A #PsppSheetViewColumn
 * @selectable: If true, the column may be selected as part of a rectangular
 * selection.
 **/
void
pspp_sheet_view_column_set_selectable (PsppSheetViewColumn *tree_column,
				      gboolean           selectable)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  selectable = !!selectable;
  if (tree_column->selectable != selectable)
    {
      if (tree_column->tree_view != NULL)
        gtk_widget_queue_draw (GTK_WIDGET (tree_column->tree_view));
      tree_column->selectable = selectable;
      g_object_notify (G_OBJECT (tree_column), "selectable");
    }
}

/**
 * pspp_sheet_view_column_get_selectable:
 * @tree_column: A #PsppSheetViewColumn
 *
 * Returns %TRUE if the column may be selected as part of a rectangular
 * selection.
 *
 * Return value: %TRUE if the column may be selected as part of a rectangular
 * selection.
 **/
gboolean
pspp_sheet_view_column_get_selectable (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), FALSE);

  return tree_column->selectable;
}


/**
 * pspp_sheet_view_column_set_row_head:
 * @tree_column: A #PsppSheetViewColumn
 * @row_head: If true, the column is a "row head", analogous to a column head.
 * See the description of the row-head property for more information.
 **/
void
pspp_sheet_view_column_set_row_head (PsppSheetViewColumn *tree_column,
				      gboolean           row_head)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  row_head = !!row_head;
  if (tree_column->row_head != row_head)
    {
      tree_column->row_head = row_head;
      g_object_notify (G_OBJECT (tree_column), "row_head");
    }
}

/**
 * pspp_sheet_view_column_get_row_head:
 * @tree_column: A #PsppSheetViewColumn
 *
 * Returns %TRUE if the column is a row head.
 *
 * Return value: %TRUE if the column is a row head.
 **/
gboolean
pspp_sheet_view_column_get_row_head (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), FALSE);

  return tree_column->row_head;
}


/**
 * pspp_sheet_view_column_set_tabbable:
 * @tree_column: A #PsppSheetViewColumn
 * @tabbable: If true, the column is "tabbable", meaning that Tab and Shift+Tab
 * in the sheet visit this column.  If false, Tab and Shift+Tab skip this
 * column.
 **/
void
pspp_sheet_view_column_set_tabbable (PsppSheetViewColumn *tree_column,
                                     gboolean           tabbable)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  tabbable = !!tabbable;
  if (tree_column->tabbable != tabbable)
    {
      tree_column->tabbable = tabbable;
      g_object_notify (G_OBJECT (tree_column), "tabbable");
    }
}

/**
 * pspp_sheet_view_column_get_tabbable:
 * @tree_column: A #PsppSheetViewColumn
 *
 * Returns %TRUE if the column is tabbable.
 *
 * Return value: %TRUE if the column is tabbable.
 **/
gboolean
pspp_sheet_view_column_get_tabbable (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), FALSE);

  return tree_column->tabbable;
}


/**
 * pspp_sheet_view_column_set_sort_column_id:
 * @tree_column: a #PsppSheetViewColumn
 * @sort_column_id: The @sort_column_id of the model to sort on.
 *
 * Sets the logical @sort_column_id that this column sorts on when this column 
 * is selected for sorting.  Doing so makes the column header clickable.
 **/
void
pspp_sheet_view_column_set_sort_column_id (PsppSheetViewColumn *tree_column,
					 gint               sort_column_id)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));
  g_return_if_fail (sort_column_id >= -1);

  if (tree_column->sort_column_id == sort_column_id)
    return;

  tree_column->sort_column_id = sort_column_id;

  /* Handle unsetting the id */
  if (sort_column_id == -1)
    {
      GtkTreeModel *model = pspp_sheet_view_get_model (PSPP_SHEET_VIEW (tree_column->tree_view));

      if (tree_column->sort_clicked_signal)
	{
	  g_signal_handler_disconnect (tree_column, tree_column->sort_clicked_signal);
	  tree_column->sort_clicked_signal = 0;
	}

      if (tree_column->sort_column_changed_signal)
	{
	  g_signal_handler_disconnect (model, tree_column->sort_column_changed_signal);
	  tree_column->sort_column_changed_signal = 0;
	}

      pspp_sheet_view_column_set_sort_order (tree_column, GTK_SORT_ASCENDING);
      pspp_sheet_view_column_set_sort_indicator (tree_column, FALSE);
      pspp_sheet_view_column_set_clickable (tree_column, FALSE);
      g_object_notify (G_OBJECT (tree_column), "sort-column-id");
      return;
    }

  pspp_sheet_view_column_set_clickable (tree_column, TRUE);

  if (! tree_column->sort_clicked_signal)
    tree_column->sort_clicked_signal = g_signal_connect (tree_column,
                                                         "clicked",
                                                         G_CALLBACK (pspp_sheet_view_column_sort),
                                                         NULL);

  pspp_sheet_view_column_setup_sort_column_id_callback (tree_column);
  g_object_notify (G_OBJECT (tree_column), "sort-column-id");
}

/**
 * pspp_sheet_view_column_get_sort_column_id:
 * @tree_column: a #PsppSheetViewColumn
 *
 * Gets the logical @sort_column_id that the model sorts on when this
 * column is selected for sorting.
 * See pspp_sheet_view_column_set_sort_column_id().
 *
 * Return value: the current @sort_column_id for this column, or -1 if
 *               this column can't be used for sorting.
 **/
gint
pspp_sheet_view_column_get_sort_column_id (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), 0);

  return tree_column->sort_column_id;
}

/**
 * pspp_sheet_view_column_set_sort_indicator:
 * @tree_column: a #PsppSheetViewColumn
 * @setting: %TRUE to display an indicator that the column is sorted
 *
 * Call this function with a @setting of %TRUE to display an arrow in
 * the header button indicating the column is sorted. Call
 * pspp_sheet_view_column_set_sort_order() to change the direction of
 * the arrow.
 * 
 **/
void
pspp_sheet_view_column_set_sort_indicator (PsppSheetViewColumn     *tree_column,
                                         gboolean               setting)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  setting = setting != FALSE;

  if (setting == tree_column->show_sort_indicator)
    return;

  tree_column->show_sort_indicator = setting;
  pspp_sheet_view_column_update_button (tree_column);
  g_object_notify (G_OBJECT (tree_column), "sort-indicator");
}

/**
 * pspp_sheet_view_column_get_sort_indicator:
 * @tree_column: a #PsppSheetViewColumn
 * 
 * Gets the value set by pspp_sheet_view_column_set_sort_indicator().
 * 
 * Return value: whether the sort indicator arrow is displayed
 **/
gboolean
pspp_sheet_view_column_get_sort_indicator  (PsppSheetViewColumn     *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), FALSE);

  return tree_column->show_sort_indicator;
}

/**
 * pspp_sheet_view_column_set_sort_order:
 * @tree_column: a #PsppSheetViewColumn
 * @order: sort order that the sort indicator should indicate
 *
 * Changes the appearance of the sort indicator. 
 * 
 * This <emphasis>does not</emphasis> actually sort the model.  Use
 * pspp_sheet_view_column_set_sort_column_id() if you want automatic sorting
 * support.  This function is primarily for custom sorting behavior, and should
 * be used in conjunction with gtk_tree_sortable_set_sort_column() to do
 * that. For custom models, the mechanism will vary. 
 * 
 * The sort indicator changes direction to indicate normal sort or reverse sort.
 * Note that you must have the sort indicator enabled to see anything when 
 * calling this function; see pspp_sheet_view_column_set_sort_indicator().
 **/
void
pspp_sheet_view_column_set_sort_order      (PsppSheetViewColumn     *tree_column,
                                          GtkSortType            order)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  if (order == tree_column->sort_order)
    return;

  tree_column->sort_order = order;
  pspp_sheet_view_column_update_button (tree_column);
  g_object_notify (G_OBJECT (tree_column), "sort-order");
}

/**
 * pspp_sheet_view_column_get_sort_order:
 * @tree_column: a #PsppSheetViewColumn
 * 
 * Gets the value set by pspp_sheet_view_column_set_sort_order().
 * 
 * Return value: the sort order the sort indicator is indicating
 **/
GtkSortType
pspp_sheet_view_column_get_sort_order      (PsppSheetViewColumn     *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), 0);

  return tree_column->sort_order;
}

/**
 * pspp_sheet_view_column_cell_set_cell_data:
 * @tree_column: A #PsppSheetViewColumn.
 * @tree_model: The #GtkTreeModel to to get the cell renderers attributes from.
 * @iter: The #GtkTreeIter to to get the cell renderer's attributes from.
 * 
 * Sets the cell renderer based on the @tree_model and @iter.  That is, for
 * every attribute mapping in @tree_column, it will get a value from the set
 * column on the @iter, and use that value to set the attribute on the cell
 * renderer.  This is used primarily by the #PsppSheetView.
 **/
void
pspp_sheet_view_column_cell_set_cell_data (PsppSheetViewColumn *tree_column,
					 GtkTreeModel      *tree_model,
					 GtkTreeIter       *iter)
{
  GSList *list;
  GValue value = { 0, };
  GList *cell_list;

  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  if (tree_model == NULL)
    return;

  for (cell_list = tree_column->cell_list; cell_list; cell_list = cell_list->next)
    {
      PsppSheetViewColumnCellInfo *info = (PsppSheetViewColumnCellInfo *) cell_list->data;
      GObject *cell = (GObject *) info->cell;

      list = info->attributes;

      g_object_freeze_notify (cell);

      while (list && list->next)
	{
	  gtk_tree_model_get_value (tree_model, iter,
				    GPOINTER_TO_INT (list->next->data),
				    &value);
	  g_object_set_property (cell, (gchar *) list->data, &value);
	  g_value_unset (&value);
	  list = list->next->next;
	}

      if (info->func)
	(* info->func) (tree_column, info->cell, tree_model, iter, info->func_data);
      g_object_thaw_notify (G_OBJECT (info->cell));
    }

}

/**
 * pspp_sheet_view_column_cell_get_size:
 * @tree_column: A #PsppSheetViewColumn.
 * @cell_area: (allow-none): The area a cell in the column will be allocated, or %NULL
 * @x_offset: (allow-none): location to return x offset of a cell relative to @cell_area, or %NULL
 * @y_offset: (allow-none): location to return y offset of a cell relative to @cell_area, or %NULL
 * @width: (allow-none): location to return width needed to render a cell, or %NULL
 * @height: (allow-none): location to return height needed to render a cell, or %NULL
 * 
 * Obtains the width and height needed to render the column.  This is used
 * primarily by the #PsppSheetView.
 **/
void
pspp_sheet_view_column_cell_get_size (PsppSheetViewColumn  *tree_column,
				    const GdkRectangle *cell_area,
				    gint               *x_offset,
				    gint               *y_offset,
				    gint               *width,
				    gint               *height)
{
  GList *list;
  gboolean first_cell = TRUE;
  gint focus_line_width;

  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  if (height)
    * height = 0;
  if (width)
    * width = 0;

  gtk_widget_style_get (tree_column->tree_view, "focus-line-width", &focus_line_width, NULL);
  
  for (list = tree_column->cell_list; list; list = list->next)
    {
      PsppSheetViewColumnCellInfo *info = (PsppSheetViewColumnCellInfo *) list->data;
      gboolean visible;
      gint new_height = 0;
      gint new_width = 0;
      g_object_get (info->cell, "visible", &visible, NULL);

      if (visible == FALSE)
	continue;

      if (first_cell == FALSE && width)
	*width += tree_column->spacing;

      gtk_cell_renderer_get_size (info->cell,
				  tree_column->tree_view,
				  cell_area,
				  x_offset,
				  y_offset,
				  &new_width,
				  &new_height);

      if (height)
	* height = MAX (*height, new_height + focus_line_width * 2);
      info->requested_width = MAX (info->requested_width, new_width + focus_line_width * 2);
      if (width)
	* width += info->requested_width;
      first_cell = FALSE;
    }
}

/* rendering, event handling and rendering focus are somewhat complicated, and
 * quite a bit of code.  Rather than duplicate them, we put them together to
 * keep the code in one place.
 *
 * To better understand what's going on, check out
 * docs/tree-column-sizing.png
 */
enum {
  CELL_ACTION_RENDER,
  CELL_ACTION_FOCUS,
  CELL_ACTION_EVENT
};

static gboolean
pspp_sheet_view_column_cell_process_action (PsppSheetViewColumn  *tree_column,
					  GdkWindow          *window,
					  const GdkRectangle *background_area,
					  const GdkRectangle *cell_area,
					  guint               flags,
					  gint                action,
					  const GdkRectangle *expose_area,     /* RENDER */
					  GdkRectangle       *focus_rectangle, /* FOCUS  */
					  GtkCellEditable   **editable_widget, /* EVENT  */
					  GdkEvent           *event,           /* EVENT  */
					  gchar              *path_string)     /* EVENT  */
{
  GList *list;
  GdkRectangle real_cell_area;
  GdkRectangle real_background_area;
  GdkRectangle real_expose_area = *cell_area;
  gint depth = 0;
  gint expand_cell_count = 0;
  gint full_requested_width = 0;
  gint extra_space;
  gint min_x, min_y, max_x, max_y;
  gint focus_line_width;
  gint special_cells;
  gint horizontal_separator;
  gboolean cursor_row = FALSE;
  gboolean first_cell = TRUE;
  gboolean rtl;
  /* If we have rtl text, we need to transform our areas */
  GdkRectangle rtl_cell_area;
  GdkRectangle rtl_background_area;

  min_x = G_MAXINT;
  min_y = G_MAXINT;
  max_x = 0;
  max_y = 0;

  rtl = (gtk_widget_get_direction (GTK_WIDGET (tree_column->tree_view)) == GTK_TEXT_DIR_RTL);
  special_cells = _pspp_sheet_view_column_count_special_cells (tree_column);

  if (special_cells > 1 && action == CELL_ACTION_FOCUS)
    {
      PsppSheetViewColumnCellInfo *info = NULL;
      gboolean found_has_focus = FALSE;

      /* one should have focus */
      for (list = tree_column->cell_list; list; list = list->next)
        {
	  info = list->data;
	  if (info && info->has_focus)
	    {
	      found_has_focus = TRUE;
	      break;
	    }
	}

      if (!found_has_focus)
        {
	  /* give the first one focus */
	  info = pspp_sheet_view_column_cell_first (tree_column)->data;
	  info->has_focus = TRUE;
	}
    }

  cursor_row = flags & GTK_CELL_RENDERER_FOCUSED;

  gtk_widget_style_get (GTK_WIDGET (tree_column->tree_view),
			"focus-line-width", &focus_line_width,
			"horizontal-separator", &horizontal_separator,
			NULL);

  real_cell_area = *cell_area;
  real_background_area = *background_area;


  real_cell_area.x += focus_line_width;
  real_cell_area.y += focus_line_width;
  real_cell_area.height -= 2 * focus_line_width;

  if (rtl)
    depth = real_background_area.width - real_cell_area.width;
  else
    depth = real_cell_area.x - real_background_area.x;

  /* Find out how much extra space we have to allocate */
  for (list = tree_column->cell_list; list; list = list->next)
    {
      PsppSheetViewColumnCellInfo *info = (PsppSheetViewColumnCellInfo *)list->data;

      if (! gtk_cell_renderer_get_visible (info->cell))
	continue;

      if (info->expand == TRUE)
	expand_cell_count ++;
      full_requested_width += info->requested_width;

      if (!first_cell)
	full_requested_width += tree_column->spacing;

      first_cell = FALSE;
    }

  extra_space = cell_area->width - full_requested_width;
  if (extra_space < 0)
    extra_space = 0;
  else if (extra_space > 0 && expand_cell_count > 0)
    extra_space /= expand_cell_count;

  /* iterate list for GTK_PACK_START cells */
  for (list = tree_column->cell_list; list; list = list->next)
    {
      PsppSheetViewColumnCellInfo *info = (PsppSheetViewColumnCellInfo *) list->data;

      if (info->pack == GTK_PACK_END)
	continue;

      if (! gtk_cell_renderer_get_visible (info->cell))
	continue;

      if ((info->has_focus || special_cells == 1) && cursor_row)
	flags |= GTK_CELL_RENDERER_FOCUSED;
      else
        flags &= ~GTK_CELL_RENDERER_FOCUSED;

      info->real_width = info->requested_width + (info->expand?extra_space:0);

      /* We constrain ourselves to only the width available */
      if (real_cell_area.x - focus_line_width + info->real_width > cell_area->x + cell_area->width)
	{
	  info->real_width = cell_area->x + cell_area->width - real_cell_area.x;
	}   

      if (real_cell_area.x > cell_area->x + cell_area->width)
	break;

      real_cell_area.width = info->real_width;
      real_cell_area.width -= 2 * focus_line_width;

      if (list->next)
	{
	  real_background_area.width = info->real_width + depth;
	}
      else
	{
          /* fill the rest of background for the last cell */
	  real_background_area.width = background_area->x + background_area->width - real_background_area.x;
	}

      rtl_cell_area = real_cell_area;
      rtl_background_area = real_background_area;
      
      if (rtl)
	{
	  rtl_cell_area.x = cell_area->x + cell_area->width - (real_cell_area.x - cell_area->x) - real_cell_area.width;
	  rtl_background_area.x = background_area->x + background_area->width - (real_background_area.x - background_area->x) - real_background_area.width;
	}

      /* RENDER */
      if (action == CELL_ACTION_RENDER)
	{
	  gtk_cell_renderer_render (info->cell,
				    window,
				    tree_column->tree_view,
				    &rtl_background_area,
				    &rtl_cell_area,
				    &real_expose_area, 
				    flags);
	}
      /* FOCUS */
      else if (action == CELL_ACTION_FOCUS)
	{
	  gint x_offset, y_offset, width, height;

	  gtk_cell_renderer_get_size (info->cell,
				      tree_column->tree_view,
				      &rtl_cell_area,
				      &x_offset, &y_offset,
				      &width, &height);

	  if (special_cells > 1)
	    {
	      if (info->has_focus)
	        {
		  min_x = rtl_cell_area.x + x_offset;
		  max_x = min_x + width;
		  min_y = rtl_cell_area.y + y_offset;
		  max_y = min_y + height;
		}
	    }
	  else
	    {
	      if (min_x > (rtl_cell_area.x + x_offset))
		min_x = rtl_cell_area.x + x_offset;
	      if (max_x < rtl_cell_area.x + x_offset + width)
		max_x = rtl_cell_area.x + x_offset + width;
	      if (min_y > (rtl_cell_area.y + y_offset))
		min_y = rtl_cell_area.y + y_offset;
	      if (max_y < rtl_cell_area.y + y_offset + height)
		max_y = rtl_cell_area.y + y_offset + height;
	    }
	}
      /* EVENT */
      else if (action == CELL_ACTION_EVENT)
	{
	  gboolean try_event = FALSE;

	  if (event)
	    {
	      if (special_cells == 1)
	        {
		  /* only 1 activatable cell -> whole column can activate */
		  if (cell_area->x <= ((GdkEventButton *)event)->x &&
		      cell_area->x + cell_area->width > ((GdkEventButton *)event)->x)
		    try_event = TRUE;
		}
	      else if (rtl_cell_area.x <= ((GdkEventButton *)event)->x &&
		  rtl_cell_area.x + rtl_cell_area.width > ((GdkEventButton *)event)->x)
		  /* only activate cell if the user clicked on an individual
		   * cell
		   */
		try_event = TRUE;
	    }
	  else if (special_cells > 1 && info->has_focus)
	    try_event = TRUE;
	  else if (special_cells == 1)
	    try_event = TRUE;

	  if (try_event)
	    {
	      gboolean visible, mode;

	      g_object_get (info->cell,
			    "visible", &visible,
			    "mode", &mode,
			    NULL);
	      if (visible && mode == GTK_CELL_RENDERER_MODE_ACTIVATABLE)
		{
		  if (gtk_cell_renderer_activate (info->cell,
						  event,
						  tree_column->tree_view,
						  path_string,
						  &rtl_background_area,
						  &rtl_cell_area,
						  flags))
		    {
                      flags &= ~GTK_CELL_RENDERER_FOCUSED;
		      return TRUE;
		    }
		}
	      else if (visible && mode == GTK_CELL_RENDERER_MODE_EDITABLE)
		{
		  *editable_widget =
		    gtk_cell_renderer_start_editing (info->cell,
						     event,
						     tree_column->tree_view,
						     path_string,
						     &rtl_background_area,
						     &rtl_cell_area,
						     flags);

		  if (*editable_widget != NULL)
		    {
		      g_return_val_if_fail (GTK_IS_CELL_EDITABLE (*editable_widget), FALSE);
		      info->in_editing_mode = TRUE;
		      pspp_sheet_view_column_focus_cell (tree_column, info->cell);
		      
                      flags &= ~GTK_CELL_RENDERER_FOCUSED;

		      return TRUE;
		    }
		}
	    }
	}

      flags &= ~GTK_CELL_RENDERER_FOCUSED;

      real_cell_area.x += (real_cell_area.width + 2 * focus_line_width + tree_column->spacing);
      real_background_area.x += real_background_area.width + tree_column->spacing;

      /* Only needed for first cell */
      depth = 0;
    }

  /* iterate list for PACK_END cells */
  for (list = g_list_last (tree_column->cell_list); list; list = list->prev)
    {
      PsppSheetViewColumnCellInfo *info = (PsppSheetViewColumnCellInfo *) list->data;

      if (info->pack == GTK_PACK_START)
	continue;

      if (! gtk_cell_renderer_get_visible (info->cell))
	continue;

      if ((info->has_focus || special_cells == 1) && cursor_row)
	flags |= GTK_CELL_RENDERER_FOCUSED;
      else
        flags &= ~GTK_CELL_RENDERER_FOCUSED;

      info->real_width = info->requested_width + (info->expand?extra_space:0);

      /* We constrain ourselves to only the width available */
      if (real_cell_area.x - focus_line_width + info->real_width > cell_area->x + cell_area->width)
	{
	  info->real_width = cell_area->x + cell_area->width - real_cell_area.x;
	}   

      if (real_cell_area.x > cell_area->x + cell_area->width)
	break;

      real_cell_area.width = info->real_width;
      real_cell_area.width -= 2 * focus_line_width;
      real_background_area.width = info->real_width + depth;

      rtl_cell_area = real_cell_area;
      rtl_background_area = real_background_area;
      if (rtl)
	{
	  rtl_cell_area.x = cell_area->x + cell_area->width - (real_cell_area.x - cell_area->x) - real_cell_area.width;
	  rtl_background_area.x = background_area->x + background_area->width - (real_background_area.x - background_area->x) - real_background_area.width;
	}

      /* RENDER */
      if (action == CELL_ACTION_RENDER)
	{
	  gtk_cell_renderer_render (info->cell,
				    window,
				    tree_column->tree_view,
				    &rtl_background_area,
				    &rtl_cell_area,
				    &real_expose_area,
				    flags);
	}
      /* FOCUS */
      else if (action == CELL_ACTION_FOCUS)
	{
	  gint x_offset, y_offset, width, height;

	  gtk_cell_renderer_get_size (info->cell,
				      tree_column->tree_view,
				      &rtl_cell_area,
				      &x_offset, &y_offset,
				      &width, &height);

	  if (special_cells > 1)
	    {
	      if (info->has_focus)
	        {
		  min_x = rtl_cell_area.x + x_offset;
		  max_x = min_x + width;
		  min_y = rtl_cell_area.y + y_offset;
		  max_y = min_y + height;
		}
	    }
	  else
	    {
	      if (min_x > (rtl_cell_area.x + x_offset))
		min_x = rtl_cell_area.x + x_offset;
	      if (max_x < rtl_cell_area.x + x_offset + width)
		max_x = rtl_cell_area.x + x_offset + width;
	      if (min_y > (rtl_cell_area.y + y_offset))
		min_y = rtl_cell_area.y + y_offset;
	      if (max_y < rtl_cell_area.y + y_offset + height)
		max_y = rtl_cell_area.y + y_offset + height;
	    }
	}
      /* EVENT */
      else if (action == CELL_ACTION_EVENT)
        {
	  gboolean try_event = FALSE;

	  if (event)
	    {
	      if (special_cells == 1)
	        {
		  /* only 1 activatable cell -> whole column can activate */
		  if (cell_area->x <= ((GdkEventButton *)event)->x &&
		      cell_area->x + cell_area->width > ((GdkEventButton *)event)->x)
		    try_event = TRUE;
		}
	      else if (rtl_cell_area.x <= ((GdkEventButton *)event)->x &&
		  rtl_cell_area.x + rtl_cell_area.width > ((GdkEventButton *)event)->x)
		/* only activate cell if the user clicked on an individual
		 * cell
		 */
		try_event = TRUE;
	    }
	  else if (special_cells > 1 && info->has_focus)
	    try_event = TRUE;
	  else if (special_cells == 1)
	    try_event = TRUE;

	  if (try_event)
	    {
	      gboolean visible, mode;

	      g_object_get (info->cell,
			    "visible", &visible,
			    "mode", &mode,
			    NULL);
	      if (visible && mode == GTK_CELL_RENDERER_MODE_ACTIVATABLE)
	        {
		  if (gtk_cell_renderer_activate (info->cell,
						  event,
						  tree_column->tree_view,
						  path_string,
						  &rtl_background_area,
						  &rtl_cell_area,
						  flags))
		    {
		      flags &= ~GTK_CELL_RENDERER_FOCUSED;
		      return TRUE;
		    }
		}
	      else if (visible && mode == GTK_CELL_RENDERER_MODE_EDITABLE)
	        {
		  *editable_widget =
		    gtk_cell_renderer_start_editing (info->cell,
						     event,
						     tree_column->tree_view,
						     path_string,
						     &rtl_background_area,
						     &rtl_cell_area,
						     flags);

		  if (*editable_widget != NULL)
		    {
		      g_return_val_if_fail (GTK_IS_CELL_EDITABLE (*editable_widget), FALSE);
		      info->in_editing_mode = TRUE;
		      pspp_sheet_view_column_focus_cell (tree_column, info->cell);

		      flags &= ~GTK_CELL_RENDERER_FOCUSED;
		      return TRUE;
		    }
		}
	    }
	}

      flags &= ~GTK_CELL_RENDERER_FOCUSED;

      real_cell_area.x += (real_cell_area.width + 2 * focus_line_width + tree_column->spacing);
      real_background_area.x += (real_background_area.width + tree_column->spacing);

      /* Only needed for first cell */
      depth = 0;
    }

  /* fill focus_rectangle when required */
  if (action == CELL_ACTION_FOCUS)
    {
      if (min_x >= max_x || min_y >= max_y)
	{
	  *focus_rectangle = *cell_area;
	  /* don't change the focus_rectangle, just draw it nicely inside
	   * the cell area */
	}
      else
	{
	  focus_rectangle->x = min_x - focus_line_width;
	  focus_rectangle->y = min_y - focus_line_width;
	  focus_rectangle->width = (max_x - min_x) + 2 * focus_line_width;
	  focus_rectangle->height = (max_y - min_y) + 2 * focus_line_width;
	}
    }

  return FALSE;
}

/**
 * pspp_sheet_view_column_cell_render:
 * @tree_column: A #PsppSheetViewColumn.
 * @window: a #GdkDrawable to draw to
 * @background_area: entire cell area (including tree expanders and maybe padding on the sides)
 * @cell_area: area normally rendered by a cell renderer
 * @expose_area: area that actually needs updating
 * @flags: flags that affect rendering
 * 
 * Renders the cell contained by #tree_column. This is used primarily by the
 * #PsppSheetView.
 **/
void
_pspp_sheet_view_column_cell_render (PsppSheetViewColumn  *tree_column,
				   GdkWindow          *window,
				   const GdkRectangle *background_area,
				   const GdkRectangle *cell_area,
				   const GdkRectangle *expose_area,
				   guint               flags)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));
  g_return_if_fail (background_area != NULL);
  g_return_if_fail (cell_area != NULL);
  g_return_if_fail (expose_area != NULL);

  pspp_sheet_view_column_cell_process_action (tree_column,
					    window,
					    background_area,
					    cell_area,
					    flags,
					    CELL_ACTION_RENDER,
					    expose_area,
					    NULL, NULL, NULL, NULL);
}

gboolean
_pspp_sheet_view_column_cell_event (PsppSheetViewColumn  *tree_column,
				  GtkCellEditable   **editable_widget,
				  GdkEvent           *event,
				  gchar              *path_string,
				  const GdkRectangle *background_area,
				  const GdkRectangle *cell_area,
				  guint               flags)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), FALSE);

  return pspp_sheet_view_column_cell_process_action (tree_column,
						   NULL,
						   background_area,
						   cell_area,
						   flags,
						   CELL_ACTION_EVENT,
						   NULL, NULL,
						   editable_widget,
						   event,
						   path_string);
}

void
_pspp_sheet_view_column_get_focus_area (PsppSheetViewColumn  *tree_column,
				      const GdkRectangle *background_area,
				      const GdkRectangle *cell_area,
				      GdkRectangle       *focus_area)
{
  pspp_sheet_view_column_cell_process_action (tree_column,
					    NULL,
					    background_area,
					    cell_area,
					    0,
					    CELL_ACTION_FOCUS,
					    NULL,
					    focus_area,
					    NULL, NULL, NULL);
}


/* cell list manipulation */
static GList *
pspp_sheet_view_column_cell_first (PsppSheetViewColumn *tree_column)
{
  GList *list = tree_column->cell_list;

  /* first GTK_PACK_START cell we find */
  for ( ; list; list = list->next)
    {
      PsppSheetViewColumnCellInfo *info = list->data;
      if (info->pack == GTK_PACK_START)
	return list;
    }

  /* hmm, else the *last* GTK_PACK_END cell */
  list = g_list_last (tree_column->cell_list);

  for ( ; list; list = list->prev)
    {
      PsppSheetViewColumnCellInfo *info = list->data;
      if (info->pack == GTK_PACK_END)
	return list;
    }

  return NULL;
}

static GList *
pspp_sheet_view_column_cell_last (PsppSheetViewColumn *tree_column)
{
  GList *list = tree_column->cell_list;

  /* *first* GTK_PACK_END cell we find */
  for ( ; list ; list = list->next)
    {
      PsppSheetViewColumnCellInfo *info = list->data;
      if (info->pack == GTK_PACK_END)
	return list;
    }

  /* hmm, else the last GTK_PACK_START cell */
  list = g_list_last (tree_column->cell_list);

  for ( ; list; list = list->prev)
    {
      PsppSheetViewColumnCellInfo *info = list->data;
      if (info->pack == GTK_PACK_START)
	return list;
    }

  return NULL;
}

static GList *
pspp_sheet_view_column_cell_next (PsppSheetViewColumn *tree_column,
				GList             *current)
{
  GList *list;
  PsppSheetViewColumnCellInfo *info = current->data;

  if (info->pack == GTK_PACK_START)
    {
      for (list = current->next; list; list = list->next)
        {
	  PsppSheetViewColumnCellInfo *inf = list->data;
	  if (inf->pack == GTK_PACK_START)
	    return list;
	}

      /* out of GTK_PACK_START cells, get *last* GTK_PACK_END one */
      list = g_list_last (tree_column->cell_list);
      for (; list; list = list->prev)
        {
	  PsppSheetViewColumnCellInfo *inf = list->data;
	  if (inf->pack == GTK_PACK_END)
	    return list;
	}
    }

  for (list = current->prev; list; list = list->prev)
    {
      PsppSheetViewColumnCellInfo *inf = list->data;
      if (inf->pack == GTK_PACK_END)
	return list;
    }

  return NULL;
}

static GList *
pspp_sheet_view_column_cell_prev (PsppSheetViewColumn *tree_column,
				GList             *current)
{
  GList *list;
  PsppSheetViewColumnCellInfo *info = current->data;

  if (info->pack == GTK_PACK_END)
    {
      for (list = current->next; list; list = list->next)
        {
	  PsppSheetViewColumnCellInfo *inf = list->data;
	  if (inf->pack == GTK_PACK_END)
	    return list;
	}

      /* out of GTK_PACK_END, get last GTK_PACK_START one */
      list = g_list_last (tree_column->cell_list);
      for ( ; list; list = list->prev)
        {
	  PsppSheetViewColumnCellInfo *inf = list->data;
	  if (inf->pack == GTK_PACK_START)
	    return list;
	}
    }

  for (list = current->prev; list; list = list->prev)
    {
      PsppSheetViewColumnCellInfo *inf = list->data;
      if (inf->pack == GTK_PACK_START)
	return list;
    }

  return NULL;
}

gboolean
_pspp_sheet_view_column_cell_focus (PsppSheetViewColumn *tree_column,
				  gint               direction,
				  gboolean           left,
				  gboolean           right)
{
  gint count;
  gboolean rtl;

  count = _pspp_sheet_view_column_count_special_cells (tree_column);
  rtl = gtk_widget_get_direction (GTK_WIDGET (tree_column->tree_view)) == GTK_TEXT_DIR_RTL;

  /* if we are the current focus column and have multiple editable cells,
   * try to select the next one, else move the focus to the next column
   */
  if (PSPP_SHEET_VIEW (tree_column->tree_view)->priv->focus_column == tree_column)
    {
      if (count > 1)
        {
          GList *next, *prev;
	  GList *list = tree_column->cell_list;
	  PsppSheetViewColumnCellInfo *info = NULL;

	  /* find current focussed cell */
	  for ( ; list; list = list->next)
	    {
	      info = list->data;
	      if (info->has_focus)
		break;
	    }

	  /* not a focussed cell in the focus column? */
	  if (!list || !info || !info->has_focus)
	    return FALSE;

	  if (rtl)
	    {
	      prev = pspp_sheet_view_column_cell_next (tree_column, list);
	      next = pspp_sheet_view_column_cell_prev (tree_column, list);
	    }
	  else
	    {
	      next = pspp_sheet_view_column_cell_next (tree_column, list);
	      prev = pspp_sheet_view_column_cell_prev (tree_column, list);
	    }

	  info->has_focus = FALSE;
	  if (direction > 0 && next)
	    {
	      info = next->data;
	      info->has_focus = TRUE;
	      return TRUE;
	    }
	  else if (direction > 0 && !next && !right)
	    {
	      /* keep focus on last cell */
	      if (rtl)
	        info = pspp_sheet_view_column_cell_first (tree_column)->data;
	      else
	        info = pspp_sheet_view_column_cell_last (tree_column)->data;

	      info->has_focus = TRUE;
	      return TRUE;
	    }
	  else if (direction < 0 && prev)
	    {
	      info = prev->data;
	      info->has_focus = TRUE;
	      return TRUE;
	    }
	  else if (direction < 0 && !prev && !left)
	    {
	      /* keep focus on first cell */
	      if (rtl)
		info = pspp_sheet_view_column_cell_last (tree_column)->data;
	      else
		info = pspp_sheet_view_column_cell_first (tree_column)->data;

	      info->has_focus = TRUE;
	      return TRUE;
	    }
	}
      return FALSE;
    }

  /* we get focus, if we have multiple editable cells, give the correct one
   * focus
   */
  if (count > 1)
    {
      GList *list = tree_column->cell_list;

      /* clear focus first */
      for ( ; list ; list = list->next)
        {
	  PsppSheetViewColumnCellInfo *info = list->data;
	  if (info->has_focus)
	    info->has_focus = FALSE;
	}

      list = NULL;
      if (rtl)
        {
	  if (direction > 0)
	    list = pspp_sheet_view_column_cell_last (tree_column);
	  else if (direction < 0)
	    list = pspp_sheet_view_column_cell_first (tree_column);
	}
      else
        {
	  if (direction > 0)
	    list = pspp_sheet_view_column_cell_first (tree_column);
	  else if (direction < 0)
	    list = pspp_sheet_view_column_cell_last (tree_column);
	}

      if (list)
	((PsppSheetViewColumnCellInfo *) list->data)->has_focus = TRUE;
    }

  return TRUE;
}

void
_pspp_sheet_view_column_cell_draw_focus (PsppSheetViewColumn  *tree_column,
				       GdkWindow          *window,
				       const GdkRectangle *background_area,
				       const GdkRectangle *cell_area,
				       const GdkRectangle *expose_area,
				       guint               flags)
{
  gint focus_line_width;
  GtkStateType cell_state;
  
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));
  gtk_widget_style_get (GTK_WIDGET (tree_column->tree_view),
			"focus-line-width", &focus_line_width, NULL);
  if (tree_column->editable_widget)
    {
      /* This function is only called on the editable row when editing.
       */
#if 0
      gtk_paint_focus (tree_column->tree_view->style,
		       window,
		       gtk_widget_get_state (tree_column->tree_view),
		       NULL,
		       tree_column->tree_view,
		       "treeview",
		       cell_area->x - focus_line_width,
		       cell_area->y - focus_line_width,
		       cell_area->width + 2 * focus_line_width,
		       cell_area->height + 2 * focus_line_width);
#endif      
    }
  else
    {
      GdkRectangle focus_rectangle;
      pspp_sheet_view_column_cell_process_action (tree_column,
						window,
						background_area,
						cell_area,
						flags,
						CELL_ACTION_FOCUS,
						expose_area,
						&focus_rectangle,
						NULL, NULL, NULL);

      cell_state = flags & GTK_CELL_RENDERER_SELECTED ? GTK_STATE_SELECTED :
	      (flags & GTK_CELL_RENDERER_PRELIT ? GTK_STATE_PRELIGHT :
	      (flags & GTK_CELL_RENDERER_INSENSITIVE ? GTK_STATE_INSENSITIVE : GTK_STATE_NORMAL));
      gtk_paint_focus (gtk_widget_get_style (GTK_WIDGET (tree_column->tree_view)),
		       window,
		       cell_state,
		       cell_area,
		       tree_column->tree_view,
		       "treeview",
		       focus_rectangle.x,
		       focus_rectangle.y,
		       focus_rectangle.width,
		       focus_rectangle.height);
    }
}

/**
 * pspp_sheet_view_column_cell_is_visible:
 * @tree_column: A #PsppSheetViewColumn
 * 
 * Returns %TRUE if any of the cells packed into the @tree_column are visible.
 * For this to be meaningful, you must first initialize the cells with
 * pspp_sheet_view_column_cell_set_cell_data()
 * 
 * Return value: %TRUE, if any of the cells packed into the @tree_column are currently visible
 **/
gboolean
pspp_sheet_view_column_cell_is_visible (PsppSheetViewColumn *tree_column)
{
  GList *list;

  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), FALSE);

  for (list = tree_column->cell_list; list; list = list->next)
    {
      PsppSheetViewColumnCellInfo *info = (PsppSheetViewColumnCellInfo *) list->data;

      if (gtk_cell_renderer_get_visible (info->cell))
	return TRUE;
    }

  return FALSE;
}

/**
 * pspp_sheet_view_column_focus_cell:
 * @tree_column: A #PsppSheetViewColumn
 * @cell: A #GtkCellRenderer
 *
 * Sets the current keyboard focus to be at @cell, if the column contains
 * 2 or more editable and activatable cells.
 *
 * Since: 2.2
 **/
void
pspp_sheet_view_column_focus_cell (PsppSheetViewColumn *tree_column,
				 GtkCellRenderer   *cell)
{
  GList *list;
  gboolean found_cell = FALSE;

  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));
  g_return_if_fail (GTK_IS_CELL_RENDERER (cell));

  if (_pspp_sheet_view_column_count_special_cells (tree_column) < 2)
    return;

  for (list = tree_column->cell_list; list; list = list->next)
    {
      PsppSheetViewColumnCellInfo *info = list->data;

      if (info->cell == cell)
        {
	  info->has_focus = TRUE;
	  found_cell = TRUE;
	  break;
	}
    }

  if (found_cell)
    {
      for (list = tree_column->cell_list; list; list = list->next)
        {
	  PsppSheetViewColumnCellInfo *info = list->data;

	  if (info->cell != cell)
	    info->has_focus = FALSE;
        }

      /* FIXME: redraw? */
    }
}

void
_pspp_sheet_view_column_cell_set_dirty (PsppSheetViewColumn *tree_column)
{
  GList *list;

  for (list = tree_column->cell_list; list; list = list->next)
    {
      PsppSheetViewColumnCellInfo *info = (PsppSheetViewColumnCellInfo *) list->data;

      info->requested_width = 0;
    }
  tree_column->dirty = TRUE;
  tree_column->requested_width = -1;
  tree_column->width = 0;

  if (tree_column->tree_view &&
      gtk_widget_get_realized (tree_column->tree_view))
    {
      _pspp_sheet_view_install_mark_rows_col_dirty (PSPP_SHEET_VIEW (tree_column->tree_view));
      gtk_widget_queue_resize (tree_column->tree_view);
    }
}

void
_pspp_sheet_view_column_start_editing (PsppSheetViewColumn *tree_column,
				     GtkCellEditable   *cell_editable)
{
  g_return_if_fail (tree_column->editable_widget == NULL);

  tree_column->editable_widget = cell_editable;
}

void
_pspp_sheet_view_column_stop_editing (PsppSheetViewColumn *tree_column)
{
  GList *list;

  g_return_if_fail (tree_column->editable_widget != NULL);

  tree_column->editable_widget = NULL;
  for (list = tree_column->cell_list; list; list = list->next)
    ((PsppSheetViewColumnCellInfo *)list->data)->in_editing_mode = FALSE;
}

void
_pspp_sheet_view_column_get_neighbor_sizes (PsppSheetViewColumn *column,
					  GtkCellRenderer   *cell,
					  gint              *left,
					  gint              *right)
{
  GList *list;
  PsppSheetViewColumnCellInfo *info;
  gint l, r;
  gboolean rtl;

  l = r = 0;

  list = pspp_sheet_view_column_cell_first (column);  

  while (list)
    {
      info = (PsppSheetViewColumnCellInfo *)list->data;
      
      list = pspp_sheet_view_column_cell_next (column, list);

      if (info->cell == cell)
	break;
      
      if (gtk_cell_renderer_get_visible (info->cell))
	l += info->real_width + column->spacing;
    }

  while (list)
    {
      info = (PsppSheetViewColumnCellInfo *)list->data;
      
      list = pspp_sheet_view_column_cell_next (column, list);

      if (gtk_cell_renderer_get_visible (info->cell))
	r += info->real_width + column->spacing;
    }

  rtl = (gtk_widget_get_direction (GTK_WIDGET (column->tree_view)) == GTK_TEXT_DIR_RTL);
  if (left)
    *left = rtl ? r : l;

  if (right)
    *right = rtl ? l : r;
}

/**
 * pspp_sheet_view_column_cell_get_position:
 * @tree_column: a #PsppSheetViewColumn
 * @cell_renderer: a #GtkCellRenderer
 * @start_pos: return location for the horizontal position of @cell within
 *            @tree_column, may be %NULL
 * @width: return location for the width of @cell, may be %NULL
 *
 * Obtains the horizontal position and size of a cell in a column. If the
 * cell is not found in the column, @start_pos and @width are not changed and
 * %FALSE is returned.
 * 
 * Return value: %TRUE if @cell belongs to @tree_column.
 */
gboolean
pspp_sheet_view_column_cell_get_position (PsppSheetViewColumn *tree_column,
					GtkCellRenderer   *cell_renderer,
					gint              *start_pos,
					gint              *width)
{
  GList *list;
  gint current_x = 0;
  gboolean found_cell = FALSE;
  PsppSheetViewColumnCellInfo *cellinfo = NULL;

  list = pspp_sheet_view_column_cell_first (tree_column);
  for (; list; list = pspp_sheet_view_column_cell_next (tree_column, list))
    {
      cellinfo = list->data;
      if (cellinfo->cell == cell_renderer)
        {
          found_cell = TRUE;
          break;
        }

      if (gtk_cell_renderer_get_visible (cellinfo->cell))
        current_x += cellinfo->real_width;
    }

  if (found_cell)
    {
      if (start_pos)
        *start_pos = current_x;
      if (width)
        *width = cellinfo->real_width;
    }

  return found_cell;
}

/**
 * pspp_sheet_view_column_queue_resize:
 * @tree_column: A #PsppSheetViewColumn
 *
 * Flags the column, and the cell renderers added to this column, to have
 * their sizes renegotiated.
 *
 * Since: 2.8
 **/
void
pspp_sheet_view_column_queue_resize (PsppSheetViewColumn *tree_column)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column));

  if (tree_column->tree_view)
    _pspp_sheet_view_column_cell_set_dirty (tree_column);
}

/**
 * pspp_sheet_view_column_get_tree_view:
 * @tree_column: A #PsppSheetViewColumn
 *
 * Returns the #PsppSheetView wherein @tree_column has been inserted.  If
 * @column is currently not inserted in any tree view, %NULL is
 * returned.
 *
 * Return value: The tree view wherein @column has been inserted if any,
 *               %NULL otherwise.
 *
 * Since: 2.12
 */
GtkWidget *
pspp_sheet_view_column_get_tree_view (PsppSheetViewColumn *tree_column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (tree_column), NULL);

  return tree_column->tree_view;
}

typedef struct {
  GtkCellLayout   *cell_layout;
  GtkCellRenderer *renderer;
  gchar           *attr_name;
} AttributesSubParserData;

static void
attributes_start_element (GMarkupParseContext *context,
                          const gchar         *element_name,
                          const gchar        **names,
                          const gchar        **values,
                          gpointer             user_data,
                          GError             **error)
{
  AttributesSubParserData *parser_data = (AttributesSubParserData*)user_data;
  guint i;

  if (strcmp (element_name, "attribute") == 0)
    {
      for (i = 0; names[i]; i++)
        if (strcmp (names[i], "name") == 0)
          parser_data->attr_name = g_strdup (values[i]);
    }
  else if (strcmp (element_name, "attributes") == 0)
    return;
  else
    g_warning ("Unsupported tag for GtkCellLayout: %s\n", element_name);
}

static void
attributes_text_element (GMarkupParseContext *context,
                         const gchar         *text,
                         gsize                text_len,
                         gpointer             user_data,
                         GError             **error)
{
  AttributesSubParserData *parser_data = (AttributesSubParserData*)user_data;
  glong l;
  gchar *endptr;
  gchar *string;
  
  if (!parser_data->attr_name)
    return;

  errno = 0;
  string = g_strndup (text, text_len);
  l = strtol (string, &endptr, 0);
  if (errno || endptr == string)
    {
      g_set_error (error, 
                   GTK_BUILDER_ERROR,
                   GTK_BUILDER_ERROR_INVALID_VALUE,
                   "Could not parse integer `%s'",
                   string);
      g_free (string);
      return;
    }
  g_free (string);

  gtk_cell_layout_add_attribute (parser_data->cell_layout,
                                 parser_data->renderer,
                                 parser_data->attr_name, l);
  g_free (parser_data->attr_name);
  parser_data->attr_name = NULL;
}

static const GMarkupParser attributes_parser =
  {
    attributes_start_element,
    NULL,
    attributes_text_element,
  };

gboolean
_gtk_cell_layout_buildable_custom_tag_start (GtkBuildable  *buildable,
                                             GtkBuilder    *builder,
                                             GObject       *child,
                                             const gchar   *tagname,
                                             GMarkupParser *parser,
                                             gpointer      *data)
{
  AttributesSubParserData *parser_data;

  if (!child)
    return FALSE;

  if (strcmp (tagname, "attributes") == 0)
    {
      parser_data = g_slice_new0 (AttributesSubParserData);
      parser_data->cell_layout = GTK_CELL_LAYOUT (buildable);
      parser_data->renderer = GTK_CELL_RENDERER (child);
      parser_data->attr_name = NULL;

      *parser = attributes_parser;
      *data = parser_data;
      return TRUE;
    }

  return FALSE;
}

void
_gtk_cell_layout_buildable_custom_tag_end (GtkBuildable *buildable,
                                           GtkBuilder   *builder,
                                           GObject      *child,
                                           const gchar  *tagname,
                                           gpointer     *data)
{
  AttributesSubParserData *parser_data;

  parser_data = (AttributesSubParserData*)data;
  g_assert (!parser_data->attr_name);
  g_slice_free (AttributesSubParserData, parser_data);
}

void
_gtk_cell_layout_buildable_add_child (GtkBuildable      *buildable,
                                      GtkBuilder        *builder,
                                      GObject           *child,
                                      const gchar       *type)
{
  GtkCellLayoutIface *iface;
  
  g_return_if_fail (GTK_IS_CELL_LAYOUT (buildable));
  g_return_if_fail (GTK_IS_CELL_RENDERER (child));

  iface = GTK_CELL_LAYOUT_GET_IFACE (buildable);
  g_return_if_fail (iface->pack_start != NULL);
  iface->pack_start (GTK_CELL_LAYOUT (buildable), GTK_CELL_RENDERER (child), FALSE);
}

void
pspp_sheet_view_column_size_request (PsppSheetViewColumn       *tree_column,
                                     GtkRequisition            *request)
{
  GtkWidget *base = GTK_WIDGET (tree_column->tree_view);
  GtkRequisition label_req;
  GtkRequisition align_req;
  GtkRequisition arrow_req;
  GtkRequisition hbox_req;
  GtkStyle **button_style;

  if (tree_column->button)
    {
      gtk_widget_size_request (tree_column->button, request);
      return;
    }

  facade_label_get_size_request (0, 0, base, tree_column->title, &label_req);
  facade_alignment_get_size_request (0, 0, 0, 0, 0, &label_req, &align_req);
  facade_arrow_get_size_request (0, 0, &arrow_req);

  facade_hbox_get_base_size_request (0, 2, 2, &hbox_req);
  facade_hbox_add_child_size_request (0, &arrow_req, 0, &hbox_req);
  facade_hbox_add_child_size_request (0, &align_req, 0, &hbox_req);

  button_style = &PSPP_SHEET_VIEW (tree_column->tree_view)->priv->button_style;
  if (*button_style == NULL)
    {
      *button_style = facade_get_style (base, GTK_TYPE_BUTTON, 0);
      g_object_ref (*button_style);
    }
  facade_button_get_size_request (0, base, *button_style, &hbox_req, request);
}

void
pspp_sheet_view_column_size_allocate (PsppSheetViewColumn       *tree_column,
                                      GtkAllocation             *allocation)
{
  tree_column->allocation = *allocation;
  if (tree_column->button)
    gtk_widget_size_allocate (tree_column->button, allocation);
}

gboolean
pspp_sheet_view_column_can_focus (PsppSheetViewColumn       *tree_column)
{
  return tree_column->reorderable || tree_column->clickable;
}

void
pspp_sheet_view_column_set_need_button (PsppSheetViewColumn       *tree_column,
                                        gboolean                   need_button)
{
  if (tree_column->need_button != need_button)
    {
      tree_column->need_button = need_button;
      pspp_sheet_view_column_update_button (tree_column);
      _pspp_sheet_view_column_realize_button (tree_column);
    }
}
