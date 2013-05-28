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

/* gtktreeview.c
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

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkkeysyms-compat.h>
#include <string.h>

#include "ui/gui/psppire-marshal.h"
#include "ui/gui/pspp-sheet-selection.h"

#define P_(STRING) STRING
#define GTK_PARAM_READABLE G_PARAM_READABLE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB
#define GTK_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

/* Many keyboard shortcuts for Mac are the same as for X
 * except they use Command key instead of Control (e.g. Cut,
 * Copy, Paste). This symbol is for those simple cases. */
#ifndef GDK_WINDOWING_QUARTZ
#define GTK_DEFAULT_ACCEL_MOD_MASK GDK_CONTROL_MASK
#else
#define GTK_DEFAULT_ACCEL_MOD_MASK GDK_META_MASK
#endif

#define PSPP_SHEET_VIEW_PRIORITY_VALIDATE (GDK_PRIORITY_REDRAW + 5)
#define PSPP_SHEET_VIEW_PRIORITY_SCROLL_SYNC (PSPP_SHEET_VIEW_PRIORITY_VALIDATE + 2)
#define PSPP_SHEET_VIEW_TIME_MS_PER_IDLE 30
#define SCROLL_EDGE_SIZE 15
#define EXPANDER_EXTRA_PADDING 4
#define PSPP_SHEET_VIEW_SEARCH_DIALOG_TIMEOUT 5000

/* The "background" areas of all rows/cells add up to cover the entire tree.
 * The background includes all inter-row and inter-cell spacing.
 * The "cell" areas are the cell_area passed in to gtk_cell_renderer_render(),
 * i.e. just the cells, no spacing.
 */

#define BACKGROUND_HEIGHT(tree_view) (tree_view->priv->fixed_height)
#define CELL_HEIGHT(tree_view, separator) ((BACKGROUND_HEIGHT (tree_view)) - (separator))

/* Translate from bin_window coordinates to rbtree (tree coordinates) and
 * vice versa.
 */
#define TREE_WINDOW_Y_TO_RBTREE_Y(tree_view,y) ((y) + tree_view->priv->dy)
#define RBTREE_Y_TO_TREE_WINDOW_Y(tree_view,y) ((y) - tree_view->priv->dy)

/* This is in bin_window coordinates */
#define BACKGROUND_FIRST_PIXEL(tree_view,node) (RBTREE_Y_TO_TREE_WINDOW_Y (tree_view, pspp_sheet_view_node_find_offset (tree_view, (node))))
#define CELL_FIRST_PIXEL(tree_view,node,separator) (BACKGROUND_FIRST_PIXEL (tree_view,node) + separator/2)

#define ROW_HEIGHT(tree_view) \
  ((tree_view->priv->fixed_height > 0) ? (tree_view->priv->fixed_height) : (tree_view)->priv->expander_size)


typedef struct _PsppSheetViewChild PsppSheetViewChild;
struct _PsppSheetViewChild
{
  GtkWidget *widget;
  gint x;
  gint y;
  gint width;
  gint height;
};


typedef struct _TreeViewDragInfo TreeViewDragInfo;
struct _TreeViewDragInfo
{
  GdkModifierType start_button_mask;
  GtkTargetList *_unused_source_target_list;
  GdkDragAction source_actions;

  GtkTargetList *_unused_dest_target_list;

  guint source_set : 1;
  guint dest_set : 1;
};


/* Signals */
enum
{
  ROW_ACTIVATED,
  COLUMNS_CHANGED,
  CURSOR_CHANGED,
  MOVE_CURSOR,
  SELECT_ALL,
  UNSELECT_ALL,
  SELECT_CURSOR_ROW,
  TOGGLE_CURSOR_ROW,
  START_INTERACTIVE_SEARCH,
  LAST_SIGNAL
};

/* Properties */
enum {
  PROP_0,
  PROP_MODEL,
  PROP_HADJUSTMENT,
  PROP_VADJUSTMENT,
  PROP_HEADERS_VISIBLE,
  PROP_HEADERS_CLICKABLE,
  PROP_REORDERABLE,
  PROP_RULES_HINT,
  PROP_ENABLE_SEARCH,
  PROP_SEARCH_COLUMN,
  PROP_HOVER_SELECTION,
  PROP_RUBBER_BANDING,
  PROP_ENABLE_GRID_LINES,
  PROP_TOOLTIP_COLUMN,
  PROP_SPECIAL_CELLS,
  PROP_FIXED_HEIGHT,
  PROP_FIXED_HEIGHT_SET
};

/* object signals */
static void     pspp_sheet_view_finalize             (GObject          *object);
static void     pspp_sheet_view_set_property         (GObject         *object,
						    guint            prop_id,
						    const GValue    *value,
						    GParamSpec      *pspec);
static void     pspp_sheet_view_get_property         (GObject         *object,
						    guint            prop_id,
						    GValue          *value,
						    GParamSpec      *pspec);

static void     pspp_sheet_view_dispose              (GObject        *object);

/* gtkwidget signals */
static void     pspp_sheet_view_realize              (GtkWidget        *widget);
static void     pspp_sheet_view_unrealize            (GtkWidget        *widget);
static void     pspp_sheet_view_map                  (GtkWidget        *widget);
static void     pspp_sheet_view_size_request         (GtkWidget        *widget,
						    GtkRequisition   *requisition);
static void     pspp_sheet_view_size_allocate        (GtkWidget        *widget,
						    GtkAllocation    *allocation);
static gboolean pspp_sheet_view_expose               (GtkWidget        *widget,
						    GdkEventExpose   *event);
static gboolean pspp_sheet_view_key_press            (GtkWidget        *widget,
						    GdkEventKey      *event);
static gboolean pspp_sheet_view_key_release          (GtkWidget        *widget,
						    GdkEventKey      *event);
static gboolean pspp_sheet_view_motion               (GtkWidget        *widget,
						    GdkEventMotion   *event);
static gboolean pspp_sheet_view_enter_notify         (GtkWidget        *widget,
						    GdkEventCrossing *event);
static gboolean pspp_sheet_view_leave_notify         (GtkWidget        *widget,
						    GdkEventCrossing *event);
static gboolean pspp_sheet_view_button_press         (GtkWidget        *widget,
						    GdkEventButton   *event);
static gboolean pspp_sheet_view_button_release       (GtkWidget        *widget,
						    GdkEventButton   *event);
static gboolean pspp_sheet_view_grab_broken          (GtkWidget          *widget,
						    GdkEventGrabBroken *event);

static void     pspp_sheet_view_set_focus_child      (GtkContainer     *container,
						    GtkWidget        *child);
static gint     pspp_sheet_view_focus_out            (GtkWidget        *widget,
						    GdkEventFocus    *event);
static gint     pspp_sheet_view_focus                (GtkWidget        *widget,
						    GtkDirectionType  direction);
static void     pspp_sheet_view_grab_focus           (GtkWidget        *widget);
static void     pspp_sheet_view_style_set            (GtkWidget        *widget,
						    GtkStyle         *previous_style);
static void     pspp_sheet_view_grab_notify          (GtkWidget        *widget,
						    gboolean          was_grabbed);
static void     pspp_sheet_view_state_changed        (GtkWidget        *widget,
						    GtkStateType      previous_state);

/* container signals */
static void     pspp_sheet_view_remove               (GtkContainer     *container,
						    GtkWidget        *widget);
static void     pspp_sheet_view_forall               (GtkContainer     *container,
						    gboolean          include_internals,
						    GtkCallback       callback,
						    gpointer          callback_data);

/* Source side drag signals */
static void pspp_sheet_view_drag_begin       (GtkWidget        *widget,
                                            GdkDragContext   *context);
static void pspp_sheet_view_drag_end         (GtkWidget        *widget,
                                            GdkDragContext   *context);
static void pspp_sheet_view_drag_data_get    (GtkWidget        *widget,
                                            GdkDragContext   *context,
                                            GtkSelectionData *selection_data,
                                            guint             info,
                                            guint             time);
static void pspp_sheet_view_drag_data_delete (GtkWidget        *widget,
                                            GdkDragContext   *context);

/* Target side drag signals */
static void     pspp_sheet_view_drag_leave         (GtkWidget        *widget,
                                                  GdkDragContext   *context,
                                                  guint             time);
static gboolean pspp_sheet_view_drag_motion        (GtkWidget        *widget,
                                                  GdkDragContext   *context,
                                                  gint              x,
                                                  gint              y,
                                                  guint             time);
static gboolean pspp_sheet_view_drag_drop          (GtkWidget        *widget,
                                                  GdkDragContext   *context,
                                                  gint              x,
                                                  gint              y,
                                                  guint             time);
static void     pspp_sheet_view_drag_data_received (GtkWidget        *widget,
                                                  GdkDragContext   *context,
                                                  gint              x,
                                                  gint              y,
                                                  GtkSelectionData *selection_data,
                                                  guint             info,
                                                  guint             time);

/* tree_model signals */
static void pspp_sheet_view_set_adjustments                 (PsppSheetView     *tree_view,
							   GtkAdjustment   *hadj,
							   GtkAdjustment   *vadj);
static gboolean pspp_sheet_view_real_move_cursor            (PsppSheetView     *tree_view,
							   GtkMovementStep  step,
							   gint             count);
static gboolean pspp_sheet_view_real_select_all             (PsppSheetView     *tree_view);
static gboolean pspp_sheet_view_real_unselect_all           (PsppSheetView     *tree_view);
static gboolean pspp_sheet_view_real_select_cursor_row      (PsppSheetView     *tree_view,
                                                             gboolean         start_editing,
                                                             PsppSheetSelectMode mode);
static gboolean pspp_sheet_view_real_toggle_cursor_row      (PsppSheetView     *tree_view);
static void pspp_sheet_view_row_changed                     (GtkTreeModel    *model,
							   GtkTreePath     *path,
							   GtkTreeIter     *iter,
							   gpointer         data);
static void pspp_sheet_view_row_inserted                    (GtkTreeModel    *model,
							   GtkTreePath     *path,
							   GtkTreeIter     *iter,
							   gpointer         data);
static void pspp_sheet_view_row_deleted                     (GtkTreeModel    *model,
							   GtkTreePath     *path,
							   gpointer         data);
static void pspp_sheet_view_rows_reordered                  (GtkTreeModel    *model,
							   GtkTreePath     *parent,
							   GtkTreeIter     *iter,
							   gint            *new_order,
							   gpointer         data);

/* Incremental reflow */
static gint validate_row             (PsppSheetView *tree_view,
					  int node,
					  GtkTreeIter *iter,
					  GtkTreePath *path);
static void     validate_visible_area    (PsppSheetView *tree_view);
static gboolean validate_rows_handler    (PsppSheetView *tree_view);
static gboolean presize_handler_callback (gpointer     data);
static void     install_presize_handler  (PsppSheetView *tree_view);
static void     install_scroll_sync_handler (PsppSheetView *tree_view);
static void     pspp_sheet_view_set_top_row   (PsppSheetView *tree_view,
					     GtkTreePath *path,
					     gint         offset);
static void	pspp_sheet_view_dy_to_top_row (PsppSheetView *tree_view);
static void     pspp_sheet_view_top_row_to_dy (PsppSheetView *tree_view);
static void     invalidate_empty_focus      (PsppSheetView *tree_view);

/* Internal functions */
static void     pspp_sheet_view_add_move_binding               (GtkBindingSet      *binding_set,
							      guint               keyval,
							      guint               modmask,
							      gboolean            add_shifted_binding,
							      GtkMovementStep     step,
							      gint                count);
static void     pspp_sheet_view_queue_draw_path                (PsppSheetView        *tree_view,
							      GtkTreePath        *path,
							      const GdkRectangle *clip_rect);
static gint     pspp_sheet_view_new_column_width               (PsppSheetView        *tree_view,
							      gint                i,
							      gint               *x);
static void     pspp_sheet_view_adjustment_changed             (GtkAdjustment      *adjustment,
							      PsppSheetView        *tree_view);
static void     pspp_sheet_view_clamp_node_visible             (PsppSheetView        *tree_view,
							      int node);
static void     pspp_sheet_view_clamp_column_visible           (PsppSheetView        *tree_view,
							      PsppSheetViewColumn  *column,
							      gboolean            focus_to_cell);
static gboolean pspp_sheet_view_maybe_begin_dragging_row       (PsppSheetView        *tree_view,
							      GdkEventMotion     *event);
static void     pspp_sheet_view_focus_to_cursor                (PsppSheetView        *tree_view);
static gboolean pspp_sheet_view_move_cursor_up_down            (PsppSheetView        *tree_view,
							      gint                count,
                                                                PsppSheetSelectMode mode);
static void     pspp_sheet_view_move_cursor_page_up_down       (PsppSheetView        *tree_view,
							      gint                count,
                                                                PsppSheetSelectMode mode);
static void     pspp_sheet_view_move_cursor_left_right         (PsppSheetView        *tree_view,
                                                                gint                count,
                                                                PsppSheetSelectMode mode);
static void     pspp_sheet_view_move_cursor_line_start_end     (PsppSheetView        *tree_view,
							        gint                count,
                                                                PsppSheetSelectMode mode);
static void     pspp_sheet_view_move_cursor_tab                (PsppSheetView        *tree_view,
							      gint                count);
static void     pspp_sheet_view_move_cursor_start_end          (PsppSheetView        *tree_view,
							      gint                count,
                                                                PsppSheetSelectMode mode);
static void     pspp_sheet_view_real_set_cursor                (PsppSheetView        *tree_view,
							      GtkTreePath        *path,
							      gboolean            clear_and_select,
                                                              gboolean            clamp_node,
                                                              PsppSheetSelectMode mode);
static gboolean pspp_sheet_view_has_special_cell               (PsppSheetView        *tree_view);
static void     pspp_sheet_view_stop_rubber_band               (PsppSheetView        *tree_view);
static void     update_prelight                              (PsppSheetView        *tree_view,
                                                              int                 x,
                                                              int                 y);
static void initialize_fixed_height_mode (PsppSheetView *tree_view);

/* interactive search */
static void     pspp_sheet_view_ensure_interactive_directory (PsppSheetView *tree_view);
static void     pspp_sheet_view_search_dialog_hide     (GtkWidget        *search_dialog,
							 PsppSheetView      *tree_view);
static void     pspp_sheet_view_search_position_func      (PsppSheetView      *tree_view,
							 GtkWidget        *search_dialog,
							 gpointer          user_data);
static void     pspp_sheet_view_search_disable_popdown    (GtkEntry         *entry,
							 GtkMenu          *menu,
							 gpointer          data);
#if GTK3_TRANSITION
static void     pspp_sheet_view_search_preedit_changed    (GtkIMContext     *im_context,
							 PsppSheetView      *tree_view);
#endif
static void     pspp_sheet_view_search_activate           (GtkEntry         *entry,
							 PsppSheetView      *tree_view);
static gboolean pspp_sheet_view_real_search_enable_popdown(gpointer          data);
static void     pspp_sheet_view_search_enable_popdown     (GtkWidget        *widget,
							 gpointer          data);
static gboolean pspp_sheet_view_search_delete_event       (GtkWidget        *widget,
							 GdkEventAny      *event,
							 PsppSheetView      *tree_view);
static gboolean pspp_sheet_view_search_button_press_event (GtkWidget        *widget,
							 GdkEventButton   *event,
							 PsppSheetView      *tree_view);
static gboolean pspp_sheet_view_search_scroll_event       (GtkWidget        *entry,
							 GdkEventScroll   *event,
							 PsppSheetView      *tree_view);
static gboolean pspp_sheet_view_search_key_press_event    (GtkWidget        *entry,
							 GdkEventKey      *event,
							 PsppSheetView      *tree_view);
static gboolean pspp_sheet_view_search_move               (GtkWidget        *window,
							 PsppSheetView      *tree_view,
							 gboolean          up);
static gboolean pspp_sheet_view_search_equal_func         (GtkTreeModel     *model,
							 gint              column,
							 const gchar      *key,
							 GtkTreeIter      *iter,
							 gpointer          search_data);
static gboolean pspp_sheet_view_search_iter               (GtkTreeModel     *model,
							 PsppSheetSelection *selection,
							 GtkTreeIter      *iter,
							 const gchar      *text,
							 gint             *count,
							 gint              n);
static void     pspp_sheet_view_search_init               (GtkWidget        *entry,
							 PsppSheetView      *tree_view);
static void     pspp_sheet_view_put                       (PsppSheetView      *tree_view,
							 GtkWidget        *child_widget,
							 gint              x,
							 gint              y,
							 gint              width,
							 gint              height);
static gboolean pspp_sheet_view_start_editing             (PsppSheetView      *tree_view,
							 GtkTreePath      *cursor_path);
static gboolean pspp_sheet_view_editable_button_press_event (GtkWidget *,
                                                             GdkEventButton *,
                                                             PsppSheetView *);
static void pspp_sheet_view_editable_clicked (GtkButton *, PsppSheetView *);
static void pspp_sheet_view_real_start_editing (PsppSheetView       *tree_view,
					      PsppSheetViewColumn *column,
					      GtkTreePath       *path,
					      GtkCellEditable   *cell_editable,
					      GdkRectangle      *cell_area,
					      GdkEvent          *event,
					      guint              flags);
static gboolean pspp_sheet_view_real_start_interactive_search (PsppSheetView *tree_view,
							     gboolean     keybinding);
static gboolean pspp_sheet_view_start_interactive_search      (PsppSheetView *tree_view);
static PsppSheetViewColumn *pspp_sheet_view_get_drop_column (PsppSheetView       *tree_view,
							 PsppSheetViewColumn *column,
							 gint               drop_position);
static void
pspp_sheet_view_adjust_cell_area (PsppSheetView        *tree_view,
                                  PsppSheetViewColumn  *column,
                                  const GdkRectangle   *background_area,
                                  gboolean              subtract_focus_rect,
                                  GdkRectangle         *cell_area);
static gint pspp_sheet_view_find_offset (PsppSheetView *tree_view,
                                         gint height,
                                         int *new_node);

/* GtkBuildable */
static void pspp_sheet_view_buildable_add_child (GtkBuildable *tree_view,
					       GtkBuilder  *builder,
					       GObject     *child,
					       const gchar *type);
static void pspp_sheet_view_buildable_init      (GtkBuildableIface *iface);


static gboolean scroll_row_timeout                   (gpointer     data);
static void     add_scroll_timeout                   (PsppSheetView *tree_view);
static void     remove_scroll_timeout                (PsppSheetView *tree_view);

static guint tree_view_signals [LAST_SIGNAL] = { 0 };

static GtkBindingSet *edit_bindings;



/* GType Methods
 */

G_DEFINE_TYPE_WITH_CODE (PsppSheetView, pspp_sheet_view, GTK_TYPE_CONTAINER,
			 G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE,
						pspp_sheet_view_buildable_init))

static void
pspp_sheet_view_class_init (PsppSheetViewClass *class)
{
  GObjectClass *o_class;
  GtkWidgetClass *widget_class;
  GtkContainerClass *container_class;
  GtkBindingSet *binding_set[2];
  int i;

  binding_set[0] = gtk_binding_set_by_class (class);

  binding_set[1] = gtk_binding_set_new ("PsppSheetViewEditing");
  edit_bindings = binding_set[1];

  o_class = (GObjectClass *) class;
  widget_class = (GtkWidgetClass *) class;
  container_class = (GtkContainerClass *) class;

  /* GObject signals */
  o_class->set_property = pspp_sheet_view_set_property;
  o_class->get_property = pspp_sheet_view_get_property;
  o_class->finalize = pspp_sheet_view_finalize;
  o_class->dispose = pspp_sheet_view_dispose;

  /* GtkWidget signals */
  widget_class->map = pspp_sheet_view_map;
  widget_class->realize = pspp_sheet_view_realize;
  widget_class->unrealize = pspp_sheet_view_unrealize;
  widget_class->size_request = pspp_sheet_view_size_request;
  widget_class->size_allocate = pspp_sheet_view_size_allocate;
  widget_class->button_press_event = pspp_sheet_view_button_press;
  widget_class->button_release_event = pspp_sheet_view_button_release;
  widget_class->grab_broken_event = pspp_sheet_view_grab_broken;
  /*widget_class->configure_event = pspp_sheet_view_configure;*/
  widget_class->motion_notify_event = pspp_sheet_view_motion;
  widget_class->expose_event = pspp_sheet_view_expose;
  widget_class->key_press_event = pspp_sheet_view_key_press;
  widget_class->key_release_event = pspp_sheet_view_key_release;
  widget_class->enter_notify_event = pspp_sheet_view_enter_notify;
  widget_class->leave_notify_event = pspp_sheet_view_leave_notify;
  widget_class->focus_out_event = pspp_sheet_view_focus_out;
  widget_class->drag_begin = pspp_sheet_view_drag_begin;
  widget_class->drag_end = pspp_sheet_view_drag_end;
  widget_class->drag_data_get = pspp_sheet_view_drag_data_get;
  widget_class->drag_data_delete = pspp_sheet_view_drag_data_delete;
  widget_class->drag_leave = pspp_sheet_view_drag_leave;
  widget_class->drag_motion = pspp_sheet_view_drag_motion;
  widget_class->drag_drop = pspp_sheet_view_drag_drop;
  widget_class->drag_data_received = pspp_sheet_view_drag_data_received;
  widget_class->focus = pspp_sheet_view_focus;
  widget_class->grab_focus = pspp_sheet_view_grab_focus;
  widget_class->style_set = pspp_sheet_view_style_set;
  widget_class->grab_notify = pspp_sheet_view_grab_notify;
  widget_class->state_changed = pspp_sheet_view_state_changed;

  /* GtkContainer signals */
  container_class->remove = pspp_sheet_view_remove;
  container_class->forall = pspp_sheet_view_forall;
  container_class->set_focus_child = pspp_sheet_view_set_focus_child;

  class->set_scroll_adjustments = pspp_sheet_view_set_adjustments;
  class->move_cursor = pspp_sheet_view_real_move_cursor;
  class->select_all = pspp_sheet_view_real_select_all;
  class->unselect_all = pspp_sheet_view_real_unselect_all;
  class->select_cursor_row = pspp_sheet_view_real_select_cursor_row;
  class->toggle_cursor_row = pspp_sheet_view_real_toggle_cursor_row;
  class->start_interactive_search = pspp_sheet_view_start_interactive_search;

  /* Properties */

  g_object_class_install_property (o_class,
                                   PROP_MODEL,
                                   g_param_spec_object ("model",
							P_("TreeView Model"),
							P_("The model for the tree view"),
							GTK_TYPE_TREE_MODEL,
							GTK_PARAM_READWRITE));

  g_object_class_install_property (o_class,
                                   PROP_HADJUSTMENT,
                                   g_param_spec_object ("hadjustment",
							P_("Horizontal Adjustment"),
                                                        P_("Horizontal Adjustment for the widget"),
                                                        GTK_TYPE_ADJUSTMENT,
                                                        GTK_PARAM_READWRITE));

  g_object_class_install_property (o_class,
                                   PROP_VADJUSTMENT,
                                   g_param_spec_object ("vadjustment",
							P_("Vertical Adjustment"),
                                                        P_("Vertical Adjustment for the widget"),
                                                        GTK_TYPE_ADJUSTMENT,
                                                        GTK_PARAM_READWRITE));

  g_object_class_install_property (o_class,
                                   PROP_HEADERS_VISIBLE,
                                   g_param_spec_boolean ("headers-visible",
							 P_("Headers Visible"),
							 P_("Show the column header buttons"),
							 TRUE,
							 GTK_PARAM_READWRITE));

  g_object_class_install_property (o_class,
                                   PROP_HEADERS_CLICKABLE,
                                   g_param_spec_boolean ("headers-clickable",
							 P_("Headers Clickable"),
							 P_("Column headers respond to click events"),
							 TRUE,
							 GTK_PARAM_READWRITE));

  g_object_class_install_property (o_class,
                                   PROP_REORDERABLE,
                                   g_param_spec_boolean ("reorderable",
							 P_("Reorderable"),
							 P_("View is reorderable"),
							 FALSE,
							 GTK_PARAM_READWRITE));

  g_object_class_install_property (o_class,
                                   PROP_RULES_HINT,
                                   g_param_spec_boolean ("rules-hint",
							 P_("Rules Hint"),
							 P_("Set a hint to the theme engine to draw rows in alternating colors"),
							 FALSE,
							 GTK_PARAM_READWRITE));

    g_object_class_install_property (o_class,
				     PROP_ENABLE_SEARCH,
				     g_param_spec_boolean ("enable-search",
							   P_("Enable Search"),
							   P_("View allows user to search through columns interactively"),
							   TRUE,
							   GTK_PARAM_READWRITE));

    g_object_class_install_property (o_class,
				     PROP_SEARCH_COLUMN,
				     g_param_spec_int ("search-column",
						       P_("Search Column"),
						       P_("Model column to search through during interactive search"),
						       -1,
						       G_MAXINT,
						       -1,
						       GTK_PARAM_READWRITE));

    /**
     * PsppSheetView:hover-selection:
     * 
     * Enables of disables the hover selection mode of @tree_view.
     * Hover selection makes the selected row follow the pointer.
     * Currently, this works only for the selection modes 
     * %PSPP_SHEET_SELECTION_SINGLE and %PSPP_SHEET_SELECTION_BROWSE.
     *
     * This mode is primarily intended for treeviews in popups, e.g.
     * in #GtkComboBox or #GtkEntryCompletion.
     *
     * Since: 2.6
     */
    g_object_class_install_property (o_class,
                                     PROP_HOVER_SELECTION,
                                     g_param_spec_boolean ("hover-selection",
                                                           P_("Hover Selection"),
                                                           P_("Whether the selection should follow the pointer"),
                                                           FALSE,
                                                           GTK_PARAM_READWRITE));

    g_object_class_install_property (o_class,
                                     PROP_RUBBER_BANDING,
                                     g_param_spec_boolean ("rubber-banding",
                                                           P_("Rubber Banding"),
                                                           P_("Whether to enable selection of multiple items by dragging the mouse pointer"),
                                                           FALSE,
                                                           GTK_PARAM_READWRITE));

    g_object_class_install_property (o_class,
                                     PROP_ENABLE_GRID_LINES,
                                     g_param_spec_enum ("enable-grid-lines",
							P_("Enable Grid Lines"),
							P_("Whether grid lines should be drawn in the tree view"),
							PSPP_TYPE_SHEET_VIEW_GRID_LINES,
							PSPP_SHEET_VIEW_GRID_LINES_NONE,
							GTK_PARAM_READWRITE));

    g_object_class_install_property (o_class,
				     PROP_TOOLTIP_COLUMN,
				     g_param_spec_int ("tooltip-column",
						       P_("Tooltip Column"),
						       P_("The column in the model containing the tooltip texts for the rows"),
						       -1,
						       G_MAXINT,
						       -1,
						       GTK_PARAM_READWRITE));

    g_object_class_install_property (o_class,
                                     PROP_SPECIAL_CELLS,
                                     g_param_spec_enum ("special-cells",
							P_("Special Cells"),
							P_("Whether rows have special cells."),
							PSPP_TYPE_SHEET_VIEW_SPECIAL_CELLS,
							PSPP_SHEET_VIEW_SPECIAL_CELLS_DETECT,
							GTK_PARAM_READWRITE));

    g_object_class_install_property (o_class,
				     PROP_FIXED_HEIGHT,
				     g_param_spec_int ("fixed-height",
						       P_("Fixed Height"),
						       P_("Height of a single row.  Normally the height of a row is determined automatically.  Writing this property sets fixed-height-set to true, preventing this property's value from changing."),
						       -1,
						       G_MAXINT,
						       -1,
						       GTK_PARAM_READWRITE));

    g_object_class_install_property (o_class,
                                     PROP_FIXED_HEIGHT_SET,
                                     g_param_spec_boolean ("fixed-height-set",
                                                           P_("Fixed Height Set"),
                                                           P_("Whether fixed-height was set externally."),
                                                           FALSE,
                                                           GTK_PARAM_READWRITE));

  /* Style properties */
#define _TREE_VIEW_EXPANDER_SIZE 12
#define _TREE_VIEW_VERTICAL_SEPARATOR 2
#define _TREE_VIEW_HORIZONTAL_SEPARATOR 2

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_int ("expander-size",
							     P_("Expander Size"),
							     P_("Size of the expander arrow"),
							     0,
							     G_MAXINT,
							     _TREE_VIEW_EXPANDER_SIZE,
							     GTK_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_int ("vertical-separator",
							     P_("Vertical Separator Width"),
							     P_("Vertical space between cells.  Must be an even number"),
							     0,
							     G_MAXINT,
							     _TREE_VIEW_VERTICAL_SEPARATOR,
							     GTK_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_int ("horizontal-separator",
							     P_("Horizontal Separator Width"),
							     P_("Horizontal space between cells.  Must be an even number"),
							     0,
							     G_MAXINT,
							     _TREE_VIEW_HORIZONTAL_SEPARATOR,
							     GTK_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_boolean ("allow-rules",
								 P_("Allow Rules"),
								 P_("Allow drawing of alternating color rows"),
								 TRUE,
								 GTK_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
                                           g_param_spec_boxed ("even-row-color",
                                                               P_("Even Row Color"),
                                                               P_("Color to use for even rows"),
							       GDK_TYPE_COLOR,
							       GTK_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
                                           g_param_spec_boxed ("odd-row-color",
                                                               P_("Odd Row Color"),
                                                               P_("Color to use for odd rows"),
							       GDK_TYPE_COLOR,
							       GTK_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_boolean ("row-ending-details",
								 P_("Row Ending details"),
								 P_("Enable extended row background theming"),
								 FALSE,
								 GTK_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_int ("grid-line-width",
							     P_("Grid line width"),
							     P_("Width, in pixels, of the tree view grid lines"),
							     0, G_MAXINT, 1,
							     GTK_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_int ("tree-line-width",
							     P_("Tree line width"),
							     P_("Width, in pixels, of the tree view lines"),
							     0, G_MAXINT, 1,
							     GTK_PARAM_READABLE));

  gtk_widget_class_install_style_property (widget_class,
					   g_param_spec_string ("tree-line-pattern",
								P_("Tree line pattern"),
								P_("Dash pattern used to draw the tree view lines"),
								"\1\1",
								GTK_PARAM_READABLE));

  /* Signals */
  /**
   * PsppSheetView::set-scroll-adjustments
   * @horizontal: the horizontal #GtkAdjustment
   * @vertical: the vertical #GtkAdjustment
   *
   * Set the scroll adjustments for the tree view. Usually scrolled containers
   * like #GtkScrolledWindow will emit this signal to connect two instances
   * of #GtkScrollbar to the scroll directions of the #PsppSheetView.
   */
  widget_class->set_scroll_adjustments_signal =
    g_signal_new ("set-scroll-adjustments",
		  G_TYPE_FROM_CLASS (o_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (PsppSheetViewClass, set_scroll_adjustments),
		  NULL, NULL,
		  psppire_marshal_VOID__OBJECT_OBJECT,
		  G_TYPE_NONE, 2,
		  GTK_TYPE_ADJUSTMENT,
		  GTK_TYPE_ADJUSTMENT);

  /**
   * PsppSheetView::row-activated:
   * @tree_view: the object on which the signal is emitted
   * @path: the #GtkTreePath for the activated row
   * @column: the #PsppSheetViewColumn in which the activation occurred
   *
   * The "row-activated" signal is emitted when the method
   * pspp_sheet_view_row_activated() is called or the user double clicks 
   * a treeview row. It is also emitted when a non-editable row is 
   * selected and one of the keys: Space, Shift+Space, Return or 
   * Enter is pressed.
   * 
   * For selection handling refer to the <link linkend="TreeWidget">tree 
   * widget conceptual overview</link> as well as #PsppSheetSelection.
   */
  tree_view_signals[ROW_ACTIVATED] =
    g_signal_new ("row-activated",
		  G_TYPE_FROM_CLASS (o_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (PsppSheetViewClass, row_activated),
		  NULL, NULL,
                  psppire_marshal_VOID__BOXED_OBJECT,
		  G_TYPE_NONE, 2,
		  GTK_TYPE_TREE_PATH,
		  PSPP_TYPE_SHEET_VIEW_COLUMN);

  /**
   * PsppSheetView::columns-changed:
   * @tree_view: the object on which the signal is emitted 
   * 
   * The number of columns of the treeview has changed.
   */
  tree_view_signals[COLUMNS_CHANGED] =
    g_signal_new ("columns-changed",
		  G_TYPE_FROM_CLASS (o_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (PsppSheetViewClass, columns_changed),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);

  /**
   * PsppSheetView::cursor-changed:
   * @tree_view: the object on which the signal is emitted
   * 
   * The position of the cursor (focused cell) has changed.
   */
  tree_view_signals[CURSOR_CHANGED] =
    g_signal_new ("cursor-changed",
		  G_TYPE_FROM_CLASS (o_class),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (PsppSheetViewClass, cursor_changed),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);

  tree_view_signals[MOVE_CURSOR] =
    g_signal_new ("move-cursor",
		  G_TYPE_FROM_CLASS (o_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (PsppSheetViewClass, move_cursor),
		  NULL, NULL,
		  psppire_marshal_BOOLEAN__ENUM_INT,
		  G_TYPE_BOOLEAN, 2,
		  GTK_TYPE_MOVEMENT_STEP,
		  G_TYPE_INT);

  tree_view_signals[SELECT_ALL] =
    g_signal_new ("select-all",
		  G_TYPE_FROM_CLASS (o_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (PsppSheetViewClass, select_all),
		  NULL, NULL,
		  psppire_marshal_BOOLEAN__VOID,
		  G_TYPE_BOOLEAN, 0);

  tree_view_signals[UNSELECT_ALL] =
    g_signal_new ("unselect-all",
		  G_TYPE_FROM_CLASS (o_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (PsppSheetViewClass, unselect_all),
		  NULL, NULL,
		  psppire_marshal_BOOLEAN__VOID,
		  G_TYPE_BOOLEAN, 0);

  tree_view_signals[SELECT_CURSOR_ROW] =
    g_signal_new ("select-cursor-row",
		  G_TYPE_FROM_CLASS (o_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (PsppSheetViewClass, select_cursor_row),
		  NULL, NULL,
		  psppire_marshal_BOOLEAN__BOOLEAN,
		  G_TYPE_BOOLEAN, 2,
		  G_TYPE_BOOLEAN, G_TYPE_INT);

  tree_view_signals[TOGGLE_CURSOR_ROW] =
    g_signal_new ("toggle-cursor-row",
		  G_TYPE_FROM_CLASS (o_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (PsppSheetViewClass, toggle_cursor_row),
		  NULL, NULL,
		  psppire_marshal_BOOLEAN__VOID,
		  G_TYPE_BOOLEAN, 0);

  tree_view_signals[START_INTERACTIVE_SEARCH] =
    g_signal_new ("start-interactive-search",
		  G_TYPE_FROM_CLASS (o_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (PsppSheetViewClass, start_interactive_search),
		  NULL, NULL,
		  psppire_marshal_BOOLEAN__VOID,
		  G_TYPE_BOOLEAN, 0);

  /* Key bindings */
  for (i = 0; i < 2; i++)
    {
      pspp_sheet_view_add_move_binding (binding_set[i], GDK_Up, 0, TRUE,
                                        GTK_MOVEMENT_DISPLAY_LINES, -1);
      pspp_sheet_view_add_move_binding (binding_set[i], GDK_KP_Up, 0, TRUE,
                                        GTK_MOVEMENT_DISPLAY_LINES, -1);

      pspp_sheet_view_add_move_binding (binding_set[i], GDK_Down, 0, TRUE,
                                        GTK_MOVEMENT_DISPLAY_LINES, 1);
      pspp_sheet_view_add_move_binding (binding_set[i], GDK_KP_Down, 0, TRUE,
                                        GTK_MOVEMENT_DISPLAY_LINES, 1);

      pspp_sheet_view_add_move_binding (binding_set[i], GDK_p, GDK_CONTROL_MASK, FALSE,
                                        GTK_MOVEMENT_DISPLAY_LINES, -1);

      pspp_sheet_view_add_move_binding (binding_set[i], GDK_n, GDK_CONTROL_MASK, FALSE,
                                        GTK_MOVEMENT_DISPLAY_LINES, 1);

      pspp_sheet_view_add_move_binding (binding_set[i], GDK_Home, 0, TRUE,
                                        GTK_MOVEMENT_DISPLAY_LINE_ENDS, -1);
      pspp_sheet_view_add_move_binding (binding_set[i], GDK_KP_Home, 0, TRUE,
                                        GTK_MOVEMENT_DISPLAY_LINE_ENDS, -1);

      pspp_sheet_view_add_move_binding (binding_set[i], GDK_End, 0, TRUE,
                                        GTK_MOVEMENT_DISPLAY_LINE_ENDS, 1);
      pspp_sheet_view_add_move_binding (binding_set[i], GDK_KP_End, 0, TRUE,
                                        GTK_MOVEMENT_DISPLAY_LINE_ENDS, 1);

      pspp_sheet_view_add_move_binding (binding_set[i], GDK_Page_Up, 0, TRUE,
                                        GTK_MOVEMENT_PAGES, -1);
      pspp_sheet_view_add_move_binding (binding_set[i], GDK_KP_Page_Up, 0, TRUE,
                                        GTK_MOVEMENT_PAGES, -1);

      pspp_sheet_view_add_move_binding (binding_set[i], GDK_Page_Down, 0, TRUE,
                                        GTK_MOVEMENT_PAGES, 1);
      pspp_sheet_view_add_move_binding (binding_set[i], GDK_KP_Page_Down, 0, TRUE,
                                        GTK_MOVEMENT_PAGES, 1);


      gtk_binding_entry_add_signal (binding_set[i], GDK_Up, GDK_CONTROL_MASK, "move-cursor", 2,
                                    G_TYPE_ENUM, GTK_MOVEMENT_BUFFER_ENDS,
                                    G_TYPE_INT, -1);

      gtk_binding_entry_add_signal (binding_set[i], GDK_Down, GDK_CONTROL_MASK, "move-cursor", 2,
                                    G_TYPE_ENUM, GTK_MOVEMENT_BUFFER_ENDS,
                                    G_TYPE_INT, 1);

      gtk_binding_entry_add_signal (binding_set[i], GDK_Right, 0, "move-cursor", 2,
                                    G_TYPE_ENUM, GTK_MOVEMENT_VISUAL_POSITIONS,
                                    G_TYPE_INT, 1);

      gtk_binding_entry_add_signal (binding_set[i], GDK_Left, 0, "move-cursor", 2,
                                    G_TYPE_ENUM, GTK_MOVEMENT_VISUAL_POSITIONS,
                                    G_TYPE_INT, -1);

      gtk_binding_entry_add_signal (binding_set[i], GDK_Tab, 0, "move-cursor", 2,
                                    G_TYPE_ENUM, GTK_MOVEMENT_LOGICAL_POSITIONS,
                                    G_TYPE_INT, 1);

      gtk_binding_entry_add_signal (binding_set[i], GDK_Tab, GDK_SHIFT_MASK, "move-cursor", 2,
                                    G_TYPE_ENUM, GTK_MOVEMENT_LOGICAL_POSITIONS,
                                    G_TYPE_INT, -1);

      gtk_binding_entry_add_signal (binding_set[i], GDK_KP_Right, 0, "move-cursor", 2,
                                    G_TYPE_ENUM, GTK_MOVEMENT_DISPLAY_LINE_ENDS,
                                    G_TYPE_INT, 1);

      gtk_binding_entry_add_signal (binding_set[i], GDK_KP_Left, 0, "move-cursor", 2,
                                    G_TYPE_ENUM, GTK_MOVEMENT_DISPLAY_LINE_ENDS,
                                    G_TYPE_INT, -1);

      gtk_binding_entry_add_signal (binding_set[i], GDK_Right, GDK_CONTROL_MASK,
                                    "move-cursor", 2,
                                    G_TYPE_ENUM, GTK_MOVEMENT_DISPLAY_LINE_ENDS,
                                    G_TYPE_INT, 1);

      gtk_binding_entry_add_signal (binding_set[i], GDK_Left, GDK_CONTROL_MASK,
                                    "move-cursor", 2,
                                    G_TYPE_ENUM, GTK_MOVEMENT_DISPLAY_LINE_ENDS,
                                    G_TYPE_INT, -1);

      gtk_binding_entry_add_signal (binding_set[i], GDK_KP_Right, GDK_CONTROL_MASK,
                                    "move-cursor", 2,
                                    G_TYPE_ENUM, GTK_MOVEMENT_VISUAL_POSITIONS,
                                    G_TYPE_INT, 1);

      gtk_binding_entry_add_signal (binding_set[i], GDK_KP_Left, GDK_CONTROL_MASK,
                                    "move-cursor", 2,
                                    G_TYPE_ENUM, GTK_MOVEMENT_VISUAL_POSITIONS,
                                    G_TYPE_INT, -1);

      gtk_binding_entry_add_signal (binding_set[i], GDK_f, GDK_CONTROL_MASK, "start-interactive-search", 0);

      gtk_binding_entry_add_signal (binding_set[i], GDK_F, GDK_CONTROL_MASK, "start-interactive-search", 0);
    }

  gtk_binding_entry_add_signal (binding_set[0], GDK_space, GDK_CONTROL_MASK, "toggle-cursor-row", 0);
  gtk_binding_entry_add_signal (binding_set[0], GDK_KP_Space, GDK_CONTROL_MASK, "toggle-cursor-row", 0);

  gtk_binding_entry_add_signal (binding_set[0], GDK_a, GDK_CONTROL_MASK, "select-all", 0);
  gtk_binding_entry_add_signal (binding_set[0], GDK_slash, GDK_CONTROL_MASK, "select-all", 0);

  gtk_binding_entry_add_signal (binding_set[0], GDK_A, GDK_CONTROL_MASK | GDK_SHIFT_MASK, "unselect-all", 0);
  gtk_binding_entry_add_signal (binding_set[0], GDK_backslash, GDK_CONTROL_MASK, "unselect-all", 0);

  gtk_binding_entry_add_signal (binding_set[0], GDK_space, GDK_SHIFT_MASK, "select-cursor-row", 1,
				G_TYPE_BOOLEAN, TRUE,
                                G_TYPE_INT, PSPP_SHEET_SELECT_MODE_EXTEND);
  gtk_binding_entry_add_signal (binding_set[0], GDK_KP_Space, GDK_SHIFT_MASK, "select-cursor-row", 1,
				G_TYPE_BOOLEAN, TRUE,
                                G_TYPE_INT, PSPP_SHEET_SELECT_MODE_EXTEND);

  gtk_binding_entry_add_signal (binding_set[0], GDK_space, 0, "select-cursor-row", 1,
				G_TYPE_BOOLEAN, TRUE,
                                G_TYPE_INT, 0);
  gtk_binding_entry_add_signal (binding_set[0], GDK_KP_Space, 0, "select-cursor-row", 1,
				G_TYPE_BOOLEAN, TRUE,
                                G_TYPE_INT, 0);
  gtk_binding_entry_add_signal (binding_set[0], GDK_Return, 0, "select-cursor-row", 1,
				G_TYPE_BOOLEAN, TRUE,
                                G_TYPE_INT, 0);
  gtk_binding_entry_add_signal (binding_set[0], GDK_ISO_Enter, 0, "select-cursor-row", 1,
				G_TYPE_BOOLEAN, TRUE,
                                G_TYPE_INT, 0);
  gtk_binding_entry_add_signal (binding_set[0], GDK_KP_Enter, 0, "select-cursor-row", 1,
				G_TYPE_BOOLEAN, TRUE,
                                G_TYPE_INT, 0);

  gtk_binding_entry_add_signal (binding_set[0], GDK_BackSpace, 0, "select-cursor-parent", 0);
  gtk_binding_entry_add_signal (binding_set[0], GDK_BackSpace, GDK_CONTROL_MASK, "select-cursor-parent", 0);

  g_type_class_add_private (o_class, sizeof (PsppSheetViewPrivate));
}

static void
pspp_sheet_view_buildable_init (GtkBuildableIface *iface)
{
  iface->add_child = pspp_sheet_view_buildable_add_child;
}

static void
pspp_sheet_view_init (PsppSheetView *tree_view)
{
  tree_view->priv = G_TYPE_INSTANCE_GET_PRIVATE (tree_view, PSPP_TYPE_SHEET_VIEW, PsppSheetViewPrivate);

  gtk_widget_set_can_focus (GTK_WIDGET (tree_view), TRUE);
  gtk_widget_set_redraw_on_allocate (GTK_WIDGET (tree_view), FALSE);

  tree_view->priv->flags =  PSPP_SHEET_VIEW_DRAW_KEYFOCUS
                            | PSPP_SHEET_VIEW_HEADERS_VISIBLE;

  /* We need some padding */
  tree_view->priv->selected = range_tower_create ();
  tree_view->priv->dy = 0;
  tree_view->priv->cursor_offset = 0;
  tree_view->priv->n_columns = 0;
  tree_view->priv->header_height = 1;
  tree_view->priv->x_drag = 0;
  tree_view->priv->drag_pos = -1;
  tree_view->priv->header_has_focus = FALSE;
  tree_view->priv->pressed_button = -1;
  tree_view->priv->press_start_x = -1;
  tree_view->priv->press_start_y = -1;
  tree_view->priv->reorderable = FALSE;
  tree_view->priv->presize_handler_timer = 0;
  tree_view->priv->scroll_sync_timer = 0;
  tree_view->priv->fixed_height = -1;
  tree_view->priv->fixed_height_set = FALSE;
  pspp_sheet_view_set_adjustments (tree_view, NULL, NULL);
  tree_view->priv->selection = _pspp_sheet_selection_new_with_tree_view (tree_view);
  tree_view->priv->enable_search = TRUE;
  tree_view->priv->search_column = -1;
  tree_view->priv->search_position_func = pspp_sheet_view_search_position_func;
  tree_view->priv->search_equal_func = pspp_sheet_view_search_equal_func;
  tree_view->priv->search_custom_entry_set = FALSE;
  tree_view->priv->typeselect_flush_timeout = 0;
  tree_view->priv->init_hadjust_value = TRUE;    
  tree_view->priv->width = 0;
          
  tree_view->priv->hover_selection = FALSE;

  tree_view->priv->rubber_banding_enable = FALSE;

  tree_view->priv->grid_lines = PSPP_SHEET_VIEW_GRID_LINES_NONE;

  tree_view->priv->tooltip_column = -1;

  tree_view->priv->special_cells = PSPP_SHEET_VIEW_SPECIAL_CELLS_DETECT;

  tree_view->priv->post_validation_flag = FALSE;

  tree_view->priv->last_button_x = -1;
  tree_view->priv->last_button_y = -1;

  tree_view->priv->event_last_x = -10000;
  tree_view->priv->event_last_y = -10000;

  tree_view->priv->prelight_node = -1;
  tree_view->priv->rubber_band_start_node = -1;
  tree_view->priv->rubber_band_end_node = -1;

  tree_view->priv->anchor_column = NULL;

  tree_view->priv->button_style = NULL;

  tree_view->dispose_has_run = FALSE;
}



/* GObject Methods
 */

static void
pspp_sheet_view_set_property (GObject         *object,
			    guint            prop_id,
			    const GValue    *value,
			    GParamSpec      *pspec)
{
  PsppSheetView *tree_view;

  tree_view = PSPP_SHEET_VIEW (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      pspp_sheet_view_set_model (tree_view, g_value_get_object (value));
      break;
    case PROP_HADJUSTMENT:
      pspp_sheet_view_set_hadjustment (tree_view, g_value_get_object (value));
      break;
    case PROP_VADJUSTMENT:
      pspp_sheet_view_set_vadjustment (tree_view, g_value_get_object (value));
      break;
    case PROP_HEADERS_VISIBLE:
      pspp_sheet_view_set_headers_visible (tree_view, g_value_get_boolean (value));
      break;
    case PROP_HEADERS_CLICKABLE:
      pspp_sheet_view_set_headers_clickable (tree_view, g_value_get_boolean (value));
      break;
    case PROP_REORDERABLE:
      pspp_sheet_view_set_reorderable (tree_view, g_value_get_boolean (value));
      break;
    case PROP_RULES_HINT:
      pspp_sheet_view_set_rules_hint (tree_view, g_value_get_boolean (value));
      break;
    case PROP_ENABLE_SEARCH:
      pspp_sheet_view_set_enable_search (tree_view, g_value_get_boolean (value));
      break;
    case PROP_SEARCH_COLUMN:
      pspp_sheet_view_set_search_column (tree_view, g_value_get_int (value));
      break;
    case PROP_HOVER_SELECTION:
      tree_view->priv->hover_selection = g_value_get_boolean (value);
      break;
    case PROP_RUBBER_BANDING:
      tree_view->priv->rubber_banding_enable = g_value_get_boolean (value);
      break;
    case PROP_ENABLE_GRID_LINES:
      pspp_sheet_view_set_grid_lines (tree_view, g_value_get_enum (value));
      break;
    case PROP_TOOLTIP_COLUMN:
      pspp_sheet_view_set_tooltip_column (tree_view, g_value_get_int (value));
      break;
    case PROP_SPECIAL_CELLS:
      pspp_sheet_view_set_special_cells (tree_view, g_value_get_enum (value));
      break;
    case PROP_FIXED_HEIGHT:
      pspp_sheet_view_set_fixed_height (tree_view, g_value_get_int (value));
      break;
    case PROP_FIXED_HEIGHT_SET:
      if (g_value_get_boolean (value))
        {
          if (!tree_view->priv->fixed_height_set
              && tree_view->priv->fixed_height >= 0)
            {
              tree_view->priv->fixed_height_set = true;
              g_object_notify (G_OBJECT (tree_view), "fixed-height-set");
            }
        }
      else
        {
          if (tree_view->priv->fixed_height_set)
            {
              tree_view->priv->fixed_height_set = false;
              g_object_notify (G_OBJECT (tree_view), "fixed-height-set");
              install_presize_handler (tree_view);
            }
        }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
pspp_sheet_view_get_property (GObject    *object,
			    guint       prop_id,
			    GValue     *value,
			    GParamSpec *pspec)
{
  PsppSheetView *tree_view;

  tree_view = PSPP_SHEET_VIEW (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, tree_view->priv->model);
      break;
    case PROP_HADJUSTMENT:
      g_value_set_object (value, tree_view->priv->hadjustment);
      break;
    case PROP_VADJUSTMENT:
      g_value_set_object (value, tree_view->priv->vadjustment);
      break;
    case PROP_HEADERS_VISIBLE:
      g_value_set_boolean (value, pspp_sheet_view_get_headers_visible (tree_view));
      break;
    case PROP_HEADERS_CLICKABLE:
      g_value_set_boolean (value, pspp_sheet_view_get_headers_clickable (tree_view));
      break;
    case PROP_REORDERABLE:
      g_value_set_boolean (value, tree_view->priv->reorderable);
      break;
    case PROP_RULES_HINT:
      g_value_set_boolean (value, tree_view->priv->has_rules);
      break;
    case PROP_ENABLE_SEARCH:
      g_value_set_boolean (value, tree_view->priv->enable_search);
      break;
    case PROP_SEARCH_COLUMN:
      g_value_set_int (value, tree_view->priv->search_column);
      break;
    case PROP_HOVER_SELECTION:
      g_value_set_boolean (value, tree_view->priv->hover_selection);
      break;
    case PROP_RUBBER_BANDING:
      g_value_set_boolean (value, tree_view->priv->rubber_banding_enable);
      break;
    case PROP_ENABLE_GRID_LINES:
      g_value_set_enum (value, tree_view->priv->grid_lines);
      break;
    case PROP_TOOLTIP_COLUMN:
      g_value_set_int (value, tree_view->priv->tooltip_column);
      break;
    case PROP_SPECIAL_CELLS:
      g_value_set_enum (value, tree_view->priv->special_cells);
      break;
    case PROP_FIXED_HEIGHT:
      g_value_set_int (value, pspp_sheet_view_get_fixed_height (tree_view));
      break;
    case PROP_FIXED_HEIGHT_SET:
      g_value_set_boolean (value, tree_view->priv->fixed_height_set);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
pspp_sheet_view_dispose (GObject *object)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (object);

  if (tree_view->dispose_has_run)
    return;

  tree_view->dispose_has_run = TRUE;

  if (tree_view->priv->selection != NULL)
    {
      _pspp_sheet_selection_set_tree_view (tree_view->priv->selection, NULL);
      g_object_unref (tree_view->priv->selection);
      tree_view->priv->selection = NULL;
    }

  if (tree_view->priv->hadjustment)
    {
      g_object_unref (tree_view->priv->hadjustment);
      tree_view->priv->hadjustment = NULL;
    }
  if (tree_view->priv->vadjustment)
    {
      g_object_unref (tree_view->priv->vadjustment);
      tree_view->priv->vadjustment = NULL;
    }

  if (tree_view->priv->button_style)
    {
      g_object_unref (tree_view->priv->button_style);
      tree_view->priv->button_style = NULL;
    }


  G_OBJECT_CLASS (pspp_sheet_view_parent_class)->dispose (object);
}



static void
pspp_sheet_view_buildable_add_child (GtkBuildable *tree_view,
				   GtkBuilder  *builder,
				   GObject     *child,
				   const gchar *type)
{
  pspp_sheet_view_append_column (PSPP_SHEET_VIEW (tree_view), PSPP_SHEET_VIEW_COLUMN (child));
}

static void
pspp_sheet_view_finalize (GObject *object)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (object);

  pspp_sheet_view_stop_editing (tree_view, TRUE);

  if (tree_view->priv->selected != NULL)
    {
      range_tower_destroy (tree_view->priv->selected);
      tree_view->priv->selected = NULL;
    }


  tree_view->priv->prelight_node = -1;


  if (tree_view->priv->scroll_to_path != NULL)
    {
      gtk_tree_row_reference_free (tree_view->priv->scroll_to_path);
      tree_view->priv->scroll_to_path = NULL;
    }

  if (tree_view->priv->drag_dest_row != NULL)
    {
      gtk_tree_row_reference_free (tree_view->priv->drag_dest_row);
      tree_view->priv->drag_dest_row = NULL;
    }

  if (tree_view->priv->top_row != NULL)
    {
      gtk_tree_row_reference_free (tree_view->priv->top_row);
      tree_view->priv->top_row = NULL;
    }

  if (tree_view->priv->column_drop_func_data &&
      tree_view->priv->column_drop_func_data_destroy)
    {
      tree_view->priv->column_drop_func_data_destroy (tree_view->priv->column_drop_func_data);
      tree_view->priv->column_drop_func_data = NULL;
    }

  if (tree_view->priv->destroy_count_destroy &&
      tree_view->priv->destroy_count_data)
    {
      tree_view->priv->destroy_count_destroy (tree_view->priv->destroy_count_data);
      tree_view->priv->destroy_count_data = NULL;
    }

  gtk_tree_row_reference_free (tree_view->priv->cursor);
  tree_view->priv->cursor = NULL;

  gtk_tree_row_reference_free (tree_view->priv->anchor);
  tree_view->priv->anchor = NULL;

  /* destroy interactive search dialog */
  if (tree_view->priv->search_window)
    {
      gtk_widget_destroy (tree_view->priv->search_window);
      tree_view->priv->search_window = NULL;
      tree_view->priv->search_entry = NULL;
      if (tree_view->priv->typeselect_flush_timeout)
	{
	  g_source_remove (tree_view->priv->typeselect_flush_timeout);
	  tree_view->priv->typeselect_flush_timeout = 0;
	}
    }

  if (tree_view->priv->search_destroy && tree_view->priv->search_user_data)
    {
      tree_view->priv->search_destroy (tree_view->priv->search_user_data);
      tree_view->priv->search_user_data = NULL;
    }

  if (tree_view->priv->search_position_destroy && tree_view->priv->search_position_user_data)
    {
      tree_view->priv->search_position_destroy (tree_view->priv->search_position_user_data);
      tree_view->priv->search_position_user_data = NULL;
    }

  pspp_sheet_view_set_model (tree_view, NULL);


  G_OBJECT_CLASS (pspp_sheet_view_parent_class)->finalize (object);
}



/* GtkWidget Methods
 */

/* GtkWidget::map helper */
static void
pspp_sheet_view_map_buttons (PsppSheetView *tree_view)
{
  GList *list;

  g_return_if_fail (gtk_widget_get_mapped (GTK_WIDGET (tree_view)));

  if (PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_HEADERS_VISIBLE))
    {
      PsppSheetViewColumn *column;

      for (list = tree_view->priv->columns; list; list = list->next)
	{
	  column = list->data;
          if (column->button != NULL &&
              gtk_widget_get_visible (column->button) &&
              !gtk_widget_get_mapped (column->button))
            gtk_widget_map (column->button);
	}
      for (list = tree_view->priv->columns; list; list = list->next)
	{
	  column = list->data;
	  if (column->visible == FALSE || column->window == NULL)
	    continue;
	  if (column->resizable)
	    {
	      gdk_window_raise (column->window);
	      gdk_window_show (column->window);
	    }
	  else
	    gdk_window_hide (column->window);
	}
      gdk_window_show (tree_view->priv->header_window);
    }
}

static void
pspp_sheet_view_map (GtkWidget *widget)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  GList *tmp_list;

  gtk_widget_set_mapped (widget, TRUE);

  tmp_list = tree_view->priv->children;
  while (tmp_list)
    {
      PsppSheetViewChild *child = tmp_list->data;
      tmp_list = tmp_list->next;

      if (gtk_widget_get_visible (child->widget))
	{
	  if (!gtk_widget_get_mapped (child->widget))
	    gtk_widget_map (child->widget);
	}
    }
  gdk_window_show (tree_view->priv->bin_window);

  pspp_sheet_view_map_buttons (tree_view);

  gdk_window_show (gtk_widget_get_window (widget));
}

static void
pspp_sheet_view_realize (GtkWidget *widget)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  GList *tmp_list;
  GdkWindowAttr attributes;
  gint attributes_mask;
  GtkAllocation allocation;
  GtkAllocation old_allocation;

  gtk_widget_set_realized (widget, TRUE);

  gtk_widget_get_allocation (widget, &allocation);
  gtk_widget_get_allocation (widget, &old_allocation);

  /* Make the main, clipping window */
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x =      allocation.x;
  attributes.y =      allocation.y;
  attributes.width =  allocation.width;
  attributes.height = allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK;

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;

  gtk_widget_set_window (widget,
			 gdk_window_new (gtk_widget_get_parent_window (widget),
					 &attributes, attributes_mask));
  gdk_window_set_user_data (gtk_widget_get_window (widget), widget);

  /* Make the window for the tree */
  attributes.x = 0;
  attributes.y = TREE_VIEW_HEADER_HEIGHT (tree_view);
  attributes.width = MAX (tree_view->priv->width, old_allocation.width);
  attributes.height = old_allocation.height;
  attributes.event_mask = (GDK_EXPOSURE_MASK |
                           GDK_SCROLL_MASK |
                           GDK_POINTER_MOTION_MASK |
                           GDK_ENTER_NOTIFY_MASK |
                           GDK_LEAVE_NOTIFY_MASK |
                           GDK_BUTTON_PRESS_MASK |
                           GDK_BUTTON_RELEASE_MASK |
                           gtk_widget_get_events (widget));

  tree_view->priv->bin_window = gdk_window_new (gtk_widget_get_window (widget),
						&attributes, attributes_mask);
  gdk_window_set_user_data (tree_view->priv->bin_window, widget);

  /* Make the column header window */
  attributes.x = 0;
  attributes.y = 0;
  attributes.width = MAX (tree_view->priv->width, old_allocation.width);
  attributes.height = tree_view->priv->header_height;
  attributes.event_mask = (GDK_EXPOSURE_MASK |
                           GDK_SCROLL_MASK |
                           GDK_BUTTON_PRESS_MASK |
                           GDK_BUTTON_RELEASE_MASK |
                           GDK_KEY_PRESS_MASK |
                           GDK_KEY_RELEASE_MASK |
                           gtk_widget_get_events (widget));

  tree_view->priv->header_window = gdk_window_new (gtk_widget_get_window (widget),
						   &attributes, attributes_mask);
  gdk_window_set_user_data (tree_view->priv->header_window, widget);

  /* Add them all up. */
  gtk_widget_set_style (widget,
		       gtk_style_attach (gtk_widget_get_style (widget), gtk_widget_get_window (widget)));
  gdk_window_set_background (tree_view->priv->bin_window, &gtk_widget_get_style (widget)->base[gtk_widget_get_state (widget)]);
  gtk_style_set_background (gtk_widget_get_style (widget), tree_view->priv->header_window, GTK_STATE_NORMAL);

  tmp_list = tree_view->priv->children;
  while (tmp_list)
    {
      PsppSheetViewChild *child = tmp_list->data;
      tmp_list = tmp_list->next;

      gtk_widget_set_parent_window (child->widget, tree_view->priv->bin_window);
    }

  for (tmp_list = tree_view->priv->columns; tmp_list; tmp_list = tmp_list->next)
    _pspp_sheet_view_column_realize_button (PSPP_SHEET_VIEW_COLUMN (tmp_list->data));

  /* Need to call those here, since they create GCs */
  pspp_sheet_view_set_grid_lines (tree_view, tree_view->priv->grid_lines);

  install_presize_handler (tree_view); 
}

static void
pspp_sheet_view_unrealize (GtkWidget *widget)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  PsppSheetViewPrivate *priv = tree_view->priv;
  GList *list;

  GTK_WIDGET_CLASS (pspp_sheet_view_parent_class)->unrealize (widget);

  if (priv->scroll_timeout != 0)
    {
      g_source_remove (priv->scroll_timeout);
      priv->scroll_timeout = 0;
    }

  if (priv->open_dest_timeout != 0)
    {
      g_source_remove (priv->open_dest_timeout);
      priv->open_dest_timeout = 0;
    }

  if (priv->presize_handler_timer != 0)
    {
      g_source_remove (priv->presize_handler_timer);
      priv->presize_handler_timer = 0;
    }

  if (priv->validate_rows_timer != 0)
    {
      g_source_remove (priv->validate_rows_timer);
      priv->validate_rows_timer = 0;
    }

  if (priv->scroll_sync_timer != 0)
    {
      g_source_remove (priv->scroll_sync_timer);
      priv->scroll_sync_timer = 0;
    }

  if (priv->typeselect_flush_timeout)
    {
      g_source_remove (priv->typeselect_flush_timeout);
      priv->typeselect_flush_timeout = 0;
    }
  
  for (list = priv->columns; list; list = list->next)
    _pspp_sheet_view_column_unrealize_button (PSPP_SHEET_VIEW_COLUMN (list->data));

  gdk_window_set_user_data (priv->bin_window, NULL);
  gdk_window_destroy (priv->bin_window);
  priv->bin_window = NULL;

  gdk_window_set_user_data (priv->header_window, NULL);
  gdk_window_destroy (priv->header_window);
  priv->header_window = NULL;

  if (priv->drag_window)
    {
      gdk_window_set_user_data (priv->drag_window, NULL);
      gdk_window_destroy (priv->drag_window);
      priv->drag_window = NULL;
    }

  if (priv->drag_highlight_window)
    {
      gdk_window_set_user_data (priv->drag_highlight_window, NULL);
      gdk_window_destroy (priv->drag_highlight_window);
      priv->drag_highlight_window = NULL;
    }

  if (tree_view->priv->columns != NULL)
    {
      list = tree_view->priv->columns;
      while (list)
	{
	  PsppSheetViewColumn *column;
	  column = PSPP_SHEET_VIEW_COLUMN (list->data);
	  list = list->next;
	  pspp_sheet_view_remove_column (tree_view, column);
	}
      tree_view->priv->columns = NULL;
    }
}

/* GtkWidget::size_request helper */
static void
pspp_sheet_view_size_request_columns (PsppSheetView *tree_view)
{
  GList *list;

  tree_view->priv->header_height = 0;

  if (tree_view->priv->model)
    {
      for (list = tree_view->priv->columns; list; list = list->next)
        {
          GtkRequisition requisition;
          PsppSheetViewColumn *column = list->data;

          pspp_sheet_view_column_size_request (column, &requisition);
	  column->button_request = requisition.width;
          tree_view->priv->header_height = MAX (tree_view->priv->header_height, requisition.height);
        }
    }
}


/* Called only by ::size_request */
static void
pspp_sheet_view_update_size (PsppSheetView *tree_view)
{
  GList *list;
  PsppSheetViewColumn *column;
  gint i;

  if (tree_view->priv->model == NULL)
    {
      tree_view->priv->width = 0;
      tree_view->priv->prev_width = 0;                   
      tree_view->priv->height = 0;
      return;
    }

  tree_view->priv->prev_width = tree_view->priv->width;  
  tree_view->priv->width = 0;

  /* keep this in sync with size_allocate below */
  for (list = tree_view->priv->columns, i = 0; list; list = list->next, i++)
    {
      gint real_requested_width = 0;
      column = list->data;
      if (!column->visible)
	continue;

      if (column->use_resized_width)
	{
	  real_requested_width = column->resized_width;
	}
      else
	{
	  real_requested_width = column->fixed_width;
	}

      if (column->min_width != -1)
	real_requested_width = MAX (real_requested_width, column->min_width);
      if (column->max_width != -1)
	real_requested_width = MIN (real_requested_width, column->max_width);

      tree_view->priv->width += real_requested_width;
    }

  tree_view->priv->height = tree_view->priv->fixed_height * tree_view->priv->row_count;
}

static void
pspp_sheet_view_size_request (GtkWidget      *widget,
			    GtkRequisition *requisition)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  GList *tmp_list;

  /* we validate some rows initially just to make sure we have some size. 
   * In practice, with a lot of static lists, this should get a good width.
   */
  initialize_fixed_height_mode (tree_view);
  pspp_sheet_view_size_request_columns (tree_view);
  pspp_sheet_view_update_size (PSPP_SHEET_VIEW (widget));

  requisition->width = tree_view->priv->width;
  requisition->height = tree_view->priv->height + TREE_VIEW_HEADER_HEIGHT (tree_view);

  tmp_list = tree_view->priv->children;

  while (tmp_list)
    {
      PsppSheetViewChild *child = tmp_list->data;
      GtkRequisition child_requisition;

      tmp_list = tmp_list->next;

      if (gtk_widget_get_visible (child->widget))
        gtk_widget_size_request (child->widget, &child_requisition);
    }
}

static void
invalidate_column (PsppSheetView       *tree_view,
                   PsppSheetViewColumn *column)
{
  gint column_offset = 0;
  GList *list;
  GtkWidget *widget = GTK_WIDGET (tree_view);
  gboolean rtl;

  if (!gtk_widget_get_realized (widget))
    return;

  rtl = (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL);
  for (list = (rtl ? g_list_last (tree_view->priv->columns) : g_list_first (tree_view->priv->columns));
       list;
       list = (rtl ? list->prev : list->next))
    {
      PsppSheetViewColumn *tmpcolumn = list->data;
      if (tmpcolumn == column)
	{
	  GdkRectangle invalid_rect;
	  GtkAllocation allocation;

	  gtk_widget_get_allocation (widget, &allocation);
	  invalid_rect.x = column_offset;
	  invalid_rect.y = 0;
	  invalid_rect.width = column->width;
	  invalid_rect.height = allocation.height;
	  
	  gdk_window_invalidate_rect (gtk_widget_get_window (widget), &invalid_rect, TRUE);
	  break;
	}
      
      column_offset += tmpcolumn->width;
    }
}

static void
invalidate_last_column (PsppSheetView *tree_view)
{
  GList *last_column;
  gboolean rtl;

  rtl = (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL);

  for (last_column = (rtl ? g_list_first (tree_view->priv->columns) : g_list_last (tree_view->priv->columns));
       last_column;
       last_column = (rtl ? last_column->next : last_column->prev))
    {
      if (PSPP_SHEET_VIEW_COLUMN (last_column->data)->visible)
        {
          invalidate_column (tree_view, last_column->data);
          return;
        }
    }
}

static gint
pspp_sheet_view_get_real_requested_width_from_column (PsppSheetView       *tree_view,
                                                    PsppSheetViewColumn *column)
{
  gint real_requested_width;

  if (column->use_resized_width)
    {
      real_requested_width = column->resized_width;
    }
  else
    {
      real_requested_width = column->fixed_width;
    }

  if (column->min_width != -1)
    real_requested_width = MAX (real_requested_width, column->min_width);
  if (column->max_width != -1)
    real_requested_width = MIN (real_requested_width, column->max_width);

  return real_requested_width;
}

static gboolean
span_intersects (int a0, int a_width,
                 int b0, int b_width)
{
  int a1 = a0 + a_width;
  int b1 = b0 + b_width;
  return (a0 >= b0 && a0 < b1) || (b0 >= a0 && b0 < a1);
}

/* GtkWidget::size_allocate helper */
static void
pspp_sheet_view_size_allocate_columns (GtkWidget *widget,
				     gboolean  *width_changed)
{
  PsppSheetView *tree_view;
  GList *list, *first_column, *last_column;
  PsppSheetViewColumn *column;
  GtkAllocation col_allocation;
  GtkAllocation allocation;
  gint width = 0;
  gint extra, extra_per_column;
  gint full_requested_width = 0;
  gint number_of_expand_columns = 0;
  gboolean column_changed = FALSE;
  gboolean rtl;

  tree_view = PSPP_SHEET_VIEW (widget);

  for (last_column = g_list_last (tree_view->priv->columns);
       last_column && !(PSPP_SHEET_VIEW_COLUMN (last_column->data)->visible);
       last_column = last_column->prev)
    ;

  if (last_column == NULL)
    return;

  for (first_column = g_list_first (tree_view->priv->columns);
       first_column && !(PSPP_SHEET_VIEW_COLUMN (first_column->data)->visible);
       first_column = first_column->next)
    ;

  col_allocation.y = 0;
  col_allocation.height = tree_view->priv->header_height;

  rtl = (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL);

  /* find out how many extra space and expandable columns we have */
  for (list = tree_view->priv->columns; list != last_column->next; list = list->next)
    {
      column = (PsppSheetViewColumn *)list->data;

      if (!column->visible)
	continue;

      full_requested_width += pspp_sheet_view_get_real_requested_width_from_column (tree_view, column);

      if (column->expand)
	number_of_expand_columns++;
    }

  gtk_widget_get_allocation (widget, &allocation);
  extra = MAX (allocation.width - full_requested_width, 0);
  if (number_of_expand_columns > 0)
    extra_per_column = extra/number_of_expand_columns;
  else
    extra_per_column = 0;

  for (list = (rtl ? last_column : first_column); 
       list != (rtl ? first_column->prev : last_column->next);
       list = (rtl ? list->prev : list->next)) 
    {
      gint real_requested_width = 0;
      gint old_width;

      column = list->data;
      old_width = column->width;

      if (!column->visible)
	continue;

      /* We need to handle the dragged button specially.
       */
      if (column == tree_view->priv->drag_column)
	{
	  GtkAllocation drag_allocation;
	  drag_allocation.width =  gdk_window_get_width (tree_view->priv->drag_window);
	  drag_allocation.height = gdk_window_get_height (tree_view->priv->drag_window);
	  drag_allocation.x = 0;
	  drag_allocation.y = 0;
          pspp_sheet_view_column_size_allocate (tree_view->priv->drag_column,
                                                &drag_allocation);
	  width += drag_allocation.width;
	  continue;
	}

      real_requested_width = pspp_sheet_view_get_real_requested_width_from_column (tree_view, column);

      col_allocation.x = width;
      column->width = real_requested_width;

      if (column->expand)
	{
	  if (number_of_expand_columns == 1)
	    {
	      /* We add the remander to the last column as
	       * */
	      column->width += extra;
	    }
	  else
	    {
	      column->width += extra_per_column;
	      extra -= extra_per_column;
	      number_of_expand_columns --;
	    }
	}

      if (column->width != old_width)
        g_object_notify (G_OBJECT (column), "width");

      col_allocation.width = column->width;
      width += column->width;

      if (column->width > old_width)
        column_changed = TRUE;

      pspp_sheet_view_column_size_allocate (column, &col_allocation);

      if (span_intersects (col_allocation.x, col_allocation.width,
                           gtk_adjustment_get_value (tree_view->priv->hadjustment),
                           allocation.width)
          && gtk_widget_get_realized (widget))
        pspp_sheet_view_column_set_need_button (column, TRUE);

      if (column->window)
	gdk_window_move_resize (column->window,
                                col_allocation.x + (rtl ? 0 : col_allocation.width) - TREE_VIEW_DRAG_WIDTH/2,
				col_allocation.y,
                                TREE_VIEW_DRAG_WIDTH, col_allocation.height);
    }

  /* We change the width here.  The user might have been resizing columns,
   * so the total width of the tree view changes.
   */
  tree_view->priv->width = width;
  if (width_changed)
    *width_changed = TRUE;

  if (column_changed)
    gtk_widget_queue_draw (GTK_WIDGET (tree_view));
}

static void
pspp_sheet_view_size_allocate (GtkWidget     *widget,
			     GtkAllocation *allocation)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  GList *tmp_list;
  gboolean width_changed = FALSE;
  GtkAllocation old_allocation;
  gtk_widget_get_allocation (widget, &old_allocation);

  if (allocation->width != old_allocation.width)
    width_changed = TRUE;


  gtk_widget_set_allocation (widget, allocation);

  tmp_list = tree_view->priv->children;

  while (tmp_list)
    {
      GtkAllocation allocation;

      PsppSheetViewChild *child = tmp_list->data;
      tmp_list = tmp_list->next;

      /* totally ignore our child's requisition */
      allocation.x = child->x;
      allocation.y = child->y;
      allocation.width = child->width;
      allocation.height = child->height;
      gtk_widget_size_allocate (child->widget, &allocation);
    }

  /* We size-allocate the columns first because the width of the
   * tree view (used in updating the adjustments below) might change.
   */
  pspp_sheet_view_size_allocate_columns (widget, &width_changed);

  gtk_adjustment_set_page_size (tree_view->priv->hadjustment, allocation->width);
  gtk_adjustment_set_page_increment (tree_view->priv->hadjustment, allocation->width * 0.9);
  gtk_adjustment_set_step_increment (tree_view->priv->hadjustment, allocation->width * 0.1);
  gtk_adjustment_set_lower (tree_view->priv->hadjustment, 0);
  gtk_adjustment_set_upper (tree_view->priv->hadjustment, MAX (gtk_adjustment_get_page_size (tree_view->priv->hadjustment), tree_view->priv->width));

  if (gtk_widget_get_direction(widget) == GTK_TEXT_DIR_RTL)   
    {
      if (allocation->width < tree_view->priv->width)
        {
	  if (tree_view->priv->init_hadjust_value)
	    {
	      gtk_adjustment_set_value (tree_view->priv->hadjustment, MAX (tree_view->priv->width - allocation->width, 0));
	      tree_view->priv->init_hadjust_value = FALSE;
	    }
	  else if (allocation->width != old_allocation.width)
	    {
	      gtk_adjustment_set_value (tree_view->priv->hadjustment, CLAMP (gtk_adjustment_get_value (tree_view->priv->hadjustment) - allocation->width + old_allocation.width, 0, tree_view->priv->width - allocation->width));
	    }
	  else
	    gtk_adjustment_set_value (tree_view->priv->hadjustment, CLAMP (tree_view->priv->width - (tree_view->priv->prev_width - gtk_adjustment_get_value (tree_view->priv->hadjustment)), 0, tree_view->priv->width - allocation->width));
	}
      else
        {
	  gtk_adjustment_set_value (tree_view->priv->hadjustment, 0);
	  tree_view->priv->init_hadjust_value = TRUE;
	}
    }
  else
    if (gtk_adjustment_get_value (tree_view->priv->hadjustment) + allocation->width > tree_view->priv->width)
      gtk_adjustment_set_value (tree_view->priv->hadjustment, MAX (tree_view->priv->width - allocation->width, 0));

  gtk_adjustment_changed (tree_view->priv->hadjustment);

  gtk_adjustment_set_page_size (tree_view->priv->vadjustment, allocation->height - TREE_VIEW_HEADER_HEIGHT (tree_view));
  gtk_adjustment_set_step_increment (tree_view->priv->vadjustment, gtk_adjustment_get_page_size (tree_view->priv->vadjustment) * 0.1);
  gtk_adjustment_set_page_increment (tree_view->priv->vadjustment, gtk_adjustment_get_page_size (tree_view->priv->vadjustment) * 0.9);
  gtk_adjustment_set_lower (tree_view->priv->vadjustment, 0);
  gtk_adjustment_set_upper (tree_view->priv->vadjustment, MAX (gtk_adjustment_get_page_size (tree_view->priv->vadjustment), tree_view->priv->height));

  gtk_adjustment_changed (tree_view->priv->vadjustment);

  /* now the adjustments and window sizes are in sync, we can sync toprow/dy again */
  if (tree_view->priv->height <= gtk_adjustment_get_page_size (tree_view->priv->vadjustment))
    gtk_adjustment_set_value (GTK_ADJUSTMENT (tree_view->priv->vadjustment), 0);
  else if (gtk_adjustment_get_value (tree_view->priv->vadjustment) + gtk_adjustment_get_page_size (tree_view->priv->vadjustment) > tree_view->priv->height)
    gtk_adjustment_set_value (GTK_ADJUSTMENT (tree_view->priv->vadjustment),
                              tree_view->priv->height - gtk_adjustment_get_page_size (tree_view->priv->vadjustment));
  else if (gtk_tree_row_reference_valid (tree_view->priv->top_row))
    pspp_sheet_view_top_row_to_dy (tree_view);
  else
    pspp_sheet_view_dy_to_top_row (tree_view);
  
  if (gtk_widget_get_realized (widget))
    {
      gdk_window_move_resize (gtk_widget_get_window (widget),
			      allocation->x, allocation->y,
			      allocation->width, allocation->height);
      gdk_window_move_resize (tree_view->priv->header_window,
			      - (gint) gtk_adjustment_get_value (tree_view->priv->hadjustment),
			      0,
			      MAX (tree_view->priv->width, allocation->width),
			      tree_view->priv->header_height);
      gdk_window_move_resize (tree_view->priv->bin_window,
			      - (gint) gtk_adjustment_get_value (tree_view->priv->hadjustment),
			      TREE_VIEW_HEADER_HEIGHT (tree_view),
			      MAX (tree_view->priv->width, allocation->width),
			      allocation->height - TREE_VIEW_HEADER_HEIGHT (tree_view));
    }

  if (tree_view->priv->row_count == 0)
    invalidate_empty_focus (tree_view);

  if (gtk_widget_get_realized (widget))
    {
      gboolean has_expand_column = FALSE;
      for (tmp_list = tree_view->priv->columns; tmp_list; tmp_list = tmp_list->next)
	{
	  if (pspp_sheet_view_column_get_expand (PSPP_SHEET_VIEW_COLUMN (tmp_list->data)))
	    {
	      has_expand_column = TRUE;
	      break;
	    }
	}

      /* This little hack only works if we have an LTR locale, and no column has the  */
      if (width_changed)
	{
	  if (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_LTR &&
	      ! has_expand_column)
	    invalidate_last_column (tree_view);
	  else
	    gtk_widget_queue_draw (widget);
	}
    }
}

/* Grabs the focus and unsets the PSPP_SHEET_VIEW_DRAW_KEYFOCUS flag */
static void
grab_focus_and_unset_draw_keyfocus (PsppSheetView *tree_view)
{
  GtkWidget *widget = GTK_WIDGET (tree_view);

  if (gtk_widget_get_can_focus (widget) && !gtk_widget_has_focus (widget))
    gtk_widget_grab_focus (widget);
  PSPP_SHEET_VIEW_UNSET_FLAG (tree_view, PSPP_SHEET_VIEW_DRAW_KEYFOCUS);
}

gboolean
pspp_sheet_view_node_is_selected (PsppSheetView *tree_view,
                                  int node)
{
  return node >= 0 && range_tower_contains (tree_view->priv->selected, node);
}

void
pspp_sheet_view_node_select (PsppSheetView *tree_view,
                             int node)
{
  range_tower_set1 (tree_view->priv->selected, node, 1);
}

void
pspp_sheet_view_node_unselect (PsppSheetView *tree_view,
                               int node)
{
  range_tower_set0 (tree_view->priv->selected, node, 1);
}

gint
pspp_sheet_view_node_next (PsppSheetView *tree_view,
                           gint node)
{
  return node + 1 < tree_view->priv->row_count ? node + 1 : -1;
}

gint
pspp_sheet_view_node_prev (PsppSheetView *tree_view,
                           gint node)
{
  return node > 0 ? node - 1 : -1;
}

static gboolean
all_columns_selected (PsppSheetView *tree_view)
{
  GList *list;

  for (list = tree_view->priv->columns; list; list = list->next)
    {
      PsppSheetViewColumn *column = list->data;
      if (column->selectable && !column->selected)
        return FALSE;
    }

  return TRUE;
}

static gboolean
pspp_sheet_view_row_head_clicked (PsppSheetView *tree_view,
                                  gint node,
                                  PsppSheetViewColumn *column,
                                  GdkEventButton *event)
{
  PsppSheetSelection *selection;
  PsppSheetSelectionMode mode;
  GtkTreePath *path;
  gboolean update_anchor;
  gboolean handled;
  guint modifiers;

  g_return_val_if_fail (tree_view != NULL, FALSE);
  g_return_val_if_fail (column != NULL, FALSE);

  selection = tree_view->priv->selection;
  mode = pspp_sheet_selection_get_mode (selection);
  if (mode != PSPP_SHEET_SELECTION_RECTANGLE)
    return FALSE;

  if (!column->row_head)
    return FALSE;

  if (event)
    {
      modifiers = event->state & gtk_accelerator_get_default_mod_mask ();
      if (event->type != GDK_BUTTON_PRESS
          || (modifiers != GDK_CONTROL_MASK && modifiers != GDK_SHIFT_MASK))
        return FALSE;
    }
  else
    modifiers = 0;

  path = gtk_tree_path_new_from_indices (node, -1);
  if (event == NULL)
    {
      pspp_sheet_selection_unselect_all (selection);
      pspp_sheet_selection_select_path (selection, path);
      pspp_sheet_selection_select_all_columns (selection);
      update_anchor = TRUE;
      handled = TRUE;
    }
  else if (event->type == GDK_BUTTON_PRESS && event->button == 3)
    {
      if (pspp_sheet_selection_count_selected_rows (selection) <= 1
          || !all_columns_selected (tree_view))
        {
          pspp_sheet_selection_unselect_all (selection);
          pspp_sheet_selection_select_path (selection, path);
          pspp_sheet_selection_select_all_columns (selection);
          update_anchor = TRUE;
          handled = FALSE;
        }
      else
        update_anchor = handled = FALSE;
    }
  else if (event->type == GDK_BUTTON_PRESS && event->button == 1
           && modifiers == GDK_CONTROL_MASK)
    {
      if (!all_columns_selected (tree_view))
        {
          pspp_sheet_selection_unselect_all (selection);
          pspp_sheet_selection_select_all_columns (selection);
        }

      if (pspp_sheet_selection_path_is_selected (selection, path))
        pspp_sheet_selection_unselect_path (selection, path);
      else
        pspp_sheet_selection_select_path (selection, path);
      update_anchor = TRUE;
      handled = TRUE;
    }
  else if (event->type == GDK_BUTTON_PRESS && event->button == 1
           && modifiers == GDK_SHIFT_MASK)
    {
      GtkTreeRowReference *anchor = tree_view->priv->anchor;
      GtkTreePath *anchor_path;

      if (all_columns_selected (tree_view)
          && gtk_tree_row_reference_valid (anchor))
        {
          update_anchor = FALSE;
          anchor_path = gtk_tree_row_reference_get_path (anchor);
        }
      else
        {
          update_anchor = TRUE;
          anchor_path = gtk_tree_path_copy (path);
        }

      pspp_sheet_selection_unselect_all (selection);
      pspp_sheet_selection_select_range (selection, anchor_path, path);
      pspp_sheet_selection_select_all_columns (selection);

      gtk_tree_path_free (anchor_path);

      handled = TRUE;
    }
  else
    update_anchor = handled = FALSE;

  if (update_anchor)
    {
      if (tree_view->priv->anchor)
        gtk_tree_row_reference_free (tree_view->priv->anchor);
      tree_view->priv->anchor =
        gtk_tree_row_reference_new_proxy (G_OBJECT (tree_view),
                                          tree_view->priv->model,
                                          path);
    }

  gtk_tree_path_free (path);
  return handled;
}

static gboolean
find_click (PsppSheetView *tree_view,
            gint x, gint y,
            gint *node,
            PsppSheetViewColumn **column,
            GdkRectangle *background_area,
            GdkRectangle *cell_area)
{
  gint y_offset;
  gboolean rtl;
  GList *list;
  gint new_y;

  /* find the node that was clicked */
  new_y = TREE_WINDOW_Y_TO_RBTREE_Y(tree_view, y);
  if (new_y < 0)
    new_y = 0;
  y_offset = -pspp_sheet_view_find_offset (tree_view, new_y, node);

  if (*node < 0)
    return FALSE;

  background_area->y = y_offset + y;
  background_area->height = ROW_HEIGHT (tree_view);
  background_area->x = 0;

  /* Let the column have a chance at selecting it. */
  rtl = (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL);
  for (list = (rtl ? g_list_last (tree_view->priv->columns) : g_list_first (tree_view->priv->columns));
       list; list = (rtl ? list->prev : list->next))
    {
      PsppSheetViewColumn *candidate = list->data;

      if (!candidate->visible)
        continue;

      background_area->width = candidate->width;
      if ((background_area->x > x) ||
          (background_area->x + background_area->width <= x))
        {
          background_area->x += background_area->width;
          continue;
        }

      /* we found the focus column */

      pspp_sheet_view_adjust_cell_area (tree_view, candidate, background_area,
                                        TRUE, cell_area);
      *column = candidate;
      return TRUE;
    }

  return FALSE;
}

static gboolean
pspp_sheet_view_button_press (GtkWidget      *widget,
			    GdkEventButton *event)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  GList *list;
  PsppSheetViewColumn *column = NULL;
  gint i;
  GdkRectangle background_area;
  GdkRectangle cell_area;
  gboolean rtl;

  rtl = (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL);
  pspp_sheet_view_stop_editing (tree_view, FALSE);


  /* Because grab_focus can cause reentrancy, we delay grab_focus until after
   * we're done handling the button press.
   */

  if (event->window == tree_view->priv->bin_window)
    {
      int node;
      GtkTreePath *path;
      gint dval;
      gint pre_val, aft_val;
      PsppSheetViewColumn *column = NULL;
      GtkCellRenderer *focus_cell = NULL;
      gboolean row_double_click = FALSE;

      /* Empty tree? */
      if (tree_view->priv->row_count == 0)
	{
	  grab_focus_and_unset_draw_keyfocus (tree_view);
	  return TRUE;
	}

      if (!find_click (tree_view, event->x, event->y, &node, &column,
                       &background_area, &cell_area))
        {
	  grab_focus_and_unset_draw_keyfocus (tree_view);
          return FALSE;
        }

      tree_view->priv->focus_column = column;

      if (pspp_sheet_view_row_head_clicked (tree_view, node, column, event))
        return TRUE;

      /* select */
      pre_val = gtk_adjustment_get_value (tree_view->priv->vadjustment);

      path = _pspp_sheet_view_find_path (tree_view, node);

      /* we only handle selection modifications on the first button press
       */
      if (event->type == GDK_BUTTON_PRESS)
        {
          PsppSheetSelectionMode mode = 0;

          if ((event->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
            mode |= PSPP_SHEET_SELECT_MODE_TOGGLE;
          if ((event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK)
            mode |= PSPP_SHEET_SELECT_MODE_EXTEND;

          focus_cell = _pspp_sheet_view_column_get_cell_at_pos (column, event->x - background_area.x);
          if (focus_cell)
            pspp_sheet_view_column_focus_cell (column, focus_cell);

          if (event->state & GDK_CONTROL_MASK)
            {
              pspp_sheet_view_real_set_cursor (tree_view, path, FALSE, TRUE, mode);
              pspp_sheet_view_real_toggle_cursor_row (tree_view);
            }
          else if (event->state & GDK_SHIFT_MASK)
            {
              pspp_sheet_view_real_set_cursor (tree_view, path, TRUE, TRUE, mode);
              pspp_sheet_view_real_select_cursor_row (tree_view, FALSE, mode);
            }
          else
            {
              pspp_sheet_view_real_set_cursor (tree_view, path, TRUE, TRUE, 0);
            }

          if (tree_view->priv->anchor_column == NULL ||
              !(event->state & GDK_SHIFT_MASK))
            tree_view->priv->anchor_column = column;
          pspp_sheet_selection_unselect_all_columns (tree_view->priv->selection);
          pspp_sheet_selection_select_column_range (tree_view->priv->selection,
                                                    tree_view->priv->anchor_column,
                                                    column);
        }

      /* the treeview may have been scrolled because of _set_cursor,
       * correct here
       */

      aft_val = gtk_adjustment_get_value (tree_view->priv->vadjustment);
      dval = pre_val - aft_val;

      cell_area.y += dval;
      background_area.y += dval;

      /* Save press to possibly begin a drag
       */
      if (!tree_view->priv->in_grab &&
	  tree_view->priv->pressed_button < 0)
        {
          tree_view->priv->pressed_button = event->button;
          tree_view->priv->press_start_x = event->x;
          tree_view->priv->press_start_y = event->y;
          tree_view->priv->press_start_node = node;

	  if (tree_view->priv->rubber_banding_enable
	      && (tree_view->priv->selection->type == PSPP_SHEET_SELECTION_MULTIPLE ||
                  tree_view->priv->selection->type == PSPP_SHEET_SELECTION_RECTANGLE))
	    {
	      tree_view->priv->press_start_y += tree_view->priv->dy;
	      tree_view->priv->rubber_band_x = event->x;
	      tree_view->priv->rubber_band_y = event->y + tree_view->priv->dy;
	      tree_view->priv->rubber_band_status = RUBBER_BAND_MAYBE_START;

	      if ((event->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
		tree_view->priv->rubber_band_ctrl = TRUE;
	      if ((event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK)
		tree_view->priv->rubber_band_shift = TRUE;

	    }
        }

      /* Test if a double click happened on the same row. */
      if (event->button == 1 && event->type == GDK_BUTTON_PRESS)
        {
          int double_click_time, double_click_distance;

          g_object_get (gtk_settings_get_for_screen (
                          gtk_widget_get_screen (widget)),
                        "gtk-double-click-time", &double_click_time,
                        "gtk-double-click-distance", &double_click_distance,
                        NULL);

          /* Same conditions as _gdk_event_button_generate */
          if (tree_view->priv->last_button_x != -1 &&
              (event->time < tree_view->priv->last_button_time + double_click_time) &&
              (ABS (event->x - tree_view->priv->last_button_x) <= double_click_distance) &&
              (ABS (event->y - tree_view->priv->last_button_y) <= double_click_distance))
            {
              /* We do no longer compare paths of this row and the
               * row clicked previously.  We use the double click
               * distance to decide whether this is a valid click,
               * allowing the mouse to slightly move over another row.
               */
              row_double_click = TRUE;

              tree_view->priv->last_button_time = 0;
              tree_view->priv->last_button_x = -1;
              tree_view->priv->last_button_y = -1;
            }
          else
            {
              tree_view->priv->last_button_time = event->time;
              tree_view->priv->last_button_x = event->x;
              tree_view->priv->last_button_y = event->y;
            }
        }

      if (row_double_click)
	{
	  gtk_grab_remove (widget);
	  pspp_sheet_view_row_activated (tree_view, path, column);

          if (tree_view->priv->pressed_button == event->button)
            tree_view->priv->pressed_button = -1;
	}

      gtk_tree_path_free (path);

      /* If we activated the row through a double click we don't want to grab
       * focus back, as moving focus to another widget is pretty common.
       */
      if (!row_double_click)
	grab_focus_and_unset_draw_keyfocus (tree_view);

      return TRUE;
    }

  /* We didn't click in the window.  Let's check to see if we clicked on a column resize window.
   */
  for (i = 0, list = tree_view->priv->columns; list; list = list->next, i++)
    {
      column = list->data;
      if (event->window == column->window &&
	  column->resizable &&
	  column->window)
	{
	  gpointer drag_data;

	  if (gdk_pointer_grab (column->window, FALSE,
				GDK_POINTER_MOTION_HINT_MASK |
				GDK_BUTTON1_MOTION_MASK |
				GDK_BUTTON_RELEASE_MASK,
				NULL, NULL, event->time))
	    return FALSE;

	  gtk_grab_add (widget);
	  PSPP_SHEET_VIEW_SET_FLAG (tree_view, PSPP_SHEET_VIEW_IN_COLUMN_RESIZE);
	  column->resized_width = column->width;

	  /* block attached dnd signal handler */
	  drag_data = g_object_get_data (G_OBJECT (widget), "gtk-site-data");
	  if (drag_data)
	    g_signal_handlers_block_matched (widget,
					     G_SIGNAL_MATCH_DATA,
					     0, 0, NULL, NULL,
					     drag_data);

	  tree_view->priv->drag_pos = i;
	  tree_view->priv->x_drag = column->allocation.x + (rtl ? 0 : column->allocation.width);

	  if (!gtk_widget_has_focus (widget))
	    gtk_widget_grab_focus (widget);

	  return TRUE;
	}
    }
  return FALSE;
}

/* GtkWidget::button_release_event helper */
static gboolean
pspp_sheet_view_button_release_drag_column (GtkWidget      *widget,
					  GdkEventButton *event)
{
  PsppSheetView *tree_view;
  GList *l;
  gboolean rtl;

  tree_view = PSPP_SHEET_VIEW (widget);

  rtl = (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL);
  gdk_display_pointer_ungrab (gtk_widget_get_display (widget), GDK_CURRENT_TIME);
  gdk_display_keyboard_ungrab (gtk_widget_get_display (widget), GDK_CURRENT_TIME);

  /* Move the button back */
  g_return_val_if_fail (tree_view->priv->drag_column->button, FALSE);

  g_object_ref (tree_view->priv->drag_column->button);
  gtk_container_remove (GTK_CONTAINER (tree_view), tree_view->priv->drag_column->button);
  gtk_widget_set_parent_window (tree_view->priv->drag_column->button, tree_view->priv->header_window);
  gtk_widget_set_parent (tree_view->priv->drag_column->button, GTK_WIDGET (tree_view));
  g_object_unref (tree_view->priv->drag_column->button);
  gtk_widget_queue_resize (widget);
  if (tree_view->priv->drag_column->resizable)
    {
      gdk_window_raise (tree_view->priv->drag_column->window);
      gdk_window_show (tree_view->priv->drag_column->window);
    }
  else
    gdk_window_hide (tree_view->priv->drag_column->window);

  gtk_widget_grab_focus (tree_view->priv->drag_column->button);

  if (rtl)
    {
      if (tree_view->priv->cur_reorder &&
	  tree_view->priv->cur_reorder->right_column != tree_view->priv->drag_column)
	pspp_sheet_view_move_column_after (tree_view, tree_view->priv->drag_column,
					 tree_view->priv->cur_reorder->right_column);
    }
  else
    {
      if (tree_view->priv->cur_reorder &&
	  tree_view->priv->cur_reorder->left_column != tree_view->priv->drag_column)
	pspp_sheet_view_move_column_after (tree_view, tree_view->priv->drag_column,
					 tree_view->priv->cur_reorder->left_column);
    }
  tree_view->priv->drag_column = NULL;
  gdk_window_hide (tree_view->priv->drag_window);

  for (l = tree_view->priv->column_drag_info; l != NULL; l = l->next)
    g_slice_free (PsppSheetViewColumnReorder, l->data);
  g_list_free (tree_view->priv->column_drag_info);
  tree_view->priv->column_drag_info = NULL;
  tree_view->priv->cur_reorder = NULL;

  if (tree_view->priv->drag_highlight_window)
    gdk_window_hide (tree_view->priv->drag_highlight_window);

  /* Reset our flags */
  tree_view->priv->drag_column_window_state = DRAG_COLUMN_WINDOW_STATE_UNSET;
  PSPP_SHEET_VIEW_UNSET_FLAG (tree_view, PSPP_SHEET_VIEW_IN_COLUMN_DRAG);

  return TRUE;
}

/* GtkWidget::button_release_event helper */
static gboolean
pspp_sheet_view_button_release_column_resize (GtkWidget      *widget,
					    GdkEventButton *event)
{
  PsppSheetView *tree_view;
  gpointer drag_data;

  tree_view = PSPP_SHEET_VIEW (widget);

  tree_view->priv->drag_pos = -1;

  /* unblock attached dnd signal handler */
  drag_data = g_object_get_data (G_OBJECT (widget), "gtk-site-data");
  if (drag_data)
    g_signal_handlers_unblock_matched (widget,
				       G_SIGNAL_MATCH_DATA,
				       0, 0, NULL, NULL,
				       drag_data);

  PSPP_SHEET_VIEW_UNSET_FLAG (tree_view, PSPP_SHEET_VIEW_IN_COLUMN_RESIZE);
  gtk_grab_remove (widget);
  gdk_display_pointer_ungrab (gdk_window_get_display (event->window),
			      event->time);
  return TRUE;
}

static gboolean
pspp_sheet_view_button_release_edit (PsppSheetView *tree_view,
                                     GdkEventButton *event)
{
  GtkCellEditable *cell_editable;
  gchar *path_string;
  GtkTreePath *path;
  gint left, right;
  GtkTreeIter iter;
  PsppSheetViewColumn *column;
  GdkRectangle background_area;
  GdkRectangle cell_area;
  GdkRectangle area;
  guint modifiers;
  guint flags;
  int node;

  if (event->window != tree_view->priv->bin_window)
    return FALSE;

  /* Ignore a released button, if that button wasn't depressed */
  if (tree_view->priv->pressed_button != event->button)
    return FALSE;

  if (!find_click (tree_view, event->x, event->y, &node, &column, &background_area,
                   &cell_area))
    return FALSE;

  /* decide if we edit */
  path = _pspp_sheet_view_find_path (tree_view, node);
  modifiers = event->state & gtk_accelerator_get_default_mod_mask ();
  if (event->button != 1 || modifiers)
    return FALSE;

  gtk_tree_model_get_iter (tree_view->priv->model, &iter, path);
  pspp_sheet_view_column_cell_set_cell_data (column,
                                             tree_view->priv->model,
                                             &iter);

  if (!pspp_sheet_view_column_get_quick_edit (column)
      && _pspp_sheet_view_column_has_editable_cell (column))
    return FALSE;

  flags = 0;                    /* FIXME: get the right flags */
  path_string = gtk_tree_path_to_string (path);

  if (!_pspp_sheet_view_column_cell_event (column,
                                           &cell_editable,
                                           (GdkEvent *)event,
                                           path_string,
                                           &background_area,
                                           &cell_area, flags))
    return FALSE;

  if (cell_editable == NULL)
    return FALSE;

  pspp_sheet_view_real_set_cursor (tree_view, path,
                                   TRUE, TRUE, 0); /* XXX mode? */
  gtk_widget_queue_draw (GTK_WIDGET (tree_view));

  area = cell_area;
  _pspp_sheet_view_column_get_neighbor_sizes (
    column, _pspp_sheet_view_column_get_edited_cell (column), &left, &right);

  area.x += left;
  area.width -= right + left;

  pspp_sheet_view_real_start_editing (tree_view,
                                      column,
                                      path,
                                      cell_editable,
                                      &area,
                                      (GdkEvent *)event,
                                      flags);
  g_free (path_string);
  gtk_tree_path_free (path);
  return TRUE;
}

static gboolean
pspp_sheet_view_button_release (GtkWidget      *widget,
			      GdkEventButton *event)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);

  pspp_sheet_view_stop_editing (tree_view, FALSE);
  if (tree_view->priv->rubber_band_status != RUBBER_BAND_ACTIVE
      && pspp_sheet_view_button_release_edit (tree_view, event))
    {
      if (tree_view->priv->pressed_button == event->button)
        tree_view->priv->pressed_button = -1;

      tree_view->priv->rubber_band_status = RUBBER_BAND_OFF;
      return TRUE;
    }

  if (PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_IN_COLUMN_DRAG))
    return pspp_sheet_view_button_release_drag_column (widget, event);

  if (tree_view->priv->rubber_band_status)
    pspp_sheet_view_stop_rubber_band (tree_view);

  if (tree_view->priv->pressed_button == event->button)
    tree_view->priv->pressed_button = -1;

  if (PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_IN_COLUMN_RESIZE))
    return pspp_sheet_view_button_release_column_resize (widget, event);

  return FALSE;
}

static gboolean
pspp_sheet_view_grab_broken (GtkWidget          *widget,
			   GdkEventGrabBroken *event)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);

  if (PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_IN_COLUMN_DRAG))
    pspp_sheet_view_button_release_drag_column (widget, (GdkEventButton *)event);

  if (PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_IN_COLUMN_RESIZE))
    pspp_sheet_view_button_release_column_resize (widget, (GdkEventButton *)event);

  return TRUE;
}

/* GtkWidget::motion_event function set.
 */

static void
do_prelight (PsppSheetView *tree_view,
             int node,
	     /* these are in bin_window coords */
             gint         x,
             gint         y)
{
  int prev_node = tree_view->priv->prelight_node;

  if (prev_node != node)
    {
      tree_view->priv->prelight_node = node;

      if (prev_node >= 0)
        _pspp_sheet_view_queue_draw_node (tree_view, prev_node, NULL);

      if (node >= 0)
        _pspp_sheet_view_queue_draw_node (tree_view, node, NULL);
    }
}


static void
prelight_or_select (PsppSheetView *tree_view,
		    int node,
		    /* these are in bin_window coords */
		    gint         x,
		    gint         y)
{
  PsppSheetSelectionMode mode = pspp_sheet_selection_get_mode (tree_view->priv->selection);
  
  if (tree_view->priv->hover_selection &&
      (mode == PSPP_SHEET_SELECTION_SINGLE || mode == PSPP_SHEET_SELECTION_BROWSE) &&
      !(tree_view->priv->edited_column &&
	tree_view->priv->edited_column->editable_widget))
    {
      if (node >= 0)
	{
          if (!pspp_sheet_view_node_is_selected (tree_view, node))
	    {
	      GtkTreePath *path;
	      
	      path = _pspp_sheet_view_find_path (tree_view, node);
	      pspp_sheet_selection_select_path (tree_view->priv->selection, path);
              if (pspp_sheet_view_node_is_selected (tree_view, node))
		{
		  PSPP_SHEET_VIEW_UNSET_FLAG (tree_view, PSPP_SHEET_VIEW_DRAW_KEYFOCUS);
		  pspp_sheet_view_real_set_cursor (tree_view, path, FALSE, FALSE, 0); /* XXX mode? */
		}
	      gtk_tree_path_free (path);
	    }
	}

      else if (mode == PSPP_SHEET_SELECTION_SINGLE)
	pspp_sheet_selection_unselect_all (tree_view->priv->selection);
    }

    do_prelight (tree_view, node, x, y);
}

static void
ensure_unprelighted (PsppSheetView *tree_view)
{
  do_prelight (tree_view,
	       -1,
	       -1000, -1000); /* coords not possibly over an arrow */

  g_assert (tree_view->priv->prelight_node < 0);
}

static void
update_prelight (PsppSheetView *tree_view,
                 gint         x,
                 gint         y)
{
  int new_y;
  int node;

  if (tree_view->priv->row_count == 0)
    return;

  if (x == -10000)
    {
      ensure_unprelighted (tree_view);
      return;
    }

  new_y = TREE_WINDOW_Y_TO_RBTREE_Y (tree_view, y);
  if (new_y < 0)
    new_y = 0;

  pspp_sheet_view_find_offset (tree_view, new_y, &node);

  if (node >= 0)
    prelight_or_select (tree_view, node, x, y);
}




/* Our motion arrow is either a box (in the case of the original spot)
 * or an arrow.  It is expander_size wide.
 */
/*
 * 11111111111111
 * 01111111111110
 * 00111111111100
 * 00011111111000
 * 00001111110000
 * 00000111100000
 * 00000111100000
 * 00000111100000
 * ~ ~ ~ ~ ~ ~ ~
 * 00000111100000
 * 00000111100000
 * 00000111100000
 * 00001111110000
 * 00011111111000
 * 00111111111100
 * 01111111111110
 * 11111111111111
 */

static void
pspp_sheet_view_motion_draw_column_motion_arrow (PsppSheetView *tree_view)
{
#if GTK3_TRANSITION
  PsppSheetViewColumnReorder *reorder = tree_view->priv->cur_reorder;
  GtkWidget *widget = GTK_WIDGET (tree_view);
  GdkBitmap *mask = NULL;
  gint x;
  gint y;
  gint width;
  gint height;
  gint arrow_type = DRAG_COLUMN_WINDOW_STATE_UNSET;
  GdkWindowAttr attributes;
  guint attributes_mask;

  if (!reorder ||
      reorder->left_column == tree_view->priv->drag_column ||
      reorder->right_column == tree_view->priv->drag_column)
    arrow_type = DRAG_COLUMN_WINDOW_STATE_ORIGINAL;
  else if (reorder->left_column || reorder->right_column)
    {
      GdkRectangle visible_rect;
      pspp_sheet_view_get_visible_rect (tree_view, &visible_rect);
      if (reorder->left_column)
	x = reorder->left_column->allocation.x + reorder->left_column->allocation.width;
      else
	x = reorder->right_column->allocation.x;

      if (x < visible_rect.x)
	arrow_type = DRAG_COLUMN_WINDOW_STATE_ARROW_LEFT;
      else if (x > visible_rect.x + visible_rect.width)
	arrow_type = DRAG_COLUMN_WINDOW_STATE_ARROW_RIGHT;
      else
        arrow_type = DRAG_COLUMN_WINDOW_STATE_ARROW;
    }

  /* We want to draw the rectangle over the initial location. */
  if (arrow_type == DRAG_COLUMN_WINDOW_STATE_ORIGINAL)
    {
      GdkGC *gc;
      GdkColor col;

      if (tree_view->priv->drag_column_window_state != DRAG_COLUMN_WINDOW_STATE_ORIGINAL)
	{
	  if (tree_view->priv->drag_highlight_window)
	    {
	      gdk_window_set_user_data (tree_view->priv->drag_highlight_window,
					NULL);
	      gdk_window_destroy (tree_view->priv->drag_highlight_window);
	    }

	  attributes.window_type = GDK_WINDOW_CHILD;
	  attributes.wclass = GDK_INPUT_OUTPUT;
          attributes.x = tree_view->priv->drag_column_x;
          attributes.y = 0;
	  width = attributes.width = tree_view->priv->drag_column->allocation.width;
	  height = attributes.height = tree_view->priv->drag_column->allocation.height;
	  attributes.visual = gtk_widget_get_visual (GTK_WIDGET (tree_view));
	  attributes.colormap = gtk_widget_get_colormap (GTK_WIDGET (tree_view));
	  attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK | GDK_EXPOSURE_MASK | GDK_POINTER_MOTION_MASK;
	  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
	  tree_view->priv->drag_highlight_window = gdk_window_new (tree_view->priv->header_window, &attributes, attributes_mask);
	  gdk_window_set_user_data (tree_view->priv->drag_highlight_window, GTK_WIDGET (tree_view));

	  mask = gdk_pixmap_new (tree_view->priv->drag_highlight_window, width, height, 1);
	  gc = gdk_gc_new (mask);
	  col.pixel = 1;
	  gdk_gc_set_foreground (gc, &col);
	  gdk_draw_rectangle (mask, gc, TRUE, 0, 0, width, height);
	  col.pixel = 0;
	  gdk_gc_set_foreground(gc, &col);
	  gdk_draw_rectangle (mask, gc, TRUE, 2, 2, width - 4, height - 4);
	  g_object_unref (gc);

	  gdk_window_shape_combine_mask (tree_view->priv->drag_highlight_window,
					 mask, 0, 0);
	  if (mask) g_object_unref (mask);
	  tree_view->priv->drag_column_window_state = DRAG_COLUMN_WINDOW_STATE_ORIGINAL;
	}
    }
  else if (arrow_type == DRAG_COLUMN_WINDOW_STATE_ARROW)
    {
      gint i, j = 1;
      GdkGC *gc;
      GdkColor col;

      width = tree_view->priv->expander_size;

      /* Get x, y, width, height of arrow */
      gdk_window_get_origin (tree_view->priv->header_window, &x, &y);
      if (reorder->left_column)
	{
	  x += reorder->left_column->allocation.x + reorder->left_column->allocation.width - width/2;
	  height = reorder->left_column->allocation.height;
	}
      else
	{
	  x += reorder->right_column->allocation.x - width/2;
	  height = reorder->right_column->allocation.height;
	}
      y -= tree_view->priv->expander_size/2; /* The arrow takes up only half the space */
      height += tree_view->priv->expander_size;

      /* Create the new window */
      if (tree_view->priv->drag_column_window_state != DRAG_COLUMN_WINDOW_STATE_ARROW)
	{
	  if (tree_view->priv->drag_highlight_window)
	    {
	      gdk_window_set_user_data (tree_view->priv->drag_highlight_window,
					NULL);
	      gdk_window_destroy (tree_view->priv->drag_highlight_window);
	    }

	  attributes.window_type = GDK_WINDOW_TEMP;
	  attributes.wclass = GDK_INPUT_OUTPUT;
	  attributes.visual = gtk_widget_get_visual (GTK_WIDGET (tree_view));
	  attributes.colormap = gtk_widget_get_colormap (GTK_WIDGET (tree_view));
	  attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK | GDK_EXPOSURE_MASK | GDK_POINTER_MOTION_MASK;
	  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
          attributes.x = x;
          attributes.y = y;
	  attributes.width = width;
	  attributes.height = height;
	  tree_view->priv->drag_highlight_window = gdk_window_new (gtk_widget_get_root_window (widget),
								   &attributes, attributes_mask);
	  gdk_window_set_user_data (tree_view->priv->drag_highlight_window, GTK_WIDGET (tree_view));

	  mask = gdk_pixmap_new (tree_view->priv->drag_highlight_window, width, height, 1);
	  gc = gdk_gc_new (mask);
	  col.pixel = 1;
	  gdk_gc_set_foreground (gc, &col);
	  gdk_draw_rectangle (mask, gc, TRUE, 0, 0, width, height);

	  /* Draw the 2 arrows as per above */
	  col.pixel = 0;
	  gdk_gc_set_foreground (gc, &col);
	  for (i = 0; i < width; i ++)
	    {
	      if (i == (width/2 - 1))
		continue;
	      gdk_draw_line (mask, gc, i, j, i, height - j);
	      if (i < (width/2 - 1))
		j++;
	      else
		j--;
	    }
	  g_object_unref (gc);
	  gdk_window_shape_combine_mask (tree_view->priv->drag_highlight_window,
					 mask, 0, 0);
	  if (mask) g_object_unref (mask);
	}

      tree_view->priv->drag_column_window_state = DRAG_COLUMN_WINDOW_STATE_ARROW;
      gdk_window_move (tree_view->priv->drag_highlight_window, x, y);
    }
  else if (arrow_type == DRAG_COLUMN_WINDOW_STATE_ARROW_LEFT ||
	   arrow_type == DRAG_COLUMN_WINDOW_STATE_ARROW_RIGHT)
    {
      gint i, j = 1;
      GdkGC *gc;
      GdkColor col;

      width = tree_view->priv->expander_size;

      /* Get x, y, width, height of arrow */
      width = width/2; /* remember, the arrow only takes half the available width */
      gdk_window_get_origin (gtk_widget_get_window (widget), &x, &y);
      if (arrow_type == DRAG_COLUMN_WINDOW_STATE_ARROW_RIGHT)
	x += widget->allocation.width - width;

      if (reorder->left_column)
	height = reorder->left_column->allocation.height;
      else
	height = reorder->right_column->allocation.height;

      y -= tree_view->priv->expander_size;
      height += 2*tree_view->priv->expander_size;

      /* Create the new window */
      if (tree_view->priv->drag_column_window_state != DRAG_COLUMN_WINDOW_STATE_ARROW_LEFT &&
	  tree_view->priv->drag_column_window_state != DRAG_COLUMN_WINDOW_STATE_ARROW_RIGHT)
	{
	  if (tree_view->priv->drag_highlight_window)
	    {
	      gdk_window_set_user_data (tree_view->priv->drag_highlight_window,
					NULL);
	      gdk_window_destroy (tree_view->priv->drag_highlight_window);
	    }

	  attributes.window_type = GDK_WINDOW_TEMP;
	  attributes.wclass = GDK_INPUT_OUTPUT;
	  attributes.visual = gtk_widget_get_visual (GTK_WIDGET (tree_view));
	  attributes.colormap = gtk_widget_get_colormap (GTK_WIDGET (tree_view));
	  attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK | GDK_EXPOSURE_MASK | GDK_POINTER_MOTION_MASK;
	  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
          attributes.x = x;
          attributes.y = y;
	  attributes.width = width;
	  attributes.height = height;
	  tree_view->priv->drag_highlight_window = gdk_window_new (NULL, &attributes, attributes_mask);
	  gdk_window_set_user_data (tree_view->priv->drag_highlight_window, GTK_WIDGET (tree_view));

	  mask = gdk_pixmap_new (tree_view->priv->drag_highlight_window, width, height, 1);
	  gc = gdk_gc_new (mask);
	  col.pixel = 1;
	  gdk_gc_set_foreground (gc, &col);
	  gdk_draw_rectangle (mask, gc, TRUE, 0, 0, width, height);

	  /* Draw the 2 arrows as per above */
	  col.pixel = 0;
	  gdk_gc_set_foreground (gc, &col);
	  j = tree_view->priv->expander_size;
	  for (i = 0; i < width; i ++)
	    {
	      gint k;
	      if (arrow_type == DRAG_COLUMN_WINDOW_STATE_ARROW_LEFT)
		k = width - i - 1;
	      else
		k = i;
	      gdk_draw_line (mask, gc, k, j, k, height - j);
	      gdk_draw_line (mask, gc, k, 0, k, tree_view->priv->expander_size - j);
	      gdk_draw_line (mask, gc, k, height, k, height - tree_view->priv->expander_size + j);
	      j--;
	    }
	  g_object_unref (gc);
	  gdk_window_shape_combine_mask (tree_view->priv->drag_highlight_window,
					 mask, 0, 0);
	  if (mask) g_object_unref (mask);
	}

      tree_view->priv->drag_column_window_state = arrow_type;
      gdk_window_move (tree_view->priv->drag_highlight_window, x, y);
   }
  else
    {
      g_warning (G_STRLOC"Invalid PsppSheetViewColumnReorder struct");
      gdk_window_hide (tree_view->priv->drag_highlight_window);
      return;
    }

  gdk_window_show (tree_view->priv->drag_highlight_window);
  gdk_window_raise (tree_view->priv->drag_highlight_window);
#endif
}

static gboolean
pspp_sheet_view_motion_resize_column (GtkWidget      *widget,
				    GdkEventMotion *event)
{
  gint x;
  gint new_width;
  PsppSheetViewColumn *column;
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);

  column = pspp_sheet_view_get_column (tree_view, tree_view->priv->drag_pos);

  if (event->is_hint || event->window != gtk_widget_get_window (widget))
    gtk_widget_get_pointer (widget, &x, NULL);
  else
    x = event->x;

  if (tree_view->priv->hadjustment)
    x += gtk_adjustment_get_value (tree_view->priv->hadjustment);

  new_width = pspp_sheet_view_new_column_width (tree_view,
					      tree_view->priv->drag_pos, &x);
  if (x != tree_view->priv->x_drag &&
      (new_width != column->fixed_width))
    {
      column->use_resized_width = TRUE;
      column->resized_width = new_width;
#if 0
      if (column->expand)
	column->resized_width -= tree_view->priv->last_extra_space_per_column;
#endif
      gtk_widget_queue_resize (widget);
    }

  return FALSE;
}


static void
pspp_sheet_view_update_current_reorder (PsppSheetView *tree_view)
{
  PsppSheetViewColumnReorder *reorder = NULL;
  GList *list;
  gint mouse_x;

  gdk_window_get_pointer (tree_view->priv->header_window, &mouse_x, NULL, NULL);
  for (list = tree_view->priv->column_drag_info; list; list = list->next)
    {
      reorder = (PsppSheetViewColumnReorder *) list->data;
      if (mouse_x >= reorder->left_align && mouse_x < reorder->right_align)
	break;
      reorder = NULL;
    }

  /*  if (reorder && reorder == tree_view->priv->cur_reorder)
      return;*/

  tree_view->priv->cur_reorder = reorder;
  pspp_sheet_view_motion_draw_column_motion_arrow (tree_view);
}

static void
pspp_sheet_view_vertical_autoscroll (PsppSheetView *tree_view)
{
  GdkRectangle visible_rect;
  gint y;
  gint offset;
  gfloat value;

  gdk_window_get_pointer (tree_view->priv->bin_window, NULL, &y, NULL);
  y += tree_view->priv->dy;

  pspp_sheet_view_get_visible_rect (tree_view, &visible_rect);

  /* see if we are near the edge. */
  offset = y - (visible_rect.y + 2 * SCROLL_EDGE_SIZE);
  if (offset > 0)
    {
      offset = y - (visible_rect.y + visible_rect.height - 2 * SCROLL_EDGE_SIZE);
      if (offset < 0)
	return;
    }

  value = CLAMP (gtk_adjustment_get_value (tree_view->priv->vadjustment) + offset, 0.0,
		 gtk_adjustment_get_upper (tree_view->priv->vadjustment) - gtk_adjustment_get_page_size (tree_view->priv->vadjustment));
  gtk_adjustment_set_value (tree_view->priv->vadjustment, value);
}

static gboolean
pspp_sheet_view_horizontal_autoscroll (PsppSheetView *tree_view)
{
  GdkRectangle visible_rect;
  gint x;
  gint offset;
  gfloat value;

  gdk_window_get_pointer (tree_view->priv->bin_window, &x, NULL, NULL);

  pspp_sheet_view_get_visible_rect (tree_view, &visible_rect);

  /* See if we are near the edge. */
  offset = x - (visible_rect.x + SCROLL_EDGE_SIZE);
  if (offset > 0)
    {
      offset = x - (visible_rect.x + visible_rect.width - SCROLL_EDGE_SIZE);
      if (offset < 0)
	return TRUE;
    }
  offset = offset/3;

  value = CLAMP (gtk_adjustment_get_value (tree_view->priv->hadjustment) + offset,
		 0.0, gtk_adjustment_get_upper (tree_view->priv->hadjustment) - gtk_adjustment_get_page_size (tree_view->priv->hadjustment));
  gtk_adjustment_set_value (tree_view->priv->hadjustment, value);

  return TRUE;

}

static gboolean
pspp_sheet_view_motion_drag_column (GtkWidget      *widget,
				  GdkEventMotion *event)
{
  PsppSheetView *tree_view = (PsppSheetView *) widget;
  PsppSheetViewColumn *column = tree_view->priv->drag_column;
  gint x, y;
  GtkAllocation allocation;

  /* Sanity Check */
  if ((column == NULL) ||
      (event->window != tree_view->priv->drag_window))
    return FALSE;

  /* Handle moving the header */
  gdk_window_get_position (tree_view->priv->drag_window, &x, &y);
  gtk_widget_get_allocation (GTK_WIDGET (tree_view), &allocation);
  x = CLAMP (x + (gint)event->x - column->drag_x, 0,
	     MAX (tree_view->priv->width, allocation.width) - column->allocation.width);
  gdk_window_move (tree_view->priv->drag_window, x, y);
  
  /* autoscroll, if needed */
  pspp_sheet_view_horizontal_autoscroll (tree_view);
  /* Update the current reorder position and arrow; */
  pspp_sheet_view_update_current_reorder (tree_view);

  return TRUE;
}

static void
pspp_sheet_view_stop_rubber_band (PsppSheetView *tree_view)
{
  remove_scroll_timeout (tree_view);
  gtk_grab_remove (GTK_WIDGET (tree_view));

  if (tree_view->priv->rubber_band_status == RUBBER_BAND_ACTIVE)
    {
      GtkTreePath *tmp_path;

      gtk_widget_queue_draw (GTK_WIDGET (tree_view));

      /* The anchor path should be set to the start path */
      tmp_path = _pspp_sheet_view_find_path (tree_view,
					   tree_view->priv->rubber_band_start_node);

      if (tree_view->priv->anchor)
	gtk_tree_row_reference_free (tree_view->priv->anchor);

      tree_view->priv->anchor =
	gtk_tree_row_reference_new_proxy (G_OBJECT (tree_view),
					  tree_view->priv->model,
					  tmp_path);

      gtk_tree_path_free (tmp_path);

      /* ... and the cursor to the end path */
      tmp_path = _pspp_sheet_view_find_path (tree_view,
					   tree_view->priv->rubber_band_end_node);
      pspp_sheet_view_real_set_cursor (PSPP_SHEET_VIEW (tree_view), tmp_path, FALSE, FALSE, 0); /* XXX mode? */
      gtk_tree_path_free (tmp_path);

      _pspp_sheet_selection_emit_changed (tree_view->priv->selection);
    }

  /* Clear status variables */
  tree_view->priv->rubber_band_status = RUBBER_BAND_OFF;
  tree_view->priv->rubber_band_shift = 0;
  tree_view->priv->rubber_band_ctrl = 0;

  tree_view->priv->rubber_band_start_node = -1;
  tree_view->priv->rubber_band_end_node = -1;
}

static void
pspp_sheet_view_update_rubber_band_selection_range (PsppSheetView *tree_view,
						 int start_node,
						 int end_node,
						 gboolean     select,
						 gboolean     skip_start,
						 gboolean     skip_end)
{
  if (start_node == end_node)
    return;

  /* We skip the first node and jump inside the loop */
  if (skip_start)
    goto skip_first;

  do
    {
      /* Small optimization by assuming insensitive nodes are never
       * selected.
       */
      if (select)
        {
	  if (tree_view->priv->rubber_band_shift)
            pspp_sheet_view_node_select (tree_view, start_node);
	  else if (tree_view->priv->rubber_band_ctrl)
	    {
	      /* Toggle the selection state */
              if (pspp_sheet_view_node_is_selected (tree_view, start_node))
                pspp_sheet_view_node_unselect (tree_view, start_node);
	      else
                pspp_sheet_view_node_select (tree_view, start_node);
	    }
	  else
            pspp_sheet_view_node_select (tree_view, start_node);
	}
      else
        {
	  /* Mirror the above */
	  if (tree_view->priv->rubber_band_shift)
                pspp_sheet_view_node_unselect (tree_view, start_node);
	  else if (tree_view->priv->rubber_band_ctrl)
	    {
	      /* Toggle the selection state */
              if (pspp_sheet_view_node_is_selected (tree_view, start_node))
                pspp_sheet_view_node_unselect (tree_view, start_node);
	      else
                pspp_sheet_view_node_select (tree_view, start_node);
	    }
	  else
            pspp_sheet_view_node_unselect (tree_view, start_node);
	}

      _pspp_sheet_view_queue_draw_node (tree_view, start_node, NULL);

      if (start_node == end_node)
	break;

skip_first:

      start_node = pspp_sheet_view_node_next (tree_view, start_node);

      if (start_node < 0)
        /* Ran out of tree */
        break;

      if (skip_end && start_node == end_node)
	break;
    }
  while (TRUE);
}

static gint
pspp_sheet_view_node_find_offset (PsppSheetView *tree_view,
                                  int node)
{
  return node * tree_view->priv->fixed_height;
}

static gint
pspp_sheet_view_find_offset (PsppSheetView *tree_view,
                             gint height,
                             int *new_node)
{
  int fixed_height = tree_view->priv->fixed_height;
  if (fixed_height <= 0
      || height < 0
      || height >= tree_view->priv->row_count * fixed_height)
    {
      *new_node = -1;
      return 0;
    }
  else
    {
      *new_node = height / fixed_height;
      return height % fixed_height;
    }
}

static void
pspp_sheet_view_update_rubber_band_selection (PsppSheetView *tree_view)
{
  int start_node;
  int end_node;

  pspp_sheet_view_find_offset (tree_view, MIN (tree_view->priv->press_start_y, tree_view->priv->rubber_band_y), &start_node);
  pspp_sheet_view_find_offset (tree_view, MAX (tree_view->priv->press_start_y, tree_view->priv->rubber_band_y), &end_node);

  /* Handle the start area first */
  if (tree_view->priv->rubber_band_start_node < 0)
    {
      pspp_sheet_view_update_rubber_band_selection_range (tree_view,
						       start_node,
						       end_node,
						       TRUE,
						       FALSE,
						       FALSE);
    }
  else if (start_node < tree_view->priv->rubber_band_start_node)
    {
      /* New node is above the old one; selection became bigger */
      pspp_sheet_view_update_rubber_band_selection_range (tree_view,
						       start_node,
						       tree_view->priv->rubber_band_start_node,
						       TRUE,
						       FALSE,
						       TRUE);
    }
  else if (start_node > tree_view->priv->rubber_band_start_node)
    {
      /* New node is below the old one; selection became smaller */
      pspp_sheet_view_update_rubber_band_selection_range (tree_view,
						       tree_view->priv->rubber_band_start_node,
						       start_node,
						       FALSE,
						       FALSE,
						       TRUE);
    }

  tree_view->priv->rubber_band_start_node = start_node;

  /* Next, handle the end area */
  if (tree_view->priv->rubber_band_end_node < 0)
    {
      /* In the event this happens, start_node was also -1; this case is
       * handled above.
       */
    }
  else if (end_node < 0)
    {
      /* Find the last node in the tree */
      pspp_sheet_view_find_offset (tree_view, tree_view->priv->height - 1,
			       &end_node);

      /* Selection reached end of the tree */
      pspp_sheet_view_update_rubber_band_selection_range (tree_view,
						       tree_view->priv->rubber_band_end_node,
						       end_node,
						       TRUE,
						       TRUE,
						       FALSE);
    }
  else if (end_node > tree_view->priv->rubber_band_end_node)
    {
      /* New node is below the old one; selection became bigger */
      pspp_sheet_view_update_rubber_band_selection_range (tree_view,
						       tree_view->priv->rubber_band_end_node,
						       end_node,
						       TRUE,
						       TRUE,
						       FALSE);
    }
  else if (end_node < tree_view->priv->rubber_band_end_node)
    {
      /* New node is above the old one; selection became smaller */
      pspp_sheet_view_update_rubber_band_selection_range (tree_view,
						       end_node,
						       tree_view->priv->rubber_band_end_node,
						       FALSE,
						       TRUE,
						       FALSE);
    }

  tree_view->priv->rubber_band_end_node = end_node;
}

#define GDK_RECTANGLE_PTR(X) ((GdkRectangle *)(X))

static void
pspp_sheet_view_update_rubber_band (PsppSheetView *tree_view)
{
  gint x, y;
  cairo_rectangle_int_t old_area;
  cairo_rectangle_int_t new_area;
  cairo_rectangle_int_t common;
  cairo_region_t *invalid_region;
  PsppSheetViewColumn *column;

  old_area.x = MIN (tree_view->priv->press_start_x, tree_view->priv->rubber_band_x);
  old_area.y = MIN (tree_view->priv->press_start_y, tree_view->priv->rubber_band_y) - tree_view->priv->dy;
  old_area.width = ABS (tree_view->priv->rubber_band_x - tree_view->priv->press_start_x) + 1;
  old_area.height = ABS (tree_view->priv->rubber_band_y - tree_view->priv->press_start_y) + 1;

  gdk_window_get_pointer (tree_view->priv->bin_window, &x, &y, NULL);

  x = MAX (x, 0);
  y = MAX (y, 0) + tree_view->priv->dy;

  new_area.x = MIN (tree_view->priv->press_start_x, x);
  new_area.y = MIN (tree_view->priv->press_start_y, y) - tree_view->priv->dy;
  new_area.width = ABS (x - tree_view->priv->press_start_x) + 1;
  new_area.height = ABS (y - tree_view->priv->press_start_y) + 1;

  invalid_region = cairo_region_create_rectangle (&old_area);
  cairo_region_union_rectangle (invalid_region, &new_area);

  gdk_rectangle_intersect (GDK_RECTANGLE_PTR (&old_area), 
			   GDK_RECTANGLE_PTR (&new_area), GDK_RECTANGLE_PTR (&common));
  if (common.width > 2 && common.height > 2)
    {
      cairo_region_t *common_region;

      /* make sure the border is invalidated */
      common.x += 1;
      common.y += 1;
      common.width -= 2;
      common.height -= 2;

      common_region = cairo_region_create_rectangle (&common);

      cairo_region_subtract (invalid_region, common_region);
      cairo_region_destroy (common_region);
    }

#if GTK_MAJOR_VERSION == 3
  gdk_window_invalidate_region (tree_view->priv->bin_window, invalid_region, TRUE);  
#else
  {
    cairo_rectangle_int_t extents;
    GdkRegion *ereg;
    cairo_region_get_extents (invalid_region, &extents);
    ereg = gdk_region_rectangle (GDK_RECTANGLE_PTR (&extents));
    gdk_window_invalidate_region (tree_view->priv->bin_window, ereg, TRUE);
    gdk_region_destroy (ereg);
  }
#endif

  cairo_region_destroy (invalid_region);

  tree_view->priv->rubber_band_x = x;
  tree_view->priv->rubber_band_y = y;
  pspp_sheet_view_get_path_at_pos (tree_view, x, y, NULL, &column, NULL, NULL);

  pspp_sheet_selection_unselect_all_columns (tree_view->priv->selection);
  pspp_sheet_selection_select_column_range (tree_view->priv->selection,
                                            tree_view->priv->anchor_column,
                                            column);

  gtk_widget_queue_draw (GTK_WIDGET (tree_view));

  pspp_sheet_view_update_rubber_band_selection (tree_view);
}

#if GTK3_TRANSITION
static void
pspp_sheet_view_paint_rubber_band (PsppSheetView  *tree_view,
				GdkRectangle *area)
{
  cairo_t *cr;
  GdkRectangle rect;
  GdkRectangle rubber_rect;
  GtkStyle *style;

  return;
  rubber_rect.x = MIN (tree_view->priv->press_start_x, tree_view->priv->rubber_band_x);
  rubber_rect.y = MIN (tree_view->priv->press_start_y, tree_view->priv->rubber_band_y) - tree_view->priv->dy;
  rubber_rect.width = ABS (tree_view->priv->press_start_x - tree_view->priv->rubber_band_x) + 1;
  rubber_rect.height = ABS (tree_view->priv->press_start_y - tree_view->priv->rubber_band_y) + 1;

  if (!gdk_rectangle_intersect (&rubber_rect, area, &rect))
    return;

  cr = gdk_cairo_create (tree_view->priv->bin_window);
  cairo_set_line_width (cr, 1.0);

  style = gtk_widget_get_style (GTK_WIDGET (tree_view));
  cairo_set_source_rgba (cr,
			 style->fg[GTK_STATE_NORMAL].red / 65535.,
			 style->fg[GTK_STATE_NORMAL].green / 65535.,
			 style->fg[GTK_STATE_NORMAL].blue / 65535.,
			 .25);

  gdk_cairo_rectangle (cr, &rect);
  cairo_clip (cr);
  cairo_paint (cr);

  cairo_set_source_rgb (cr,
			style->fg[GTK_STATE_NORMAL].red / 65535.,
			style->fg[GTK_STATE_NORMAL].green / 65535.,
			style->fg[GTK_STATE_NORMAL].blue / 65535.);

  cairo_rectangle (cr,
		   rubber_rect.x + 0.5, rubber_rect.y + 0.5,
		   rubber_rect.width - 1, rubber_rect.height - 1);
  cairo_stroke (cr);

  cairo_destroy (cr);
}
#endif


static gboolean
pspp_sheet_view_motion_bin_window (GtkWidget      *widget,
				 GdkEventMotion *event)
{
  PsppSheetView *tree_view;
  int node;
  gint new_y;

  tree_view = (PsppSheetView *) widget;

  if (tree_view->priv->row_count == 0)
    return FALSE;

  if (tree_view->priv->rubber_band_status == RUBBER_BAND_MAYBE_START)
    {
      GdkRectangle background_area, cell_area;
      PsppSheetViewColumn *column;

      if (find_click (tree_view, event->x, event->y, &node, &column,
                      &background_area, &cell_area)
          && tree_view->priv->focus_column == column
          && tree_view->priv->press_start_node == node)
        return FALSE;

      gtk_grab_add (GTK_WIDGET (tree_view));
      pspp_sheet_view_update_rubber_band (tree_view);

      tree_view->priv->rubber_band_status = RUBBER_BAND_ACTIVE;
    }
  else if (tree_view->priv->rubber_band_status == RUBBER_BAND_ACTIVE)
    {
      pspp_sheet_view_update_rubber_band (tree_view);

      add_scroll_timeout (tree_view);
    }

  /* only check for an initiated drag when a button is pressed */
  if (tree_view->priv->pressed_button >= 0
      && !tree_view->priv->rubber_band_status)
    pspp_sheet_view_maybe_begin_dragging_row (tree_view, event);

  new_y = TREE_WINDOW_Y_TO_RBTREE_Y(tree_view, event->y);
  if (new_y < 0)
    new_y = 0;

  pspp_sheet_view_find_offset (tree_view, new_y, &node);

  tree_view->priv->event_last_x = event->x;
  tree_view->priv->event_last_y = event->y;

  prelight_or_select (tree_view, node, event->x, event->y);

  return TRUE;
}

static gboolean
pspp_sheet_view_motion (GtkWidget      *widget,
		      GdkEventMotion *event)
{
  PsppSheetView *tree_view;

  tree_view = (PsppSheetView *) widget;

  /* Resizing a column */
  if (PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_IN_COLUMN_RESIZE))
    return pspp_sheet_view_motion_resize_column (widget, event);

  /* Drag column */
  if (PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_IN_COLUMN_DRAG))
    return pspp_sheet_view_motion_drag_column (widget, event);

  /* Sanity check it */
  if (event->window == tree_view->priv->bin_window)
    return pspp_sheet_view_motion_bin_window (widget, event);

  return FALSE;
}

/* Invalidate the focus rectangle near the edge of the bin_window; used when
 * the tree is empty.
 */
static void
invalidate_empty_focus (PsppSheetView *tree_view)
{
  GdkRectangle area;

  if (!gtk_widget_has_focus (GTK_WIDGET (tree_view)))
    return;

  area.x = 0;
  area.y = 0;
  area.width = gdk_window_get_width (tree_view->priv->bin_window);
  area.height = gdk_window_get_height (tree_view->priv->bin_window);
  gdk_window_invalidate_rect (tree_view->priv->bin_window, &area, FALSE);
}

/* Draws a focus rectangle near the edge of the bin_window; used when the tree
 * is empty.
 */
static void
draw_empty_focus (PsppSheetView *tree_view)
{
  GtkWidget *widget = GTK_WIDGET (tree_view);
  gint w, h;
  cairo_t *cr = gdk_cairo_create (tree_view->priv->bin_window);

  if (!gtk_widget_has_focus (widget))
    return;

  w = gdk_window_get_width (tree_view->priv->bin_window);
  h = gdk_window_get_height (tree_view->priv->bin_window);

  w -= 2;
  h -= 2;

  if (w > 0 && h > 0)
    gtk_paint_focus (gtk_widget_get_style (widget),
		     cr,
		     gtk_widget_get_state (widget),
		     widget,
		     NULL,
		     1, 1, w, h);
  cairo_destroy (cr);
}

static void
pspp_sheet_view_draw_vertical_grid_lines (PsppSheetView    *tree_view,
					  cairo_t *cr,
					  gint n_visible_columns,
					  gint min_y,
					  gint max_y)
{
  GList *list = tree_view->priv->columns;
  gint i = 0;
  gint current_x = 0;

  if (tree_view->priv->grid_lines != PSPP_SHEET_VIEW_GRID_LINES_VERTICAL
      && tree_view->priv->grid_lines != PSPP_SHEET_VIEW_GRID_LINES_BOTH)
    return;

  /* Only draw the lines for visible rows and columns */
  for (list = tree_view->priv->columns; list; list = list->next, i++)
    {
      PsppSheetViewColumn *column = list->data;

      /* We don't want a line for the last column */
      if (i == n_visible_columns - 1)
	break;

      if (! column->visible)
	continue;

      current_x += column->width;

      cairo_set_line_width (cr, 1.0);
      cairo_set_line_cap (cr, CAIRO_LINE_CAP_SQUARE);
      cairo_move_to (cr, current_x - 0.5, min_y);
      cairo_line_to (cr, current_x - 0.5 , max_y - min_y);
      
      cairo_stroke (cr);
    }
}

/* Warning: Very scary function.
 * Modify at your own risk
 *
 * KEEP IN SYNC WITH pspp_sheet_view_create_row_drag_icon()!
 * FIXME: It's not...
 */
static gboolean
pspp_sheet_view_bin_expose (GtkWidget      *widget,
			    cairo_t *cr)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  GtkTreePath *path;
  GList *list;
  int node;
  int cursor = -1;
  int drag_highlight = -1;
  GtkTreeIter iter;
  gint new_y;
  gint y_offset, cell_offset;
  gint max_height;
  GdkRectangle background_area;
  GdkRectangle cell_area;
  guint flags;
  gint bin_window_width;
  gint bin_window_height;
  GtkTreePath *cursor_path;
  GtkTreePath *drag_dest_path;
  GList *first_column, *last_column;
  gint vertical_separator;
  gint horizontal_separator;
  gint focus_line_width;
  gboolean allow_rules;
  gboolean has_special_cell;
  gboolean rtl;
  gint n_visible_columns;
  gint grid_line_width;
  gboolean row_ending_details;
  gboolean draw_vgrid_lines, draw_hgrid_lines;
  gint min_y, max_y;

  cairo_t *bwcr = gdk_cairo_create (tree_view->priv->bin_window);
  GdkRectangle Zarea;
  GtkAllocation allocation;
  gtk_widget_get_allocation (widget, &allocation);

  Zarea.x =      0;
  Zarea.y =      0;
  Zarea.height = allocation.height;

  rtl = (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL);

  gtk_widget_style_get (widget,
			"horizontal-separator", &horizontal_separator,
			"vertical-separator", &vertical_separator,
			"allow-rules", &allow_rules,
			"focus-line-width", &focus_line_width,
			"row-ending-details", &row_ending_details,
			NULL);

  if (tree_view->priv->row_count == 0)
    {
      draw_empty_focus (tree_view);
      return TRUE;
    }

#if GTK3_TRANSITION
  /* clip event->area to the visible area */
  if (Zarea.height < 0.5)
    return TRUE;
#endif

  validate_visible_area (tree_view);

  new_y = TREE_WINDOW_Y_TO_RBTREE_Y (tree_view, Zarea.y);

  if (new_y < 0)
    new_y = 0;
  y_offset = -pspp_sheet_view_find_offset (tree_view, new_y, &node);
  bin_window_width = 
    gdk_window_get_width (tree_view->priv->bin_window);

  bin_window_height = 
    gdk_window_get_height (tree_view->priv->bin_window);


  if (tree_view->priv->height < bin_window_height)
    {
      gtk_paint_flat_box (gtk_widget_get_style (widget),
                          cr,
                          gtk_widget_get_state (widget),
                          GTK_SHADOW_NONE,
                          widget,
                          "cell_even",
                          0, tree_view->priv->height,
                          bin_window_width,
                          bin_window_height - tree_view->priv->height);
    }

  if (node < 0)
    return TRUE;

  /* find the path for the node */
  path = _pspp_sheet_view_find_path ((PsppSheetView *)widget, node);
  gtk_tree_model_get_iter (tree_view->priv->model,
			   &iter,
			   path);
  gtk_tree_path_free (path);
  
  cursor_path = NULL;
  drag_dest_path = NULL;

  if (tree_view->priv->cursor)
    cursor_path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);

  if (cursor_path)
    _pspp_sheet_view_find_node (tree_view, cursor_path, &cursor);

  if (tree_view->priv->drag_dest_row)
    drag_dest_path = gtk_tree_row_reference_get_path (tree_view->priv->drag_dest_row);

  if (drag_dest_path)
    _pspp_sheet_view_find_node (tree_view, drag_dest_path,
                                &drag_highlight);

  draw_vgrid_lines =
    tree_view->priv->grid_lines == PSPP_SHEET_VIEW_GRID_LINES_VERTICAL
    || tree_view->priv->grid_lines == PSPP_SHEET_VIEW_GRID_LINES_BOTH;
  draw_hgrid_lines =
    tree_view->priv->grid_lines == PSPP_SHEET_VIEW_GRID_LINES_HORIZONTAL
    || tree_view->priv->grid_lines == PSPP_SHEET_VIEW_GRID_LINES_BOTH;

  if (draw_vgrid_lines || draw_hgrid_lines)
    gtk_widget_style_get (widget, "grid-line-width", &grid_line_width, NULL);
  
  n_visible_columns = 0;
  for (list = tree_view->priv->columns; list; list = list->next)
    {
      if (! PSPP_SHEET_VIEW_COLUMN (list->data)->visible)
	continue;
      n_visible_columns ++;
    }

  /* Find the last column */
  for (last_column = g_list_last (tree_view->priv->columns);
       last_column && !(PSPP_SHEET_VIEW_COLUMN (last_column->data)->visible);
       last_column = last_column->prev)
    ;

  /* and the first */
  for (first_column = g_list_first (tree_view->priv->columns);
       first_column && !(PSPP_SHEET_VIEW_COLUMN (first_column->data)->visible);
       first_column = first_column->next)
    ;

  /* Actually process the expose event.  To do this, we want to
   * start at the first node of the event, and walk the tree in
   * order, drawing each successive node.
   */

  min_y = y_offset;
  do
    {
      gboolean parity;
      gboolean is_first = FALSE;
      gboolean is_last = FALSE;
      gboolean done = FALSE;
      gboolean selected;

      max_height = ROW_HEIGHT (tree_view);

      cell_offset = 0;

      background_area.y = y_offset + Zarea.y;
      background_area.height = max_height;
      max_y = background_area.y + max_height;

      flags = 0;

      if (node == tree_view->priv->prelight_node)
	flags |= GTK_CELL_RENDERER_PRELIT;

      selected = pspp_sheet_view_node_is_selected (tree_view, node);

      parity = node % 2;

      if (tree_view->priv->special_cells == PSPP_SHEET_VIEW_SPECIAL_CELLS_DETECT)
        {
          /* we *need* to set cell data on all cells before the call
           * to _has_special_cell, else _has_special_cell() does not
           * return a correct value.
           */
          for (list = (rtl ? g_list_last (tree_view->priv->columns) : g_list_first (tree_view->priv->columns));
               list;
               list = (rtl ? list->prev : list->next))
            {
              PsppSheetViewColumn *column = list->data;
              pspp_sheet_view_column_cell_set_cell_data (column,
                                                         tree_view->priv->model,
                                                         &iter);
            }

          has_special_cell = pspp_sheet_view_has_special_cell (tree_view);
        }
      else
        has_special_cell = tree_view->priv->special_cells == PSPP_SHEET_VIEW_SPECIAL_CELLS_YES;

      for (list = (rtl ? g_list_last (tree_view->priv->columns) : g_list_first (tree_view->priv->columns));
	   list;
	   list = (rtl ? list->prev : list->next))
	{
	  PsppSheetViewColumn *column = list->data;
	  const gchar *detail = NULL;
          gboolean selected_column;
	  GtkStateType state;

	  if (!column->visible)
            continue;

          if (tree_view->priv->selection->type == PSPP_SHEET_SELECTION_RECTANGLE)
            selected_column = column->selected && column->selectable;
          else
            selected_column = TRUE;

#if GTK3_TRANSITION
	  if (cell_offset > Zarea.x + Zarea.width ||
	      cell_offset + column->width < Zarea.x)
	    {
	      cell_offset += column->width;
	      continue;
	    }
#endif

          if (selected && selected_column)
            flags |= GTK_CELL_RENDERER_SELECTED;
          else
            flags &= ~GTK_CELL_RENDERER_SELECTED;

          if (column->show_sort_indicator)
	    flags |= GTK_CELL_RENDERER_SORTED;
          else
            flags &= ~GTK_CELL_RENDERER_SORTED;

	  if (cursor == node)
            flags |= GTK_CELL_RENDERER_FOCUSED;
          else
            flags &= ~GTK_CELL_RENDERER_FOCUSED;

	  background_area.x = cell_offset;
	  background_area.width = column->width;

          cell_area = background_area;
          cell_area.y += vertical_separator / 2;
          cell_area.x += horizontal_separator / 2;
          cell_area.height -= vertical_separator;
	  cell_area.width -= horizontal_separator;

	  if (draw_vgrid_lines)
	    {
	      if (list == first_column)
	        {
		  cell_area.width -= grid_line_width / 2;
		}
	      else if (list == last_column)
	        {
		  cell_area.x += grid_line_width / 2;
		  cell_area.width -= grid_line_width / 2;
		}
	      else
	        {
	          cell_area.x += grid_line_width / 2;
	          cell_area.width -= grid_line_width;
		}
	    }

	  if (draw_hgrid_lines)
	    {
	      cell_area.y += grid_line_width / 2;
	      cell_area.height -= grid_line_width;
	    }

#if GTK3_TRANSITION
	  if (gdk_region_rect_in (event->region, &background_area) == GDK_OVERLAP_RECTANGLE_OUT)
	    {
	      cell_offset += column->width;
	      continue;
	    }
#endif

	  pspp_sheet_view_column_cell_set_cell_data (column,
                                                     tree_view->priv->model,
                                                     &iter);

          /* Select the detail for drawing the cell.  relevant
           * factors are parity, sortedness, and whether to
           * display rules.
           */
          if (allow_rules && tree_view->priv->has_rules)
            {
              if ((flags & GTK_CELL_RENDERER_SORTED) &&
		  n_visible_columns >= 3)
                {
                  if (parity)
                    detail = "cell_odd_ruled_sorted";
                  else
                    detail = "cell_even_ruled_sorted";
                }
              else
                {
                  if (parity)
                    detail = "cell_odd_ruled";
                  else
                    detail = "cell_even_ruled";
                }
            }
          else
            {
              if ((flags & GTK_CELL_RENDERER_SORTED) &&
		  n_visible_columns >= 3)
                {
                  if (parity)
                    detail = "cell_odd_sorted";
                  else
                    detail = "cell_even_sorted";
                }
              else
                {
                  if (parity)
                    detail = "cell_odd";
                  else
                    detail = "cell_even";
                }
            }

          g_assert (detail);

	  if (gtk_widget_get_state (widget) == GTK_STATE_INSENSITIVE)
	    state = GTK_STATE_INSENSITIVE;	    
	  else if (flags & GTK_CELL_RENDERER_SELECTED)
	    state = GTK_STATE_SELECTED;
	  else
	    state = GTK_STATE_NORMAL;

	  /* Draw background */
	  if (row_ending_details)
	    {
	      char new_detail[128];

	      is_first = (rtl ? !list->next : !list->prev);
	      is_last = (rtl ? !list->prev : !list->next);

	      /* (I don't like the snprintfs either, but couldn't find a
	       * less messy way).
	       */
	      if (is_first && is_last)
		g_snprintf (new_detail, 127, "%s", detail);
	      else if (is_first)
		g_snprintf (new_detail, 127, "%s_start", detail);
	      else if (is_last)
		g_snprintf (new_detail, 127, "%s_end", detail);
	      else
		g_snprintf (new_detail, 128, "%s_middle", detail);

	      gtk_paint_flat_box (gtk_widget_get_style (widget),
				  cr,
				  state,
				  GTK_SHADOW_NONE,
				  widget,
				  new_detail,
				  background_area.x,
				  background_area.y,
				  background_area.width,
				  background_area.height);
	    }
	  else
	    {
	      gtk_paint_flat_box (gtk_widget_get_style (widget),
				  cr,
				  state,
				  GTK_SHADOW_NONE,
				  widget,
				  detail,
				  background_area.x,
				  background_area.y,
				  background_area.width,
				  background_area.height);
	    }

	  if (draw_hgrid_lines)
	    {
	      cairo_set_line_width (cr, 1.0);
	      cairo_set_line_cap (cr, CAIRO_LINE_CAP_SQUARE);

	      if (background_area.y >= 0)
		{
#if GTK3_TRANSITION
		  gdk_draw_line (event->window,
				 tree_view->priv->grid_line_gc[widget->state],
				 background_area.x, background_area.y,
				 background_area.x + background_area.width,
				 background_area.y);
#else
		  cairo_move_to (cr, background_area.x, background_area.y - 0.5);
		  cairo_line_to (cr, background_area.x + background_area.width,
				 background_area.y - 0.5);
#endif
		}

	      if (y_offset + max_height >= Zarea.height - 0.5)
		{
#if GTK3_TRANSITION
		  gdk_draw_line (event->window,
				 tree_view->priv->grid_line_gc[widget->state],
				 background_area.x, background_area.y + max_height,
				 background_area.x + background_area.width,
				 background_area.y + max_height);
#else

		  cairo_move_to (cr, background_area.x, background_area.y + max_height - 0.5);
		  cairo_line_to (cr, background_area.x + background_area.width,
				 background_area.y + max_height - 0.5);
#endif
		}
	      cairo_stroke (cr);
	    }

          _pspp_sheet_view_column_cell_render (column,
                                               cr,
                                               &background_area,
                                               &cell_area,
                                               flags);

          if (node == cursor && has_special_cell &&
	      ((column == tree_view->priv->focus_column &&
		PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_DRAW_KEYFOCUS) &&
		gtk_widget_has_focus (widget)) ||
	       (column == tree_view->priv->edited_column)))
	    {
	      _pspp_sheet_view_column_cell_draw_focus (column,
						       cr,
						     &background_area,
						     &cell_area,
						     flags);
	    }

	  cell_offset += column->width;
	}

      if (cell_offset < Zarea.x)
        {
          gtk_paint_flat_box (gtk_widget_get_style (widget),
                              cr,
                              GTK_STATE_NORMAL,
                              GTK_SHADOW_NONE,
                              widget,
                              "base",
                              cell_offset,
                              background_area.y,
                              Zarea.x - cell_offset,
                              background_area.height);
        }

      if (node == drag_highlight)
        {
          /* Draw indicator for the drop
           */
          gint highlight_y = -1;
	  int node = -1;
	  gint width;

          switch (tree_view->priv->drag_dest_pos)
            {
            case PSPP_SHEET_VIEW_DROP_BEFORE:
              highlight_y = background_area.y - 1;
	      if (highlight_y < 0)
		      highlight_y = 0;
              break;

            case PSPP_SHEET_VIEW_DROP_AFTER:
              highlight_y = background_area.y + background_area.height - 1;
              break;

            case PSPP_SHEET_VIEW_DROP_INTO_OR_BEFORE:
            case PSPP_SHEET_VIEW_DROP_INTO_OR_AFTER:
	      _pspp_sheet_view_find_node (tree_view, drag_dest_path, &node);

	      if (node < 0)
		break;
	      width = gdk_window_get_width (tree_view->priv->bin_window);

	      if (row_ending_details)
		gtk_paint_focus (gtk_widget_get_style (widget),
			         bwcr,
				 gtk_widget_get_state (widget),
				 widget,
				 (is_first
				  ? (is_last ? "treeview-drop-indicator" : "treeview-drop-indicator-left" )
				  : (is_last ? "treeview-drop-indicator-right" : "tree-view-drop-indicator-middle" )),
				 0, BACKGROUND_FIRST_PIXEL (tree_view, node)
				 - focus_line_width / 2,
				 width, ROW_HEIGHT (tree_view)
			       - focus_line_width + 1);
	      else
		gtk_paint_focus (gtk_widget_get_style (widget),
			         bwcr,
				 gtk_widget_get_state (widget),
				 widget,
				 "treeview-drop-indicator",
				 0, BACKGROUND_FIRST_PIXEL (tree_view, node)
				 - focus_line_width / 2,
				 width, ROW_HEIGHT (tree_view)
				 - focus_line_width + 1);
              break;
            }

#if GTK3_TRANSITION
          if (highlight_y >= 0)
            {
              gdk_draw_line (event->window,
                             widget->style->fg_gc[gtk_widget_get_state (widget)],
                             0,
                             highlight_y,
                             rtl ? 0 : bin_window_width,
                             highlight_y);
            }
#endif
        }

      /* draw the big row-spanning focus rectangle, if needed */
      if (!has_special_cell && node == cursor &&
	  PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_DRAW_KEYFOCUS) &&
	  gtk_widget_has_focus (widget))
        {
	  gint tmp_y, tmp_height;
	  gint width;
	  GtkStateType focus_rect_state;

	  focus_rect_state =
	    flags & GTK_CELL_RENDERER_SELECTED ? GTK_STATE_SELECTED :
	    (flags & GTK_CELL_RENDERER_PRELIT ? GTK_STATE_PRELIGHT :
	     (flags & GTK_CELL_RENDERER_INSENSITIVE ? GTK_STATE_INSENSITIVE :
	      GTK_STATE_NORMAL));

	  width = gdk_window_get_width (tree_view->priv->bin_window);
	  
	  if (draw_hgrid_lines)
	    {
	      tmp_y = BACKGROUND_FIRST_PIXEL (tree_view, node) + grid_line_width / 2;
	      tmp_height = ROW_HEIGHT (tree_view) - grid_line_width;
	    }
	  else
	    {
	      tmp_y = BACKGROUND_FIRST_PIXEL (tree_view, node);
	      tmp_height = ROW_HEIGHT (tree_view);
	    }

	  if (row_ending_details)
	    gtk_paint_focus (gtk_widget_get_style (widget),
			     bwcr,
			     focus_rect_state,
			     widget,
			     (is_first
			      ? (is_last ? "treeview" : "treeview-left" )
			      : (is_last ? "treeview-right" : "treeview-middle" )),
			     0, tmp_y,
			     width, tmp_height);
	  else
	    gtk_paint_focus (gtk_widget_get_style (widget),
			     bwcr,
			     focus_rect_state,
			     widget,
			     "treeview",
			     0, tmp_y,
			     width, tmp_height);
	}

      y_offset += max_height;

      do
        {
          node = pspp_sheet_view_node_next (tree_view, node);
          if (node >= 0)
            {
              gboolean has_next = gtk_tree_model_iter_next (tree_view->priv->model, &iter);
              done = TRUE;

              /* Sanity Check! */
              TREE_VIEW_INTERNAL_ASSERT (has_next, FALSE);
            }
          else
            goto done;
        }
      while (!done);
    }
  while (y_offset < Zarea.height);

done:
  pspp_sheet_view_draw_vertical_grid_lines (tree_view, cr, n_visible_columns,
                                   min_y, max_y);

#if GTK3_TRANSITION
 if (tree_view->priv->rubber_band_status == RUBBER_BAND_ACTIVE)
   {
     GdkRectangle *rectangles;
     gint n_rectangles;

     gdk_region_get_rectangles (event->region,
				&rectangles,
				&n_rectangles);

     while (n_rectangles--)
       pspp_sheet_view_paint_rubber_band (tree_view, &rectangles[n_rectangles]);

     g_free (rectangles);
   }
#endif

  if (cursor_path)
    gtk_tree_path_free (cursor_path);

  if (drag_dest_path)
    gtk_tree_path_free (drag_dest_path);

  return FALSE;
}

static gboolean
pspp_sheet_view_expose (GtkWidget      *widget,
                        GdkEventExpose *event)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  cairo_t *cr = gdk_cairo_create (event->window);

  if (event->window == tree_view->priv->bin_window)
    {
      gboolean retval;
      GList *tmp_list;

      retval = pspp_sheet_view_bin_expose (widget, event);

      /* We can't just chain up to Container::expose as it will try to send the
       * event to the headers, so we handle propagating it to our children
       * (eg. widgets being edited) ourselves.
       */
      tmp_list = tree_view->priv->children;
      while (tmp_list)
	{
	  PsppSheetViewChild *child = tmp_list->data;
	  tmp_list = tmp_list->next;

	  gtk_container_propagate_draw (GTK_CONTAINER (tree_view), child->widget, cr);
	}

      return retval;
    }
  else if (event->window == tree_view->priv->header_window)
    {
      gint n_visible_columns;
      GList *list;

      gtk_paint_flat_box (gtk_widget_get_style (widget),
                          cr,
                          GTK_STATE_NORMAL,
                          GTK_SHADOW_NONE,
                          widget,
                          "cell_odd",
                          event->area.x,
                          event->area.y,
                          event->area.width,
                          event->area.height);

      for (list = tree_view->priv->columns; list != NULL; list = list->next)
	{
	  PsppSheetViewColumn *column = list->data;

	  if (column == tree_view->priv->drag_column || !column->visible)
	    continue;

          if (span_intersects (column->allocation.x, column->allocation.width,
                               event->area.x, event->area.width)
              && column->button != NULL)
            gtk_container_propagate_expose (GTK_CONTAINER (tree_view),
                                            column->button, event);
	}

      n_visible_columns = 0;
      for (list = tree_view->priv->columns; list; list = list->next)
        {
          if (! PSPP_SHEET_VIEW_COLUMN (list->data)->visible)
            continue;
          n_visible_columns ++;
        }
      pspp_sheet_view_draw_vertical_grid_lines (tree_view,
						cr,
						n_visible_columns,
						event->area.y,
						event->area.height);
    }
  else if (event->window == tree_view->priv->drag_window)
    {
      gtk_container_propagate_expose (GTK_CONTAINER (tree_view),
				      tree_view->priv->drag_column->button,
				      event);
    }
  return TRUE;
}

enum
{
  DROP_HOME,
  DROP_RIGHT,
  DROP_LEFT,
  DROP_END
};

/* returns 0x1 when no column has been found -- yes it's hackish */
static PsppSheetViewColumn *
pspp_sheet_view_get_drop_column (PsppSheetView       *tree_view,
			       PsppSheetViewColumn *column,
			       gint               drop_position)
{
  PsppSheetViewColumn *left_column = NULL;
  PsppSheetViewColumn *cur_column = NULL;
  GList *tmp_list;

  if (!column->reorderable)
    return (PsppSheetViewColumn *)0x1;

  switch (drop_position)
    {
      case DROP_HOME:
	/* find first column where we can drop */
	tmp_list = tree_view->priv->columns;
	if (column == PSPP_SHEET_VIEW_COLUMN (tmp_list->data))
	  return (PsppSheetViewColumn *)0x1;

	while (tmp_list)
	  {
	    g_assert (tmp_list);

	    cur_column = PSPP_SHEET_VIEW_COLUMN (tmp_list->data);
	    tmp_list = tmp_list->next;

	    if (left_column && left_column->visible == FALSE)
	      continue;

	    if (!tree_view->priv->column_drop_func)
	      return left_column;

	    if (!tree_view->priv->column_drop_func (tree_view, column, left_column, cur_column, tree_view->priv->column_drop_func_data))
	      {
		left_column = cur_column;
		continue;
	      }

	    return left_column;
	  }

	if (!tree_view->priv->column_drop_func)
	  return left_column;

	if (tree_view->priv->column_drop_func (tree_view, column, left_column, NULL, tree_view->priv->column_drop_func_data))
	  return left_column;
	else
	  return (PsppSheetViewColumn *)0x1;
	break;

      case DROP_RIGHT:
	/* find first column after column where we can drop */
	tmp_list = tree_view->priv->columns;

	for (; tmp_list; tmp_list = tmp_list->next)
	  if (PSPP_SHEET_VIEW_COLUMN (tmp_list->data) == column)
	    break;

	if (!tmp_list || !tmp_list->next)
	  return (PsppSheetViewColumn *)0x1;

	tmp_list = tmp_list->next;
	left_column = PSPP_SHEET_VIEW_COLUMN (tmp_list->data);
	tmp_list = tmp_list->next;

	while (tmp_list)
	  {
	    g_assert (tmp_list);

	    cur_column = PSPP_SHEET_VIEW_COLUMN (tmp_list->data);
	    tmp_list = tmp_list->next;

	    if (left_column && left_column->visible == FALSE)
	      {
		left_column = cur_column;
		if (tmp_list)
		  tmp_list = tmp_list->next;
	        continue;
	      }

	    if (!tree_view->priv->column_drop_func)
	      return left_column;

	    if (!tree_view->priv->column_drop_func (tree_view, column, left_column, cur_column, tree_view->priv->column_drop_func_data))
	      {
		left_column = cur_column;
		continue;
	      }

	    return left_column;
	  }

	if (!tree_view->priv->column_drop_func)
	  return left_column;

	if (tree_view->priv->column_drop_func (tree_view, column, left_column, NULL, tree_view->priv->column_drop_func_data))
	  return left_column;
	else
	  return (PsppSheetViewColumn *)0x1;
	break;

      case DROP_LEFT:
	/* find first column before column where we can drop */
	tmp_list = tree_view->priv->columns;

	for (; tmp_list; tmp_list = tmp_list->next)
	  if (PSPP_SHEET_VIEW_COLUMN (tmp_list->data) == column)
	    break;

	if (!tmp_list || !tmp_list->prev)
	  return (PsppSheetViewColumn *)0x1;

	tmp_list = tmp_list->prev;
	cur_column = PSPP_SHEET_VIEW_COLUMN (tmp_list->data);
	tmp_list = tmp_list->prev;

	while (tmp_list)
	  {
	    g_assert (tmp_list);

	    left_column = PSPP_SHEET_VIEW_COLUMN (tmp_list->data);

	    if (left_column && !left_column->visible)
	      {
		/*if (!tmp_list->prev)
		  return (PsppSheetViewColumn *)0x1;
		  */
/*
		cur_column = PSPP_SHEET_VIEW_COLUMN (tmp_list->prev->data);
		tmp_list = tmp_list->prev->prev;
		continue;*/

		cur_column = left_column;
		if (tmp_list)
		  tmp_list = tmp_list->prev;
		continue;
	      }

	    if (!tree_view->priv->column_drop_func)
	      return left_column;

	    if (tree_view->priv->column_drop_func (tree_view, column, left_column, cur_column, tree_view->priv->column_drop_func_data))
	      return left_column;

	    cur_column = left_column;
	    tmp_list = tmp_list->prev;
	  }

	if (!tree_view->priv->column_drop_func)
	  return NULL;

	if (tree_view->priv->column_drop_func (tree_view, column, NULL, cur_column, tree_view->priv->column_drop_func_data))
	  return NULL;
	else
	  return (PsppSheetViewColumn *)0x1;
	break;

      case DROP_END:
	/* same as DROP_HOME case, but doing it backwards */
	tmp_list = g_list_last (tree_view->priv->columns);
	cur_column = NULL;

	if (column == PSPP_SHEET_VIEW_COLUMN (tmp_list->data))
	  return (PsppSheetViewColumn *)0x1;

	while (tmp_list)
	  {
	    g_assert (tmp_list);

	    left_column = PSPP_SHEET_VIEW_COLUMN (tmp_list->data);

	    if (left_column && !left_column->visible)
	      {
		cur_column = left_column;
		tmp_list = tmp_list->prev;
	      }

	    if (!tree_view->priv->column_drop_func)
	      return left_column;

	    if (tree_view->priv->column_drop_func (tree_view, column, left_column, cur_column, tree_view->priv->column_drop_func_data))
	      return left_column;

	    cur_column = left_column;
	    tmp_list = tmp_list->prev;
	  }

	if (!tree_view->priv->column_drop_func)
	  return NULL;

	if (tree_view->priv->column_drop_func (tree_view, column, NULL, cur_column, tree_view->priv->column_drop_func_data))
	  return NULL;
	else
	  return (PsppSheetViewColumn *)0x1;
	break;
    }

  return (PsppSheetViewColumn *)0x1;
}

static gboolean
pspp_sheet_view_key_press (GtkWidget   *widget,
			 GdkEventKey *event)
{
  PsppSheetView *tree_view = (PsppSheetView *) widget;

  if (tree_view->priv->rubber_band_status)
    {
      if (event->keyval == GDK_Escape)
	pspp_sheet_view_stop_rubber_band (tree_view);

      return TRUE;
    }

  if (PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_IN_COLUMN_DRAG))
    {
      if (event->keyval == GDK_Escape)
	{
	  tree_view->priv->cur_reorder = NULL;
	  pspp_sheet_view_button_release_drag_column (widget, NULL);
	}
      return TRUE;
    }

  if (PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_HEADERS_VISIBLE))
    {
      GList *focus_column;
      gboolean rtl;

      rtl = (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL);

      for (focus_column = tree_view->priv->columns;
           focus_column;
           focus_column = focus_column->next)
        {
          PsppSheetViewColumn *column = PSPP_SHEET_VIEW_COLUMN (focus_column->data);

          if (column->button && gtk_widget_has_focus (column->button))
            break;
        }

      if (focus_column &&
          (event->state & GDK_SHIFT_MASK) && (event->state & GDK_MOD1_MASK) &&
          (event->keyval == GDK_Left || event->keyval == GDK_KP_Left
           || event->keyval == GDK_Right || event->keyval == GDK_KP_Right))
        {
          PsppSheetViewColumn *column = PSPP_SHEET_VIEW_COLUMN (focus_column->data);

          if (!column->resizable)
            {
              gtk_widget_error_bell (widget);
              return TRUE;
            }

          if (event->keyval == (rtl ? GDK_Right : GDK_Left)
              || event->keyval == (rtl ? GDK_KP_Right : GDK_KP_Left))
            {
              gint old_width = column->resized_width;

              column->resized_width = MAX (column->resized_width,
                                           column->width);
              column->resized_width -= 2;
              if (column->resized_width < 0)
                column->resized_width = 0;

              if (column->min_width == -1)
                column->resized_width = MAX (column->button_request,
                                             column->resized_width);
              else
                column->resized_width = MAX (column->min_width,
                                             column->resized_width);

              if (column->max_width != -1)
                column->resized_width = MIN (column->resized_width,
                                             column->max_width);

              column->use_resized_width = TRUE;

              if (column->resized_width != old_width)
                gtk_widget_queue_resize (widget);
              else
                gtk_widget_error_bell (widget);
            }
          else if (event->keyval == (rtl ? GDK_Left : GDK_Right)
                   || event->keyval == (rtl ? GDK_KP_Left : GDK_KP_Right))
            {
              gint old_width = column->resized_width;

              column->resized_width = MAX (column->resized_width,
                                           column->width);
              column->resized_width += 2;

              if (column->max_width != -1)
                column->resized_width = MIN (column->resized_width,
                                             column->max_width);

              column->use_resized_width = TRUE;

              if (column->resized_width != old_width)
                gtk_widget_queue_resize (widget);
              else
                gtk_widget_error_bell (widget);
            }

          return TRUE;
        }

      if (focus_column &&
          (event->state & GDK_MOD1_MASK) &&
          (event->keyval == GDK_Left || event->keyval == GDK_KP_Left
           || event->keyval == GDK_Right || event->keyval == GDK_KP_Right
           || event->keyval == GDK_Home || event->keyval == GDK_KP_Home
           || event->keyval == GDK_End || event->keyval == GDK_KP_End))
        {
          PsppSheetViewColumn *column = PSPP_SHEET_VIEW_COLUMN (focus_column->data);

          if (event->keyval == (rtl ? GDK_Right : GDK_Left)
              || event->keyval == (rtl ? GDK_KP_Right : GDK_KP_Left))
            {
              PsppSheetViewColumn *col;
              col = pspp_sheet_view_get_drop_column (tree_view, column, DROP_LEFT);
              if (col != (PsppSheetViewColumn *)0x1)
                pspp_sheet_view_move_column_after (tree_view, column, col);
              else
                gtk_widget_error_bell (widget);
            }
          else if (event->keyval == (rtl ? GDK_Left : GDK_Right)
                   || event->keyval == (rtl ? GDK_KP_Left : GDK_KP_Right))
            {
              PsppSheetViewColumn *col;
              col = pspp_sheet_view_get_drop_column (tree_view, column, DROP_RIGHT);
              if (col != (PsppSheetViewColumn *)0x1)
                pspp_sheet_view_move_column_after (tree_view, column, col);
              else
                gtk_widget_error_bell (widget);
            }
          else if (event->keyval == GDK_Home || event->keyval == GDK_KP_Home)
            {
              PsppSheetViewColumn *col;
              col = pspp_sheet_view_get_drop_column (tree_view, column, DROP_HOME);
              if (col != (PsppSheetViewColumn *)0x1)
                pspp_sheet_view_move_column_after (tree_view, column, col);
              else
                gtk_widget_error_bell (widget);
            }
          else if (event->keyval == GDK_End || event->keyval == GDK_KP_End)
            {
              PsppSheetViewColumn *col;
              col = pspp_sheet_view_get_drop_column (tree_view, column, DROP_END);
              if (col != (PsppSheetViewColumn *)0x1)
                pspp_sheet_view_move_column_after (tree_view, column, col);
              else
                gtk_widget_error_bell (widget);
            }

          return TRUE;
        }
    }

  /* Chain up to the parent class.  It handles the keybindings. */
  if (GTK_WIDGET_CLASS (pspp_sheet_view_parent_class)->key_press_event (widget, event))
    return TRUE;

  if (tree_view->priv->search_entry_avoid_unhandled_binding)
    {
      tree_view->priv->search_entry_avoid_unhandled_binding = FALSE;
      return FALSE;
    }

  /* We pass the event to the search_entry.  If its text changes, then we start
   * the typeahead find capabilities. */
  if (gtk_widget_has_focus (GTK_WIDGET (tree_view))
      && tree_view->priv->enable_search
      && !tree_view->priv->search_custom_entry_set)
    {
      GdkEvent *new_event;
      char *old_text;
      const char *new_text;
      gboolean retval;
      GdkScreen *screen;
      gboolean text_modified;
      gulong popup_menu_id;

      pspp_sheet_view_ensure_interactive_directory (tree_view);

      /* Make a copy of the current text */
      old_text = g_strdup (gtk_entry_get_text (GTK_ENTRY (tree_view->priv->search_entry)));
      new_event = gdk_event_copy ((GdkEvent *) event);
      g_object_unref (((GdkEventKey *) new_event)->window);
      ((GdkEventKey *) new_event)->window = g_object_ref (gtk_widget_get_window (tree_view->priv->search_window));
      gtk_widget_realize (tree_view->priv->search_window);

      popup_menu_id = g_signal_connect (tree_view->priv->search_entry, 
					"popup-menu", G_CALLBACK (gtk_true),
                                        NULL);

      /* Move the entry off screen */
      screen = gtk_widget_get_screen (GTK_WIDGET (tree_view));
      gtk_window_move (GTK_WINDOW (tree_view->priv->search_window),
		       gdk_screen_get_width (screen) + 1,
		       gdk_screen_get_height (screen) + 1);
      gtk_widget_show (tree_view->priv->search_window);

      /* Send the event to the window.  If the preedit_changed signal is emitted
       * during this event, we will set priv->imcontext_changed  */
      tree_view->priv->imcontext_changed = FALSE;
      retval = gtk_widget_event (tree_view->priv->search_window, new_event);
      gdk_event_free (new_event);
      gtk_widget_hide (tree_view->priv->search_window);

      g_signal_handler_disconnect (tree_view->priv->search_entry, 
				   popup_menu_id);

      /* We check to make sure that the entry tried to handle the text, and that
       * the text has changed.
       */
      new_text = gtk_entry_get_text (GTK_ENTRY (tree_view->priv->search_entry));
      text_modified = strcmp (old_text, new_text) != 0;
      g_free (old_text);
      if (tree_view->priv->imcontext_changed ||    /* we're in a preedit */
	  (retval && text_modified))               /* ...or the text was modified */
	{
	  if (pspp_sheet_view_real_start_interactive_search (tree_view, FALSE))
	    {
	      gtk_widget_grab_focus (GTK_WIDGET (tree_view));
	      return TRUE;
	    }
	  else
	    {
	      gtk_entry_set_text (GTK_ENTRY (tree_view->priv->search_entry), "");
	      return FALSE;
	    }
	}
    }

  return FALSE;
}

static gboolean
pspp_sheet_view_key_release (GtkWidget   *widget,
			   GdkEventKey *event)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);

  if (tree_view->priv->rubber_band_status)
    return TRUE;

  return GTK_WIDGET_CLASS (pspp_sheet_view_parent_class)->key_release_event (widget, event);
}

/* FIXME Is this function necessary? Can I get an enter_notify event
 * w/o either an expose event or a mouse motion event?
 */
static gboolean
pspp_sheet_view_enter_notify (GtkWidget        *widget,
			    GdkEventCrossing *event)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  int node;
  gint new_y;

  /* Sanity check it */
  if (event->window != tree_view->priv->bin_window)
    return FALSE;

  if (tree_view->priv->row_count == 0)
    return FALSE;

  if (event->mode == GDK_CROSSING_GRAB ||
      event->mode == GDK_CROSSING_GTK_GRAB ||
      event->mode == GDK_CROSSING_GTK_UNGRAB ||
      event->mode == GDK_CROSSING_STATE_CHANGED)
    return TRUE;

  /* find the node internally */
  new_y = TREE_WINDOW_Y_TO_RBTREE_Y(tree_view, event->y);
  if (new_y < 0)
    new_y = 0;
  pspp_sheet_view_find_offset (tree_view, new_y, &node);

  tree_view->priv->event_last_x = event->x;
  tree_view->priv->event_last_y = event->y;

  prelight_or_select (tree_view, node, event->x, event->y);

  return TRUE;
}

static gboolean
pspp_sheet_view_leave_notify (GtkWidget        *widget,
			    GdkEventCrossing *event)
{
  PsppSheetView *tree_view;

  if (event->mode == GDK_CROSSING_GRAB)
    return TRUE;

  tree_view = PSPP_SHEET_VIEW (widget);

  if (tree_view->priv->prelight_node >= 0)
    _pspp_sheet_view_queue_draw_node (tree_view,
                                   tree_view->priv->prelight_node,
                                   NULL);

  tree_view->priv->event_last_x = -10000;
  tree_view->priv->event_last_y = -10000;

  prelight_or_select (tree_view,
		      -1,
		      -1000, -1000); /* coords not possibly over an arrow */

  return TRUE;
}


static gint
pspp_sheet_view_focus_out (GtkWidget     *widget,
			 GdkEventFocus *event)
{
  PsppSheetView *tree_view;

  tree_view = PSPP_SHEET_VIEW (widget);

  gtk_widget_queue_draw (widget);

  /* destroy interactive search dialog */
  if (tree_view->priv->search_window)
    pspp_sheet_view_search_dialog_hide (tree_view->priv->search_window, tree_view);

  return FALSE;
}


/* Incremental Reflow
 */

static void
pspp_sheet_view_node_queue_redraw (PsppSheetView *tree_view,
				 int node)
{
  GtkAllocation allocation;
  gint y = pspp_sheet_view_node_find_offset (tree_view, node)
    - gtk_adjustment_get_value (tree_view->priv->vadjustment)
    + TREE_VIEW_HEADER_HEIGHT (tree_view);

  gtk_widget_get_allocation (GTK_WIDGET (tree_view), &allocation);

  gtk_widget_queue_draw_area (GTK_WIDGET (tree_view),
			      0, y,
			      allocation.width,
                              tree_view->priv->fixed_height);
}

static gboolean
node_is_visible (PsppSheetView *tree_view,
		 int node)
{
  int y;
  int height;

  y = pspp_sheet_view_node_find_offset (tree_view, node);
  height = ROW_HEIGHT (tree_view);

  if (y >= gtk_adjustment_get_value (tree_view->priv->vadjustment) &&
      y + height <= (gtk_adjustment_get_value (tree_view->priv->vadjustment)
	             + gtk_adjustment_get_page_size (tree_view->priv->vadjustment)))
    return TRUE;

  return FALSE;
}

/* Returns the row height. */
static gint
validate_row (PsppSheetView *tree_view,
	      int node,
	      GtkTreeIter *iter,
	      GtkTreePath *path)
{
  PsppSheetViewColumn *column;
  GList *list, *first_column, *last_column;
  gint height = 0;
  gint horizontal_separator;
  gint vertical_separator;
  gint focus_line_width;
  gboolean draw_vgrid_lines, draw_hgrid_lines;
  gint focus_pad;
  gint grid_line_width;
  gboolean wide_separators;
  gint separator_height;

  gtk_widget_style_get (GTK_WIDGET (tree_view),
			"focus-padding", &focus_pad,
			"focus-line-width", &focus_line_width,
			"horizontal-separator", &horizontal_separator,
			"vertical-separator", &vertical_separator,
			"grid-line-width", &grid_line_width,
                        "wide-separators",  &wide_separators,
                        "separator-height", &separator_height,
			NULL);
  
  draw_vgrid_lines =
    tree_view->priv->grid_lines == PSPP_SHEET_VIEW_GRID_LINES_VERTICAL
    || tree_view->priv->grid_lines == PSPP_SHEET_VIEW_GRID_LINES_BOTH;
  draw_hgrid_lines =
    tree_view->priv->grid_lines == PSPP_SHEET_VIEW_GRID_LINES_HORIZONTAL
    || tree_view->priv->grid_lines == PSPP_SHEET_VIEW_GRID_LINES_BOTH;

  for (last_column = g_list_last (tree_view->priv->columns);
       last_column && !(PSPP_SHEET_VIEW_COLUMN (last_column->data)->visible);
       last_column = last_column->prev)
    ;

  for (first_column = g_list_first (tree_view->priv->columns);
       first_column && !(PSPP_SHEET_VIEW_COLUMN (first_column->data)->visible);
       first_column = first_column->next)
    ;

  for (list = tree_view->priv->columns; list; list = list->next)
    {
      gint tmp_width;
      gint tmp_height;

      column = list->data;

      if (! column->visible)
	continue;

      pspp_sheet_view_column_cell_set_cell_data (column, tree_view->priv->model, iter);
      pspp_sheet_view_column_cell_get_size (column,
					  NULL, NULL, NULL,
					  &tmp_width, &tmp_height);

      tmp_height += vertical_separator;
      height = MAX (height, tmp_height);

      tmp_width = tmp_width + horizontal_separator;

      if (draw_vgrid_lines)
        {
	  if (list->data == first_column || list->data == last_column)
	    tmp_width += grid_line_width / 2.0;
	  else
	    tmp_width += grid_line_width;
	}

      if (tmp_width > column->requested_width)
        column->requested_width = tmp_width;
    }

  if (draw_hgrid_lines)
    height += grid_line_width;

  tree_view->priv->post_validation_flag = TRUE;
  return height;
}


static void
validate_visible_area (PsppSheetView *tree_view)
{
  GtkTreePath *path = NULL;
  GtkTreePath *above_path = NULL;
  GtkTreeIter iter;
  int node = -1;
  gboolean size_changed = FALSE;
  gint total_height;
  gint area_above = 0;
  gint area_below = 0;
  GtkAllocation allocation;

  if (tree_view->priv->row_count == 0)
    return;

  if (tree_view->priv->scroll_to_path == NULL)
    return;

  gtk_widget_get_allocation (GTK_WIDGET (tree_view), &allocation);

  total_height = allocation.height - TREE_VIEW_HEADER_HEIGHT (tree_view);

  if (total_height == 0)
    return;

  path = gtk_tree_row_reference_get_path (tree_view->priv->scroll_to_path);
  if (path)
    {
      /* we are going to scroll, and will update dy */
      _pspp_sheet_view_find_node (tree_view, path, &node);
      gtk_tree_model_get_iter (tree_view->priv->model, &iter, path);

      if (tree_view->priv->scroll_to_use_align)
        {
          gint height = ROW_HEIGHT (tree_view);
          area_above = (total_height - height) *
            tree_view->priv->scroll_to_row_align;
          area_below = total_height - area_above - height;
          area_above = MAX (area_above, 0);
          area_below = MAX (area_below, 0);
        }
      else
        {
          /* two cases:
           * 1) row not visible
           * 2) row visible
           */
          gint dy;
          gint height = ROW_HEIGHT (tree_view);

          dy = pspp_sheet_view_node_find_offset (tree_view, node);

          if (dy >= gtk_adjustment_get_value (tree_view->priv->vadjustment) &&
              dy + height <= (gtk_adjustment_get_value (tree_view->priv->vadjustment)
                              + gtk_adjustment_get_page_size (tree_view->priv->vadjustment)))
            {
              /* row visible: keep the row at the same position */
              area_above = dy - gtk_adjustment_get_value (tree_view->priv->vadjustment);
              area_below = (gtk_adjustment_get_value (tree_view->priv->vadjustment) +
                            gtk_adjustment_get_page_size (tree_view->priv->vadjustment))
                - dy - height;
            }
          else
            {
              /* row not visible */
              if (dy >= 0
                  && dy + height <= gtk_adjustment_get_page_size (tree_view->priv->vadjustment))
                {
                  /* row at the beginning -- fixed */
                  area_above = dy;
                  area_below = gtk_adjustment_get_page_size (tree_view->priv->vadjustment)
                    - area_above - height;
                }
              else if (dy >= (gtk_adjustment_get_upper (tree_view->priv->vadjustment) -
                              gtk_adjustment_get_page_size (tree_view->priv->vadjustment)))
                {
                  /* row at the end -- fixed */
                  area_above = dy - (gtk_adjustment_get_upper (tree_view->priv->vadjustment) -
                                     gtk_adjustment_get_page_size (tree_view->priv->vadjustment));
                  area_below = gtk_adjustment_get_page_size (tree_view->priv->vadjustment) -
                    area_above - height;

                  if (area_below < 0)
                    {
                      area_above = gtk_adjustment_get_page_size (tree_view->priv->vadjustment) - height;
                      area_below = 0;
                    }
                }
              else
                {
                  /* row somewhere in the middle, bring it to the top
                   * of the view
                   */
                  area_above = 0;
                  area_below = total_height - height;
                }
            }
        }
    }
  else
    /* the scroll to isn't valid; ignore it.
     */
    {
      gtk_tree_row_reference_free (tree_view->priv->scroll_to_path);
      tree_view->priv->scroll_to_path = NULL;
      return;
    }

  above_path = gtk_tree_path_copy (path);

  /* Now, we walk forwards and backwards, measuring rows. Unfortunately,
   * backwards is much slower then forward, as there is no iter_prev function.
   * We go forwards first in case we run out of tree.  Then we go backwards to
   * fill out the top.
   */
  while (node >= 0 && area_below > 0)
    {
      gboolean done = FALSE;
      do
        {
          node = pspp_sheet_view_node_next (tree_view, node);
          if (node >= 0)
            {
              gboolean has_next = gtk_tree_model_iter_next (tree_view->priv->model, &iter);
              done = TRUE;
              gtk_tree_path_next (path);

              /* Sanity Check! */
              TREE_VIEW_INTERNAL_ASSERT_VOID (has_next);
            }
          else
            break;
        }
      while (!done);

      if (node < 0)
        break;

      area_below -= ROW_HEIGHT (tree_view);
    }
  gtk_tree_path_free (path);

  /* If we ran out of tree, and have extra area_below left, we need to add it
   * to area_above */
  if (area_below > 0)
    area_above += area_below;

  _pspp_sheet_view_find_node (tree_view, above_path, &node);

  /* We walk backwards */
  while (area_above > 0)
    {
      node = pspp_sheet_view_node_prev (tree_view, node);

      /* Always find the new path in the tree.  We cannot just assume
       * a gtk_tree_path_prev() is enough here, as there might be children
       * in between this node and the previous sibling node.  If this
       * appears to be a performance hotspot in profiles, we can look into
       * intrigate logic for keeping path, node and iter in sync like
       * we do for forward walks.  (Which will be hard because of the lacking
       * iter_prev).
       */

      if (node < 0)
	break;

      gtk_tree_path_free (above_path);
      above_path = _pspp_sheet_view_find_path (tree_view, node);

      gtk_tree_model_get_iter (tree_view->priv->model, &iter, above_path);

      area_above -= ROW_HEIGHT (tree_view);
    }

  /* set the dy here to scroll to the path,
   * and sync the top row accordingly
   */
  pspp_sheet_view_set_top_row (tree_view, above_path, -area_above);
  pspp_sheet_view_top_row_to_dy (tree_view);

  /* update width/height and queue a resize */
  if (size_changed)
    {
      GtkRequisition requisition;

      /* We temporarily guess a size, under the assumption that it will be the
       * same when we get our next size_allocate.  If we don't do this, we'll be
       * in an inconsistent state if we call top_row_to_dy. */

      gtk_widget_size_request (GTK_WIDGET (tree_view), &requisition);
      gtk_adjustment_set_upper (tree_view->priv->hadjustment, MAX (gtk_adjustment_get_upper (tree_view->priv->hadjustment), (gfloat)requisition.width));
      gtk_adjustment_set_upper (tree_view->priv->vadjustment, MAX (gtk_adjustment_get_upper (tree_view->priv->vadjustment), (gfloat)requisition.height));
      gtk_adjustment_changed (tree_view->priv->hadjustment);
      gtk_adjustment_changed (tree_view->priv->vadjustment);
      gtk_widget_queue_resize (GTK_WIDGET (tree_view));
    }

  gtk_tree_row_reference_free (tree_view->priv->scroll_to_path);
  tree_view->priv->scroll_to_path = NULL;

  if (above_path)
    gtk_tree_path_free (above_path);

  if (tree_view->priv->scroll_to_column)
    {
      tree_view->priv->scroll_to_column = NULL;
    }
  gtk_widget_queue_draw (GTK_WIDGET (tree_view));
}

static void
initialize_fixed_height_mode (PsppSheetView *tree_view)
{
  if (!tree_view->priv->row_count)
    return;

  if (tree_view->priv->fixed_height_set)
    return;

  if (tree_view->priv->fixed_height < 0)
    {
      GtkTreeIter iter;
      GtkTreePath *path;

      int node = 0;

      path = _pspp_sheet_view_find_path (tree_view, node);
      gtk_tree_model_get_iter (tree_view->priv->model, &iter, path);

      tree_view->priv->fixed_height = validate_row (tree_view, node, &iter, path);

      gtk_tree_path_free (path);

      g_object_notify (G_OBJECT (tree_view), "fixed-height");
    }
}

/* Our strategy for finding nodes to validate is a little convoluted.  We find
 * the left-most uninvalidated node.  We then try walking right, validating
 * nodes.  Once we find a valid node, we repeat the previous process of finding
 * the first invalid node.
 */

static gboolean
validate_rows_handler (PsppSheetView *tree_view)
{
  initialize_fixed_height_mode (tree_view);
  if (tree_view->priv->validate_rows_timer)
    {
      g_source_remove (tree_view->priv->validate_rows_timer);
      tree_view->priv->validate_rows_timer = 0;
    }

  return FALSE;
}

static gboolean
do_presize_handler (PsppSheetView *tree_view)
{
  GtkRequisition requisition;

  validate_visible_area (tree_view);
  tree_view->priv->presize_handler_timer = 0;

  if (! gtk_widget_get_realized (GTK_WIDGET (tree_view)))
    return FALSE;

  gtk_widget_size_request (GTK_WIDGET (tree_view), &requisition);

  gtk_adjustment_set_upper (tree_view->priv->hadjustment, MAX (gtk_adjustment_get_upper (tree_view->priv->hadjustment), (gfloat)requisition.width));
  gtk_adjustment_set_upper (tree_view->priv->vadjustment, MAX (gtk_adjustment_get_upper (tree_view->priv->vadjustment), (gfloat)requisition.height));
  gtk_adjustment_changed (tree_view->priv->hadjustment);
  gtk_adjustment_changed (tree_view->priv->vadjustment);
  gtk_widget_queue_resize (GTK_WIDGET (tree_view));
		   
  return FALSE;
}

static gboolean
presize_handler_callback (gpointer data)
{
  do_presize_handler (PSPP_SHEET_VIEW (data));
		   
  return FALSE;
}

static void
install_presize_handler (PsppSheetView *tree_view)
{
  if (! gtk_widget_get_realized (GTK_WIDGET (tree_view)))
    return;

  if (! tree_view->priv->presize_handler_timer)
    {
      tree_view->priv->presize_handler_timer =
	gdk_threads_add_idle_full (GTK_PRIORITY_RESIZE - 2, presize_handler_callback, tree_view, NULL);
    }
  if (! tree_view->priv->validate_rows_timer)
    {
      tree_view->priv->validate_rows_timer =
	gdk_threads_add_idle_full (PSPP_SHEET_VIEW_PRIORITY_VALIDATE, (GSourceFunc) validate_rows_handler, tree_view, NULL);
    }
}

static gboolean
scroll_sync_handler (PsppSheetView *tree_view)
{
  if (tree_view->priv->height <= gtk_adjustment_get_page_size (tree_view->priv->vadjustment))
    gtk_adjustment_set_value (GTK_ADJUSTMENT (tree_view->priv->vadjustment), 0);
  else if (gtk_tree_row_reference_valid (tree_view->priv->top_row))
    pspp_sheet_view_top_row_to_dy (tree_view);
  else
    pspp_sheet_view_dy_to_top_row (tree_view);

  tree_view->priv->scroll_sync_timer = 0;

  return FALSE;
}

static void
install_scroll_sync_handler (PsppSheetView *tree_view)
{
  if (!gtk_widget_get_realized (GTK_WIDGET (tree_view)))
    return;

  if (!tree_view->priv->scroll_sync_timer)
    {
      tree_view->priv->scroll_sync_timer =
	gdk_threads_add_idle_full (PSPP_SHEET_VIEW_PRIORITY_SCROLL_SYNC, (GSourceFunc) scroll_sync_handler, tree_view, NULL);
    }
}

static void
pspp_sheet_view_set_top_row (PsppSheetView *tree_view,
			   GtkTreePath *path,
			   gint         offset)
{
  gtk_tree_row_reference_free (tree_view->priv->top_row);

  if (!path)
    {
      tree_view->priv->top_row = NULL;
      tree_view->priv->top_row_dy = 0;
    }
  else
    {
      tree_view->priv->top_row = gtk_tree_row_reference_new_proxy (G_OBJECT (tree_view), tree_view->priv->model, path);
      tree_view->priv->top_row_dy = offset;
    }
}

/* Always call this iff dy is in the visible range.  If the tree is empty, then
 * it's set to be NULL, and top_row_dy is 0;
 */
static void
pspp_sheet_view_dy_to_top_row (PsppSheetView *tree_view)
{
  gint offset;
  GtkTreePath *path;
  int node;

  if (tree_view->priv->row_count == 0)
    {
      pspp_sheet_view_set_top_row (tree_view, NULL, 0);
    }
  else
    {
      offset = pspp_sheet_view_find_offset (tree_view,
                                            tree_view->priv->dy,
                                            &node);

      if (node < 0)
        {
	  pspp_sheet_view_set_top_row (tree_view, NULL, 0);
	}
      else
        {
	  path = _pspp_sheet_view_find_path (tree_view, node);
	  pspp_sheet_view_set_top_row (tree_view, path, offset);
	  gtk_tree_path_free (path);
	}
    }
}

static void
pspp_sheet_view_top_row_to_dy (PsppSheetView *tree_view)
{
  GtkTreePath *path;
  int node;
  int new_dy;

  /* Avoid recursive calls */
  if (tree_view->priv->in_top_row_to_dy)
    return;

  if (tree_view->priv->top_row)
    path = gtk_tree_row_reference_get_path (tree_view->priv->top_row);
  else
    path = NULL;

  if (!path)
    node = -1;
  else
    _pspp_sheet_view_find_node (tree_view, path, &node);

  if (path)
    gtk_tree_path_free (path);

  if (node < 0)
    {
      /* keep dy and set new toprow */
      gtk_tree_row_reference_free (tree_view->priv->top_row);
      tree_view->priv->top_row = NULL;
      tree_view->priv->top_row_dy = 0;
      /* DO NOT install the idle handler */
      pspp_sheet_view_dy_to_top_row (tree_view);
      return;
    }

  if (ROW_HEIGHT (tree_view) < tree_view->priv->top_row_dy)
    {
      /* new top row -- do NOT install the idle handler */
      pspp_sheet_view_dy_to_top_row (tree_view);
      return;
    }

  new_dy = pspp_sheet_view_node_find_offset (tree_view, node);
  new_dy += tree_view->priv->top_row_dy;

  if (new_dy + gtk_adjustment_get_page_size (tree_view->priv->vadjustment) > tree_view->priv->height)
    new_dy = tree_view->priv->height - gtk_adjustment_get_page_size (tree_view->priv->vadjustment);

  new_dy = MAX (0, new_dy);

  tree_view->priv->in_top_row_to_dy = TRUE;
  gtk_adjustment_set_value (tree_view->priv->vadjustment, (gdouble)new_dy);
  tree_view->priv->in_top_row_to_dy = FALSE;
}


void
_pspp_sheet_view_install_mark_rows_col_dirty (PsppSheetView *tree_view)
{
  install_presize_handler (tree_view);
}

/* Drag-and-drop */

static void
set_source_row (GdkDragContext *context,
                GtkTreeModel   *model,
                GtkTreePath    *source_row)
{
  g_object_set_data_full (G_OBJECT (context),
                          "gtk-tree-view-source-row",
                          source_row ? gtk_tree_row_reference_new (model, source_row) : NULL,
                          (GDestroyNotify) (source_row ? gtk_tree_row_reference_free : NULL));
}

static GtkTreePath*
get_source_row (GdkDragContext *context)
{
  GtkTreeRowReference *ref =
    g_object_get_data (G_OBJECT (context), "gtk-tree-view-source-row");

  if (ref)
    return gtk_tree_row_reference_get_path (ref);
  else
    return NULL;
}

typedef struct
{
  GtkTreeRowReference *dest_row;
  guint                path_down_mode   : 1;
  guint                empty_view_drop  : 1;
  guint                drop_append_mode : 1;
}
DestRow;

static void
dest_row_free (gpointer data)
{
  DestRow *dr = (DestRow *)data;

  gtk_tree_row_reference_free (dr->dest_row);
  g_slice_free (DestRow, dr);
}

static void
set_dest_row (GdkDragContext *context,
              GtkTreeModel   *model,
              GtkTreePath    *dest_row,
              gboolean        path_down_mode,
              gboolean        empty_view_drop,
              gboolean        drop_append_mode)
{
  DestRow *dr;

  if (!dest_row)
    {
      g_object_set_data_full (G_OBJECT (context), "gtk-tree-view-dest-row",
                              NULL, NULL);
      return;
    }

  dr = g_slice_new (DestRow);

  dr->dest_row = gtk_tree_row_reference_new (model, dest_row);
  dr->path_down_mode = path_down_mode != FALSE;
  dr->empty_view_drop = empty_view_drop != FALSE;
  dr->drop_append_mode = drop_append_mode != FALSE;

  g_object_set_data_full (G_OBJECT (context), "gtk-tree-view-dest-row",
                          dr, (GDestroyNotify) dest_row_free);
}

static GtkTreePath*
get_dest_row (GdkDragContext *context,
              gboolean       *path_down_mode)
{
  DestRow *dr =
    g_object_get_data (G_OBJECT (context), "gtk-tree-view-dest-row");

  if (dr)
    {
      GtkTreePath *path = NULL;

      if (path_down_mode)
        *path_down_mode = dr->path_down_mode;

      if (dr->dest_row)
        path = gtk_tree_row_reference_get_path (dr->dest_row);
      else if (dr->empty_view_drop)
        path = gtk_tree_path_new_from_indices (0, -1);
      else
        path = NULL;

      if (path && dr->drop_append_mode)
        gtk_tree_path_next (path);

      return path;
    }
  else
    return NULL;
}

/* Get/set whether drag_motion requested the drag data and
 * drag_data_received should thus not actually insert the data,
 * since the data doesn't result from a drop.
 */
static void
set_status_pending (GdkDragContext *context,
                    GdkDragAction   suggested_action)
{
  g_object_set_data (G_OBJECT (context),
                     "gtk-tree-view-status-pending",
                     GINT_TO_POINTER (suggested_action));
}

static GdkDragAction
get_status_pending (GdkDragContext *context)
{
  return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (context),
                                             "gtk-tree-view-status-pending"));
}

static TreeViewDragInfo*
get_info (PsppSheetView *tree_view)
{
  return g_object_get_data (G_OBJECT (tree_view), "gtk-tree-view-drag-info");
}

static void
destroy_info (TreeViewDragInfo *di)
{
  g_slice_free (TreeViewDragInfo, di);
}

static TreeViewDragInfo*
ensure_info (PsppSheetView *tree_view)
{
  TreeViewDragInfo *di;

  di = get_info (tree_view);

  if (di == NULL)
    {
      di = g_slice_new0 (TreeViewDragInfo);

      g_object_set_data_full (G_OBJECT (tree_view),
                              "gtk-tree-view-drag-info",
                              di,
                              (GDestroyNotify) destroy_info);
    }

  return di;
}

static void
remove_info (PsppSheetView *tree_view)
{
  g_object_set_data (G_OBJECT (tree_view), "gtk-tree-view-drag-info", NULL);
}

#if 0
static gint
drag_scan_timeout (gpointer data)
{
  PsppSheetView *tree_view;
  gint x, y;
  GdkModifierType state;
  GtkTreePath *path = NULL;
  PsppSheetViewColumn *column = NULL;
  GdkRectangle visible_rect;

  GDK_THREADS_ENTER ();

  tree_view = PSPP_SHEET_VIEW (data);

  gdk_window_get_pointer (tree_view->priv->bin_window,
                          &x, &y, &state);

  pspp_sheet_view_get_visible_rect (tree_view, &visible_rect);

  /* See if we are near the edge. */
  if ((x - visible_rect.x) < SCROLL_EDGE_SIZE ||
      (visible_rect.x + visible_rect.width - x) < SCROLL_EDGE_SIZE ||
      (y - visible_rect.y) < SCROLL_EDGE_SIZE ||
      (visible_rect.y + visible_rect.height - y) < SCROLL_EDGE_SIZE)
    {
      pspp_sheet_view_get_path_at_pos (tree_view,
                                     tree_view->priv->bin_window,
                                     x, y,
                                     &path,
                                     &column,
                                     NULL,
                                     NULL);

      if (path != NULL)
        {
          pspp_sheet_view_scroll_to_cell (tree_view,
                                        path,
                                        column,
					TRUE,
                                        0.5, 0.5);

          gtk_tree_path_free (path);
        }
    }

  GDK_THREADS_LEAVE ();

  return TRUE;
}
#endif /* 0 */

static void
add_scroll_timeout (PsppSheetView *tree_view)
{
  if (tree_view->priv->scroll_timeout == 0)
    {
      tree_view->priv->scroll_timeout =
	gdk_threads_add_timeout (150, scroll_row_timeout, tree_view);
    }
}

static void
remove_scroll_timeout (PsppSheetView *tree_view)
{
  if (tree_view->priv->scroll_timeout != 0)
    {
      g_source_remove (tree_view->priv->scroll_timeout);
      tree_view->priv->scroll_timeout = 0;
    }
}

static gboolean
check_model_dnd (GtkTreeModel *model,
                 GType         required_iface,
                 const gchar  *signal)
{
  if (model == NULL || !G_TYPE_CHECK_INSTANCE_TYPE ((model), required_iface))
    {
      g_warning ("You must override the default '%s' handler "
                 "on PsppSheetView when using models that don't support "
                 "the %s interface and enabling drag-and-drop. The simplest way to do this "
                 "is to connect to '%s' and call "
                 "g_signal_stop_emission_by_name() in your signal handler to prevent "
                 "the default handler from running. Look at the source code "
                 "for the default handler in gtktreeview.c to get an idea what "
                 "your handler should do. (gtktreeview.c is in the GTK source "
                 "code.) If you're using GTK from a language other than C, "
                 "there may be a more natural way to override default handlers, e.g. via derivation.",
                 signal, g_type_name (required_iface), signal);
      return FALSE;
    }
  else
    return TRUE;
}

static gboolean
scroll_row_timeout (gpointer data)
{
  PsppSheetView *tree_view = data;

  pspp_sheet_view_horizontal_autoscroll (tree_view);
  pspp_sheet_view_vertical_autoscroll (tree_view);

  if (tree_view->priv->rubber_band_status == RUBBER_BAND_ACTIVE)
    pspp_sheet_view_update_rubber_band (tree_view);

  return TRUE;
}

/* Returns TRUE if event should not be propagated to parent widgets */
static gboolean
set_destination_row (PsppSheetView    *tree_view,
                     GdkDragContext *context,
                     /* coordinates relative to the widget */
                     gint            x,
                     gint            y,
                     GdkDragAction  *suggested_action,
                     GdkAtom        *target)
{
  GtkTreePath *path = NULL;
  PsppSheetViewDropPosition pos;
  PsppSheetViewDropPosition old_pos;
  TreeViewDragInfo *di;
  GtkWidget *widget;
  GtkTreePath *old_dest_path = NULL;
  gboolean can_drop = FALSE;

  *suggested_action = 0;
  *target = GDK_NONE;

  widget = GTK_WIDGET (tree_view);

  di = get_info (tree_view);

  if (di == NULL || y - TREE_VIEW_HEADER_HEIGHT (tree_view) < 0)
    {
      /* someone unset us as a drag dest, note that if
       * we return FALSE drag_leave isn't called
       */

      pspp_sheet_view_set_drag_dest_row (tree_view,
                                       NULL,
                                       PSPP_SHEET_VIEW_DROP_BEFORE);

      remove_scroll_timeout (PSPP_SHEET_VIEW (widget));

      return FALSE; /* no longer a drop site */
    }

  *target = gtk_drag_dest_find_target (widget, context,
                                       gtk_drag_dest_get_target_list (widget));
  if (*target == GDK_NONE)
    {
      return FALSE;
    }

  if (!pspp_sheet_view_get_dest_row_at_pos (tree_view,
                                          x, y,
                                          &path,
                                          &pos))
    {
      gint n_children;
      GtkTreeModel *model;

      /* the row got dropped on empty space, let's setup a special case
       */

      if (path)
	gtk_tree_path_free (path);

      model = pspp_sheet_view_get_model (tree_view);

      n_children = gtk_tree_model_iter_n_children (model, NULL);
      if (n_children)
        {
          pos = PSPP_SHEET_VIEW_DROP_AFTER;
          path = gtk_tree_path_new_from_indices (n_children - 1, -1);
        }
      else
        {
          pos = PSPP_SHEET_VIEW_DROP_BEFORE;
          path = gtk_tree_path_new_from_indices (0, -1);
        }

      can_drop = TRUE;

      goto out;
    }

  g_assert (path);

  /* If we left the current row's "open" zone, unset the timeout for
   * opening the row
   */
  pspp_sheet_view_get_drag_dest_row (tree_view,
                                   &old_dest_path,
                                   &old_pos);

  if (old_dest_path)
    gtk_tree_path_free (old_dest_path);

  if (TRUE /* FIXME if the location droppable predicate */)
    {
      can_drop = TRUE;
    }

out:
  if (can_drop)
    {
      GtkWidget *source_widget;

      *suggested_action = gdk_drag_context_get_suggested_action (context);
      source_widget = gtk_drag_get_source_widget (context);

      if (source_widget == widget)
        {
          /* Default to MOVE, unless the user has
           * pressed ctrl or shift to affect available actions
           */
          if ((gdk_drag_context_get_actions (context) & GDK_ACTION_MOVE) != 0)
            *suggested_action = GDK_ACTION_MOVE;
        }

      pspp_sheet_view_set_drag_dest_row (PSPP_SHEET_VIEW (widget),
                                       path, pos);
    }
  else
    {
      /* can't drop here */
      pspp_sheet_view_set_drag_dest_row (PSPP_SHEET_VIEW (widget),
                                       NULL,
                                       PSPP_SHEET_VIEW_DROP_BEFORE);
    }

  if (path)
    gtk_tree_path_free (path);

  return TRUE;
}

static GtkTreePath*
get_logical_dest_row (PsppSheetView *tree_view,
                      gboolean    *path_down_mode,
                      gboolean    *drop_append_mode)
{
  /* adjust path to point to the row the drop goes in front of */
  GtkTreePath *path = NULL;
  PsppSheetViewDropPosition pos;

  g_return_val_if_fail (path_down_mode != NULL, NULL);
  g_return_val_if_fail (drop_append_mode != NULL, NULL);

  *path_down_mode = FALSE;
  *drop_append_mode = 0;

  pspp_sheet_view_get_drag_dest_row (tree_view, &path, &pos);

  if (path == NULL)
    return NULL;

  if (pos == PSPP_SHEET_VIEW_DROP_BEFORE)
    ; /* do nothing */
  else if (pos == PSPP_SHEET_VIEW_DROP_INTO_OR_BEFORE ||
           pos == PSPP_SHEET_VIEW_DROP_INTO_OR_AFTER)
    *path_down_mode = TRUE;
  else
    {
      GtkTreeIter iter;
      GtkTreeModel *model = pspp_sheet_view_get_model (tree_view);

      g_assert (pos == PSPP_SHEET_VIEW_DROP_AFTER);

      if (!gtk_tree_model_get_iter (model, &iter, path) ||
          !gtk_tree_model_iter_next (model, &iter))
        *drop_append_mode = 1;
      else
        {
          *drop_append_mode = 0;
          gtk_tree_path_next (path);
        }
    }

  return path;
}

static gboolean
pspp_sheet_view_maybe_begin_dragging_row (PsppSheetView      *tree_view,
                                        GdkEventMotion   *event)
{
  GtkWidget *widget = GTK_WIDGET (tree_view);
  GdkDragContext *context;
  TreeViewDragInfo *di;
  GtkTreePath *path = NULL;
  gint button;
  gint cell_x, cell_y;
  GtkTreeModel *model;
  gboolean retval = FALSE;

  di = get_info (tree_view);

  if (di == NULL || !di->source_set)
    goto out;

  if (tree_view->priv->pressed_button < 0)
    goto out;

  if (!gtk_drag_check_threshold (widget,
                                 tree_view->priv->press_start_x,
                                 tree_view->priv->press_start_y,
                                 event->x, event->y))
    goto out;

  model = pspp_sheet_view_get_model (tree_view);

  if (model == NULL)
    goto out;

  button = tree_view->priv->pressed_button;
  tree_view->priv->pressed_button = -1;

  pspp_sheet_view_get_path_at_pos (tree_view,
                                 tree_view->priv->press_start_x,
                                 tree_view->priv->press_start_y,
                                 &path,
                                 NULL,
                                 &cell_x,
                                 &cell_y);

  if (path == NULL)
    goto out;

  if (!GTK_IS_TREE_DRAG_SOURCE (model) ||
      !gtk_tree_drag_source_row_draggable (GTK_TREE_DRAG_SOURCE (model),
					   path))
    goto out;

  if (!(GDK_BUTTON1_MASK << (button - 1) & di->start_button_mask))
    goto out;

  /* Now we can begin the drag */

  retval = TRUE;

  context = gtk_drag_begin (widget,
                            gtk_drag_source_get_target_list (widget),
                            di->source_actions,
                            button,
                            (GdkEvent*)event);

  set_source_row (context, model, path);

 out:
  if (path)
    gtk_tree_path_free (path);

  return retval;
}



static void
pspp_sheet_view_drag_begin (GtkWidget      *widget,
                          GdkDragContext *context)
{
#if GTK3_TRANSITION
  PsppSheetView *tree_view;
  GtkTreePath *path = NULL;
  gint cell_x, cell_y;
  GdkPixmap *row_pix;
  TreeViewDragInfo *di;

  tree_view = PSPP_SHEET_VIEW (widget);

  /* if the user uses a custom DND source impl, we don't set the icon here */
  di = get_info (tree_view);

  if (di == NULL || !di->source_set)
    return;

  pspp_sheet_view_get_path_at_pos (tree_view,
                                 tree_view->priv->press_start_x,
                                 tree_view->priv->press_start_y,
                                 &path,
                                 NULL,
                                 &cell_x,
                                 &cell_y);

  g_return_if_fail (path != NULL);

  row_pix = pspp_sheet_view_create_row_drag_icon (tree_view,
                                                path);

  gtk_drag_set_icon_pixmap (context,
                            gdk_drawable_get_colormap (row_pix),
                            row_pix,
                            NULL,
                            /* the + 1 is for the black border in the icon */
                            tree_view->priv->press_start_x + 1,
                            cell_y + 1);

  g_object_unref (row_pix);
  gtk_tree_path_free (path);
#endif
}


static void
pspp_sheet_view_drag_end (GtkWidget      *widget,
                        GdkDragContext *context)
{
  /* do nothing */
}

/* Default signal implementations for the drag signals */
static void
pspp_sheet_view_drag_data_get (GtkWidget        *widget,
                             GdkDragContext   *context,
                             GtkSelectionData *selection_data,
                             guint             info,
                             guint             time)
{
  PsppSheetView *tree_view;
  GtkTreeModel *model;
  TreeViewDragInfo *di;
  GtkTreePath *source_row;

  tree_view = PSPP_SHEET_VIEW (widget);

  model = pspp_sheet_view_get_model (tree_view);

  if (model == NULL)
    return;

  di = get_info (PSPP_SHEET_VIEW (widget));

  if (di == NULL)
    return;

  source_row = get_source_row (context);

  if (source_row == NULL)
    return;

  /* We can implement the GTK_TREE_MODEL_ROW target generically for
   * any model; for DragSource models there are some other targets
   * we also support.
   */

  if (GTK_IS_TREE_DRAG_SOURCE (model) &&
      gtk_tree_drag_source_drag_data_get (GTK_TREE_DRAG_SOURCE (model),
                                          source_row,
                                          selection_data))
    goto done;

  /* If drag_data_get does nothing, try providing row data. */
  if (gtk_selection_data_get_target (selection_data) == gdk_atom_intern_static_string ("GTK_TREE_MODEL_ROW"))
    {
      gtk_tree_set_row_drag_data (selection_data,
				  model,
				  source_row);
    }

 done:
  gtk_tree_path_free (source_row);
}


static void
pspp_sheet_view_drag_data_delete (GtkWidget      *widget,
                                GdkDragContext *context)
{
  TreeViewDragInfo *di;
  GtkTreeModel *model;
  PsppSheetView *tree_view;
  GtkTreePath *source_row;

  tree_view = PSPP_SHEET_VIEW (widget);
  model = pspp_sheet_view_get_model (tree_view);

  if (!check_model_dnd (model, GTK_TYPE_TREE_DRAG_SOURCE, "drag_data_delete"))
    return;

  di = get_info (tree_view);

  if (di == NULL)
    return;

  source_row = get_source_row (context);

  if (source_row == NULL)
    return;

  gtk_tree_drag_source_drag_data_delete (GTK_TREE_DRAG_SOURCE (model),
                                         source_row);

  gtk_tree_path_free (source_row);

  set_source_row (context, NULL, NULL);
}

static void
pspp_sheet_view_drag_leave (GtkWidget      *widget,
                          GdkDragContext *context,
                          guint             time)
{
  /* unset any highlight row */
  pspp_sheet_view_set_drag_dest_row (PSPP_SHEET_VIEW (widget),
                                   NULL,
                                   PSPP_SHEET_VIEW_DROP_BEFORE);

  remove_scroll_timeout (PSPP_SHEET_VIEW (widget));
}


static gboolean
pspp_sheet_view_drag_motion (GtkWidget        *widget,
                           GdkDragContext   *context,
			   /* coordinates relative to the widget */
                           gint              x,
                           gint              y,
                           guint             time)
{
  gboolean empty;
  GtkTreePath *path = NULL;
  PsppSheetViewDropPosition pos;
  PsppSheetView *tree_view;
  GdkDragAction suggested_action = 0;
  GdkAtom target;

  tree_view = PSPP_SHEET_VIEW (widget);

  if (!set_destination_row (tree_view, context, x, y, &suggested_action, &target))
    return FALSE;

  pspp_sheet_view_get_drag_dest_row (tree_view, &path, &pos);

  /* we only know this *after* set_desination_row */
  empty = tree_view->priv->empty_view_drop;

  if (path == NULL && !empty)
    {
      /* Can't drop here. */
      gdk_drag_status (context, 0, time);
    }
  else
    {
      if (tree_view->priv->open_dest_timeout == 0 &&
          (pos == PSPP_SHEET_VIEW_DROP_INTO_OR_AFTER ||
           pos == PSPP_SHEET_VIEW_DROP_INTO_OR_BEFORE))
        {
          /* Nothing. */
        }
      else
        {
	  add_scroll_timeout (tree_view);
	}

      if (target == gdk_atom_intern_static_string ("GTK_TREE_MODEL_ROW"))
        {
          /* Request data so we can use the source row when
           * determining whether to accept the drop
           */
          set_status_pending (context, suggested_action);
          gtk_drag_get_data (widget, context, target, time);
        }
      else
        {
          set_status_pending (context, 0);
          gdk_drag_status (context, suggested_action, time);
        }
    }

  if (path)
    gtk_tree_path_free (path);

  return TRUE;
}


static gboolean
pspp_sheet_view_drag_drop (GtkWidget        *widget,
                         GdkDragContext   *context,
			 /* coordinates relative to the widget */
                         gint              x,
                         gint              y,
                         guint             time)
{
  PsppSheetView *tree_view;
  GtkTreePath *path;
  GdkDragAction suggested_action = 0;
  GdkAtom target = GDK_NONE;
  TreeViewDragInfo *di;
  GtkTreeModel *model;
  gboolean path_down_mode;
  gboolean drop_append_mode;

  tree_view = PSPP_SHEET_VIEW (widget);

  model = pspp_sheet_view_get_model (tree_view);

  remove_scroll_timeout (PSPP_SHEET_VIEW (widget));

  di = get_info (tree_view);

  if (di == NULL)
    return FALSE;

  if (!check_model_dnd (model, GTK_TYPE_TREE_DRAG_DEST, "drag_drop"))
    return FALSE;

  if (!set_destination_row (tree_view, context, x, y, &suggested_action, &target))
    return FALSE;

  path = get_logical_dest_row (tree_view, &path_down_mode, &drop_append_mode);

  if (target != GDK_NONE && path != NULL)
    {
      /* in case a motion had requested drag data, change things so we
       * treat drag data receives as a drop.
       */
      set_status_pending (context, 0);
      set_dest_row (context, model, path,
                    path_down_mode, tree_view->priv->empty_view_drop,
                    drop_append_mode);
    }

  if (path)
    gtk_tree_path_free (path);

  /* Unset this thing */
  pspp_sheet_view_set_drag_dest_row (PSPP_SHEET_VIEW (widget),
                                   NULL,
                                   PSPP_SHEET_VIEW_DROP_BEFORE);

  if (target != GDK_NONE)
    {
      gtk_drag_get_data (widget, context, target, time);
      return TRUE;
    }
  else
    return FALSE;
}

static void
pspp_sheet_view_drag_data_received (GtkWidget        *widget,
                                  GdkDragContext   *context,
				  /* coordinates relative to the widget */
                                  gint              x,
                                  gint              y,
                                  GtkSelectionData *selection_data,
                                  guint             info,
                                  guint             time)
{
  GtkTreePath *path;
  TreeViewDragInfo *di;
  gboolean accepted = FALSE;
  GtkTreeModel *model;
  PsppSheetView *tree_view;
  GtkTreePath *dest_row;
  GdkDragAction suggested_action;
  gboolean path_down_mode;
  gboolean drop_append_mode;

  tree_view = PSPP_SHEET_VIEW (widget);

  model = pspp_sheet_view_get_model (tree_view);

  if (!check_model_dnd (model, GTK_TYPE_TREE_DRAG_DEST, "drag_data_received"))
    return;

  di = get_info (tree_view);

  if (di == NULL)
    return;

  suggested_action = get_status_pending (context);

  if (suggested_action)
    {
      /* We are getting this data due to a request in drag_motion,
       * rather than due to a request in drag_drop, so we are just
       * supposed to call drag_status, not actually paste in the
       * data.
       */
      path = get_logical_dest_row (tree_view, &path_down_mode,
                                   &drop_append_mode);

      if (path == NULL)
        suggested_action = 0;
      else if (path_down_mode)
        gtk_tree_path_down (path);

      if (suggested_action)
        {
	  if (!gtk_tree_drag_dest_row_drop_possible (GTK_TREE_DRAG_DEST (model),
						     path,
						     selection_data))
            {
              if (path_down_mode)
                {
                  path_down_mode = FALSE;
                  gtk_tree_path_up (path);

                  if (!gtk_tree_drag_dest_row_drop_possible (GTK_TREE_DRAG_DEST (model),
                                                             path,
                                                             selection_data))
                    suggested_action = 0;
                }
              else
	        suggested_action = 0;
            }
        }

      gdk_drag_status (context, suggested_action, time);

      if (path)
        gtk_tree_path_free (path);

      /* If you can't drop, remove user drop indicator until the next motion */
      if (suggested_action == 0)
        pspp_sheet_view_set_drag_dest_row (PSPP_SHEET_VIEW (widget),
                                         NULL,
                                         PSPP_SHEET_VIEW_DROP_BEFORE);

      return;
    }

  dest_row = get_dest_row (context, &path_down_mode);

  if (dest_row == NULL)
    return;

  if (gtk_selection_data_get_length (selection_data) >= 0)
    {
      if (path_down_mode)
        {
          gtk_tree_path_down (dest_row);
          if (!gtk_tree_drag_dest_row_drop_possible (GTK_TREE_DRAG_DEST (model),
                                                     dest_row, selection_data))
            gtk_tree_path_up (dest_row);
        }
    }

  if (gtk_selection_data_get_length (selection_data) >= 0)
    {
      if (gtk_tree_drag_dest_drag_data_received (GTK_TREE_DRAG_DEST (model),
                                                 dest_row,
                                                 selection_data))
        accepted = TRUE;
    }

  gtk_drag_finish (context,
                   accepted,
                   (gdk_drag_context_get_actions (context) == GDK_ACTION_MOVE),
                   time);

  if (gtk_tree_path_get_depth (dest_row) == 1
      && gtk_tree_path_get_indices (dest_row)[0] == 0)
    {
      /* special special case drag to "0", scroll to first item */
      if (!tree_view->priv->scroll_to_path)
        pspp_sheet_view_scroll_to_cell (tree_view, dest_row, NULL, FALSE, 0.0, 0.0);
    }

  gtk_tree_path_free (dest_row);

  /* drop dest_row */
  set_dest_row (context, NULL, NULL, FALSE, FALSE, FALSE);
}



/* GtkContainer Methods
 */


static void
pspp_sheet_view_remove (GtkContainer *container,
		      GtkWidget    *widget)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (container);
  PsppSheetViewChild *child = NULL;
  GList *tmp_list;

  tmp_list = tree_view->priv->children;
  while (tmp_list)
    {
      child = tmp_list->data;
      if (child->widget == widget)
	{
	  gtk_widget_unparent (widget);

	  tree_view->priv->children = g_list_remove_link (tree_view->priv->children, tmp_list);
	  g_list_free_1 (tmp_list);
	  g_slice_free (PsppSheetViewChild, child);
	  return;
	}

      tmp_list = tmp_list->next;
    }

  tmp_list = tree_view->priv->columns;

  while (tmp_list)
    {
      PsppSheetViewColumn *column;
      column = tmp_list->data;
      if (column->button == widget)
	{
	  gtk_widget_unparent (widget);
	  return;
	}
      tmp_list = tmp_list->next;
    }
}

static void
pspp_sheet_view_forall (GtkContainer *container,
		      gboolean      include_internals,
		      GtkCallback   callback,
		      gpointer      callback_data)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (container);
  PsppSheetViewChild *child = NULL;
  PsppSheetViewColumn *column;
  GList *tmp_list;

  tmp_list = tree_view->priv->children;
  while (tmp_list)
    {
      child = tmp_list->data;
      tmp_list = tmp_list->next;

      (* callback) (child->widget, callback_data);
    }
  if (include_internals == FALSE)
    return;

  for (tmp_list = tree_view->priv->columns; tmp_list; tmp_list = tmp_list->next)
    {
      column = tmp_list->data;

      if (column->button)
	(* callback) (column->button, callback_data);
    }
}

/* Returns TRUE if the treeview contains no "special" (editable or activatable)
 * cells. If so we draw one big row-spanning focus rectangle.
 */
static gboolean
pspp_sheet_view_has_special_cell (PsppSheetView *tree_view)
{
  GList *list;

  if (tree_view->priv->special_cells != PSPP_SHEET_VIEW_SPECIAL_CELLS_DETECT)
    return tree_view->priv->special_cells = PSPP_SHEET_VIEW_SPECIAL_CELLS_YES;

  for (list = tree_view->priv->columns; list; list = list->next)
    {
      if (!((PsppSheetViewColumn *)list->data)->visible)
	continue;
      if (_pspp_sheet_view_column_count_special_cells (list->data))
	return TRUE;
    }

  return FALSE;
}

static void
pspp_sheet_view_focus_column (PsppSheetView *tree_view,
                              PsppSheetViewColumn *focus_column,
                              gboolean clamp_column_visible)
{
  g_return_if_fail (focus_column != NULL);

  tree_view->priv->focus_column = focus_column;
  if (!focus_column->button)
    {
      pspp_sheet_view_column_set_need_button (focus_column, TRUE);
      //      g_return_if_fail (focus_column->button != NULL);
      if (focus_column->button == NULL)
	return;
    }

  if (gtk_container_get_focus_child (GTK_CONTAINER (tree_view)) != focus_column->button)
    gtk_widget_grab_focus (focus_column->button);

  if (clamp_column_visible)
    pspp_sheet_view_clamp_column_visible (tree_view, focus_column, FALSE);
}

/* Returns TRUE if the focus is within the headers, after the focus operation is
 * done
 */
static gboolean
pspp_sheet_view_header_focus (PsppSheetView      *tree_view,
			    GtkDirectionType  dir,
			    gboolean          clamp_column_visible)
{
  GtkWidget *focus_child;
  PsppSheetViewColumn *focus_column;
  GList *last_column, *first_column;
  GList *tmp_list;
  gboolean rtl;

  if (! PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_HEADERS_VISIBLE))
    return FALSE;

  focus_child = gtk_container_get_focus_child (GTK_CONTAINER (tree_view));

  first_column = tree_view->priv->columns;
  while (first_column)
    {
      PsppSheetViewColumn *c = PSPP_SHEET_VIEW_COLUMN (first_column->data);

      if (pspp_sheet_view_column_can_focus (c) && c->visible)
	break;
      first_column = first_column->next;
    }

  /* No headers are visible, or are focusable.  We can't focus in or out.
   */
  if (first_column == NULL)
    return FALSE;

  last_column = g_list_last (tree_view->priv->columns);
  while (last_column)
    {
      PsppSheetViewColumn *c = PSPP_SHEET_VIEW_COLUMN (last_column->data);

      if (pspp_sheet_view_column_can_focus (c) && c->visible)
	break;
      last_column = last_column->prev;
    }


  rtl = (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL);

  switch (dir)
    {
    case GTK_DIR_TAB_BACKWARD:
    case GTK_DIR_TAB_FORWARD:
    case GTK_DIR_UP:
    case GTK_DIR_DOWN:
      if (focus_child == NULL)
	{
	  if (tree_view->priv->focus_column != NULL &&
              pspp_sheet_view_column_can_focus (tree_view->priv->focus_column))
	    focus_column = tree_view->priv->focus_column;
	  else
            focus_column = first_column->data;
          pspp_sheet_view_focus_column (tree_view, focus_column,
                                        clamp_column_visible);
	  return TRUE;
	}
      return FALSE;

    case GTK_DIR_LEFT:
    case GTK_DIR_RIGHT:
      if (focus_child == NULL)
	{
	  if (tree_view->priv->focus_column != NULL)
	    focus_column = tree_view->priv->focus_column;
	  else if (dir == GTK_DIR_LEFT)
	    focus_column = last_column->data;
	  else
	    focus_column = first_column->data;
          pspp_sheet_view_focus_column (tree_view, focus_column,
                                        clamp_column_visible);
	  return TRUE;
	}

      if (gtk_widget_child_focus (focus_child, dir))
	{
	  /* The focus moves inside the button. */
	  /* This is probably a great example of bad UI */
          if (clamp_column_visible)
            pspp_sheet_view_clamp_column_visible (tree_view,
                                                  tree_view->priv->focus_column,
                                                  FALSE);
	  return TRUE;
	}

      /* We need to move the focus among the row of buttons. */
      for (tmp_list = tree_view->priv->columns; tmp_list; tmp_list = tmp_list->next)
	if (PSPP_SHEET_VIEW_COLUMN (tmp_list->data)->button == focus_child)
	  break;

      if ((tmp_list == first_column && dir == (rtl ? GTK_DIR_RIGHT : GTK_DIR_LEFT))
	  || (tmp_list == last_column && dir == (rtl ? GTK_DIR_LEFT : GTK_DIR_RIGHT)))
        {
	  gtk_widget_error_bell (GTK_WIDGET (tree_view));
	  return TRUE;
	}

      while (tmp_list)
	{
	  PsppSheetViewColumn *column;

	  if (dir == (rtl ? GTK_DIR_LEFT : GTK_DIR_RIGHT))
	    tmp_list = tmp_list->next;
	  else
	    tmp_list = tmp_list->prev;

	  if (tmp_list == NULL)
	    {
	      g_warning ("Internal button not found");
	      break;
	    }
	  column = tmp_list->data;
          if (column->visible &&
	      pspp_sheet_view_column_can_focus (column))
            {
              pspp_sheet_view_column_set_need_button (column, TRUE);
              if (column->button)
                {
                  pspp_sheet_view_focus_column (tree_view, column,
                                                clamp_column_visible);
                  return TRUE;
                }
	    }
	}
      return FALSE;

    default:
      g_assert_not_reached ();
      break;
    }

  return FALSE;
}

/* This function returns in 'path' the first focusable path, if the given path
 * is already focusable, it's the returned one.
 *
 */
static gboolean
search_first_focusable_path (PsppSheetView  *tree_view,
			     GtkTreePath **path,
			     gboolean      search_forward,
			     int *new_node)
{
  /* XXX this function is trivial given that the sheetview doesn't support
     separator rows */
  int node = -1;

  if (!path || !*path)
    return FALSE;

  _pspp_sheet_view_find_node (tree_view, *path, &node);

  if (node < 0)
    return FALSE;

  if (new_node)
    *new_node = node;

  return (*path != NULL);
}

static gint
pspp_sheet_view_focus (GtkWidget        *widget,
		     GtkDirectionType  direction)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  GtkContainer *container = GTK_CONTAINER (widget);
  GtkWidget *focus_child;

  if (!gtk_widget_is_sensitive (widget) || !gtk_widget_get_can_focus (widget))
    return FALSE;

  focus_child = gtk_container_get_focus_child (container);

  pspp_sheet_view_stop_editing (PSPP_SHEET_VIEW (widget), FALSE);
  /* Case 1.  Headers currently have focus. */
  if (focus_child)
    {
      switch (direction)
	{
	case GTK_DIR_LEFT:
	case GTK_DIR_RIGHT:
	  pspp_sheet_view_header_focus (tree_view, direction, TRUE);
	  return TRUE;
	case GTK_DIR_TAB_BACKWARD:
	case GTK_DIR_UP:
	  return FALSE;
	case GTK_DIR_TAB_FORWARD:
	case GTK_DIR_DOWN:
	  gtk_widget_grab_focus (widget);
	  return TRUE;
	default:
	  g_assert_not_reached ();
	  return FALSE;
	}
    }

  /* Case 2. We don't have focus at all. */
  if (!gtk_widget_has_focus (widget))
    {
      if (!pspp_sheet_view_header_focus (tree_view, direction, FALSE))
	gtk_widget_grab_focus (widget);
      return TRUE;
    }

  /* Case 3. We have focus already. */
  if (direction == GTK_DIR_TAB_BACKWARD)
    return (pspp_sheet_view_header_focus (tree_view, direction, FALSE));
  else if (direction == GTK_DIR_TAB_FORWARD)
    return FALSE;

  /* Other directions caught by the keybindings */
  gtk_widget_grab_focus (widget);
  return TRUE;
}

static void
pspp_sheet_view_grab_focus (GtkWidget *widget)
{
  GTK_WIDGET_CLASS (pspp_sheet_view_parent_class)->grab_focus (widget);

  pspp_sheet_view_focus_to_cursor (PSPP_SHEET_VIEW (widget));
}

static void
pspp_sheet_view_style_set (GtkWidget *widget,
			 GtkStyle *previous_style)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  GList *list;
  PsppSheetViewColumn *column;

  if (gtk_widget_get_realized (widget))
    {
      gdk_window_set_background (tree_view->priv->bin_window, &gtk_widget_get_style (widget)->base[gtk_widget_get_state (widget)]);
      gtk_style_set_background (gtk_widget_get_style (widget), tree_view->priv->header_window, GTK_STATE_NORMAL);
      pspp_sheet_view_set_grid_lines (tree_view, tree_view->priv->grid_lines);
    }

  gtk_widget_style_get (widget,
			"expander-size", &tree_view->priv->expander_size,
			NULL);
  tree_view->priv->expander_size += EXPANDER_EXTRA_PADDING;

  for (list = tree_view->priv->columns; list; list = list->next)
    {
      column = list->data;
      _pspp_sheet_view_column_cell_set_dirty (column);
    }

  tree_view->priv->fixed_height = -1;

  /* Invalidate cached button style. */
  if (tree_view->priv->button_style)
    {
      g_object_unref (tree_view->priv->button_style);
      tree_view->priv->button_style = NULL;
    }

  gtk_widget_queue_resize (widget);
}


static void
pspp_sheet_view_set_focus_child (GtkContainer *container,
			       GtkWidget    *child)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (container);
  GList *list;

  for (list = tree_view->priv->columns; list; list = list->next)
    {
      if (PSPP_SHEET_VIEW_COLUMN (list->data)->button == child)
	{
	  tree_view->priv->focus_column = PSPP_SHEET_VIEW_COLUMN (list->data);
	  break;
	}
    }

  GTK_CONTAINER_CLASS (pspp_sheet_view_parent_class)->set_focus_child (container, child);
}

static void
pspp_sheet_view_set_adjustments (PsppSheetView   *tree_view,
			       GtkAdjustment *hadj,
			       GtkAdjustment *vadj)
{
  gboolean need_adjust = FALSE;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  if (hadj)
    g_return_if_fail (GTK_IS_ADJUSTMENT (hadj));
  else
    hadj = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));
  if (vadj)
    g_return_if_fail (GTK_IS_ADJUSTMENT (vadj));
  else
    vadj = GTK_ADJUSTMENT (gtk_adjustment_new (0.0, 0.0, 0.0, 0.0, 0.0, 0.0));

  if (tree_view->priv->hadjustment && (tree_view->priv->hadjustment != hadj))
    {
      g_signal_handlers_disconnect_by_func (tree_view->priv->hadjustment,
					    pspp_sheet_view_adjustment_changed,
					    tree_view);
      g_object_unref (tree_view->priv->hadjustment);
    }

  if (tree_view->priv->vadjustment && (tree_view->priv->vadjustment != vadj))
    {
      g_signal_handlers_disconnect_by_func (tree_view->priv->vadjustment,
					    pspp_sheet_view_adjustment_changed,
					    tree_view);
      g_object_unref (tree_view->priv->vadjustment);
    }

  if (tree_view->priv->hadjustment != hadj)
    {
      tree_view->priv->hadjustment = hadj;
      g_object_ref_sink (tree_view->priv->hadjustment);

      g_signal_connect (tree_view->priv->hadjustment, "value-changed",
			G_CALLBACK (pspp_sheet_view_adjustment_changed),
			tree_view);
      need_adjust = TRUE;
    }

  if (tree_view->priv->vadjustment != vadj)
    {
      tree_view->priv->vadjustment = vadj;
      g_object_ref_sink (tree_view->priv->vadjustment);

      g_signal_connect (tree_view->priv->vadjustment, "value-changed",
			G_CALLBACK (pspp_sheet_view_adjustment_changed),
			tree_view);
      need_adjust = TRUE;
    }

  if (need_adjust)
    pspp_sheet_view_adjustment_changed (NULL, tree_view);
}


static gboolean
pspp_sheet_view_real_move_cursor (PsppSheetView       *tree_view,
				GtkMovementStep    step,
				gint               count)
{
  PsppSheetSelectMode mode;
  GdkModifierType state;

  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), FALSE);
  g_return_val_if_fail (step == GTK_MOVEMENT_LOGICAL_POSITIONS ||
			step == GTK_MOVEMENT_VISUAL_POSITIONS ||
			step == GTK_MOVEMENT_DISPLAY_LINES ||
			step == GTK_MOVEMENT_PAGES ||
			step == GTK_MOVEMENT_BUFFER_ENDS ||
			step == GTK_MOVEMENT_DISPLAY_LINE_ENDS, FALSE);

  if (tree_view->priv->row_count == 0)
    return FALSE;
  if (!gtk_widget_has_focus (GTK_WIDGET (tree_view)))
    return FALSE;

  pspp_sheet_view_stop_editing (tree_view, FALSE);
  PSPP_SHEET_VIEW_SET_FLAG (tree_view, PSPP_SHEET_VIEW_DRAW_KEYFOCUS);
  gtk_widget_grab_focus (GTK_WIDGET (tree_view));

  mode = 0;
  if (gtk_get_current_event_state (&state))
    {
      if ((state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
        mode |= PSPP_SHEET_SELECT_MODE_TOGGLE;
      if ((state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK)
        mode |= PSPP_SHEET_SELECT_MODE_EXTEND;
    }
  /* else we assume not pressed */

  switch (step)
    {
    case GTK_MOVEMENT_LOGICAL_POSITIONS:
      pspp_sheet_view_move_cursor_tab (tree_view, count);
      break;
    case GTK_MOVEMENT_VISUAL_POSITIONS:
      pspp_sheet_view_move_cursor_left_right (tree_view, count, mode);
      break;
    case GTK_MOVEMENT_DISPLAY_LINES:
      pspp_sheet_view_move_cursor_up_down (tree_view, count, mode);
      break;
    case GTK_MOVEMENT_PAGES:
      pspp_sheet_view_move_cursor_page_up_down (tree_view, count, mode);
      break;
    case GTK_MOVEMENT_BUFFER_ENDS:
      pspp_sheet_view_move_cursor_start_end (tree_view, count, mode);
      break;
    case GTK_MOVEMENT_DISPLAY_LINE_ENDS:
      pspp_sheet_view_move_cursor_line_start_end (tree_view, count, mode);
      break;
    default:
      g_assert_not_reached ();
    }

  return TRUE;
}

static void
pspp_sheet_view_put (PsppSheetView *tree_view,
		   GtkWidget   *child_widget,
		   /* in bin_window coordinates */
		   gint         x,
		   gint         y,
		   gint         width,
		   gint         height)
{
  PsppSheetViewChild *child;
  
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (GTK_IS_WIDGET (child_widget));

  child = g_slice_new (PsppSheetViewChild);

  child->widget = child_widget;
  child->x = x;
  child->y = y;
  child->width = width;
  child->height = height;

  tree_view->priv->children = g_list_append (tree_view->priv->children, child);

  if (gtk_widget_get_realized (GTK_WIDGET (tree_view)))
    gtk_widget_set_parent_window (child->widget, tree_view->priv->bin_window);
  
  gtk_widget_set_parent (child_widget, GTK_WIDGET (tree_view));
}

void
_pspp_sheet_view_child_move_resize (PsppSheetView *tree_view,
				  GtkWidget   *widget,
				  /* in tree coordinates */
				  gint         x,
				  gint         y,
				  gint         width,
				  gint         height)
{
  PsppSheetViewChild *child = NULL;
  GList *list;
  GdkRectangle allocation;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (GTK_IS_WIDGET (widget));

  for (list = tree_view->priv->children; list; list = list->next)
    {
      if (((PsppSheetViewChild *)list->data)->widget == widget)
	{
	  child = list->data;
	  break;
	}
    }
  if (child == NULL)
    return;

  allocation.x = child->x = x;
  allocation.y = child->y = y;
  allocation.width = child->width = width;
  allocation.height = child->height = height;

  if (gtk_widget_get_realized (widget))
    gtk_widget_size_allocate (widget, &allocation);
}


/* TreeModel Callbacks
 */

static void
pspp_sheet_view_row_changed (GtkTreeModel *model,
			   GtkTreePath  *path,
			   GtkTreeIter  *iter,
			   gpointer      data)
{
  PsppSheetView *tree_view = (PsppSheetView *)data;
  int node;
  gboolean free_path = FALSE;
  GtkTreePath *cursor_path;

  g_return_if_fail (path != NULL || iter != NULL);

  if (tree_view->priv->cursor != NULL)
    cursor_path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);
  else
    cursor_path = NULL;

  if (tree_view->priv->edited_column &&
      (cursor_path == NULL || gtk_tree_path_compare (cursor_path, path) == 0))
    pspp_sheet_view_stop_editing (tree_view, TRUE);

  if (cursor_path != NULL)
    gtk_tree_path_free (cursor_path);

  if (path == NULL)
    {
      path = gtk_tree_model_get_path (model, iter);
      free_path = TRUE;
    }
  else if (iter == NULL)
    gtk_tree_model_get_iter (model, iter, path);

  _pspp_sheet_view_find_node (tree_view,
                              path,
                              &node);

  if (node >= 0)
    {
      if (gtk_widget_get_realized (GTK_WIDGET (tree_view)))
        pspp_sheet_view_node_queue_redraw (tree_view, node);
    }
  
  if (free_path)
    gtk_tree_path_free (path);
}

static void
pspp_sheet_view_row_inserted (GtkTreeModel *model,
			    GtkTreePath  *path,
			    GtkTreeIter  *iter,
			    gpointer      data)
{
  PsppSheetView *tree_view = (PsppSheetView *) data;
  gint *indices;
  int tmpnode = -1;
  gint height = tree_view->priv->fixed_height;
  gboolean free_path = FALSE;
  gboolean node_visible = TRUE;

  g_return_if_fail (path != NULL || iter != NULL);

  if (path == NULL)
    {
      path = gtk_tree_model_get_path (model, iter);
      free_path = TRUE;
    }
  else if (iter == NULL)
    gtk_tree_model_get_iter (model, iter, path);

  tree_view->priv->row_count = gtk_tree_model_iter_n_children (model, NULL);

  /* Update all row-references */
  gtk_tree_row_reference_inserted (G_OBJECT (data), path);
  indices = gtk_tree_path_get_indices (path);
  tmpnode = indices[0];

  range_tower_insert0 (tree_view->priv->selected, tmpnode, 1);

  if (height > 0)
    {
      if (node_visible && node_is_visible (tree_view, tmpnode))
	gtk_widget_queue_resize (GTK_WIDGET (tree_view));
      else
	gtk_widget_queue_resize_no_redraw (GTK_WIDGET (tree_view));
    }
  else
    install_presize_handler (tree_view);
  if (free_path)
    gtk_tree_path_free (path);
}

static void
pspp_sheet_view_row_deleted (GtkTreeModel *model,
			   GtkTreePath  *path,
			   gpointer      data)
{
  PsppSheetView *tree_view = (PsppSheetView *)data;
  int node;

  g_return_if_fail (path != NULL);

  gtk_tree_row_reference_deleted (G_OBJECT (data), path);

  _pspp_sheet_view_find_node (tree_view, path, &node);

  if (node < 0)
    return;

  range_tower_delete (tree_view->priv->selected, node, 1);

  /* Ensure we don't have a dangling pointer to a dead node */
  ensure_unprelighted (tree_view);

  /* Cancel editting if we've started */
  pspp_sheet_view_stop_editing (tree_view, TRUE);

  if (tree_view->priv->destroy_count_func)
    {
      gint child_count = 0;
      tree_view->priv->destroy_count_func (tree_view, path, child_count, tree_view->priv->destroy_count_data);
    }

  tree_view->priv->row_count = gtk_tree_model_iter_n_children (model, NULL);

  if (! gtk_tree_row_reference_valid (tree_view->priv->top_row))
    {
      gtk_tree_row_reference_free (tree_view->priv->top_row);
      tree_view->priv->top_row = NULL;
    }

  install_scroll_sync_handler (tree_view);

  gtk_widget_queue_resize (GTK_WIDGET (tree_view));

#if 0
  if (helper_data.changed)
    g_signal_emit_by_name (tree_view->priv->selection, "changed");
#endif
}

static void
pspp_sheet_view_rows_reordered (GtkTreeModel *model,
			      GtkTreePath  *parent,
			      GtkTreeIter  *iter,
			      gint         *new_order,
			      gpointer      data)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (data);
  gint len;

  /* XXX need to adjust selection */
  len = gtk_tree_model_iter_n_children (model, iter);

  if (len < 2)
    return;

  gtk_tree_row_reference_reordered (G_OBJECT (data),
				    parent,
				    iter,
				    new_order);

  if (gtk_tree_path_get_depth (parent) != 0)
    return;

  if (tree_view->priv->edited_column)
    pspp_sheet_view_stop_editing (tree_view, TRUE);

  /* we need to be unprelighted */
  ensure_unprelighted (tree_view);

  gtk_widget_queue_draw (GTK_WIDGET (tree_view));

  pspp_sheet_view_dy_to_top_row (tree_view);
}


/* Internal tree functions
 */


static void
pspp_sheet_view_get_background_xrange (PsppSheetView       *tree_view,
                                     PsppSheetViewColumn *column,
                                     gint              *x1,
                                     gint              *x2)
{
  PsppSheetViewColumn *tmp_column = NULL;
  gint total_width;
  GList *list;
  gboolean rtl;

  if (x1)
    *x1 = 0;

  if (x2)
    *x2 = 0;

  rtl = (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL);

  total_width = 0;
  for (list = (rtl ? g_list_last (tree_view->priv->columns) : g_list_first (tree_view->priv->columns));
       list;
       list = (rtl ? list->prev : list->next))
    {
      tmp_column = list->data;

      if (tmp_column == column)
        break;

      if (tmp_column->visible)
        total_width += tmp_column->width;
    }

  if (tmp_column != column)
    {
      g_warning (G_STRLOC": passed-in column isn't in the tree");
      return;
    }

  if (x1)
    *x1 = total_width;

  if (x2)
    {
      if (column->visible)
        *x2 = total_width + column->width;
      else
        *x2 = total_width; /* width of 0 */
    }
}

/* Make sure the node is visible vertically */
static void
pspp_sheet_view_clamp_node_visible (PsppSheetView *tree_view,
                                    int node)
{
  gint node_dy, height;
  GtkTreePath *path = NULL;

  if (!gtk_widget_get_realized (GTK_WIDGET (tree_view)))
    return;

  /* just return if the node is visible, avoiding a costly expose */
  node_dy = pspp_sheet_view_node_find_offset (tree_view, node);
  height = ROW_HEIGHT (tree_view);
  if (node_dy >= gtk_adjustment_get_value (tree_view->priv->vadjustment)
      && node_dy + height <= (gtk_adjustment_get_value (tree_view->priv->vadjustment)
                              + gtk_adjustment_get_page_size (tree_view->priv->vadjustment)))
    return;

  path = _pspp_sheet_view_find_path (tree_view, node);
  if (path)
    {
      /* We process updates because we want to clear old selected items when we scroll.
       * if this is removed, we get a "selection streak" at the bottom. */
      gdk_window_process_updates (tree_view->priv->bin_window, TRUE);
      pspp_sheet_view_scroll_to_cell (tree_view, path, NULL, FALSE, 0.0, 0.0);
      gtk_tree_path_free (path);
    }
}

static void
pspp_sheet_view_clamp_column_visible (PsppSheetView       *tree_view,
				    PsppSheetViewColumn *column,
				    gboolean           focus_to_cell)
{
  gint x, width;

  if (column == NULL)
    return;

  x = column->allocation.x;
  width = column->allocation.width;

  if (width > gtk_adjustment_get_page_size (tree_view->priv->hadjustment))
    {
      /* The column is larger than the horizontal page size.  If the
       * column has cells which can be focussed individually, then we make
       * sure the cell which gets focus is fully visible (if even the
       * focus cell is bigger than the page size, we make sure the
       * left-hand side of the cell is visible).
       *
       * If the column does not have those so-called special cells, we
       * make sure the left-hand side of the column is visible.
       */

      if (focus_to_cell && pspp_sheet_view_has_special_cell (tree_view))
        {
	  GtkTreePath *cursor_path;
	  GdkRectangle background_area, cell_area, focus_area;

	  cursor_path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);

	  pspp_sheet_view_get_cell_area (tree_view,
				       cursor_path, column, &cell_area);
	  pspp_sheet_view_get_background_area (tree_view,
					     cursor_path, column,
					     &background_area);

	  gtk_tree_path_free (cursor_path);

	  _pspp_sheet_view_column_get_focus_area (column,
						&background_area,
						&cell_area,
						&focus_area);

	  x = focus_area.x;
	  width = focus_area.width;

	  if (width < gtk_adjustment_get_page_size (tree_view->priv->hadjustment))
	    {
	      if ((gtk_adjustment_get_value (tree_view->priv->hadjustment) + gtk_adjustment_get_page_size (tree_view->priv->hadjustment)) < (x + width))
		gtk_adjustment_set_value (tree_view->priv->hadjustment,
					  x + width - gtk_adjustment_get_page_size (tree_view->priv->hadjustment));
	      else if (gtk_adjustment_get_value (tree_view->priv->hadjustment) > x)
		gtk_adjustment_set_value (tree_view->priv->hadjustment, x);
	    }
	}

      gtk_adjustment_set_value (tree_view->priv->hadjustment,
				CLAMP (x,
				       gtk_adjustment_get_lower (tree_view->priv->hadjustment),
				       gtk_adjustment_get_upper (tree_view->priv->hadjustment)
				       - gtk_adjustment_get_page_size (tree_view->priv->hadjustment)));
    }
  else
    {
      if ((gtk_adjustment_get_value (tree_view->priv->hadjustment) + gtk_adjustment_get_page_size (tree_view->priv->hadjustment)) < (x + width))
	  gtk_adjustment_set_value (tree_view->priv->hadjustment,
				    x + width - gtk_adjustment_get_page_size (tree_view->priv->hadjustment));
      else if (gtk_adjustment_get_value (tree_view->priv->hadjustment) > x)
	gtk_adjustment_set_value (tree_view->priv->hadjustment, x);
  }
}

GtkTreePath *
_pspp_sheet_view_find_path (PsppSheetView *tree_view,
                            int node)
{
  GtkTreePath *path;

  path = gtk_tree_path_new ();
  if (node >= 0)
    gtk_tree_path_append_index (path, node);
  return path;
}

void
_pspp_sheet_view_find_node (PsppSheetView  *tree_view,
			  GtkTreePath  *path,
			  int *node)
{
  gint *indices = gtk_tree_path_get_indices (path);
  gint depth = gtk_tree_path_get_depth (path);

  *node = -1;
  if (depth == 0 || indices[0] < 0 || indices[0] >= tree_view->priv->row_count)
    return;
  *node = indices[0];
}

static void
pspp_sheet_view_add_move_binding (GtkBindingSet  *binding_set,
				guint           keyval,
				guint           modmask,
				gboolean        add_shifted_binding,
				GtkMovementStep step,
				gint            count)
{
  
  gtk_binding_entry_add_signal (binding_set, keyval, modmask,
                                "move-cursor", 2,
                                G_TYPE_ENUM, step,
                                G_TYPE_INT, count);

  if (add_shifted_binding)
    gtk_binding_entry_add_signal (binding_set, keyval, GDK_SHIFT_MASK,
				  "move-cursor", 2,
				  G_TYPE_ENUM, step,
				  G_TYPE_INT, count);

  if ((modmask & GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
   return;

  gtk_binding_entry_add_signal (binding_set, keyval, GDK_CONTROL_MASK | GDK_SHIFT_MASK,
                                "move-cursor", 2,
                                G_TYPE_ENUM, step,
                                G_TYPE_INT, count);

  gtk_binding_entry_add_signal (binding_set, keyval, GDK_CONTROL_MASK,
                                "move-cursor", 2,
                                G_TYPE_ENUM, step,
                                G_TYPE_INT, count);
}

static void
pspp_sheet_view_set_column_drag_info (PsppSheetView       *tree_view,
				    PsppSheetViewColumn *column)
{
  PsppSheetViewColumn *left_column;
  PsppSheetViewColumn *cur_column = NULL;
  PsppSheetViewColumnReorder *reorder;
  gboolean rtl;
  GList *tmp_list;
  gint left;

  /* We want to precalculate the motion list such that we know what column slots
   * are available.
   */
  left_column = NULL;
  rtl = (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL);

  /* First, identify all possible drop spots */
  if (rtl)
    tmp_list = g_list_last (tree_view->priv->columns);
  else
    tmp_list = g_list_first (tree_view->priv->columns);

  while (tmp_list)
    {
      cur_column = PSPP_SHEET_VIEW_COLUMN (tmp_list->data);
      tmp_list = rtl?g_list_previous (tmp_list):g_list_next (tmp_list);

      if (cur_column->visible == FALSE)
	continue;

      /* If it's not the column moving and func tells us to skip over the column, we continue. */
      if (left_column != column && cur_column != column &&
	  tree_view->priv->column_drop_func &&
	  ! tree_view->priv->column_drop_func (tree_view, column, left_column, cur_column, tree_view->priv->column_drop_func_data))
	{
	  left_column = cur_column;
	  continue;
	}
      reorder = g_slice_new0 (PsppSheetViewColumnReorder);
      reorder->left_column = left_column;
      left_column = reorder->right_column = cur_column;

      tree_view->priv->column_drag_info = g_list_append (tree_view->priv->column_drag_info, reorder);
    }

  /* Add the last one */
  if (tree_view->priv->column_drop_func == NULL ||
      ((left_column != column) &&
       tree_view->priv->column_drop_func (tree_view, column, left_column, NULL, tree_view->priv->column_drop_func_data)))
    {
      reorder = g_slice_new0 (PsppSheetViewColumnReorder);
      reorder->left_column = left_column;
      reorder->right_column = NULL;
      tree_view->priv->column_drag_info = g_list_append (tree_view->priv->column_drag_info, reorder);
    }

  /* We quickly check to see if it even makes sense to reorder columns. */
  /* If there is nothing that can be moved, then we return */

  if (tree_view->priv->column_drag_info == NULL)
    return;

  /* We know there are always 2 slots possbile, as you can always return column. */
  /* If that's all there is, return */
  if (tree_view->priv->column_drag_info->next == NULL || 
      (tree_view->priv->column_drag_info->next->next == NULL &&
       ((PsppSheetViewColumnReorder *)tree_view->priv->column_drag_info->data)->right_column == column &&
       ((PsppSheetViewColumnReorder *)tree_view->priv->column_drag_info->next->data)->left_column == column))
    {
      for (tmp_list = tree_view->priv->column_drag_info; tmp_list; tmp_list = tmp_list->next)
	g_slice_free (PsppSheetViewColumnReorder, tmp_list->data);
      g_list_free (tree_view->priv->column_drag_info);
      tree_view->priv->column_drag_info = NULL;
      return;
    }
  /* We fill in the ranges for the columns, now that we've isolated them */
  left = - TREE_VIEW_COLUMN_DRAG_DEAD_MULTIPLIER (tree_view);

  for (tmp_list = tree_view->priv->column_drag_info; tmp_list; tmp_list = tmp_list->next)
    {
      reorder = (PsppSheetViewColumnReorder *) tmp_list->data;

      reorder->left_align = left;
      if (tmp_list->next != NULL)
	{
	  g_assert (tmp_list->next->data);
	  left = reorder->right_align = (reorder->right_column->allocation.x +
					 reorder->right_column->allocation.width +
					 ((PsppSheetViewColumnReorder *)tmp_list->next->data)->left_column->allocation.x)/2;
	}
      else
	{
	  gint width = gdk_window_get_width (tree_view->priv->header_window);
	  reorder->right_align = width + TREE_VIEW_COLUMN_DRAG_DEAD_MULTIPLIER (tree_view);
	}
    }
}

void
_pspp_sheet_view_column_start_drag (PsppSheetView       *tree_view,
				  PsppSheetViewColumn *column)
{
  GdkEvent *send_event;
  GtkAllocation allocation;
  gint x, y;
  GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (tree_view));
  GdkDisplay *display = gdk_screen_get_display (screen);

  g_return_if_fail (tree_view->priv->column_drag_info == NULL);
  g_return_if_fail (tree_view->priv->cur_reorder == NULL);
  g_return_if_fail (column->button);

  pspp_sheet_view_set_column_drag_info (tree_view, column);

  if (tree_view->priv->column_drag_info == NULL)
    return;

  if (tree_view->priv->drag_window == NULL)
    {
      GdkWindowAttr attributes;
      guint attributes_mask;

      attributes.window_type = GDK_WINDOW_CHILD;
      attributes.wclass = GDK_INPUT_OUTPUT;
      attributes.x = column->allocation.x;
      attributes.y = 0;
      attributes.width = column->allocation.width;
      attributes.height = column->allocation.height;
      attributes.visual = gtk_widget_get_visual (GTK_WIDGET (tree_view));
      attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK | GDK_EXPOSURE_MASK | GDK_POINTER_MOTION_MASK;
      attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL ;

      tree_view->priv->drag_window = gdk_window_new (tree_view->priv->bin_window,
						     &attributes,
						     attributes_mask);
      gdk_window_set_user_data (tree_view->priv->drag_window, GTK_WIDGET (tree_view));
    }

  gdk_display_pointer_ungrab (display, GDK_CURRENT_TIME);
  gdk_display_keyboard_ungrab (display, GDK_CURRENT_TIME);

  gtk_grab_remove (column->button);

  send_event = gdk_event_new (GDK_LEAVE_NOTIFY);
  send_event->crossing.send_event = TRUE;
  send_event->crossing.window = g_object_ref (gtk_button_get_event_window (GTK_BUTTON (column->button)));
  send_event->crossing.subwindow = NULL;
  send_event->crossing.detail = GDK_NOTIFY_ANCESTOR;
  send_event->crossing.time = GDK_CURRENT_TIME;

  gtk_propagate_event (column->button, send_event);
  gdk_event_free (send_event);

  send_event = gdk_event_new (GDK_BUTTON_RELEASE);
  send_event->button.window = g_object_ref (gdk_screen_get_root_window (screen));
  send_event->button.send_event = TRUE;
  send_event->button.time = GDK_CURRENT_TIME;
  send_event->button.x = -1;
  send_event->button.y = -1;
  send_event->button.axes = NULL;
  send_event->button.state = 0;
  send_event->button.button = 1;
  send_event->button.device = 
    gdk_device_manager_get_client_pointer (gdk_display_get_device_manager (display));

  send_event->button.x_root = 0;
  send_event->button.y_root = 0;

  gtk_propagate_event (column->button, send_event);
  gdk_event_free (send_event);

  /* Kids, don't try this at home */
  g_object_ref (column->button);
  gtk_container_remove (GTK_CONTAINER (tree_view), column->button);
  gtk_widget_set_parent_window (column->button, tree_view->priv->drag_window);
  gtk_widget_set_parent (column->button, GTK_WIDGET (tree_view));
  g_object_unref (column->button);

  tree_view->priv->drag_column_x = column->allocation.x;
  allocation = column->allocation;
  allocation.x = 0;
  gtk_widget_size_allocate (column->button, &allocation);
  gtk_widget_set_parent_window (column->button, tree_view->priv->drag_window);

  tree_view->priv->drag_column = column;
  gdk_window_show (tree_view->priv->drag_window);

  gdk_window_get_origin (tree_view->priv->header_window, &x, &y);

  gtk_widget_grab_focus (GTK_WIDGET (tree_view));
  while (gtk_events_pending ())
    gtk_main_iteration ();

  PSPP_SHEET_VIEW_SET_FLAG (tree_view, PSPP_SHEET_VIEW_IN_COLUMN_DRAG);
  gdk_pointer_grab (tree_view->priv->drag_window,
		    FALSE,
		    GDK_POINTER_MOTION_MASK|GDK_BUTTON_RELEASE_MASK,
		    NULL, NULL, GDK_CURRENT_TIME);
  gdk_keyboard_grab (tree_view->priv->drag_window,
		     FALSE,
		     GDK_CURRENT_TIME);
}

void
_pspp_sheet_view_queue_draw_node (PsppSheetView        *tree_view,
				int node,
				const GdkRectangle *clip_rect)
{
  GdkRectangle rect;
  GtkAllocation allocation;

  if (!gtk_widget_get_realized (GTK_WIDGET (tree_view)))
    return;

  gtk_widget_get_allocation (GTK_WIDGET (tree_view), &allocation);
  rect.x = 0;
  rect.width = MAX (tree_view->priv->width, allocation.width);

  rect.y = BACKGROUND_FIRST_PIXEL (tree_view, node);
  rect.height = ROW_HEIGHT (tree_view);

  if (clip_rect)
    {
      GdkRectangle new_rect;

      gdk_rectangle_intersect (clip_rect, &rect, &new_rect);

      gdk_window_invalidate_rect (tree_view->priv->bin_window, &new_rect, TRUE);
    }
  else
    {
      gdk_window_invalidate_rect (tree_view->priv->bin_window, &rect, TRUE);
    }
}

static void
pspp_sheet_view_queue_draw_path (PsppSheetView        *tree_view,
                               GtkTreePath        *path,
                               const GdkRectangle *clip_rect)
{
  int node = -1;

  _pspp_sheet_view_find_node (tree_view, path, &node);

  if (node)
    _pspp_sheet_view_queue_draw_node (tree_view, node, clip_rect);
}

static void
pspp_sheet_view_focus_to_cursor (PsppSheetView *tree_view)

{
  GtkTreePath *cursor_path;

  if ((tree_view->priv->row_count == 0) ||
      (! gtk_widget_get_realized (GTK_WIDGET (tree_view))))
    return;

  cursor_path = NULL;
  if (tree_view->priv->cursor)
    cursor_path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);

  if (cursor_path == NULL)
    {
      /* There's no cursor.  Move the cursor to the first selected row, if any
       * are selected, otherwise to the first row in the sheetview.
       */
      GList *selected_rows;
      GtkTreeModel *model;
      PsppSheetSelection *selection;

      selection = pspp_sheet_view_get_selection (tree_view);
      selected_rows = pspp_sheet_selection_get_selected_rows (selection, &model);

      if (selected_rows)
	{
          /* XXX we could avoid doing O(n) work to get this result */
          cursor_path = gtk_tree_path_copy((const GtkTreePath *)(selected_rows->data));
	  g_list_foreach (selected_rows, (GFunc)gtk_tree_path_free, NULL);
	  g_list_free (selected_rows);
        }
      else
	{
	  cursor_path = gtk_tree_path_new_first ();
	  search_first_focusable_path (tree_view, &cursor_path,
				       TRUE, NULL);
	}

      gtk_tree_row_reference_free (tree_view->priv->cursor);
      tree_view->priv->cursor = NULL;

      if (cursor_path)
	{
	  if (tree_view->priv->selection->type == PSPP_SHEET_SELECTION_MULTIPLE ||
              tree_view->priv->selection->type == PSPP_SHEET_SELECTION_RECTANGLE)
	    pspp_sheet_view_real_set_cursor (tree_view, cursor_path, FALSE, FALSE, 0);
	  else
	    pspp_sheet_view_real_set_cursor (tree_view, cursor_path, TRUE, FALSE, 0);
	}
    }

  if (cursor_path)
    {
      /* Now find a column for the cursor. */
      PSPP_SHEET_VIEW_SET_FLAG (tree_view, PSPP_SHEET_VIEW_DRAW_KEYFOCUS);

      pspp_sheet_view_queue_draw_path (tree_view, cursor_path, NULL);
      gtk_tree_path_free (cursor_path);

      if (tree_view->priv->focus_column == NULL)
	{
	  GList *list;
	  for (list = tree_view->priv->columns; list; list = list->next)
	    {
	      if (PSPP_SHEET_VIEW_COLUMN (list->data)->visible)
		{
		  tree_view->priv->focus_column = PSPP_SHEET_VIEW_COLUMN (list->data);
                  pspp_sheet_selection_unselect_all_columns (tree_view->priv->selection);
                  pspp_sheet_selection_select_column (tree_view->priv->selection, tree_view->priv->focus_column);
		  break;
		}
	    }

	}
    }
}

static gboolean
pspp_sheet_view_move_cursor_up_down (PsppSheetView *tree_view,
				   gint         count,
                                   PsppSheetSelectMode mode)
{
  gint selection_count;
  int cursor_node = -1;
  int new_cursor_node = -1;
  GtkTreePath *cursor_path = NULL;
  gboolean grab_focus = TRUE;

  if (! gtk_widget_has_focus (GTK_WIDGET (tree_view)))
    return FALSE;

  cursor_path = NULL;
  if (!gtk_tree_row_reference_valid (tree_view->priv->cursor))
    /* FIXME: we lost the cursor; should we get the first? */
    return FALSE;

  cursor_path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);
  _pspp_sheet_view_find_node (tree_view, cursor_path, &cursor_node);

  if (cursor_node < 0)
    /* FIXME: we lost the cursor; should we get the first? */
    return FALSE;

  selection_count = pspp_sheet_selection_count_selected_rows (tree_view->priv->selection);

  if (selection_count == 0
      && tree_view->priv->selection->type != PSPP_SHEET_SELECTION_NONE
      && !(mode & PSPP_SHEET_SELECT_MODE_TOGGLE))
    {
      /* Don't move the cursor, but just select the current node */
      new_cursor_node = cursor_node;
    }
  else
    {
      if (count == -1)
	new_cursor_node = pspp_sheet_view_node_prev (tree_view, cursor_node);
      else
	new_cursor_node = pspp_sheet_view_node_next (tree_view, cursor_node);
    }

  gtk_tree_path_free (cursor_path);

  if (new_cursor_node)
    {
      cursor_path = _pspp_sheet_view_find_path (tree_view, new_cursor_node);

      search_first_focusable_path (tree_view, &cursor_path,
				   (count != -1),
				   &new_cursor_node);

      if (cursor_path)
	gtk_tree_path_free (cursor_path);
    }

  /*
   * If the list has only one item and multi-selection is set then select
   * the row (if not yet selected).
   */
  if ((tree_view->priv->selection->type == PSPP_SHEET_SELECTION_MULTIPLE ||
       tree_view->priv->selection->type == PSPP_SHEET_SELECTION_RECTANGLE) &&
      new_cursor_node < 0)
    {
      if (count == -1)
        new_cursor_node = pspp_sheet_view_node_next (tree_view, cursor_node);
      else
        new_cursor_node = pspp_sheet_view_node_prev (tree_view, cursor_node);

      if (new_cursor_node < 0
	  && !pspp_sheet_view_node_is_selected (tree_view, cursor_node))
        {
          new_cursor_node = cursor_node;
        }
      else
        {
          new_cursor_node = -1;
        }
    }

  if (new_cursor_node >= 0)
    {
      cursor_path = _pspp_sheet_view_find_path (tree_view, new_cursor_node);
      pspp_sheet_view_real_set_cursor (tree_view, cursor_path, TRUE, TRUE, mode);
      gtk_tree_path_free (cursor_path);
    }
  else
    {
      pspp_sheet_view_clamp_node_visible (tree_view, cursor_node);

      if (!(mode & PSPP_SHEET_SELECT_MODE_EXTEND))
        {
          if (! gtk_widget_keynav_failed (GTK_WIDGET (tree_view),
                                          count < 0 ?
                                          GTK_DIR_UP : GTK_DIR_DOWN))
            {
              GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (tree_view));

              if (toplevel)
                gtk_widget_child_focus (toplevel,
                                        count < 0 ?
                                        GTK_DIR_TAB_BACKWARD :
                                        GTK_DIR_TAB_FORWARD);

              grab_focus = FALSE;
            }
        }
      else
        {
          gtk_widget_error_bell (GTK_WIDGET (tree_view));
        }
    }

  if (grab_focus)
    gtk_widget_grab_focus (GTK_WIDGET (tree_view));

  return new_cursor_node >= 0;
}

static void
pspp_sheet_view_move_cursor_page_up_down (PsppSheetView *tree_view,
                                          gint         count,
                                          PsppSheetSelectMode mode)
{
  int cursor_node = -1;
  GtkTreePath *old_cursor_path = NULL;
  GtkTreePath *cursor_path = NULL;
  int start_cursor_node = -1;
  gint y;
  gint window_y;
  gint vertical_separator;

  if (!gtk_widget_has_focus (GTK_WIDGET (tree_view)))
    return;

  if (gtk_tree_row_reference_valid (tree_view->priv->cursor))
    old_cursor_path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);
  else
    /* This is sorta weird.  Focus in should give us a cursor */
    return;

  gtk_widget_style_get (GTK_WIDGET (tree_view), "vertical-separator", &vertical_separator, NULL);
  _pspp_sheet_view_find_node (tree_view, old_cursor_path, &cursor_node);

  if (cursor_node < 0)
    {
      /* FIXME: we lost the cursor.  Should we try to get one? */
      gtk_tree_path_free (old_cursor_path);
      return;
    }

  y = pspp_sheet_view_node_find_offset (tree_view, cursor_node);
  window_y = RBTREE_Y_TO_TREE_WINDOW_Y (tree_view, y);
  y += tree_view->priv->cursor_offset;
  y += count * (int)gtk_adjustment_get_page_increment (tree_view->priv->vadjustment);
  y = CLAMP (y, (gint)gtk_adjustment_get_lower (tree_view->priv->vadjustment),  (gint)gtk_adjustment_get_upper (tree_view->priv->vadjustment) - vertical_separator);

  if (y >= tree_view->priv->height)
    y = tree_view->priv->height - 1;

  tree_view->priv->cursor_offset =
    pspp_sheet_view_find_offset (tree_view, y, &cursor_node);

  if (tree_view->priv->cursor_offset > BACKGROUND_HEIGHT (tree_view))
    {
      cursor_node = pspp_sheet_view_node_next (tree_view, cursor_node);
      tree_view->priv->cursor_offset -= BACKGROUND_HEIGHT (tree_view);
    }

  y -= tree_view->priv->cursor_offset;
  cursor_path = _pspp_sheet_view_find_path (tree_view, cursor_node);

  start_cursor_node = cursor_node;

  if (! search_first_focusable_path (tree_view, &cursor_path,
				     (count != -1),
				     &cursor_node))
    {
      /* It looks like we reached the end of the view without finding
       * a focusable row.  We will step backwards to find the last
       * focusable row.
       */
      cursor_node = start_cursor_node;
      cursor_path = _pspp_sheet_view_find_path (tree_view, cursor_node);

      search_first_focusable_path (tree_view, &cursor_path,
				   (count == -1),
				   &cursor_node);
    }

  if (!cursor_path)
    goto cleanup;

  /* update y */
  y = pspp_sheet_view_node_find_offset (tree_view, cursor_node);

  pspp_sheet_view_real_set_cursor (tree_view, cursor_path, TRUE, FALSE, mode);

  y -= window_y;
  pspp_sheet_view_scroll_to_point (tree_view, -1, y);
  pspp_sheet_view_clamp_node_visible (tree_view, cursor_node);
  _pspp_sheet_view_queue_draw_node (tree_view, cursor_node, NULL);

  if (!gtk_tree_path_compare (old_cursor_path, cursor_path))
    gtk_widget_error_bell (GTK_WIDGET (tree_view));

  gtk_widget_grab_focus (GTK_WIDGET (tree_view));

cleanup:
  gtk_tree_path_free (old_cursor_path);
  gtk_tree_path_free (cursor_path);
}

static void
pspp_sheet_view_move_cursor_left_right (PsppSheetView *tree_view,
                                        gint         count,
                                        PsppSheetSelectMode mode)
{
  int cursor_node = -1;
  GtkTreePath *cursor_path = NULL;
  PsppSheetViewColumn *column;
  GtkTreeIter iter;
  GList *list;
  gboolean found_column = FALSE;
  gboolean rtl;

  rtl = (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL);

  if (!gtk_widget_has_focus (GTK_WIDGET (tree_view)))
    return;

  if (gtk_tree_row_reference_valid (tree_view->priv->cursor))
    cursor_path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);
  else
    return;

  _pspp_sheet_view_find_node (tree_view, cursor_path, &cursor_node);
  if (cursor_node < 0)
    return;
  if (gtk_tree_model_get_iter (tree_view->priv->model, &iter, cursor_path) == FALSE)
    {
      gtk_tree_path_free (cursor_path);
      return;
    }
  gtk_tree_path_free (cursor_path);

  list = rtl ? g_list_last (tree_view->priv->columns) : g_list_first (tree_view->priv->columns);
  if (tree_view->priv->focus_column)
    {
      for (; list; list = (rtl ? list->prev : list->next))
	{
	  if (list->data == tree_view->priv->focus_column)
	    break;
	}
    }

  while (list)
    {
      gboolean left, right;

      column = list->data;
      if (column->visible == FALSE || column->row_head)
	goto loop_end;

      pspp_sheet_view_column_cell_set_cell_data (column,
					       tree_view->priv->model,
					       &iter);

      if (rtl)
        {
	  right = list->prev ? TRUE : FALSE;
	  left = list->next ? TRUE : FALSE;
	}
      else
        {
	  left = list->prev ? TRUE : FALSE;
	  right = list->next ? TRUE : FALSE;
        }

      if (_pspp_sheet_view_column_cell_focus (column, count, left, right))
	{
	  tree_view->priv->focus_column = column;
	  found_column = TRUE;
	  break;
	}
    loop_end:
      if (count == 1)
	list = rtl ? list->prev : list->next;
      else
	list = rtl ? list->next : list->prev;
    }

  if (found_column)
    {
      _pspp_sheet_view_queue_draw_node (tree_view, cursor_node, NULL);
      g_signal_emit (tree_view, tree_view_signals[CURSOR_CHANGED], 0);
      gtk_widget_grab_focus (GTK_WIDGET (tree_view));
    }
  else
    {
      gtk_widget_error_bell (GTK_WIDGET (tree_view));
    }

  pspp_sheet_view_clamp_column_visible (tree_view,
				      tree_view->priv->focus_column, TRUE);
}

static void
pspp_sheet_view_move_cursor_line_start_end (PsppSheetView *tree_view,
                                            gint         count,
                                            PsppSheetSelectMode mode)
{
  int cursor_node = -1;
  GtkTreePath *cursor_path = NULL;
  PsppSheetViewColumn *column;
  PsppSheetViewColumn *found_column;
  GtkTreeIter iter;
  GList *list;
  gboolean rtl;

  rtl = (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL);

  if (!gtk_widget_has_focus (GTK_WIDGET (tree_view)))
    return;

  if (gtk_tree_row_reference_valid (tree_view->priv->cursor))
    cursor_path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);
  else
    return;

  _pspp_sheet_view_find_node (tree_view, cursor_path, &cursor_node);
  if (cursor_node < 0)
    return;
  if (gtk_tree_model_get_iter (tree_view->priv->model, &iter, cursor_path) == FALSE)
    {
      gtk_tree_path_free (cursor_path);
      return;
    }
  gtk_tree_path_free (cursor_path);

  list = rtl ? g_list_last (tree_view->priv->columns) : g_list_first (tree_view->priv->columns);
  if (tree_view->priv->focus_column)
    {
      for (; list; list = (rtl ? list->prev : list->next))
	{
	  if (list->data == tree_view->priv->focus_column)
	    break;
	}
    }

  found_column = NULL;
  while (list)
    {
      gboolean left, right;

      column = list->data;
      if (column->visible == FALSE || column->row_head)
	goto loop_end;

      pspp_sheet_view_column_cell_set_cell_data (column,
					       tree_view->priv->model,
					       &iter);

      if (rtl)
        {
	  right = list->prev ? TRUE : FALSE;
	  left = list->next ? TRUE : FALSE;
	}
      else
        {
	  left = list->prev ? TRUE : FALSE;
	  right = list->next ? TRUE : FALSE;
        }

      if (column->tabbable
          && _pspp_sheet_view_column_cell_focus (column, count, left, right))
        found_column = column;

    loop_end:
      if (count == 1)
	list = rtl ? list->prev : list->next;
      else
	list = rtl ? list->next : list->prev;
    }

  if (found_column)
    {
      tree_view->priv->focus_column = found_column;
      _pspp_sheet_view_queue_draw_node (tree_view, cursor_node, NULL);
      g_signal_emit (tree_view, tree_view_signals[CURSOR_CHANGED], 0);
      gtk_widget_grab_focus (GTK_WIDGET (tree_view));
    }

  pspp_sheet_view_clamp_column_visible (tree_view,
				      tree_view->priv->focus_column, TRUE);
}

static gboolean
try_move_cursor_tab (PsppSheetView *tree_view,
                     gboolean start_at_focus_column,
                     gint count)
{
  PsppSheetViewColumn *column;
  GtkTreeIter iter;
  int cursor_node = -1;
  GtkTreePath *cursor_path = NULL;
  gboolean rtl;
  GList *list;

  if (gtk_tree_row_reference_valid (tree_view->priv->cursor))
    cursor_path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);
  else
    return TRUE;

  _pspp_sheet_view_find_node (tree_view, cursor_path, &cursor_node);
  if (cursor_node < 0)
    return TRUE;
  if (gtk_tree_model_get_iter (tree_view->priv->model, &iter, cursor_path) == FALSE)
    {
      gtk_tree_path_free (cursor_path);
      return TRUE;
    }
  gtk_tree_path_free (cursor_path);

  rtl = gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL;
  if (start_at_focus_column)
    {
      list = (rtl
              ? g_list_last (tree_view->priv->columns)
              : g_list_first (tree_view->priv->columns));
      if (tree_view->priv->focus_column)
        {
          for (; list; list = (rtl ? list->prev : list->next))
            {
              if (list->data == tree_view->priv->focus_column)
                break;
            }
        }
    }
  else
    {
      list = (rtl ^ (count == 1)
              ? g_list_first (tree_view->priv->columns)
              : g_list_last (tree_view->priv->columns));
    }

  while (list)
    {
      gboolean left, right;

      column = list->data;
      if (column->visible == FALSE || !column->tabbable)
	goto loop_end;

      pspp_sheet_view_column_cell_set_cell_data (column,
                                                 tree_view->priv->model,
                                                 &iter);

      if (rtl)
        {
	  right = list->prev ? TRUE : FALSE;
	  left = list->next ? TRUE : FALSE;
	}
      else
        {
	  left = list->prev ? TRUE : FALSE;
	  right = list->next ? TRUE : FALSE;
        }

      if (column->tabbable
          && _pspp_sheet_view_column_cell_focus (column, count, left, right))
	{
	  tree_view->priv->focus_column = column;
          _pspp_sheet_view_queue_draw_node (tree_view, cursor_node, NULL);
          g_signal_emit (tree_view, tree_view_signals[CURSOR_CHANGED], 0);
          gtk_widget_grab_focus (GTK_WIDGET (tree_view));
          return TRUE;
	}
    loop_end:
      if (count == 1)
	list = rtl ? list->prev : list->next;
      else
	list = rtl ? list->next : list->prev;
    }

  return FALSE;
}

static void
pspp_sheet_view_move_cursor_tab (PsppSheetView *tree_view,
                                 gint         count)
{
  if (!gtk_widget_has_focus (GTK_WIDGET (tree_view)))
    return;

  if (!try_move_cursor_tab (tree_view, TRUE, count))
    {
      if (pspp_sheet_view_move_cursor_up_down (tree_view, count, 0)
          && !try_move_cursor_tab (tree_view, FALSE, count))
        gtk_widget_error_bell (GTK_WIDGET (tree_view));
    }

  pspp_sheet_view_clamp_column_visible (tree_view,
                                        tree_view->priv->focus_column, TRUE);
}

static void
pspp_sheet_view_move_cursor_start_end (PsppSheetView *tree_view,
                                       gint         count,
                                       PsppSheetSelectMode mode)
{
  int cursor_node;
  GtkTreePath *path;
  GtkTreePath *old_path;

  if (!gtk_widget_has_focus (GTK_WIDGET (tree_view)))
    return;

  g_return_if_fail (tree_view->priv->row_count > 0);

  pspp_sheet_view_get_cursor (tree_view, &old_path, NULL);

  if (count == -1)
    {
      /* Now go forward to find the first focusable row. */
      path = _pspp_sheet_view_find_path (tree_view, 0);
      search_first_focusable_path (tree_view, &path,
				   TRUE, &cursor_node);
    }
  else
    {
      /* Now go backwards to find last focusable row. */
      path = _pspp_sheet_view_find_path (tree_view, tree_view->priv->row_count - 1);
      search_first_focusable_path (tree_view, &path,
				   FALSE, &cursor_node);
    }

  if (!path)
    goto cleanup;

  if (gtk_tree_path_compare (old_path, path))
    {
      pspp_sheet_view_real_set_cursor (tree_view, path, TRUE, TRUE, mode);
      gtk_widget_grab_focus (GTK_WIDGET (tree_view));
    }
  else
    {
      gtk_widget_error_bell (GTK_WIDGET (tree_view));
    }

cleanup:
  gtk_tree_path_free (old_path);
  gtk_tree_path_free (path);
}

static gboolean
pspp_sheet_view_real_select_all (PsppSheetView *tree_view)
{
  if (!gtk_widget_has_focus (GTK_WIDGET (tree_view)))
    return FALSE;

  if (tree_view->priv->selection->type != PSPP_SHEET_SELECTION_MULTIPLE &&
      tree_view->priv->selection->type != PSPP_SHEET_SELECTION_RECTANGLE)
    return FALSE;

  pspp_sheet_selection_select_all (tree_view->priv->selection);

  return TRUE;
}

static gboolean
pspp_sheet_view_real_unselect_all (PsppSheetView *tree_view)
{
  if (!gtk_widget_has_focus (GTK_WIDGET (tree_view)))
    return FALSE;

  if (tree_view->priv->selection->type != PSPP_SHEET_SELECTION_MULTIPLE &&
      tree_view->priv->selection->type != PSPP_SHEET_SELECTION_RECTANGLE)
    return FALSE;

  pspp_sheet_selection_unselect_all (tree_view->priv->selection);

  return TRUE;
}

static gboolean
pspp_sheet_view_real_select_cursor_row (PsppSheetView *tree_view,
                                        gboolean     start_editing,
                                        PsppSheetSelectMode mode)
{
  int new_node = -1;
  int cursor_node = -1;
  GtkTreePath *cursor_path = NULL;

  if (!gtk_widget_has_focus (GTK_WIDGET (tree_view)))
    return FALSE;

  if (tree_view->priv->cursor)
    cursor_path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);

  if (cursor_path == NULL)
    return FALSE;

  _pspp_sheet_view_find_node (tree_view, cursor_path,
                              &cursor_node);

  if (cursor_node < 0)
    {
      gtk_tree_path_free (cursor_path);
      return FALSE;
    }

  if (!(mode & PSPP_SHEET_SELECT_MODE_EXTEND) && start_editing &&
      tree_view->priv->focus_column)
    {
      if (pspp_sheet_view_start_editing (tree_view, cursor_path))
	{
	  gtk_tree_path_free (cursor_path);
	  return TRUE;
	}
    }

  _pspp_sheet_selection_internal_select_node (tree_view->priv->selection,
					    cursor_node,
					    cursor_path,
                                            mode,
					    FALSE);

  /* We bail out if the original (tree, node) don't exist anymore after
   * handling the selection-changed callback.  We do return TRUE because
   * the key press has been handled at this point.
   */
  _pspp_sheet_view_find_node (tree_view, cursor_path, &new_node);

  if (cursor_node != new_node)
    return FALSE;

  pspp_sheet_view_clamp_node_visible (tree_view, cursor_node);

  gtk_widget_grab_focus (GTK_WIDGET (tree_view));
  _pspp_sheet_view_queue_draw_node (tree_view, cursor_node, NULL);

  if (!(mode & PSPP_SHEET_SELECT_MODE_EXTEND))
    pspp_sheet_view_row_activated (tree_view, cursor_path,
                                 tree_view->priv->focus_column);
    
  gtk_tree_path_free (cursor_path);

  return TRUE;
}

static gboolean
pspp_sheet_view_real_toggle_cursor_row (PsppSheetView *tree_view)
{
  int new_node = -1;
  int cursor_node = -1;
  GtkTreePath *cursor_path = NULL;

  if (!gtk_widget_has_focus (GTK_WIDGET (tree_view)))
    return FALSE;

  cursor_path = NULL;
  if (tree_view->priv->cursor)
    cursor_path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);

  if (cursor_path == NULL)
    return FALSE;

  _pspp_sheet_view_find_node (tree_view, cursor_path, &cursor_node);
  if (cursor_node < 0)
    {
      gtk_tree_path_free (cursor_path);
      return FALSE;
    }

  _pspp_sheet_selection_internal_select_node (tree_view->priv->selection,
					    cursor_node,
					    cursor_path,
                                            PSPP_SHEET_SELECT_MODE_TOGGLE,
					    FALSE);

  /* We bail out if the original (tree, node) don't exist anymore after
   * handling the selection-changed callback.  We do return TRUE because
   * the key press has been handled at this point.
   */
  _pspp_sheet_view_find_node (tree_view, cursor_path, &new_node);

  if (cursor_node != new_node)
    return FALSE;

  pspp_sheet_view_clamp_node_visible (tree_view, cursor_node);

  gtk_widget_grab_focus (GTK_WIDGET (tree_view));
  pspp_sheet_view_queue_draw_path (tree_view, cursor_path, NULL);
  gtk_tree_path_free (cursor_path);

  return TRUE;
}

static gboolean
pspp_sheet_view_search_entry_flush_timeout (PsppSheetView *tree_view)
{
  pspp_sheet_view_search_dialog_hide (tree_view->priv->search_window, tree_view);
  tree_view->priv->typeselect_flush_timeout = 0;

  return FALSE;
}

/* Cut and paste from gtkwindow.c */
static void
send_focus_change (GtkWidget *widget,
		   gboolean   in)
{
  GdkEvent *fevent = gdk_event_new (GDK_FOCUS_CHANGE);

  fevent->focus_change.type = GDK_FOCUS_CHANGE;
  fevent->focus_change.window = g_object_ref (gtk_widget_get_window (widget));
  fevent->focus_change.in = in;
  
  gtk_widget_send_focus_change (widget, fevent);
  gdk_event_free (fevent);
}

static void
pspp_sheet_view_ensure_interactive_directory (PsppSheetView *tree_view)
{
  GtkWidget *frame, *vbox, *toplevel;
  GdkScreen *screen;

  if (tree_view->priv->search_custom_entry_set)
    return;

  toplevel = gtk_widget_get_toplevel (GTK_WIDGET (tree_view));
  screen = gtk_widget_get_screen (GTK_WIDGET (tree_view));

   if (tree_view->priv->search_window != NULL)
     {
       if (gtk_window_get_group (GTK_WINDOW (toplevel)))
	 gtk_window_group_add_window (gtk_window_get_group (GTK_WINDOW (toplevel)),
				      GTK_WINDOW (tree_view->priv->search_window));
       else if (gtk_window_get_group (GTK_WINDOW (tree_view->priv->search_window)))
	 gtk_window_group_remove_window (gtk_window_get_group (GTK_WINDOW (tree_view->priv->search_window)),
					 GTK_WINDOW (tree_view->priv->search_window));
       gtk_window_set_screen (GTK_WINDOW (tree_view->priv->search_window), screen);
       return;
     }
   
  tree_view->priv->search_window = gtk_window_new (GTK_WINDOW_POPUP);
  gtk_window_set_screen (GTK_WINDOW (tree_view->priv->search_window), screen);

  if (gtk_window_get_group (GTK_WINDOW (toplevel)))
    gtk_window_group_add_window (gtk_window_get_group (GTK_WINDOW (toplevel)),
				 GTK_WINDOW (tree_view->priv->search_window));

  gtk_window_set_type_hint (GTK_WINDOW (tree_view->priv->search_window),
			    GDK_WINDOW_TYPE_HINT_UTILITY);
  gtk_window_set_modal (GTK_WINDOW (tree_view->priv->search_window), TRUE);
  g_signal_connect (tree_view->priv->search_window, "delete-event",
		    G_CALLBACK (pspp_sheet_view_search_delete_event),
		    tree_view);
  g_signal_connect (tree_view->priv->search_window, "key-press-event",
		    G_CALLBACK (pspp_sheet_view_search_key_press_event),
		    tree_view);
  g_signal_connect (tree_view->priv->search_window, "button-press-event",
		    G_CALLBACK (pspp_sheet_view_search_button_press_event),
		    tree_view);
  g_signal_connect (tree_view->priv->search_window, "scroll-event",
		    G_CALLBACK (pspp_sheet_view_search_scroll_event),
		    tree_view);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_widget_show (frame);
  gtk_container_add (GTK_CONTAINER (tree_view->priv->search_window), frame);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 3);

  /* add entry */
  tree_view->priv->search_entry = gtk_entry_new ();
  gtk_widget_show (tree_view->priv->search_entry);
  g_signal_connect (tree_view->priv->search_entry, "populate-popup",
		    G_CALLBACK (pspp_sheet_view_search_disable_popdown),
		    tree_view);
  g_signal_connect (tree_view->priv->search_entry,
		    "activate", G_CALLBACK (pspp_sheet_view_search_activate),
		    tree_view);

#if GTK3_TRANSITION
  g_signal_connect (GTK_ENTRY (tree_view->priv->search_entry)->im_context,
		    "preedit-changed",
		    G_CALLBACK (pspp_sheet_view_search_preedit_changed),
		    tree_view);
#endif

  gtk_container_add (GTK_CONTAINER (vbox),
		     tree_view->priv->search_entry);

  gtk_widget_realize (tree_view->priv->search_entry);
}

/* Pops up the interactive search entry.  If keybinding is TRUE then the user
 * started this by typing the start_interactive_search keybinding.  Otherwise, it came from 
 */
static gboolean
pspp_sheet_view_real_start_interactive_search (PsppSheetView *tree_view,
					     gboolean     keybinding)
{
  /* We only start interactive search if we have focus or the columns
   * have focus.  If one of our children have focus, we don't want to
   * start the search.
   */
  GList *list;
  gboolean found_focus = FALSE;
  GtkWidgetClass *entry_parent_class;
  
  if (!tree_view->priv->enable_search && !keybinding)
    return FALSE;

  if (tree_view->priv->search_custom_entry_set)
    return FALSE;

  if (tree_view->priv->search_window != NULL &&
      gtk_widget_get_visible (tree_view->priv->search_window))
    return TRUE;

  for (list = tree_view->priv->columns; list; list = list->next)
    {
      PsppSheetViewColumn *column;

      column = list->data;
      if (! column->visible)
	continue;

      if (column->button && gtk_widget_has_focus (column->button))
	{
	  found_focus = TRUE;
	  break;
	}
    }
  
  if (gtk_widget_has_focus (GTK_WIDGET (tree_view)))
    found_focus = TRUE;

  if (!found_focus)
    return FALSE;

  if (tree_view->priv->search_column < 0)
    return FALSE;

  pspp_sheet_view_ensure_interactive_directory (tree_view);

  if (keybinding)
    gtk_entry_set_text (GTK_ENTRY (tree_view->priv->search_entry), "");

  /* done, show it */
  tree_view->priv->search_position_func (tree_view, tree_view->priv->search_window, tree_view->priv->search_position_user_data);
  gtk_widget_show (tree_view->priv->search_window);
  if (tree_view->priv->search_entry_changed_id == 0)
    {
      tree_view->priv->search_entry_changed_id =
	g_signal_connect (tree_view->priv->search_entry, "changed",
			  G_CALLBACK (pspp_sheet_view_search_init),
			  tree_view);
    }

  tree_view->priv->typeselect_flush_timeout =
    gdk_threads_add_timeout (PSPP_SHEET_VIEW_SEARCH_DIALOG_TIMEOUT,
		   (GSourceFunc) pspp_sheet_view_search_entry_flush_timeout,
		   tree_view);

  /* Grab focus will select all the text.  We don't want that to happen, so we
   * call the parent instance and bypass the selection change.  This is probably
   * really non-kosher. */
  entry_parent_class = g_type_class_peek_parent (GTK_ENTRY_GET_CLASS (tree_view->priv->search_entry));
  (entry_parent_class->grab_focus) (tree_view->priv->search_entry);

  /* send focus-in event */
  send_focus_change (tree_view->priv->search_entry, TRUE);

  /* search first matching iter */
  pspp_sheet_view_search_init (tree_view->priv->search_entry, tree_view);

  return TRUE;
}

static gboolean
pspp_sheet_view_start_interactive_search (PsppSheetView *tree_view)
{
  return pspp_sheet_view_real_start_interactive_search (tree_view, TRUE);
}

/* this function returns the new width of the column being resized given
 * the column and x position of the cursor; the x cursor position is passed
 * in as a pointer and automagicly corrected if it's beyond min/max limits
 */
static gint
pspp_sheet_view_new_column_width (PsppSheetView *tree_view,
				gint       i,
				gint      *x)
{
  PsppSheetViewColumn *column;
  gint width;
  gboolean rtl;

  /* first translate the x position from gtk_widget_get_window (widget)
   * to clist->clist_window
   */
  rtl = (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL);
  column = g_list_nth (tree_view->priv->columns, i)->data;
  width = rtl ? (column->allocation.x + column->allocation.width - *x) : (*x - column->allocation.x);
 
  /* Clamp down the value */
  if (column->min_width == -1)
    width = MAX (column->button_request, width);
  else
    width = MAX (column->min_width, width);
  if (column->max_width != -1)
    width = MIN (width, column->max_width);

  *x = rtl ? (column->allocation.x + column->allocation.width - width) : (column->allocation.x + width);
 
  return width;
}


/* FIXME this adjust_allocation is a big cut-and-paste from
 * GtkCList, needs to be some "official" way to do this
 * factored out.
 */
typedef struct
{
  GdkWindow *window;
  int dx;
  int dy;
} ScrollData;

/* The window to which gtk_widget_get_window (widget) is relative */
#define ALLOCATION_WINDOW(widget)		\
   (!gtk_widget_get_has_window (widget) ?		\
    gtk_widget_get_window (widget) :                          \
    gdk_window_get_parent (gtk_widget_get_window (widget)))

static void
adjust_allocation_recurse (GtkWidget *widget,
			   gpointer   data)
{
  ScrollData *scroll_data = data;
  GtkAllocation allocation;
  gtk_widget_get_allocation (widget, &allocation);
  /* Need to really size allocate instead of just poking
   * into widget->allocation if the widget is not realized.
   * FIXME someone figure out why this was.
   */
  if (!gtk_widget_get_realized (widget))
    {
      if (gtk_widget_get_visible (widget))
	{
	  GdkRectangle tmp_rectangle = allocation;
	  tmp_rectangle.x += scroll_data->dx;
          tmp_rectangle.y += scroll_data->dy;
          
	  gtk_widget_size_allocate (widget, &tmp_rectangle);
	}
    }
  else
    {
      if (ALLOCATION_WINDOW (widget) == scroll_data->window)
	{
	  allocation.x += scroll_data->dx;
          allocation.y += scroll_data->dy;
          
	  if (GTK_IS_CONTAINER (widget))
	    gtk_container_forall (GTK_CONTAINER (widget),
				  adjust_allocation_recurse,
				  data);
	}
    }
}

static void
adjust_allocation (GtkWidget *widget,
		   int        dx,
                   int        dy)
{
  ScrollData scroll_data;

  if (gtk_widget_get_realized (widget))
    scroll_data.window = ALLOCATION_WINDOW (widget);
  else
    scroll_data.window = NULL;
    
  scroll_data.dx = dx;
  scroll_data.dy = dy;
  
  adjust_allocation_recurse (widget, &scroll_data);
}

void 
pspp_sheet_view_column_update_button (PsppSheetViewColumn *tree_column);

/* Callbacks */
static void
pspp_sheet_view_adjustment_changed (GtkAdjustment *adjustment,
				  PsppSheetView   *tree_view)
{
  if (gtk_widget_get_realized (GTK_WIDGET (tree_view)))
    {
      GList *list;
      gint dy;
	
      gdk_window_move (tree_view->priv->bin_window,
		       - gtk_adjustment_get_value (tree_view->priv->hadjustment),
		       TREE_VIEW_HEADER_HEIGHT (tree_view));
      gdk_window_move (tree_view->priv->header_window,
		       - gtk_adjustment_get_value (tree_view->priv->hadjustment),
		       0);
      dy = tree_view->priv->dy - (int) gtk_adjustment_get_value (tree_view->priv->vadjustment);
      if (dy)
	{
          update_prelight (tree_view,
                           tree_view->priv->event_last_x,
                           tree_view->priv->event_last_y - dy);

	  if (tree_view->priv->edited_column &&
              GTK_IS_WIDGET (tree_view->priv->edited_column->editable_widget))
	    {
	      GList *list;
	      GtkWidget *widget;
	      PsppSheetViewChild *child = NULL;

	      widget = GTK_WIDGET (tree_view->priv->edited_column->editable_widget);
	      adjust_allocation (widget, 0, dy); 
	      
	      for (list = tree_view->priv->children; list; list = list->next)
		{
		  child = (PsppSheetViewChild *)list->data;
		  if (child->widget == widget)
		    {
		      child->y += dy;
		      break;
		    }
		}
	    }
	}
      gdk_window_scroll (tree_view->priv->bin_window, 0, dy);

      if (tree_view->priv->dy != (int) gtk_adjustment_get_value (tree_view->priv->vadjustment))
        {
          /* update our dy and top_row */
          tree_view->priv->dy = (int) gtk_adjustment_get_value (tree_view->priv->vadjustment);

          if (!tree_view->priv->in_top_row_to_dy)
            pspp_sheet_view_dy_to_top_row (tree_view);
	}

      for (list = tree_view->priv->columns; list; list = list->next)
        {
          PsppSheetViewColumn *column = list->data;
          GtkAllocation *col_allocation = &column->allocation;
	  GtkAllocation widget_allocation;
	  gtk_widget_get_allocation (GTK_WIDGET (tree_view), &widget_allocation);

          if (span_intersects (col_allocation->x, col_allocation->width,
                               gtk_adjustment_get_value (tree_view->priv->hadjustment),
                               widget_allocation.width))
            {
              pspp_sheet_view_column_set_need_button (column, TRUE);
              if (!column->button)
                pspp_sheet_view_column_update_button (column);
            }
        }
    }
}



/* Public methods
 */

/**
 * pspp_sheet_view_new:
 *
 * Creates a new #PsppSheetView widget.
 *
 * Return value: A newly created #PsppSheetView widget.
 **/
GtkWidget *
pspp_sheet_view_new (void)
{
  return g_object_new (PSPP_TYPE_SHEET_VIEW, NULL);
}

/**
 * pspp_sheet_view_new_with_model:
 * @model: the model.
 *
 * Creates a new #PsppSheetView widget with the model initialized to @model.
 *
 * Return value: A newly created #PsppSheetView widget.
 **/
GtkWidget *
pspp_sheet_view_new_with_model (GtkTreeModel *model)
{
  return g_object_new (PSPP_TYPE_SHEET_VIEW, "model", model, NULL);
}

/* Public Accessors
 */

/**
 * pspp_sheet_view_get_model:
 * @tree_view: a #PsppSheetView
 *
 * Returns the model the #PsppSheetView is based on.  Returns %NULL if the
 * model is unset.
 *
 * Return value: A #GtkTreeModel, or %NULL if none is currently being used.
 **/
GtkTreeModel *
pspp_sheet_view_get_model (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), NULL);

  return tree_view->priv->model;
}

/**
 * pspp_sheet_view_set_model:
 * @tree_view: A #GtkTreeNode.
 * @model: (allow-none): The model.
 *
 * Sets the model for a #PsppSheetView.  If the @tree_view already has a model
 * set, it will remove it before setting the new model.  If @model is %NULL,
 * then it will unset the old model.
 **/
void
pspp_sheet_view_set_model (PsppSheetView  *tree_view,
			 GtkTreeModel *model)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (model == NULL || GTK_IS_TREE_MODEL (model));

  if (model == tree_view->priv->model)
    return;

  if (tree_view->priv->scroll_to_path)
    {
      gtk_tree_row_reference_free (tree_view->priv->scroll_to_path);
      tree_view->priv->scroll_to_path = NULL;
    }

  if (tree_view->priv->model)
    {
      GList *tmplist = tree_view->priv->columns;

      if (tree_view->priv->selected)
        range_tower_set0 (tree_view->priv->selected, 0, ULONG_MAX);
      pspp_sheet_view_stop_editing (tree_view, TRUE);

      g_signal_handlers_disconnect_by_func (tree_view->priv->model,
					    pspp_sheet_view_row_changed,
					    tree_view);
      g_signal_handlers_disconnect_by_func (tree_view->priv->model,
					    pspp_sheet_view_row_inserted,
					    tree_view);
      g_signal_handlers_disconnect_by_func (tree_view->priv->model,
					    pspp_sheet_view_row_deleted,
					    tree_view);
      g_signal_handlers_disconnect_by_func (tree_view->priv->model,
					    pspp_sheet_view_rows_reordered,
					    tree_view);

      for (; tmplist; tmplist = tmplist->next)
	_pspp_sheet_view_column_unset_model (tmplist->data,
					   tree_view->priv->model);

      tree_view->priv->prelight_node = -1;

      gtk_tree_row_reference_free (tree_view->priv->drag_dest_row);
      tree_view->priv->drag_dest_row = NULL;
      gtk_tree_row_reference_free (tree_view->priv->cursor);
      tree_view->priv->cursor = NULL;
      gtk_tree_row_reference_free (tree_view->priv->anchor);
      tree_view->priv->anchor = NULL;
      gtk_tree_row_reference_free (tree_view->priv->top_row);
      tree_view->priv->top_row = NULL;
      gtk_tree_row_reference_free (tree_view->priv->scroll_to_path);
      tree_view->priv->scroll_to_path = NULL;

      tree_view->priv->scroll_to_column = NULL;

      g_object_unref (tree_view->priv->model);

      tree_view->priv->search_column = -1;
      tree_view->priv->fixed_height = -1;
      tree_view->priv->dy = tree_view->priv->top_row_dy = 0;
      tree_view->priv->last_button_x = -1;
      tree_view->priv->last_button_y = -1;
    }

  tree_view->priv->model = model;

  if (tree_view->priv->model)
    {
      gint i;

      if (tree_view->priv->search_column == -1)
	{
	  for (i = 0; i < gtk_tree_model_get_n_columns (model); i++)
	    {
	      GType type = gtk_tree_model_get_column_type (model, i);

	      if (g_value_type_transformable (type, G_TYPE_STRING))
		{
		  tree_view->priv->search_column = i;
		  break;
		}
	    }
	}

      g_object_ref (tree_view->priv->model);
      g_signal_connect (tree_view->priv->model,
			"row-changed",
			G_CALLBACK (pspp_sheet_view_row_changed),
			tree_view);
      g_signal_connect (tree_view->priv->model,
			"row-inserted",
			G_CALLBACK (pspp_sheet_view_row_inserted),
			tree_view);
      g_signal_connect (tree_view->priv->model,
			"row-deleted",
			G_CALLBACK (pspp_sheet_view_row_deleted),
			tree_view);
      g_signal_connect (tree_view->priv->model,
			"rows-reordered",
			G_CALLBACK (pspp_sheet_view_rows_reordered),
			tree_view);

      tree_view->priv->row_count = gtk_tree_model_iter_n_children (tree_view->priv->model, NULL);

      /*  FIXME: do I need to do this? pspp_sheet_view_create_buttons (tree_view); */
      install_presize_handler (tree_view);
    }

  g_object_notify (G_OBJECT (tree_view), "model");

  if (tree_view->priv->selection)
    _pspp_sheet_selection_emit_changed (tree_view->priv->selection);

  if (gtk_widget_get_realized (GTK_WIDGET (tree_view)))
    gtk_widget_queue_resize (GTK_WIDGET (tree_view));
}

/**
 * pspp_sheet_view_get_selection:
 * @tree_view: A #PsppSheetView.
 *
 * Gets the #PsppSheetSelection associated with @tree_view.
 *
 * Return value: A #PsppSheetSelection object.
 **/
PsppSheetSelection *
pspp_sheet_view_get_selection (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), NULL);

  return tree_view->priv->selection;
}

/**
 * pspp_sheet_view_get_hadjustment:
 * @tree_view: A #PsppSheetView
 *
 * Gets the #GtkAdjustment currently being used for the horizontal aspect.
 *
 * Return value: A #GtkAdjustment object, or %NULL if none is currently being
 * used.
 **/
GtkAdjustment *
pspp_sheet_view_get_hadjustment (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), NULL);

  if (tree_view->priv->hadjustment == NULL)
    pspp_sheet_view_set_hadjustment (tree_view, NULL);

  return tree_view->priv->hadjustment;
}

/**
 * pspp_sheet_view_set_hadjustment:
 * @tree_view: A #PsppSheetView
 * @adjustment: (allow-none): The #GtkAdjustment to set, or %NULL
 *
 * Sets the #GtkAdjustment for the current horizontal aspect.
 **/
void
pspp_sheet_view_set_hadjustment (PsppSheetView   *tree_view,
			       GtkAdjustment *adjustment)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  pspp_sheet_view_set_adjustments (tree_view,
				 adjustment,
				 tree_view->priv->vadjustment);

  g_object_notify (G_OBJECT (tree_view), "hadjustment");
}

/**
 * pspp_sheet_view_get_vadjustment:
 * @tree_view: A #PsppSheetView
 *
 * Gets the #GtkAdjustment currently being used for the vertical aspect.
 *
 * Return value: A #GtkAdjustment object, or %NULL if none is currently being
 * used.
 **/
GtkAdjustment *
pspp_sheet_view_get_vadjustment (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), NULL);

  if (tree_view->priv->vadjustment == NULL)
    pspp_sheet_view_set_vadjustment (tree_view, NULL);

  return tree_view->priv->vadjustment;
}

/**
 * pspp_sheet_view_set_vadjustment:
 * @tree_view: A #PsppSheetView
 * @adjustment: (allow-none): The #GtkAdjustment to set, or %NULL
 *
 * Sets the #GtkAdjustment for the current vertical aspect.
 **/
void
pspp_sheet_view_set_vadjustment (PsppSheetView   *tree_view,
			       GtkAdjustment *adjustment)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  pspp_sheet_view_set_adjustments (tree_view,
				 tree_view->priv->hadjustment,
				 adjustment);

  g_object_notify (G_OBJECT (tree_view), "vadjustment");
}

/* Column and header operations */

/**
 * pspp_sheet_view_get_headers_visible:
 * @tree_view: A #PsppSheetView.
 *
 * Returns %TRUE if the headers on the @tree_view are visible.
 *
 * Return value: Whether the headers are visible or not.
 **/
gboolean
pspp_sheet_view_get_headers_visible (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), FALSE);

  return PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_HEADERS_VISIBLE);
}

/**
 * pspp_sheet_view_set_headers_visible:
 * @tree_view: A #PsppSheetView.
 * @headers_visible: %TRUE if the headers are visible
 *
 * Sets the visibility state of the headers.
 **/
void
pspp_sheet_view_set_headers_visible (PsppSheetView *tree_view,
				   gboolean     headers_visible)
{
  gint x, y;
  GList *list;
  PsppSheetViewColumn *column;
  GtkAllocation allocation;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  gtk_widget_get_allocation (GTK_WIDGET (tree_view), &allocation);

  headers_visible = !! headers_visible;

  if (PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_HEADERS_VISIBLE) == headers_visible)
    return;

  if (headers_visible)
    PSPP_SHEET_VIEW_SET_FLAG (tree_view, PSPP_SHEET_VIEW_HEADERS_VISIBLE);
  else
    PSPP_SHEET_VIEW_UNSET_FLAG (tree_view, PSPP_SHEET_VIEW_HEADERS_VISIBLE);

  if (gtk_widget_get_realized (GTK_WIDGET (tree_view)))
    {
      gdk_window_get_position (tree_view->priv->bin_window, &x, &y);
      if (headers_visible)
	{
	  gdk_window_move_resize (tree_view->priv->bin_window, x, y  + TREE_VIEW_HEADER_HEIGHT (tree_view), 
				  tree_view->priv->width, allocation.height -  + TREE_VIEW_HEADER_HEIGHT (tree_view));

          if (gtk_widget_get_mapped (GTK_WIDGET (tree_view)))
            pspp_sheet_view_map_buttons (tree_view);
 	}
      else
	{
	  gdk_window_move_resize (tree_view->priv->bin_window, x, y, tree_view->priv->width, tree_view->priv->height);

	  for (list = tree_view->priv->columns; list; list = list->next)
	    {
	      column = list->data;
              if (column->button)
                gtk_widget_unmap (column->button);
	    }
	  gdk_window_hide (tree_view->priv->header_window);
	}
    }

  gtk_adjustment_set_page_size (tree_view->priv->vadjustment, allocation.height - TREE_VIEW_HEADER_HEIGHT (tree_view));
  gtk_adjustment_set_page_increment (tree_view->priv->vadjustment, (allocation.height - TREE_VIEW_HEADER_HEIGHT (tree_view)) / 2);
  gtk_adjustment_set_lower (tree_view->priv->vadjustment, 0);
  gtk_adjustment_set_upper (tree_view->priv->vadjustment, tree_view->priv->height);
  gtk_adjustment_changed (tree_view->priv->vadjustment);

  gtk_widget_queue_resize (GTK_WIDGET (tree_view));

  g_object_notify (G_OBJECT (tree_view), "headers-visible");
}

/**
 * pspp_sheet_view_columns_autosize:
 * @tree_view: A #PsppSheetView.
 *
 * Resizes all columns to their optimal width. Only works after the
 * treeview has been realized.
 **/
void
pspp_sheet_view_columns_autosize (PsppSheetView *tree_view)
{
  gboolean dirty = FALSE;
  GList *list;
  PsppSheetViewColumn *column;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  for (list = tree_view->priv->columns; list; list = list->next)
    {
      column = list->data;
      _pspp_sheet_view_column_cell_set_dirty (column);
      dirty = TRUE;
    }

  if (dirty)
    gtk_widget_queue_resize (GTK_WIDGET (tree_view));
}

/**
 * pspp_sheet_view_set_headers_clickable:
 * @tree_view: A #PsppSheetView.
 * @setting: %TRUE if the columns are clickable.
 *
 * Allow the column title buttons to be clicked.
 **/
void
pspp_sheet_view_set_headers_clickable (PsppSheetView *tree_view,
				     gboolean   setting)
{
  GList *list;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  for (list = tree_view->priv->columns; list; list = list->next)
    pspp_sheet_view_column_set_clickable (PSPP_SHEET_VIEW_COLUMN (list->data), setting);

  g_object_notify (G_OBJECT (tree_view), "headers-clickable");
}


/**
 * pspp_sheet_view_get_headers_clickable:
 * @tree_view: A #PsppSheetView.
 *
 * Returns whether all header columns are clickable.
 *
 * Return value: %TRUE if all header columns are clickable, otherwise %FALSE
 *
 * Since: 2.10
 **/
gboolean 
pspp_sheet_view_get_headers_clickable (PsppSheetView *tree_view)
{
  GList *list;
  
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), FALSE);

  for (list = tree_view->priv->columns; list; list = list->next)
    if (!PSPP_SHEET_VIEW_COLUMN (list->data)->clickable)
      return FALSE;

  return TRUE;
}

/**
 * pspp_sheet_view_set_rules_hint
 * @tree_view: a #PsppSheetView
 * @setting: %TRUE if the tree requires reading across rows
 *
 * This function tells GTK+ that the user interface for your
 * application requires users to read across tree rows and associate
 * cells with one another. By default, GTK+ will then render the tree
 * with alternating row colors. Do <emphasis>not</emphasis> use it
 * just because you prefer the appearance of the ruled tree; that's a
 * question for the theme. Some themes will draw tree rows in
 * alternating colors even when rules are turned off, and users who
 * prefer that appearance all the time can choose those themes. You
 * should call this function only as a <emphasis>semantic</emphasis>
 * hint to the theme engine that your tree makes alternating colors
 * useful from a functional standpoint (since it has lots of columns,
 * generally).
 *
 **/
void
pspp_sheet_view_set_rules_hint (PsppSheetView  *tree_view,
                              gboolean      setting)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  setting = setting != FALSE;

  if (tree_view->priv->has_rules != setting)
    {
      tree_view->priv->has_rules = setting;
      gtk_widget_queue_draw (GTK_WIDGET (tree_view));
    }

  g_object_notify (G_OBJECT (tree_view), "rules-hint");
}

/**
 * pspp_sheet_view_get_rules_hint
 * @tree_view: a #PsppSheetView
 *
 * Gets the setting set by pspp_sheet_view_set_rules_hint().
 *
 * Return value: %TRUE if rules are useful for the user of this tree
 **/
gboolean
pspp_sheet_view_get_rules_hint (PsppSheetView  *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), FALSE);

  return tree_view->priv->has_rules;
}

/* Public Column functions
 */

/**
 * pspp_sheet_view_append_column:
 * @tree_view: A #PsppSheetView.
 * @column: The #PsppSheetViewColumn to add.
 *
 * Appends @column to the list of columns.
 *
 * Return value: The number of columns in @tree_view after appending.
 **/
gint
pspp_sheet_view_append_column (PsppSheetView       *tree_view,
			     PsppSheetViewColumn *column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), -1);
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (column), -1);
  g_return_val_if_fail (column->tree_view == NULL, -1);

  return pspp_sheet_view_insert_column (tree_view, column, -1);
}


/**
 * pspp_sheet_view_remove_column:
 * @tree_view: A #PsppSheetView.
 * @column: The #PsppSheetViewColumn to remove.
 *
 * Removes @column from @tree_view.
 *
 * Return value: The number of columns in @tree_view after removing.
 **/
gint
pspp_sheet_view_remove_column (PsppSheetView       *tree_view,
                             PsppSheetViewColumn *column)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), -1);
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (column), -1);
  g_return_val_if_fail (column->tree_view == GTK_WIDGET (tree_view), -1);

  if (tree_view->priv->focus_column == column)
    tree_view->priv->focus_column = NULL;

  if (tree_view->priv->edited_column == column)
    {
      pspp_sheet_view_stop_editing (tree_view, TRUE);

      /* no need to, but just to be sure ... */
      tree_view->priv->edited_column = NULL;
    }

  _pspp_sheet_view_column_unset_tree_view (column);

  tree_view->priv->columns = g_list_remove (tree_view->priv->columns, column);
  tree_view->priv->n_columns--;

  if (gtk_widget_get_realized (GTK_WIDGET (tree_view)))
    {
      GList *list;

      _pspp_sheet_view_column_unrealize_button (column);
      for (list = tree_view->priv->columns; list; list = list->next)
	{
	  PsppSheetViewColumn *tmp_column;

	  tmp_column = PSPP_SHEET_VIEW_COLUMN (list->data);
	  if (tmp_column->visible)
	    _pspp_sheet_view_column_cell_set_dirty (tmp_column);
	}

      if (tree_view->priv->n_columns == 0 &&
	  pspp_sheet_view_get_headers_visible (tree_view) && 
	  tree_view->priv->header_window)
	gdk_window_hide (tree_view->priv->header_window);

      gtk_widget_queue_resize (GTK_WIDGET (tree_view));
    }

  g_object_unref (column);
  g_signal_emit (tree_view, tree_view_signals[COLUMNS_CHANGED], 0);

  return tree_view->priv->n_columns;
}

/**
 * pspp_sheet_view_insert_column:
 * @tree_view: A #PsppSheetView.
 * @column: The #PsppSheetViewColumn to be inserted.
 * @position: The position to insert @column in.
 *
 * This inserts the @column into the @tree_view at @position.  If @position is
 * -1, then the column is inserted at the end.
 *
 * Return value: The number of columns in @tree_view after insertion.
 **/
gint
pspp_sheet_view_insert_column (PsppSheetView       *tree_view,
                             PsppSheetViewColumn *column,
                             gint               position)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), -1);
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW_COLUMN (column), -1);
  g_return_val_if_fail (column->tree_view == NULL, -1);

  g_object_ref_sink (column);

  if (tree_view->priv->n_columns == 0 &&
      gtk_widget_get_realized (GTK_WIDGET (tree_view)) &&
      pspp_sheet_view_get_headers_visible (tree_view))
    {
      gdk_window_show (tree_view->priv->header_window);
    }

  tree_view->priv->columns = g_list_insert (tree_view->priv->columns,
					    column, position);
  tree_view->priv->n_columns++;

  _pspp_sheet_view_column_set_tree_view (column, tree_view);

  if (gtk_widget_get_realized (GTK_WIDGET (tree_view)))
    {
      GList *list;

      _pspp_sheet_view_column_realize_button (column);

      for (list = tree_view->priv->columns; list; list = list->next)
	{
	  column = PSPP_SHEET_VIEW_COLUMN (list->data);
	  if (column->visible)
	    _pspp_sheet_view_column_cell_set_dirty (column);
	}
      gtk_widget_queue_resize (GTK_WIDGET (tree_view));
    }

  g_signal_emit (tree_view, tree_view_signals[COLUMNS_CHANGED], 0);

  return tree_view->priv->n_columns;
}

/**
 * pspp_sheet_view_insert_column_with_attributes:
 * @tree_view: A #PsppSheetView
 * @position: The position to insert the new column in.
 * @title: The title to set the header to.
 * @cell: The #GtkCellRenderer.
 * @Varargs: A %NULL-terminated list of attributes.
 *
 * Creates a new #PsppSheetViewColumn and inserts it into the @tree_view at
 * @position.  If @position is -1, then the newly created column is inserted at
 * the end.  The column is initialized with the attributes given.
 *
 * Return value: The number of columns in @tree_view after insertion.
 **/
gint
pspp_sheet_view_insert_column_with_attributes (PsppSheetView     *tree_view,
					     gint             position,
					     const gchar     *title,
					     GtkCellRenderer *cell,
					     ...)
{
  PsppSheetViewColumn *column;
  gchar *attribute;
  va_list args;
  gint column_id;

  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), -1);

  column = pspp_sheet_view_column_new ();
  pspp_sheet_view_column_set_title (column, title);
  pspp_sheet_view_column_pack_start (column, cell, TRUE);

  va_start (args, cell);

  attribute = va_arg (args, gchar *);

  while (attribute != NULL)
    {
      column_id = va_arg (args, gint);
      pspp_sheet_view_column_add_attribute (column, cell, attribute, column_id);
      attribute = va_arg (args, gchar *);
    }

  va_end (args);

  pspp_sheet_view_insert_column (tree_view, column, position);

  return tree_view->priv->n_columns;
}

/**
 * pspp_sheet_view_insert_column_with_data_func:
 * @tree_view: a #PsppSheetView
 * @position: Position to insert, -1 for append
 * @title: column title
 * @cell: cell renderer for column
 * @func: function to set attributes of cell renderer
 * @data: data for @func
 * @dnotify: destroy notifier for @data
 *
 * Convenience function that inserts a new column into the #PsppSheetView
 * with the given cell renderer and a #GtkCellDataFunc to set cell renderer
 * attributes (normally using data from the model). See also
 * pspp_sheet_view_column_set_cell_data_func(), pspp_sheet_view_column_pack_start().
 *
 * Return value: number of columns in the tree view post-insert
 **/
gint
pspp_sheet_view_insert_column_with_data_func  (PsppSheetView               *tree_view,
                                             gint                       position,
                                             const gchar               *title,
                                             GtkCellRenderer           *cell,
                                             PsppSheetCellDataFunc        func,
                                             gpointer                   data,
                                             GDestroyNotify             dnotify)
{
  PsppSheetViewColumn *column;

  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), -1);

  column = pspp_sheet_view_column_new ();
  pspp_sheet_view_column_set_title (column, title);
  pspp_sheet_view_column_pack_start (column, cell, TRUE);
  pspp_sheet_view_column_set_cell_data_func (column, cell, func, data, dnotify);

  pspp_sheet_view_insert_column (tree_view, column, position);

  return tree_view->priv->n_columns;
}

/**
 * pspp_sheet_view_get_column:
 * @tree_view: A #PsppSheetView.
 * @n: The position of the column, counting from 0.
 *
 * Gets the #PsppSheetViewColumn at the given position in the #tree_view.
 *
 * Return value: The #PsppSheetViewColumn, or %NULL if the position is outside the
 * range of columns.
 **/
PsppSheetViewColumn *
pspp_sheet_view_get_column (PsppSheetView *tree_view,
			  gint         n)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), NULL);

  if (n < 0 || n >= tree_view->priv->n_columns)
    return NULL;

  if (tree_view->priv->columns == NULL)
    return NULL;

  return PSPP_SHEET_VIEW_COLUMN (g_list_nth (tree_view->priv->columns, n)->data);
}

/**
 * pspp_sheet_view_get_columns:
 * @tree_view: A #PsppSheetView
 *
 * Returns a #GList of all the #PsppSheetViewColumn s currently in @tree_view.
 * The returned list must be freed with g_list_free ().
 *
 * Return value: (element-type PsppSheetViewColumn) (transfer container): A list of #PsppSheetViewColumn s
 **/
GList *
pspp_sheet_view_get_columns (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), NULL);

  return g_list_copy (tree_view->priv->columns);
}

/**
 * pspp_sheet_view_move_column_after:
 * @tree_view: A #PsppSheetView
 * @column: The #PsppSheetViewColumn to be moved.
 * @base_column: (allow-none): The #PsppSheetViewColumn to be moved relative to, or %NULL.
 *
 * Moves @column to be after to @base_column.  If @base_column is %NULL, then
 * @column is placed in the first position.
 **/
void
pspp_sheet_view_move_column_after (PsppSheetView       *tree_view,
				 PsppSheetViewColumn *column,
				 PsppSheetViewColumn *base_column)
{
  GList *column_list_el, *base_el = NULL;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  column_list_el = g_list_find (tree_view->priv->columns, column);
  g_return_if_fail (column_list_el != NULL);

  if (base_column)
    {
      base_el = g_list_find (tree_view->priv->columns, base_column);
      g_return_if_fail (base_el != NULL);
    }

  if (column_list_el->prev == base_el)
    return;

  tree_view->priv->columns = g_list_remove_link (tree_view->priv->columns, column_list_el);
  if (base_el == NULL)
    {
      column_list_el->prev = NULL;
      column_list_el->next = tree_view->priv->columns;
      if (column_list_el->next)
	column_list_el->next->prev = column_list_el;
      tree_view->priv->columns = column_list_el;
    }
  else
    {
      column_list_el->prev = base_el;
      column_list_el->next = base_el->next;
      if (column_list_el->next)
	column_list_el->next->prev = column_list_el;
      base_el->next = column_list_el;
    }

  if (gtk_widget_get_realized (GTK_WIDGET (tree_view)))
    {
      gtk_widget_queue_resize (GTK_WIDGET (tree_view));
      pspp_sheet_view_size_allocate_columns (GTK_WIDGET (tree_view), NULL);
    }

  g_signal_emit (tree_view, tree_view_signals[COLUMNS_CHANGED], 0);
}

/**
 * pspp_sheet_view_set_column_drag_function:
 * @tree_view: A #PsppSheetView.
 * @func: (allow-none): A function to determine which columns are reorderable, or %NULL.
 * @user_data: (allow-none): User data to be passed to @func, or %NULL
 * @destroy: (allow-none): Destroy notifier for @user_data, or %NULL
 *
 * Sets a user function for determining where a column may be dropped when
 * dragged.  This function is called on every column pair in turn at the
 * beginning of a column drag to determine where a drop can take place.  The
 * arguments passed to @func are: the @tree_view, the #PsppSheetViewColumn being
 * dragged, the two #PsppSheetViewColumn s determining the drop spot, and
 * @user_data.  If either of the #PsppSheetViewColumn arguments for the drop spot
 * are %NULL, then they indicate an edge.  If @func is set to be %NULL, then
 * @tree_view reverts to the default behavior of allowing all columns to be
 * dropped everywhere.
 **/
void
pspp_sheet_view_set_column_drag_function (PsppSheetView               *tree_view,
					PsppSheetViewColumnDropFunc  func,
					gpointer                   user_data,
					GDestroyNotify             destroy)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  if (tree_view->priv->column_drop_func_data_destroy)
    tree_view->priv->column_drop_func_data_destroy (tree_view->priv->column_drop_func_data);

  tree_view->priv->column_drop_func = func;
  tree_view->priv->column_drop_func_data = user_data;
  tree_view->priv->column_drop_func_data_destroy = destroy;
}

/**
 * pspp_sheet_view_scroll_to_point:
 * @tree_view: a #PsppSheetView
 * @tree_x: X coordinate of new top-left pixel of visible area, or -1
 * @tree_y: Y coordinate of new top-left pixel of visible area, or -1
 *
 * Scrolls the tree view such that the top-left corner of the visible
 * area is @tree_x, @tree_y, where @tree_x and @tree_y are specified
 * in tree coordinates.  The @tree_view must be realized before
 * this function is called.  If it isn't, you probably want to be
 * using pspp_sheet_view_scroll_to_cell().
 *
 * If either @tree_x or @tree_y are -1, then that direction isn't scrolled.
 **/
void
pspp_sheet_view_scroll_to_point (PsppSheetView *tree_view,
                               gint         tree_x,
                               gint         tree_y)
{
  GtkAdjustment *hadj;
  GtkAdjustment *vadj;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (gtk_widget_get_realized (GTK_WIDGET (tree_view)));

  hadj = tree_view->priv->hadjustment;
  vadj = tree_view->priv->vadjustment;

  if (tree_x != -1)
    gtk_adjustment_set_value (hadj, CLAMP (tree_x, gtk_adjustment_get_lower (hadj), gtk_adjustment_get_upper (hadj) - gtk_adjustment_get_page_size (hadj)));
  if (tree_y != -1)
    gtk_adjustment_set_value (vadj, CLAMP (tree_y, gtk_adjustment_get_lower (vadj), gtk_adjustment_get_upper (vadj) - gtk_adjustment_get_page_size (vadj)));
}

/**
 * pspp_sheet_view_scroll_to_cell:
 * @tree_view: A #PsppSheetView.
 * @path: (allow-none): The path of the row to move to, or %NULL.
 * @column: (allow-none): The #PsppSheetViewColumn to move horizontally to, or %NULL.
 * @use_align: whether to use alignment arguments, or %FALSE.
 * @row_align: The vertical alignment of the row specified by @path.
 * @col_align: The horizontal alignment of the column specified by @column.
 *
 * Moves the alignments of @tree_view to the position specified by @column and
 * @path.  If @column is %NULL, then no horizontal scrolling occurs.  Likewise,
 * if @path is %NULL no vertical scrolling occurs.  At a minimum, one of @column
 * or @path need to be non-%NULL.  @row_align determines where the row is
 * placed, and @col_align determines where @column is placed.  Both are expected
 * to be between 0.0 and 1.0. 0.0 means left/top alignment, 1.0 means
 * right/bottom alignment, 0.5 means center.
 *
 * If @use_align is %FALSE, then the alignment arguments are ignored, and the
 * tree does the minimum amount of work to scroll the cell onto the screen.
 * This means that the cell will be scrolled to the edge closest to its current
 * position.  If the cell is currently visible on the screen, nothing is done.
 *
 * This function only works if the model is set, and @path is a valid row on the
 * model.  If the model changes before the @tree_view is realized, the centered
 * path will be modified to reflect this change.
 **/
void
pspp_sheet_view_scroll_to_cell (PsppSheetView       *tree_view,
                              GtkTreePath       *path,
                              PsppSheetViewColumn *column,
			      gboolean           use_align,
                              gfloat             row_align,
                              gfloat             col_align)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (tree_view->priv->model != NULL);
  g_return_if_fail (row_align >= 0.0 && row_align <= 1.0);
  g_return_if_fail (col_align >= 0.0 && col_align <= 1.0);
  g_return_if_fail (path != NULL || column != NULL);

#if 0
  g_print ("pspp_sheet_view_scroll_to_cell:\npath: %s\ncolumn: %s\nuse_align: %d\nrow_align: %f\ncol_align: %f\n",
	   gtk_tree_path_to_string (path), column?"non-null":"null", use_align, row_align, col_align);
#endif
  row_align = CLAMP (row_align, 0.0, 1.0);
  col_align = CLAMP (col_align, 0.0, 1.0);


  /* Note: Despite the benefits that come from having one code path for the
   * scrolling code, we short-circuit validate_visible_area's immplementation as
   * it is much slower than just going to the point.
   */
  if (!gtk_widget_get_visible (GTK_WIDGET (tree_view)) ||
      !gtk_widget_get_realized (GTK_WIDGET (tree_view))
      /* XXX || GTK_WIDGET_ALLOC_NEEDED (tree_view) */)
    {
      if (tree_view->priv->scroll_to_path)
	gtk_tree_row_reference_free (tree_view->priv->scroll_to_path);

      tree_view->priv->scroll_to_path = NULL;
      tree_view->priv->scroll_to_column = NULL;

      if (path)
	tree_view->priv->scroll_to_path = gtk_tree_row_reference_new_proxy (G_OBJECT (tree_view), tree_view->priv->model, path);
      if (column)
	tree_view->priv->scroll_to_column = column;
      tree_view->priv->scroll_to_use_align = use_align;
      tree_view->priv->scroll_to_row_align = row_align;
      tree_view->priv->scroll_to_col_align = col_align;

      install_presize_handler (tree_view);
    }
  else
    {
      GdkRectangle cell_rect;
      GdkRectangle vis_rect;
      gint dest_x, dest_y;

      pspp_sheet_view_get_background_area (tree_view, path, column, &cell_rect);
      pspp_sheet_view_get_visible_rect (tree_view, &vis_rect);

      cell_rect.y = TREE_WINDOW_Y_TO_RBTREE_Y (tree_view, cell_rect.y);

      dest_x = vis_rect.x;
      dest_y = vis_rect.y;

      if (column)
	{
	  if (use_align)
	    {
	      dest_x = cell_rect.x - ((vis_rect.width - cell_rect.width) * col_align);
	    }
	  else
	    {
	      if (cell_rect.x < vis_rect.x)
		dest_x = cell_rect.x;
	      if (cell_rect.x + cell_rect.width > vis_rect.x + vis_rect.width)
		dest_x = cell_rect.x + cell_rect.width - vis_rect.width;
	    }
	}

      if (path)
	{
	  if (use_align)
	    {
	      dest_y = cell_rect.y - ((vis_rect.height - cell_rect.height) * row_align);
	      dest_y = MAX (dest_y, 0);
	    }
	  else
	    {
	      if (cell_rect.y < vis_rect.y)
		dest_y = cell_rect.y;
	      if (cell_rect.y + cell_rect.height > vis_rect.y + vis_rect.height)
		dest_y = cell_rect.y + cell_rect.height - vis_rect.height;
	    }
	}

      pspp_sheet_view_scroll_to_point (tree_view, dest_x, dest_y);
    }
}

/**
 * pspp_sheet_view_row_activated:
 * @tree_view: A #PsppSheetView
 * @path: The #GtkTreePath to be activated.
 * @column: The #PsppSheetViewColumn to be activated.
 *
 * Activates the cell determined by @path and @column.
 **/
void
pspp_sheet_view_row_activated (PsppSheetView       *tree_view,
			     GtkTreePath       *path,
			     PsppSheetViewColumn *column)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  g_signal_emit (tree_view, tree_view_signals[ROW_ACTIVATED], 0, path, column);
}


/**
 * pspp_sheet_view_get_reorderable:
 * @tree_view: a #PsppSheetView
 *
 * Retrieves whether the user can reorder the tree via drag-and-drop. See
 * pspp_sheet_view_set_reorderable().
 *
 * Return value: %TRUE if the tree can be reordered.
 **/
gboolean
pspp_sheet_view_get_reorderable (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), FALSE);

  return tree_view->priv->reorderable;
}

/**
 * pspp_sheet_view_set_reorderable:
 * @tree_view: A #PsppSheetView.
 * @reorderable: %TRUE, if the tree can be reordered.
 *
 * This function is a convenience function to allow you to reorder
 * models that support the #GtkDragSourceIface and the
 * #GtkDragDestIface.  Both #GtkTreeStore and #GtkListStore support
 * these.  If @reorderable is %TRUE, then the user can reorder the
 * model by dragging and dropping rows. The developer can listen to
 * these changes by connecting to the model's row_inserted and
 * row_deleted signals. The reordering is implemented by setting up
 * the tree view as a drag source and destination. Therefore, drag and
 * drop can not be used in a reorderable view for any other purpose.
 *
 * This function does not give you any degree of control over the order -- any
 * reordering is allowed.  If more control is needed, you should probably
 * handle drag and drop manually.
 **/
void
pspp_sheet_view_set_reorderable (PsppSheetView *tree_view,
			       gboolean     reorderable)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  reorderable = reorderable != FALSE;

  if (tree_view->priv->reorderable == reorderable)
    return;

  if (reorderable)
    {
      const GtkTargetEntry row_targets[] = {
        { "GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_WIDGET, 0 }
      };

      pspp_sheet_view_enable_model_drag_source (tree_view,
					      GDK_BUTTON1_MASK,
					      row_targets,
					      G_N_ELEMENTS (row_targets),
					      GDK_ACTION_MOVE);
      pspp_sheet_view_enable_model_drag_dest (tree_view,
					    row_targets,
					    G_N_ELEMENTS (row_targets),
					    GDK_ACTION_MOVE);
    }
  else
    {
      pspp_sheet_view_unset_rows_drag_source (tree_view);
      pspp_sheet_view_unset_rows_drag_dest (tree_view);
    }

  tree_view->priv->reorderable = reorderable;

  g_object_notify (G_OBJECT (tree_view), "reorderable");
}

/* If CLEAR_AND_SELECT is true, then the row will be selected and, unless Shift
   is pressed, other rows will be unselected.

   If CLAMP_NODE is true, then the sheetview will scroll to make the row
   visible. */
static void
pspp_sheet_view_real_set_cursor (PsppSheetView     *tree_view,
			       GtkTreePath     *path,
			       gboolean         clear_and_select,
                               gboolean         clamp_node,
                               PsppSheetSelectMode mode)
{
  int node = -1;

  if (gtk_tree_row_reference_valid (tree_view->priv->cursor))
    {
      GtkTreePath *cursor_path;
      cursor_path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);
      pspp_sheet_view_queue_draw_path (tree_view, cursor_path, NULL);
      gtk_tree_path_free (cursor_path);
    }

  gtk_tree_row_reference_free (tree_view->priv->cursor);
  tree_view->priv->cursor = NULL;

  _pspp_sheet_view_find_node (tree_view, path, &node);
  tree_view->priv->cursor =
    gtk_tree_row_reference_new_proxy (G_OBJECT (tree_view),
                                      tree_view->priv->model,
                                      path);

  if (tree_view->priv->row_count > 0)
    {
      int new_node = -1;

      if (clear_and_select && !(mode & PSPP_SHEET_SELECT_MODE_TOGGLE))
        _pspp_sheet_selection_internal_select_node (tree_view->priv->selection,
                                                    node, path, mode,
                                                    FALSE);

      /* We have to re-find tree and node here again, somebody might have
       * cleared the node or the whole tree in the PsppSheetSelection::changed
       * callback. If the nodes differ we bail out here.
       */
      _pspp_sheet_view_find_node (tree_view, path, &new_node);

      if (node != new_node)
        return;

      if (clamp_node)
        {
	  pspp_sheet_view_clamp_node_visible (tree_view, node);
	  _pspp_sheet_view_queue_draw_node (tree_view, node, NULL);
	}
    }

  g_signal_emit (tree_view, tree_view_signals[CURSOR_CHANGED], 0);
}

/**
 * pspp_sheet_view_get_cursor:
 * @tree_view: A #PsppSheetView
 * @path: (allow-none): A pointer to be filled with the current cursor path, or %NULL
 * @focus_column: (allow-none): A pointer to be filled with the current focus column, or %NULL
 *
 * Fills in @path and @focus_column with the current path and focus column.  If
 * the cursor isn't currently set, then *@path will be %NULL.  If no column
 * currently has focus, then *@focus_column will be %NULL.
 *
 * The returned #GtkTreePath must be freed with gtk_tree_path_free() when
 * you are done with it.
 **/
void
pspp_sheet_view_get_cursor (PsppSheetView        *tree_view,
			  GtkTreePath       **path,
			  PsppSheetViewColumn **focus_column)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  if (path)
    {
      if (gtk_tree_row_reference_valid (tree_view->priv->cursor))
	*path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);
      else
	*path = NULL;
    }

  if (focus_column)
    {
      *focus_column = tree_view->priv->focus_column;
    }
}

/**
 * pspp_sheet_view_set_cursor:
 * @tree_view: A #PsppSheetView
 * @path: A #GtkTreePath
 * @focus_column: (allow-none): A #PsppSheetViewColumn, or %NULL
 * @start_editing: %TRUE if the specified cell should start being edited.
 *
 * Sets the current keyboard focus to be at @path, and selects it.  This is
 * useful when you want to focus the user's attention on a particular row.  If
 * @focus_column is not %NULL, then focus is given to the column specified by 
 * it. Additionally, if @focus_column is specified, and @start_editing is 
 * %TRUE, then editing should be started in the specified cell.  
 * This function is often followed by @gtk_widget_grab_focus (@tree_view) 
 * in order to give keyboard focus to the widget.  Please note that editing 
 * can only happen when the widget is realized.
 *
 * If @path is invalid for @model, the current cursor (if any) will be unset
 * and the function will return without failing.
 **/
void
pspp_sheet_view_set_cursor (PsppSheetView       *tree_view,
			  GtkTreePath       *path,
			  PsppSheetViewColumn *focus_column,
			  gboolean           start_editing)
{
  pspp_sheet_view_set_cursor_on_cell (tree_view, path, focus_column,
				    NULL, start_editing);
}

/**
 * pspp_sheet_view_set_cursor_on_cell:
 * @tree_view: A #PsppSheetView
 * @path: A #GtkTreePath
 * @focus_column: (allow-none): A #PsppSheetViewColumn, or %NULL
 * @focus_cell: (allow-none): A #GtkCellRenderer, or %NULL
 * @start_editing: %TRUE if the specified cell should start being edited.
 *
 * Sets the current keyboard focus to be at @path, and selects it.  This is
 * useful when you want to focus the user's attention on a particular row.  If
 * @focus_column is not %NULL, then focus is given to the column specified by
 * it. If @focus_column and @focus_cell are not %NULL, and @focus_column
 * contains 2 or more editable or activatable cells, then focus is given to
 * the cell specified by @focus_cell. Additionally, if @focus_column is
 * specified, and @start_editing is %TRUE, then editing should be started in
 * the specified cell.  This function is often followed by
 * @gtk_widget_grab_focus (@tree_view) in order to give keyboard focus to the
 * widget.  Please note that editing can only happen when the widget is
 * realized.
 *
 * If @path is invalid for @model, the current cursor (if any) will be unset
 * and the function will return without failing.
 *
 * Since: 2.2
 **/
void
pspp_sheet_view_set_cursor_on_cell (PsppSheetView       *tree_view,
				  GtkTreePath       *path,
				  PsppSheetViewColumn *focus_column,
				  GtkCellRenderer   *focus_cell,
				  gboolean           start_editing)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (path != NULL);
  g_return_if_fail (focus_column == NULL || PSPP_IS_SHEET_VIEW_COLUMN (focus_column));

  if (!tree_view->priv->model)
    return;

  if (focus_cell)
    {
      g_return_if_fail (focus_column);
      g_return_if_fail (GTK_IS_CELL_RENDERER (focus_cell));
    }

  /* cancel the current editing, if it exists */
  if (tree_view->priv->edited_column &&
      tree_view->priv->edited_column->editable_widget)
    pspp_sheet_view_stop_editing (tree_view, TRUE);

  pspp_sheet_view_real_set_cursor (tree_view, path, TRUE, TRUE, 0);

  if (focus_column && focus_column->visible)
    {
      GList *list;
      gboolean column_in_tree = FALSE;

      for (list = tree_view->priv->columns; list; list = list->next)
	if (list->data == focus_column)
	  {
	    column_in_tree = TRUE;
	    break;
	  }
      g_return_if_fail (column_in_tree);
      tree_view->priv->focus_column = focus_column;
      if (focus_cell)
	pspp_sheet_view_column_focus_cell (focus_column, focus_cell);
      if (start_editing)
	pspp_sheet_view_start_editing (tree_view, path);

      pspp_sheet_selection_unselect_all_columns (tree_view->priv->selection);
      pspp_sheet_selection_select_column (tree_view->priv->selection, focus_column);

    }
}

/**
 * pspp_sheet_view_get_bin_window:
 * @tree_view: A #PsppSheetView
 * 
 * Returns the window that @tree_view renders to.  This is used primarily to
 * compare to <literal>event->window</literal> to confirm that the event on
 * @tree_view is on the right window.
 * 
 * Return value: A #GdkWindow, or %NULL when @tree_view hasn't been realized yet
 **/
GdkWindow *
pspp_sheet_view_get_bin_window (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), NULL);

  return tree_view->priv->bin_window;
}

/**
 * pspp_sheet_view_get_path_at_pos:
 * @tree_view: A #PsppSheetView.
 * @x: The x position to be identified (relative to bin_window).
 * @y: The y position to be identified (relative to bin_window).
 * @path: (out) (allow-none): A pointer to a #GtkTreePath pointer to be filled in, or %NULL
 * @column: (out) (allow-none): A pointer to a #PsppSheetViewColumn pointer to be filled in, or %NULL
 * @cell_x: (out) (allow-none): A pointer where the X coordinate relative to the cell can be placed, or %NULL
 * @cell_y: (out) (allow-none): A pointer where the Y coordinate relative to the cell can be placed, or %NULL
 *
 * Finds the path at the point (@x, @y), relative to bin_window coordinates
 * (please see pspp_sheet_view_get_bin_window()).
 * That is, @x and @y are relative to an events coordinates. @x and @y must
 * come from an event on the @tree_view only where <literal>event->window ==
 * pspp_sheet_view_get_bin_window (<!-- -->)</literal>. It is primarily for
 * things like popup menus. If @path is non-%NULL, then it will be filled
 * with the #GtkTreePath at that point.  This path should be freed with
 * gtk_tree_path_free().  If @column is non-%NULL, then it will be filled
 * with the column at that point.  @cell_x and @cell_y return the coordinates
 * relative to the cell background (i.e. the @background_area passed to
 * gtk_cell_renderer_render()).  This function is only meaningful if
 * @tree_view is realized.  Therefore this function will always return %FALSE
 * if @tree_view is not realized or does not have a model.
 *
 * For converting widget coordinates (eg. the ones you get from
 * GtkWidget::query-tooltip), please see
 * pspp_sheet_view_convert_widget_to_bin_window_coords().
 *
 * Return value: %TRUE if a row exists at that coordinate.
 **/
gboolean
pspp_sheet_view_get_path_at_pos (PsppSheetView        *tree_view,
			       gint                x,
			       gint                y,
			       GtkTreePath       **path,
			       PsppSheetViewColumn **column,
                               gint               *cell_x,
                               gint               *cell_y)
{
  int node;
  gint y_offset;

  g_return_val_if_fail (tree_view != NULL, FALSE);

  if (path)
    *path = NULL;
  if (column)
    *column = NULL;

  if (tree_view->priv->bin_window == NULL)
    return FALSE;

  if (tree_view->priv->row_count == 0)
    return FALSE;

  if (x > gtk_adjustment_get_upper (tree_view->priv->hadjustment))
    return FALSE;

  if (x < 0 || y < 0)
    return FALSE;

  if (column || cell_x)
    {
      PsppSheetViewColumn *tmp_column;
      PsppSheetViewColumn *last_column = NULL;
      GList *list;
      gint remaining_x = x;
      gboolean found = FALSE;
      gboolean rtl;

      rtl = (gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL);
      for (list = (rtl ? g_list_last (tree_view->priv->columns) : g_list_first (tree_view->priv->columns));
	   list;
	   list = (rtl ? list->prev : list->next))
	{
	  tmp_column = list->data;

	  if (tmp_column->visible == FALSE)
	    continue;

	  last_column = tmp_column;
	  if (remaining_x <= tmp_column->width)
	    {
              found = TRUE;

              if (column)
                *column = tmp_column;

              if (cell_x)
                *cell_x = remaining_x;

	      break;
	    }
	  remaining_x -= tmp_column->width;
	}

      /* If found is FALSE and there is a last_column, then it the remainder
       * space is in that area
       */
      if (!found)
        {
	  if (last_column)
	    {
	      if (column)
		*column = last_column;
	      
	      if (cell_x)
		*cell_x = last_column->width + remaining_x;
	    }
	  else
	    {
	      return FALSE;
	    }
	}
    }

  y_offset = pspp_sheet_view_find_offset (tree_view,
                                          TREE_WINDOW_Y_TO_RBTREE_Y (tree_view, y),
                                          &node);

  if (node < 0)
    return FALSE;

  if (cell_y)
    *cell_y = y_offset;

  if (path)
    *path = _pspp_sheet_view_find_path (tree_view, node);

  return TRUE;
}

/* Computes 'cell_area' from 'background_area', which must be the background
   area for a cell.  Set 'subtract_focus_rect' to TRUE to compute the cell area
   as passed to a GtkCellRenderer's "render" function, or to FALSE to compute
   the cell area as passed to _pspp_sheet_view_column_cell_render().

   'column' is required to properly adjust 'cell_area->x' and
   'cell_area->width'.  It may be set to NULL if these values are not of
   interest.  In this case 'cell_area->x' and 'cell_area->width' will be
   returned as 0. */
static void
pspp_sheet_view_adjust_cell_area (PsppSheetView        *tree_view,
                                  PsppSheetViewColumn  *column,
                                  const GdkRectangle   *background_area,
                                  gboolean              subtract_focus_rect,
                                  GdkRectangle         *cell_area)
{
  gint vertical_separator;
  gint horizontal_separator;

  *cell_area = *background_area;

  gtk_widget_style_get (GTK_WIDGET (tree_view),
			"vertical-separator", &vertical_separator,
			"horizontal-separator", &horizontal_separator,
			NULL);
  cell_area->x += horizontal_separator / 2;
  cell_area->y += vertical_separator / 2;
  cell_area->width -= horizontal_separator;
  cell_area->height -= vertical_separator;

  if (subtract_focus_rect)
    {
      int focus_line_width;

      gtk_widget_style_get (GTK_WIDGET (tree_view),
                            "focus-line-width", &focus_line_width,
                            NULL);
      cell_area->x += focus_line_width;
      cell_area->y += focus_line_width;
      cell_area->width -= 2 * focus_line_width;
      cell_area->height -= 2 * focus_line_width;
    }

  if (tree_view->priv->grid_lines != PSPP_SHEET_VIEW_GRID_LINES_NONE)
    {
      gint grid_line_width;
      gtk_widget_style_get (GTK_WIDGET (tree_view),
                            "grid-line-width", &grid_line_width,
                            NULL);

      if ((tree_view->priv->grid_lines == PSPP_SHEET_VIEW_GRID_LINES_VERTICAL
           || tree_view->priv->grid_lines == PSPP_SHEET_VIEW_GRID_LINES_BOTH)
          && column != NULL)
        {
          PsppSheetViewColumn *first_column, *last_column;
          GList *list;

          /* Find the last visible column. */
          last_column = NULL;
          for (list = g_list_last (tree_view->priv->columns);
               list;
               list = list->prev)
            {
              PsppSheetViewColumn *c = list->data;
              if (c->visible)
                {
                  last_column = c;
                  break;
                }
            }

          /* Find the first visible column. */
          first_column = NULL;
          for (list = g_list_first (tree_view->priv->columns);
               list;
               list = list->next)
            {
              PsppSheetViewColumn *c = list->data;
              if (c->visible)
                {
                  first_column = c;
                  break;
                }
            }

          if (column == first_column)
            {
              cell_area->width -= grid_line_width / 2;
            }
          else if (column == last_column)
            {
              cell_area->x += grid_line_width / 2;
              cell_area->width -= grid_line_width / 2;
            }
          else
            {
              cell_area->x += grid_line_width / 2;
              cell_area->width -= grid_line_width;
            }
        }

      if (tree_view->priv->grid_lines == PSPP_SHEET_VIEW_GRID_LINES_HORIZONTAL
          || tree_view->priv->grid_lines == PSPP_SHEET_VIEW_GRID_LINES_BOTH)
        {
          cell_area->y += grid_line_width / 2;
          cell_area->height -= grid_line_width;
        }
    }

  if (column == NULL)
    {
      cell_area->x = 0;
      cell_area->width = 0;
    }
}

/**
 * pspp_sheet_view_get_cell_area:
 * @tree_view: a #PsppSheetView
 * @path: (allow-none): a #GtkTreePath for the row, or %NULL to get only horizontal coordinates
 * @column: (allow-none): a #PsppSheetViewColumn for the column, or %NULL to get only vertical coordinates
 * @rect: rectangle to fill with cell rect
 *
 * Fills the bounding rectangle in bin_window coordinates for the cell at the
 * row specified by @path and the column specified by @column.  If @path is
 * %NULL, or points to a path not currently displayed, the @y and @height fields
 * of the rectangle will be filled with 0. If @column is %NULL, the @x and @width
 * fields will be filled with 0.  The sum of all cell rects does not cover the
 * entire tree; there are extra pixels in between rows, for example. The
 * returned rectangle is equivalent to the @cell_area passed to
 * gtk_cell_renderer_render().  This function is only valid if @tree_view is
 * realized.
 **/
void
pspp_sheet_view_get_cell_area (PsppSheetView        *tree_view,
                             GtkTreePath        *path,
                             PsppSheetViewColumn  *column,
                             GdkRectangle       *rect)
{
  GdkRectangle background_area;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (column == NULL || PSPP_IS_SHEET_VIEW_COLUMN (column));
  g_return_if_fail (rect != NULL);
  g_return_if_fail (!column || column->tree_view == (GtkWidget *) tree_view);
  g_return_if_fail (gtk_widget_get_realized (GTK_WIDGET (tree_view)));

  pspp_sheet_view_get_background_area (tree_view, path, column,
                                       &background_area);
  pspp_sheet_view_adjust_cell_area (tree_view, column, &background_area,
                                    FALSE, rect);
}

/**
 * pspp_sheet_view_get_background_area:
 * @tree_view: a #PsppSheetView
 * @path: (allow-none): a #GtkTreePath for the row, or %NULL to get only horizontal coordinates
 * @column: (allow-none): a #PsppSheetViewColumn for the column, or %NULL to get only vertical coordiantes
 * @rect: rectangle to fill with cell background rect
 *
 * Fills the bounding rectangle in bin_window coordinates for the cell at the
 * row specified by @path and the column specified by @column.  If @path is
 * %NULL, or points to a node not found in the tree, the @y and @height fields of
 * the rectangle will be filled with 0. If @column is %NULL, the @x and @width
 * fields will be filled with 0.  The returned rectangle is equivalent to the
 * @background_area passed to gtk_cell_renderer_render().  These background
 * areas tile to cover the entire bin window.  Contrast with the @cell_area,
 * returned by pspp_sheet_view_get_cell_area(), which returns only the cell
 * itself, excluding surrounding borders.
 *
 **/
void
pspp_sheet_view_get_background_area (PsppSheetView        *tree_view,
                                   GtkTreePath        *path,
                                   PsppSheetViewColumn  *column,
                                   GdkRectangle       *rect)
{
  int node = -1;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (column == NULL || PSPP_IS_SHEET_VIEW_COLUMN (column));
  g_return_if_fail (rect != NULL);

  rect->x = 0;
  rect->y = 0;
  rect->width = 0;
  rect->height = 0;

  if (path)
    {
      /* Get vertical coords */

      _pspp_sheet_view_find_node (tree_view, path, &node);
      if (node < 0)
	return;

      rect->y = BACKGROUND_FIRST_PIXEL (tree_view, node);

      rect->height = ROW_HEIGHT (tree_view);
    }

  if (column)
    {
      gint x2 = 0;

      pspp_sheet_view_get_background_xrange (tree_view, column, &rect->x, &x2);
      rect->width = x2 - rect->x;
    }
}

/**
 * pspp_sheet_view_get_visible_rect:
 * @tree_view: a #PsppSheetView
 * @visible_rect: rectangle to fill
 *
 * Fills @visible_rect with the currently-visible region of the
 * buffer, in tree coordinates. Convert to bin_window coordinates with
 * pspp_sheet_view_convert_tree_to_bin_window_coords().
 * Tree coordinates start at 0,0 for row 0 of the tree, and cover the entire
 * scrollable area of the tree.
 **/
void
pspp_sheet_view_get_visible_rect (PsppSheetView  *tree_view,
                                GdkRectangle *visible_rect)
{
  GtkWidget *widget;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  widget = GTK_WIDGET (tree_view);

  if (visible_rect)
    {
      GtkAllocation allocation;
      gtk_widget_get_allocation (widget, &allocation);
      visible_rect->x = gtk_adjustment_get_value (tree_view->priv->hadjustment);
      visible_rect->y = gtk_adjustment_get_value (tree_view->priv->vadjustment);
      visible_rect->width  = allocation.width;
      visible_rect->height = allocation.height - TREE_VIEW_HEADER_HEIGHT (tree_view);
    }
}

/**
 * pspp_sheet_view_widget_to_tree_coords:
 * @tree_view: a #PsppSheetView
 * @wx: X coordinate relative to bin_window
 * @wy: Y coordinate relative to bin_window
 * @tx: return location for tree X coordinate
 * @ty: return location for tree Y coordinate
 *
 * Converts bin_window coordinates to coordinates for the
 * tree (the full scrollable area of the tree).
 *
 * Deprecated: 2.12: Due to historial reasons the name of this function is
 * incorrect.  For converting coordinates relative to the widget to
 * bin_window coordinates, please see
 * pspp_sheet_view_convert_widget_to_bin_window_coords().
 *
 **/
void
pspp_sheet_view_widget_to_tree_coords (PsppSheetView *tree_view,
				      gint         wx,
				      gint         wy,
				      gint        *tx,
				      gint        *ty)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  if (tx)
    *tx = wx + gtk_adjustment_get_value (tree_view->priv->hadjustment);
  if (ty)
    *ty = wy + tree_view->priv->dy;
}

/**
 * pspp_sheet_view_tree_to_widget_coords:
 * @tree_view: a #PsppSheetView
 * @tx: tree X coordinate
 * @ty: tree Y coordinate
 * @wx: return location for X coordinate relative to bin_window
 * @wy: return location for Y coordinate relative to bin_window
 *
 * Converts tree coordinates (coordinates in full scrollable area of the tree)
 * to bin_window coordinates.
 *
 * Deprecated: 2.12: Due to historial reasons the name of this function is
 * incorrect.  For converting bin_window coordinates to coordinates relative
 * to bin_window, please see
 * pspp_sheet_view_convert_bin_window_to_widget_coords().
 *
 **/
void
pspp_sheet_view_tree_to_widget_coords (PsppSheetView *tree_view,
                                     gint         tx,
                                     gint         ty,
                                     gint        *wx,
                                     gint        *wy)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  if (wx)
    *wx = tx - gtk_adjustment_get_value (tree_view->priv->hadjustment);
  if (wy)
    *wy = ty - tree_view->priv->dy;
}


/**
 * pspp_sheet_view_convert_widget_to_tree_coords:
 * @tree_view: a #PsppSheetView
 * @wx: X coordinate relative to the widget
 * @wy: Y coordinate relative to the widget
 * @tx: return location for tree X coordinate
 * @ty: return location for tree Y coordinate
 *
 * Converts widget coordinates to coordinates for the
 * tree (the full scrollable area of the tree).
 *
 * Since: 2.12
 **/
void
pspp_sheet_view_convert_widget_to_tree_coords (PsppSheetView *tree_view,
                                             gint         wx,
                                             gint         wy,
                                             gint        *tx,
                                             gint        *ty)
{
  gint x, y;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  pspp_sheet_view_convert_widget_to_bin_window_coords (tree_view,
						     wx, wy,
						     &x, &y);
  pspp_sheet_view_convert_bin_window_to_tree_coords (tree_view,
						   x, y,
						   tx, ty);
}

/**
 * pspp_sheet_view_convert_tree_to_widget_coords:
 * @tree_view: a #PsppSheetView
 * @tx: X coordinate relative to the tree
 * @ty: Y coordinate relative to the tree
 * @wx: return location for widget X coordinate
 * @wy: return location for widget Y coordinate
 *
 * Converts tree coordinates (coordinates in full scrollable area of the tree)
 * to widget coordinates.
 *
 * Since: 2.12
 **/
void
pspp_sheet_view_convert_tree_to_widget_coords (PsppSheetView *tree_view,
                                             gint         tx,
                                             gint         ty,
                                             gint        *wx,
                                             gint        *wy)
{
  gint x, y;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  pspp_sheet_view_convert_tree_to_bin_window_coords (tree_view,
						   tx, ty,
						   &x, &y);
  pspp_sheet_view_convert_bin_window_to_widget_coords (tree_view,
						     x, y,
						     wx, wy);
}

/**
 * pspp_sheet_view_convert_widget_to_bin_window_coords:
 * @tree_view: a #PsppSheetView
 * @wx: X coordinate relative to the widget
 * @wy: Y coordinate relative to the widget
 * @bx: return location for bin_window X coordinate
 * @by: return location for bin_window Y coordinate
 *
 * Converts widget coordinates to coordinates for the bin_window
 * (see pspp_sheet_view_get_bin_window()).
 *
 * Since: 2.12
 **/
void
pspp_sheet_view_convert_widget_to_bin_window_coords (PsppSheetView *tree_view,
                                                   gint         wx,
                                                   gint         wy,
                                                   gint        *bx,
                                                   gint        *by)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  if (bx)
    *bx = wx + gtk_adjustment_get_value (tree_view->priv->hadjustment);
  if (by)
    *by = wy - TREE_VIEW_HEADER_HEIGHT (tree_view);
}

/**
 * pspp_sheet_view_convert_bin_window_to_widget_coords:
 * @tree_view: a #PsppSheetView
 * @bx: bin_window X coordinate
 * @by: bin_window Y coordinate
 * @wx: return location for widget X coordinate
 * @wy: return location for widget Y coordinate
 *
 * Converts bin_window coordinates (see pspp_sheet_view_get_bin_window())
 * to widget relative coordinates.
 *
 * Since: 2.12
 **/
void
pspp_sheet_view_convert_bin_window_to_widget_coords (PsppSheetView *tree_view,
                                                   gint         bx,
                                                   gint         by,
                                                   gint        *wx,
                                                   gint        *wy)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  if (wx)
    *wx = bx - gtk_adjustment_get_value (tree_view->priv->hadjustment);
  if (wy)
    *wy = by + TREE_VIEW_HEADER_HEIGHT (tree_view);
}

/**
 * pspp_sheet_view_convert_tree_to_bin_window_coords:
 * @tree_view: a #PsppSheetView
 * @tx: tree X coordinate
 * @ty: tree Y coordinate
 * @bx: return location for X coordinate relative to bin_window
 * @by: return location for Y coordinate relative to bin_window
 *
 * Converts tree coordinates (coordinates in full scrollable area of the tree)
 * to bin_window coordinates.
 *
 * Since: 2.12
 **/
void
pspp_sheet_view_convert_tree_to_bin_window_coords (PsppSheetView *tree_view,
                                                 gint         tx,
                                                 gint         ty,
                                                 gint        *bx,
                                                 gint        *by)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  if (bx)
    *bx = tx;
  if (by)
    *by = ty - tree_view->priv->dy;
}

/**
 * pspp_sheet_view_convert_bin_window_to_tree_coords:
 * @tree_view: a #PsppSheetView
 * @bx: X coordinate relative to bin_window
 * @by: Y coordinate relative to bin_window
 * @tx: return location for tree X coordinate
 * @ty: return location for tree Y coordinate
 *
 * Converts bin_window coordinates to coordinates for the
 * tree (the full scrollable area of the tree).
 *
 * Since: 2.12
 **/
void
pspp_sheet_view_convert_bin_window_to_tree_coords (PsppSheetView *tree_view,
                                                 gint         bx,
                                                 gint         by,
                                                 gint        *tx,
                                                 gint        *ty)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  if (tx)
    *tx = bx;
  if (ty)
    *ty = by + tree_view->priv->dy;
}



/**
 * pspp_sheet_view_get_visible_range:
 * @tree_view: A #PsppSheetView
 * @start_path: (allow-none): Return location for start of region, or %NULL.
 * @end_path: (allow-none): Return location for end of region, or %NULL.
 *
 * Sets @start_path and @end_path to be the first and last visible path.
 * Note that there may be invisible paths in between.
 *
 * The paths should be freed with gtk_tree_path_free() after use.
 *
 * Returns: %TRUE, if valid paths were placed in @start_path and @end_path.
 *
 * Since: 2.8
 **/
gboolean
pspp_sheet_view_get_visible_range (PsppSheetView  *tree_view,
                                 GtkTreePath **start_path,
                                 GtkTreePath **end_path)
{
  int node;
  gboolean retval;
  
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), FALSE);

  if (!tree_view->priv->row_count)
    return FALSE;

  retval = TRUE;

  if (start_path)
    {
      pspp_sheet_view_find_offset (tree_view,
                                   TREE_WINDOW_Y_TO_RBTREE_Y (tree_view, 0),
                                   &node);
      if (node >= 0)
        *start_path = _pspp_sheet_view_find_path (tree_view, node);
      else
        retval = FALSE;
    }

  if (end_path)
    {
      gint y;

      if (tree_view->priv->height < gtk_adjustment_get_page_size (tree_view->priv->vadjustment))
        y = tree_view->priv->height - 1;
      else
        y = TREE_WINDOW_Y_TO_RBTREE_Y (tree_view, gtk_adjustment_get_page_size (tree_view->priv->vadjustment)) - 1;

      pspp_sheet_view_find_offset (tree_view, y, &node);
      if (node >= 0)
        *end_path = _pspp_sheet_view_find_path (tree_view, node);
      else
        retval = FALSE;
    }

  return retval;
}

static void
unset_reorderable (PsppSheetView *tree_view)
{
  if (tree_view->priv->reorderable)
    {
      tree_view->priv->reorderable = FALSE;
      g_object_notify (G_OBJECT (tree_view), "reorderable");
    }
}

/**
 * pspp_sheet_view_enable_model_drag_source:
 * @tree_view: a #PsppSheetView
 * @start_button_mask: Mask of allowed buttons to start drag
 * @targets: the table of targets that the drag will support
 * @n_targets: the number of items in @targets
 * @actions: the bitmask of possible actions for a drag from this
 *    widget
 *
 * Turns @tree_view into a drag source for automatic DND. Calling this
 * method sets #PsppSheetView:reorderable to %FALSE.
 **/
void
pspp_sheet_view_enable_model_drag_source (PsppSheetView              *tree_view,
					GdkModifierType           start_button_mask,
					const GtkTargetEntry     *targets,
					gint                      n_targets,
					GdkDragAction             actions)
{
  TreeViewDragInfo *di;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  gtk_drag_source_set (GTK_WIDGET (tree_view),
		       0,
		       targets,
		       n_targets,
		       actions);

  di = ensure_info (tree_view);

  di->start_button_mask = start_button_mask;
  di->source_actions = actions;
  di->source_set = TRUE;

  unset_reorderable (tree_view);
}

/**
 * pspp_sheet_view_enable_model_drag_dest:
 * @tree_view: a #PsppSheetView
 * @targets: the table of targets that the drag will support
 * @n_targets: the number of items in @targets
 * @actions: the bitmask of possible actions for a drag from this
 *    widget
 * 
 * Turns @tree_view into a drop destination for automatic DND. Calling
 * this method sets #PsppSheetView:reorderable to %FALSE.
 **/
void
pspp_sheet_view_enable_model_drag_dest (PsppSheetView              *tree_view,
				      const GtkTargetEntry     *targets,
				      gint                      n_targets,
				      GdkDragAction             actions)
{
  TreeViewDragInfo *di;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  gtk_drag_dest_set (GTK_WIDGET (tree_view),
                     0,
                     targets,
                     n_targets,
                     actions);

  di = ensure_info (tree_view);
  di->dest_set = TRUE;

  unset_reorderable (tree_view);
}

/**
 * pspp_sheet_view_unset_rows_drag_source:
 * @tree_view: a #PsppSheetView
 *
 * Undoes the effect of
 * pspp_sheet_view_enable_model_drag_source(). Calling this method sets
 * #PsppSheetView:reorderable to %FALSE.
 **/
void
pspp_sheet_view_unset_rows_drag_source (PsppSheetView *tree_view)
{
  TreeViewDragInfo *di;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  di = get_info (tree_view);

  if (di)
    {
      if (di->source_set)
        {
          gtk_drag_source_unset (GTK_WIDGET (tree_view));
          di->source_set = FALSE;
        }

      if (!di->dest_set && !di->source_set)
        remove_info (tree_view);
    }
  
  unset_reorderable (tree_view);
}

/**
 * pspp_sheet_view_unset_rows_drag_dest:
 * @tree_view: a #PsppSheetView
 *
 * Undoes the effect of
 * pspp_sheet_view_enable_model_drag_dest(). Calling this method sets
 * #PsppSheetView:reorderable to %FALSE.
 **/
void
pspp_sheet_view_unset_rows_drag_dest (PsppSheetView *tree_view)
{
  TreeViewDragInfo *di;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  di = get_info (tree_view);

  if (di)
    {
      if (di->dest_set)
        {
          gtk_drag_dest_unset (GTK_WIDGET (tree_view));
          di->dest_set = FALSE;
        }

      if (!di->dest_set && !di->source_set)
        remove_info (tree_view);
    }

  unset_reorderable (tree_view);
}

/**
 * pspp_sheet_view_set_drag_dest_row:
 * @tree_view: a #PsppSheetView
 * @path: (allow-none): The path of the row to highlight, or %NULL.
 * @pos: Specifies whether to drop before, after or into the row
 * 
 * Sets the row that is highlighted for feedback.
 **/
void
pspp_sheet_view_set_drag_dest_row (PsppSheetView            *tree_view,
                                 GtkTreePath            *path,
                                 PsppSheetViewDropPosition pos)
{
  GtkTreePath *current_dest;

  /* Note; this function is exported to allow a custom DND
   * implementation, so it can't touch TreeViewDragInfo
   */

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  current_dest = NULL;

  if (tree_view->priv->drag_dest_row)
    {
      current_dest = gtk_tree_row_reference_get_path (tree_view->priv->drag_dest_row);
      gtk_tree_row_reference_free (tree_view->priv->drag_dest_row);
    }

  /* special case a drop on an empty model */
  tree_view->priv->empty_view_drop = 0;

  if (pos == PSPP_SHEET_VIEW_DROP_BEFORE && path
      && gtk_tree_path_get_depth (path) == 1
      && gtk_tree_path_get_indices (path)[0] == 0)
    {
      gint n_children;

      n_children = gtk_tree_model_iter_n_children (tree_view->priv->model,
                                                   NULL);

      if (!n_children)
        tree_view->priv->empty_view_drop = 1;
    }

  tree_view->priv->drag_dest_pos = pos;

  if (path)
    {
      tree_view->priv->drag_dest_row =
        gtk_tree_row_reference_new_proxy (G_OBJECT (tree_view), tree_view->priv->model, path);
      pspp_sheet_view_queue_draw_path (tree_view, path, NULL);
    }
  else
    tree_view->priv->drag_dest_row = NULL;

  if (current_dest)
    {
      int node, new_node;

      _pspp_sheet_view_find_node (tree_view, current_dest, &node);
      _pspp_sheet_view_queue_draw_node (tree_view, node, NULL);

      if (node >= 0)
	{
	  new_node = pspp_sheet_view_node_next (tree_view, node);
	  if (new_node >= 0)
	    _pspp_sheet_view_queue_draw_node (tree_view, new_node, NULL);

	  new_node = pspp_sheet_view_node_prev (tree_view, node);
	  if (new_node >= 0)
	    _pspp_sheet_view_queue_draw_node (tree_view, new_node, NULL);
	}
      gtk_tree_path_free (current_dest);
    }
}

/**
 * pspp_sheet_view_get_drag_dest_row:
 * @tree_view: a #PsppSheetView
 * @path: (allow-none): Return location for the path of the highlighted row, or %NULL.
 * @pos: (allow-none): Return location for the drop position, or %NULL
 * 
 * Gets information about the row that is highlighted for feedback.
 **/
void
pspp_sheet_view_get_drag_dest_row (PsppSheetView              *tree_view,
                                 GtkTreePath             **path,
                                 PsppSheetViewDropPosition  *pos)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  if (path)
    {
      if (tree_view->priv->drag_dest_row)
        *path = gtk_tree_row_reference_get_path (tree_view->priv->drag_dest_row);
      else
        {
          if (tree_view->priv->empty_view_drop)
            *path = gtk_tree_path_new_from_indices (0, -1);
          else
            *path = NULL;
        }
    }

  if (pos)
    *pos = tree_view->priv->drag_dest_pos;
}

/**
 * pspp_sheet_view_get_dest_row_at_pos:
 * @tree_view: a #PsppSheetView
 * @drag_x: the position to determine the destination row for
 * @drag_y: the position to determine the destination row for
 * @path: (allow-none): Return location for the path of the highlighted row, or %NULL.
 * @pos: (allow-none): Return location for the drop position, or %NULL
 * 
 * Determines the destination row for a given position.  @drag_x and
 * @drag_y are expected to be in widget coordinates.  This function is only
 * meaningful if @tree_view is realized.  Therefore this function will always
 * return %FALSE if @tree_view is not realized or does not have a model.
 * 
 * Return value: whether there is a row at the given position, %TRUE if this
 * is indeed the case.
 **/
gboolean
pspp_sheet_view_get_dest_row_at_pos (PsppSheetView             *tree_view,
                                   gint                     drag_x,
                                   gint                     drag_y,
                                   GtkTreePath            **path,
                                   PsppSheetViewDropPosition *pos)
{
  gint cell_y;
  gint bin_x, bin_y;
  gdouble offset_into_row;
  gdouble third;
  GdkRectangle cell;
  PsppSheetViewColumn *column = NULL;
  GtkTreePath *tmp_path = NULL;

  /* Note; this function is exported to allow a custom DND
   * implementation, so it can't touch TreeViewDragInfo
   */

  g_return_val_if_fail (tree_view != NULL, FALSE);
  g_return_val_if_fail (drag_x >= 0, FALSE);
  g_return_val_if_fail (drag_y >= 0, FALSE);

  if (path)
    *path = NULL;

  if (tree_view->priv->bin_window == NULL)
    return FALSE;

  if (tree_view->priv->row_count == 0)
    return FALSE;

  /* If in the top third of a row, we drop before that row; if
   * in the bottom third, drop after that row; if in the middle,
   * and the row has children, drop into the row.
   */
  pspp_sheet_view_convert_widget_to_bin_window_coords (tree_view, drag_x, drag_y,
						     &bin_x, &bin_y);

  if (!pspp_sheet_view_get_path_at_pos (tree_view,
				      bin_x,
				      bin_y,
                                      &tmp_path,
                                      &column,
                                      NULL,
                                      &cell_y))
    return FALSE;

  pspp_sheet_view_get_background_area (tree_view, tmp_path, column,
                                     &cell);

  offset_into_row = cell_y;

  if (path)
    *path = tmp_path;
  else
    gtk_tree_path_free (tmp_path);

  tmp_path = NULL;

  third = cell.height / 3.0;

  if (pos)
    {
      if (offset_into_row < third)
        {
          *pos = PSPP_SHEET_VIEW_DROP_BEFORE;
        }
      else if (offset_into_row < (cell.height / 2.0))
        {
          *pos = PSPP_SHEET_VIEW_DROP_INTO_OR_BEFORE;
        }
      else if (offset_into_row < third * 2.0)
        {
          *pos = PSPP_SHEET_VIEW_DROP_INTO_OR_AFTER;
        }
      else
        {
          *pos = PSPP_SHEET_VIEW_DROP_AFTER;
        }
    }

  return TRUE;
}


#if GTK3_TRANSITION
/* KEEP IN SYNC WITH PSPP_SHEET_VIEW_BIN_EXPOSE */
/**
 * pspp_sheet_view_create_row_drag_icon:
 * @tree_view: a #PsppSheetView
 * @path: a #GtkTreePath in @tree_view
 *
 * Creates a #GdkPixmap representation of the row at @path.  
 * This image is used for a drag icon.
 *
 * Return value: a newly-allocated pixmap of the drag icon.
 **/
GdkPixmap *
pspp_sheet_view_create_row_drag_icon (PsppSheetView  *tree_view,
                                    GtkTreePath  *path)
{
  GtkTreeIter   iter;
  int node;
  gint cell_offset;
  GList *list;
  GdkRectangle background_area;
  GdkRectangle expose_area;
  GtkWidget *widget;
  /* start drawing inside the black outline */
  gint x = 1, y = 1;
  GdkDrawable *drawable;
  gint bin_window_width;
  gboolean rtl;

  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), NULL);
  g_return_val_if_fail (path != NULL, NULL);

  widget = GTK_WIDGET (tree_view);

  if (!gtk_widget_get_realized (widget))
    return NULL;

  _pspp_sheet_view_find_node (tree_view,
                            path,
                            &node);

  if (node < 0)
    return NULL;

  if (!gtk_tree_model_get_iter (tree_view->priv->model,
                                &iter,
                                path))
    return NULL;
  
  cell_offset = x;

  background_area.y = y;
  background_area.height = ROW_HEIGHT (tree_view);

  bin_window_width = gdk_window_get_width (tree_view->priv->bin_window);

  drawable = gdk_pixmap_new (tree_view->priv->bin_window,
                             bin_window_width + 2,
                             background_area.height + 2,
                             -1);

  expose_area.x = 0;
  expose_area.y = 0;
  expose_area.width = bin_window_width + 2;
  expose_area.height = background_area.height + 2;

#if GTK3_TRANSITION
  gdk_draw_rectangle (drawable,
                      widget->style->base_gc [gtk_widget_get_state (widget)],
                      TRUE,
                      0, 0,
                      bin_window_width + 2,
                      background_area.height + 2);
#endif

  rtl = gtk_widget_get_direction (GTK_WIDGET (tree_view)) == GTK_TEXT_DIR_RTL;

  for (list = (rtl ? g_list_last (tree_view->priv->columns) : g_list_first (tree_view->priv->columns));
      list;
      list = (rtl ? list->prev : list->next))
    {
      PsppSheetViewColumn *column = list->data;
      GdkRectangle cell_area;
      gint vertical_separator;

      if (!column->visible)
        continue;

      pspp_sheet_view_column_cell_set_cell_data (column, tree_view->priv->model, &iter);

      background_area.x = cell_offset;
      background_area.width = column->width;

      gtk_widget_style_get (widget,
			    "vertical-separator", &vertical_separator,
			    NULL);

      cell_area = background_area;

      cell_area.y += vertical_separator / 2;
      cell_area.height -= vertical_separator;

      if (pspp_sheet_view_column_cell_is_visible (column))
        _pspp_sheet_view_column_cell_render (column,
                                             drawable,
                                             &background_area,
                                             &cell_area,
                                             &expose_area,
                                             0);
      cell_offset += column->width;
    }

#if GTK3_TRANSITION
  gdk_draw_rectangle (drawable,
                      widget->style->black_gc,
                      FALSE,
                      0, 0,
                      bin_window_width + 1,
                      background_area.height + 1);
#endif

  return drawable;
}
#endif

/**
 * pspp_sheet_view_set_destroy_count_func:
 * @tree_view: A #PsppSheetView
 * @func: (allow-none): Function to be called when a view row is destroyed, or %NULL
 * @data: (allow-none): User data to be passed to @func, or %NULL
 * @destroy: (allow-none): Destroy notifier for @data, or %NULL
 *
 * This function should almost never be used.  It is meant for private use by
 * ATK for determining the number of visible children that are removed when a row is deleted.
 **/
void
pspp_sheet_view_set_destroy_count_func (PsppSheetView             *tree_view,
				      PsppSheetDestroyCountFunc  func,
				      gpointer                 data,
				      GDestroyNotify           destroy)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  if (tree_view->priv->destroy_count_destroy)
    tree_view->priv->destroy_count_destroy (tree_view->priv->destroy_count_data);

  tree_view->priv->destroy_count_func = func;
  tree_view->priv->destroy_count_data = data;
  tree_view->priv->destroy_count_destroy = destroy;
}


/*
 * Interactive search
 */

/**
 * pspp_sheet_view_set_enable_search:
 * @tree_view: A #PsppSheetView
 * @enable_search: %TRUE, if the user can search interactively
 *
 * If @enable_search is set, then the user can type in text to search through
 * the tree interactively (this is sometimes called "typeahead find").
 * 
 * Note that even if this is %FALSE, the user can still initiate a search 
 * using the "start-interactive-search" key binding.
 */
void
pspp_sheet_view_set_enable_search (PsppSheetView *tree_view,
				 gboolean     enable_search)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  enable_search = !!enable_search;
  
  if (tree_view->priv->enable_search != enable_search)
    {
       tree_view->priv->enable_search = enable_search;
       g_object_notify (G_OBJECT (tree_view), "enable-search");
    }
}

/**
 * pspp_sheet_view_get_enable_search:
 * @tree_view: A #PsppSheetView
 *
 * Returns whether or not the tree allows to start interactive searching 
 * by typing in text.
 *
 * Return value: whether or not to let the user search interactively
 */
gboolean
pspp_sheet_view_get_enable_search (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), FALSE);

  return tree_view->priv->enable_search;
}


/**
 * pspp_sheet_view_get_search_column:
 * @tree_view: A #PsppSheetView
 *
 * Gets the column searched on by the interactive search code.
 *
 * Return value: the column the interactive search code searches in.
 */
gint
pspp_sheet_view_get_search_column (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), -1);

  return (tree_view->priv->search_column);
}

/**
 * pspp_sheet_view_set_search_column:
 * @tree_view: A #PsppSheetView
 * @column: the column of the model to search in, or -1 to disable searching
 *
 * Sets @column as the column where the interactive search code should
 * search in for the current model. 
 * 
 * If the search column is set, users can use the "start-interactive-search"
 * key binding to bring up search popup. The enable-search property controls
 * whether simply typing text will also start an interactive search.
 *
 * Note that @column refers to a column of the current model. The search 
 * column is reset to -1 when the model is changed.
 */
void
pspp_sheet_view_set_search_column (PsppSheetView *tree_view,
				 gint         column)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (column >= -1);

  if (tree_view->priv->search_column == column)
    return;

  tree_view->priv->search_column = column;
  g_object_notify (G_OBJECT (tree_view), "search-column");
}

/**
 * pspp_sheet_view_get_search_equal_func:
 * @tree_view: A #PsppSheetView
 *
 * Returns the compare function currently in use.
 *
 * Return value: the currently used compare function for the search code.
 */

PsppSheetViewSearchEqualFunc
pspp_sheet_view_get_search_equal_func (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), NULL);

  return tree_view->priv->search_equal_func;
}

/**
 * pspp_sheet_view_set_search_equal_func:
 * @tree_view: A #PsppSheetView
 * @search_equal_func: the compare function to use during the search
 * @search_user_data: (allow-none): user data to pass to @search_equal_func, or %NULL
 * @search_destroy: (allow-none): Destroy notifier for @search_user_data, or %NULL
 *
 * Sets the compare function for the interactive search capabilities; note
 * that somewhat like strcmp() returning 0 for equality
 * #PsppSheetViewSearchEqualFunc returns %FALSE on matches.
 **/
void
pspp_sheet_view_set_search_equal_func (PsppSheetView                *tree_view,
				     PsppSheetViewSearchEqualFunc  search_equal_func,
				     gpointer                    search_user_data,
				     GDestroyNotify              search_destroy)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (search_equal_func != NULL);

  if (tree_view->priv->search_destroy)
    tree_view->priv->search_destroy (tree_view->priv->search_user_data);

  tree_view->priv->search_equal_func = search_equal_func;
  tree_view->priv->search_user_data = search_user_data;
  tree_view->priv->search_destroy = search_destroy;
  if (tree_view->priv->search_equal_func == NULL)
    tree_view->priv->search_equal_func = pspp_sheet_view_search_equal_func;
}

/**
 * pspp_sheet_view_get_search_entry:
 * @tree_view: A #PsppSheetView
 *
 * Returns the #GtkEntry which is currently in use as interactive search
 * entry for @tree_view.  In case the built-in entry is being used, %NULL
 * will be returned.
 *
 * Return value: the entry currently in use as search entry.
 *
 * Since: 2.10
 */
GtkEntry *
pspp_sheet_view_get_search_entry (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), NULL);

  if (tree_view->priv->search_custom_entry_set)
    return GTK_ENTRY (tree_view->priv->search_entry);

  return NULL;
}

/**
 * pspp_sheet_view_set_search_entry:
 * @tree_view: A #PsppSheetView
 * @entry: (allow-none): the entry the interactive search code of @tree_view should use or %NULL
 *
 * Sets the entry which the interactive search code will use for this
 * @tree_view.  This is useful when you want to provide a search entry
 * in our interface at all time at a fixed position.  Passing %NULL for
 * @entry will make the interactive search code use the built-in popup
 * entry again.
 *
 * Since: 2.10
 */
void
pspp_sheet_view_set_search_entry (PsppSheetView *tree_view,
				GtkEntry    *entry)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (entry == NULL || GTK_IS_ENTRY (entry));

  if (tree_view->priv->search_custom_entry_set)
    {
      if (tree_view->priv->search_entry_changed_id)
        {
	  g_signal_handler_disconnect (tree_view->priv->search_entry,
				       tree_view->priv->search_entry_changed_id);
	  tree_view->priv->search_entry_changed_id = 0;
	}
      g_signal_handlers_disconnect_by_func (tree_view->priv->search_entry,
					    G_CALLBACK (pspp_sheet_view_search_key_press_event),
					    tree_view);

      g_object_unref (tree_view->priv->search_entry);
    }
  else if (tree_view->priv->search_window)
    {
      gtk_widget_destroy (tree_view->priv->search_window);

      tree_view->priv->search_window = NULL;
    }

  if (entry)
    {
      tree_view->priv->search_entry = g_object_ref (entry);
      tree_view->priv->search_custom_entry_set = TRUE;

      if (tree_view->priv->search_entry_changed_id == 0)
        {
          tree_view->priv->search_entry_changed_id =
	    g_signal_connect (tree_view->priv->search_entry, "changed",
			      G_CALLBACK (pspp_sheet_view_search_init),
			      tree_view);
	}
      
        g_signal_connect (tree_view->priv->search_entry, "key-press-event",
		          G_CALLBACK (pspp_sheet_view_search_key_press_event),
		          tree_view);

	pspp_sheet_view_search_init (tree_view->priv->search_entry, tree_view);
    }
  else
    {
      tree_view->priv->search_entry = NULL;
      tree_view->priv->search_custom_entry_set = FALSE;
    }
}

/**
 * pspp_sheet_view_set_search_position_func:
 * @tree_view: A #PsppSheetView
 * @func: (allow-none): the function to use to position the search dialog, or %NULL
 *    to use the default search position function
 * @data: (allow-none): user data to pass to @func, or %NULL
 * @destroy: (allow-none): Destroy notifier for @data, or %NULL
 *
 * Sets the function to use when positioning the search dialog.
 *
 * Since: 2.10
 **/
void
pspp_sheet_view_set_search_position_func (PsppSheetView                   *tree_view,
				        PsppSheetViewSearchPositionFunc  func,
				        gpointer                       user_data,
				        GDestroyNotify                 destroy)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  if (tree_view->priv->search_position_destroy)
    tree_view->priv->search_position_destroy (tree_view->priv->search_position_user_data);

  tree_view->priv->search_position_func = func;
  tree_view->priv->search_position_user_data = user_data;
  tree_view->priv->search_position_destroy = destroy;
  if (tree_view->priv->search_position_func == NULL)
    tree_view->priv->search_position_func = pspp_sheet_view_search_position_func;
}

/**
 * pspp_sheet_view_get_search_position_func:
 * @tree_view: A #PsppSheetView
 *
 * Returns the positioning function currently in use.
 *
 * Return value: the currently used function for positioning the search dialog.
 *
 * Since: 2.10
 */
PsppSheetViewSearchPositionFunc
pspp_sheet_view_get_search_position_func (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), NULL);

  return tree_view->priv->search_position_func;
}


static void
pspp_sheet_view_search_dialog_hide (GtkWidget   *search_dialog,
				  PsppSheetView *tree_view)
{
  if (tree_view->priv->disable_popdown)
    return;

  if (tree_view->priv->search_entry_changed_id)
    {
      g_signal_handler_disconnect (tree_view->priv->search_entry,
				   tree_view->priv->search_entry_changed_id);
      tree_view->priv->search_entry_changed_id = 0;
    }
  if (tree_view->priv->typeselect_flush_timeout)
    {
      g_source_remove (tree_view->priv->typeselect_flush_timeout);
      tree_view->priv->typeselect_flush_timeout = 0;
    }
	
  if (gtk_widget_get_visible (search_dialog))
    {
      /* send focus-in event */
      send_focus_change (GTK_WIDGET (tree_view->priv->search_entry), FALSE);
      gtk_widget_hide (search_dialog);
      gtk_entry_set_text (GTK_ENTRY (tree_view->priv->search_entry), "");
      send_focus_change (GTK_WIDGET (tree_view), TRUE);
    }
}

static void
pspp_sheet_view_search_position_func (PsppSheetView *tree_view,
				    GtkWidget   *search_dialog,
				    gpointer     user_data)
{
  gint x, y;
  gint tree_x, tree_y;
  gint tree_width, tree_height;
  GdkWindow *tree_window = gtk_widget_get_window (GTK_WIDGET (tree_view));
  GdkScreen *screen = gdk_window_get_screen (tree_window);
  GtkRequisition requisition;
  gint monitor_num;
  GdkRectangle monitor;

  monitor_num = gdk_screen_get_monitor_at_window (screen, tree_window);
  gdk_screen_get_monitor_geometry (screen, monitor_num, &monitor);

  gtk_widget_realize (search_dialog);

  gdk_window_get_origin (tree_window, &tree_x, &tree_y);
  tree_width = gdk_window_get_width (tree_window);
  tree_height = gdk_window_get_height (tree_window);

  gtk_widget_size_request (search_dialog, &requisition);

  if (tree_x + tree_width > gdk_screen_get_width (screen))
    x = gdk_screen_get_width (screen) - requisition.width;
  else if (tree_x + tree_width - requisition.width < 0)
    x = 0;
  else
    x = tree_x + tree_width - requisition.width;

  if (tree_y + tree_height + requisition.height > gdk_screen_get_height (screen))
    y = gdk_screen_get_height (screen) - requisition.height;
  else if (tree_y + tree_height < 0) /* isn't really possible ... */
    y = 0;
  else
    y = tree_y + tree_height;

  gtk_window_move (GTK_WINDOW (search_dialog), x, y);
}

static void
pspp_sheet_view_search_disable_popdown (GtkEntry *entry,
				      GtkMenu  *menu,
				      gpointer  data)
{
  PsppSheetView *tree_view = (PsppSheetView *)data;

  tree_view->priv->disable_popdown = 1;
  g_signal_connect (menu, "hide",
		    G_CALLBACK (pspp_sheet_view_search_enable_popdown), data);
}

#if GTK3_TRANSITION
/* Because we're visible but offscreen, we just set a flag in the preedit
 * callback.
 */
static void
pspp_sheet_view_search_preedit_changed (GtkIMContext *im_context,
				      PsppSheetView  *tree_view)
{
  tree_view->priv->imcontext_changed = 1;
  if (tree_view->priv->typeselect_flush_timeout)
    {
      g_source_remove (tree_view->priv->typeselect_flush_timeout);
      tree_view->priv->typeselect_flush_timeout =
	gdk_threads_add_timeout (PSPP_SHEET_VIEW_SEARCH_DIALOG_TIMEOUT,
		       (GSourceFunc) pspp_sheet_view_search_entry_flush_timeout,
		       tree_view);
    }

}
#endif

static void
pspp_sheet_view_search_activate (GtkEntry    *entry,
			       PsppSheetView *tree_view)
{
  GtkTreePath *path;
  int node;

  pspp_sheet_view_search_dialog_hide (tree_view->priv->search_window,
				    tree_view);

  /* If we have a row selected and it's the cursor row, we activate
   * the row XXX */
  if (gtk_tree_row_reference_valid (tree_view->priv->cursor))
    {
      path = gtk_tree_row_reference_get_path (tree_view->priv->cursor);
      
      _pspp_sheet_view_find_node (tree_view, path, &node);
      
      if (node >= 0 && pspp_sheet_view_node_is_selected (tree_view, node))
	pspp_sheet_view_row_activated (tree_view, path, tree_view->priv->focus_column);
      
      gtk_tree_path_free (path);
    }
}

static gboolean
pspp_sheet_view_real_search_enable_popdown (gpointer data)
{
  PsppSheetView *tree_view = (PsppSheetView *)data;

  tree_view->priv->disable_popdown = 0;

  return FALSE;
}

static void
pspp_sheet_view_search_enable_popdown (GtkWidget *widget,
				     gpointer   data)
{
  gdk_threads_add_timeout_full (G_PRIORITY_HIGH, 200, pspp_sheet_view_real_search_enable_popdown, g_object_ref (data), g_object_unref);
}

static gboolean
pspp_sheet_view_search_delete_event (GtkWidget *widget,
				   GdkEventAny *event,
				   PsppSheetView *tree_view)
{
  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);

  pspp_sheet_view_search_dialog_hide (widget, tree_view);

  return TRUE;
}

static gboolean
pspp_sheet_view_search_button_press_event (GtkWidget *widget,
					 GdkEventButton *event,
					 PsppSheetView *tree_view)
{
  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);

  pspp_sheet_view_search_dialog_hide (widget, tree_view);

  if (event->window == tree_view->priv->bin_window)
    pspp_sheet_view_button_press (GTK_WIDGET (tree_view), event);

  return TRUE;
}

static gboolean
pspp_sheet_view_search_scroll_event (GtkWidget *widget,
				   GdkEventScroll *event,
				   PsppSheetView *tree_view)
{
  gboolean retval = FALSE;

  if (event->direction == GDK_SCROLL_UP)
    {
      pspp_sheet_view_search_move (widget, tree_view, TRUE);
      retval = TRUE;
    }
  else if (event->direction == GDK_SCROLL_DOWN)
    {
      pspp_sheet_view_search_move (widget, tree_view, FALSE);
      retval = TRUE;
    }

  /* renew the flush timeout */
  if (retval && tree_view->priv->typeselect_flush_timeout
      && !tree_view->priv->search_custom_entry_set)
    {
      g_source_remove (tree_view->priv->typeselect_flush_timeout);
      tree_view->priv->typeselect_flush_timeout =
	gdk_threads_add_timeout (PSPP_SHEET_VIEW_SEARCH_DIALOG_TIMEOUT,
		       (GSourceFunc) pspp_sheet_view_search_entry_flush_timeout,
		       tree_view);
    }

  return retval;
}

static gboolean
pspp_sheet_view_search_key_press_event (GtkWidget *widget,
				      GdkEventKey *event,
				      PsppSheetView *tree_view)
{
  gboolean retval = FALSE;

  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), FALSE);

  /* close window and cancel the search */
  if (!tree_view->priv->search_custom_entry_set
      && (event->keyval == GDK_Escape ||
          event->keyval == GDK_Tab ||
	    event->keyval == GDK_KP_Tab ||
	    event->keyval == GDK_ISO_Left_Tab))
    {
      pspp_sheet_view_search_dialog_hide (widget, tree_view);
      return TRUE;
    }

  /* select previous matching iter */
  if (event->keyval == GDK_Up || event->keyval == GDK_KP_Up)
    {
      if (!pspp_sheet_view_search_move (widget, tree_view, TRUE))
        gtk_widget_error_bell (widget);

      retval = TRUE;
    }

  if (((event->state & (GTK_DEFAULT_ACCEL_MOD_MASK | GDK_SHIFT_MASK)) == (GTK_DEFAULT_ACCEL_MOD_MASK | GDK_SHIFT_MASK))
      && (event->keyval == GDK_g || event->keyval == GDK_G))
    {
      if (!pspp_sheet_view_search_move (widget, tree_view, TRUE))
        gtk_widget_error_bell (widget);

      retval = TRUE;
    }

  /* select next matching iter */
  if (event->keyval == GDK_Down || event->keyval == GDK_KP_Down)
    {
      if (!pspp_sheet_view_search_move (widget, tree_view, FALSE))
        gtk_widget_error_bell (widget);

      retval = TRUE;
    }

  if (((event->state & (GTK_DEFAULT_ACCEL_MOD_MASK | GDK_SHIFT_MASK)) == GTK_DEFAULT_ACCEL_MOD_MASK)
      && (event->keyval == GDK_g || event->keyval == GDK_G))
    {
      if (!pspp_sheet_view_search_move (widget, tree_view, FALSE))
        gtk_widget_error_bell (widget);

      retval = TRUE;
    }

  /* renew the flush timeout */
  if (retval && tree_view->priv->typeselect_flush_timeout
      && !tree_view->priv->search_custom_entry_set)
    {
      g_source_remove (tree_view->priv->typeselect_flush_timeout);
      tree_view->priv->typeselect_flush_timeout =
	gdk_threads_add_timeout (PSPP_SHEET_VIEW_SEARCH_DIALOG_TIMEOUT,
		       (GSourceFunc) pspp_sheet_view_search_entry_flush_timeout,
		       tree_view);
    }

  return retval;
}

/*  this function returns FALSE if there is a search string but
 *  nothing was found, and TRUE otherwise.
 */
static gboolean
pspp_sheet_view_search_move (GtkWidget   *window,
			   PsppSheetView *tree_view,
			   gboolean     up)
{
  gboolean ret;
  gint len;
  gint count = 0;
  const gchar *text;
  GtkTreeIter iter;
  GtkTreeModel *model;
  PsppSheetSelection *selection;

  text = gtk_entry_get_text (GTK_ENTRY (tree_view->priv->search_entry));

  g_return_val_if_fail (text != NULL, FALSE);

  len = strlen (text);

  if (up && tree_view->priv->selected_iter == 1)
    return strlen (text) < 1;

  len = strlen (text);

  if (len < 1)
    return TRUE;

  model = pspp_sheet_view_get_model (tree_view);
  selection = pspp_sheet_view_get_selection (tree_view);

  /* search */
  pspp_sheet_selection_unselect_all (selection);
  if (!gtk_tree_model_get_iter_first (model, &iter))
    return TRUE;

  ret = pspp_sheet_view_search_iter (model, selection, &iter, text,
				   &count, up?((tree_view->priv->selected_iter) - 1):((tree_view->priv->selected_iter + 1)));

  if (ret)
    {
      /* found */
      tree_view->priv->selected_iter += up?(-1):(1);
      return TRUE;
    }
  else
    {
      /* return to old iter */
      count = 0;
      gtk_tree_model_get_iter_first (model, &iter);
      pspp_sheet_view_search_iter (model, selection,
				 &iter, text,
				 &count, tree_view->priv->selected_iter);
      return FALSE;
    }
}

static gboolean
pspp_sheet_view_search_equal_func (GtkTreeModel *model,
				 gint          column,
				 const gchar  *key,
				 GtkTreeIter  *iter,
				 gpointer      search_data)
{
  gboolean retval = TRUE;
  const gchar *str;
  gchar *normalized_string;
  gchar *normalized_key;
  gchar *case_normalized_string = NULL;
  gchar *case_normalized_key = NULL;
  GValue value = {0,};
  GValue transformed = {0,};

  gtk_tree_model_get_value (model, iter, column, &value);

  g_value_init (&transformed, G_TYPE_STRING);

  if (!g_value_transform (&value, &transformed))
    {
      g_value_unset (&value);
      return TRUE;
    }

  g_value_unset (&value);

  str = g_value_get_string (&transformed);
  if (!str)
    {
      g_value_unset (&transformed);
      return TRUE;
    }

  normalized_string = g_utf8_normalize (str, -1, G_NORMALIZE_ALL);
  normalized_key = g_utf8_normalize (key, -1, G_NORMALIZE_ALL);

  if (normalized_string && normalized_key)
    {
      case_normalized_string = g_utf8_casefold (normalized_string, -1);
      case_normalized_key = g_utf8_casefold (normalized_key, -1);

      if (strncmp (case_normalized_key, case_normalized_string, strlen (case_normalized_key)) == 0)
        retval = FALSE;
    }

  g_value_unset (&transformed);
  g_free (normalized_key);
  g_free (normalized_string);
  g_free (case_normalized_key);
  g_free (case_normalized_string);

  return retval;
}

static gboolean
pspp_sheet_view_search_iter (GtkTreeModel     *model,
                             PsppSheetSelection *selection,
                             GtkTreeIter      *iter,
                             const gchar      *text,
                             gint             *count,
                             gint              n)
{
  int node = -1;
  GtkTreePath *path;

  PsppSheetView *tree_view = pspp_sheet_selection_get_tree_view (selection);

  path = gtk_tree_model_get_path (model, iter);
  _pspp_sheet_view_find_node (tree_view, path, &node);

  do
    {
      gboolean done = FALSE;

      if (! tree_view->priv->search_equal_func (model, tree_view->priv->search_column, text, iter, tree_view->priv->search_user_data))
        {
          (*count)++;
          if (*count == n)
            {
              pspp_sheet_view_scroll_to_cell (tree_view, path, NULL,
                                              TRUE, 0.5, 0.0);
              pspp_sheet_selection_select_iter (selection, iter);
              pspp_sheet_view_real_set_cursor (tree_view, path, FALSE, TRUE, 0);

	      if (path)
		gtk_tree_path_free (path);

              return TRUE;
            }
        }


      do
        {
          node = pspp_sheet_view_node_next (tree_view, node);

          if (node >= 0)
            {
              gboolean has_next;

              has_next = gtk_tree_model_iter_next (model, iter);

              done = TRUE;
              gtk_tree_path_next (path);

              /* sanity check */
              TREE_VIEW_INTERNAL_ASSERT (has_next, FALSE);
            }
          else
            {
              if (path)
                gtk_tree_path_free (path);

              /* we've run out of tree, done with this func */
              return FALSE;
            }
        }
      while (!done);
    }
  while (1);

  return FALSE;
}

static void
pspp_sheet_view_search_init (GtkWidget   *entry,
			   PsppSheetView *tree_view)
{
  gint ret;
  gint count = 0;
  const gchar *text;
  GtkTreeIter iter;
  GtkTreeModel *model;
  PsppSheetSelection *selection;

  g_return_if_fail (GTK_IS_ENTRY (entry));
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  text = gtk_entry_get_text (GTK_ENTRY (entry));

  model = pspp_sheet_view_get_model (tree_view);
  selection = pspp_sheet_view_get_selection (tree_view);

  /* search */
  pspp_sheet_selection_unselect_all (selection);
  if (tree_view->priv->typeselect_flush_timeout
      && !tree_view->priv->search_custom_entry_set)
    {
      g_source_remove (tree_view->priv->typeselect_flush_timeout);
      tree_view->priv->typeselect_flush_timeout =
	gdk_threads_add_timeout (PSPP_SHEET_VIEW_SEARCH_DIALOG_TIMEOUT,
		       (GSourceFunc) pspp_sheet_view_search_entry_flush_timeout,
		       tree_view);
    }

  if (*text == '\0')
    return;

  if (!gtk_tree_model_get_iter_first (model, &iter))
    return;

  ret = pspp_sheet_view_search_iter (model, selection,
				   &iter, text,
				   &count, 1);

  if (ret)
    tree_view->priv->selected_iter = 1;
}

static void
pspp_sheet_view_remove_widget (GtkCellEditable *cell_editable,
			     PsppSheetView     *tree_view)
{
  if (tree_view->priv->edited_column == NULL)
    return;

  _pspp_sheet_view_column_stop_editing (tree_view->priv->edited_column);
  tree_view->priv->edited_column = NULL;

  if (gtk_widget_has_focus (GTK_WIDGET (cell_editable)))
    gtk_widget_grab_focus (GTK_WIDGET (tree_view));

  g_signal_handlers_disconnect_by_func (cell_editable,
					pspp_sheet_view_remove_widget,
					tree_view);
  g_signal_handlers_disconnect_by_func (cell_editable,
					pspp_sheet_view_editable_button_press_event,
					tree_view);
  g_signal_handlers_disconnect_by_func (cell_editable,
					pspp_sheet_view_editable_clicked,
					tree_view);

  gtk_container_remove (GTK_CONTAINER (tree_view),
			GTK_WIDGET (cell_editable));  

  /* FIXME should only redraw a single node */
  gtk_widget_queue_draw (GTK_WIDGET (tree_view));
}

static gboolean
pspp_sheet_view_start_editing (PsppSheetView *tree_view,
			     GtkTreePath *cursor_path)
{
  GtkTreeIter iter;
  GdkRectangle background_area;
  GdkRectangle cell_area;
  GtkCellEditable *editable_widget = NULL;
  gchar *path_string;
  guint flags = 0; /* can be 0, as the flags are primarily for rendering */
  gint retval = FALSE;
  int cursor_node;

  g_assert (tree_view->priv->focus_column);

  if (!gtk_widget_get_realized (GTK_WIDGET (tree_view)))
    return FALSE;

  _pspp_sheet_view_find_node (tree_view, cursor_path, &cursor_node);
  if (cursor_node < 0)
    return FALSE;

  path_string = gtk_tree_path_to_string (cursor_path);
  gtk_tree_model_get_iter (tree_view->priv->model, &iter, cursor_path);

  pspp_sheet_view_column_cell_set_cell_data (tree_view->priv->focus_column,
					   tree_view->priv->model,
					   &iter);
  pspp_sheet_view_get_background_area (tree_view,
				     cursor_path,
				     tree_view->priv->focus_column,
				     &background_area);
  pspp_sheet_view_get_cell_area (tree_view,
			       cursor_path,
			       tree_view->priv->focus_column,
			       &cell_area);

  if (_pspp_sheet_view_column_cell_event (tree_view->priv->focus_column,
					&editable_widget,
					NULL,
					path_string,
					&background_area,
					&cell_area,
					flags))
    {
      retval = TRUE;
      if (editable_widget != NULL)
	{
	  gint left, right;
	  GdkRectangle area;
	  GtkCellRenderer *cell;

	  area = cell_area;
	  cell = _pspp_sheet_view_column_get_edited_cell (tree_view->priv->focus_column);

	  _pspp_sheet_view_column_get_neighbor_sizes (tree_view->priv->focus_column, cell, &left, &right);

	  area.x += left;
	  area.width -= right + left;

	  pspp_sheet_view_real_start_editing (tree_view,
					    tree_view->priv->focus_column,
					    cursor_path,
					    editable_widget,
					    &area,
					    NULL,
					    flags);
	}

    }
  g_free (path_string);
  return retval;
}

static gboolean
pspp_sheet_view_editable_button_press_event (GtkWidget *widget,
                                             GdkEventButton *event,
                                             PsppSheetView *sheet_view)
{
  gint node;

  node = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget),
                                             "pspp-sheet-view-node"));
  return pspp_sheet_view_row_head_clicked (sheet_view,
                                           node,
                                           sheet_view->priv->edited_column,
                                           event);
}

static void
pspp_sheet_view_editable_clicked (GtkButton *button,
                                  PsppSheetView *sheet_view)
{
  pspp_sheet_view_editable_button_press_event (GTK_WIDGET (button), NULL,
                                               sheet_view);
}

static gboolean
is_all_selected (GtkWidget *widget)
{
  GtkEntryBuffer *buffer;
  gint start_pos, end_pos;

  if (!GTK_IS_ENTRY (widget))
    return FALSE;

  buffer = gtk_entry_get_buffer (GTK_ENTRY (widget));
  return (gtk_editable_get_selection_bounds (GTK_EDITABLE (widget),
                                             &start_pos, &end_pos)
          && start_pos == 0
          && end_pos == gtk_entry_buffer_get_length (buffer));
}

static gboolean
is_at_left (GtkWidget *widget)
{
  return (GTK_IS_ENTRY (widget)
          && gtk_editable_get_position (GTK_EDITABLE (widget)) == 0);
}

static gboolean
is_at_right (GtkWidget *widget)
{
  GtkEntryBuffer *buffer;
  gint length;

  if (!GTK_IS_ENTRY (widget))
    return FALSE;

  buffer = gtk_entry_get_buffer (GTK_ENTRY (widget));
  length = gtk_entry_buffer_get_length (buffer);
  return gtk_editable_get_position (GTK_EDITABLE (widget)) == length;
}

static gboolean
pspp_sheet_view_event (GtkWidget *widget,
                       GdkEventKey *event,
                       PsppSheetView *tree_view)
{
  PsppSheetViewColumn *column;
  GtkTreePath *path;
  gboolean handled;
  gboolean cancel;
  guint keyval;
  gint row;

  /* Intercept only key press events.
     It would make sense to use "key-press-event" instead of "event", but
     GtkEntry attaches its own signal handler to "key-press-event" that runs
     before ours and overrides our desired behavior for GDK_Up and GDK_Down.
  */
  if (event->type != GDK_KEY_PRESS)
    return FALSE;

  keyval = event->keyval;
  cancel = FALSE;
  switch (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK))
    {
    case 0:
      switch (event->keyval)
        {
        case GDK_Left:      case GDK_KP_Left:
        case GDK_Home:      case GDK_KP_Home:
          if (!is_all_selected (widget) && !is_at_left (widget))
            return FALSE;
          break;

        case GDK_Right:     case GDK_KP_Right:
        case GDK_End:       case GDK_KP_End:
          if (!is_all_selected (widget) && !is_at_right (widget))
            return FALSE;
          break;

        case GDK_Up:        case GDK_KP_Up:
        case GDK_Down:      case GDK_KP_Down:
          break;

        case GDK_Page_Up:   case GDK_KP_Page_Up:
        case GDK_Page_Down: case GDK_KP_Page_Down:
          break;

        case GDK_Escape:
          cancel = TRUE;
          break;

        case GDK_Return:
          keyval = GDK_Down;
          break;

        case GDK_Tab:       case GDK_KP_Tab:
        case GDK_ISO_Left_Tab:
          keyval = GDK_Tab;
          break;

        default:
          return FALSE;
        }
      break;

    case GDK_SHIFT_MASK:
      switch (event->keyval)
        {
        case GDK_Tab:
        case GDK_ISO_Left_Tab:
          keyval = GDK_Tab;
          break;

        default:
          return FALSE;
        }
      break;

    case GDK_CONTROL_MASK:
      switch (event->keyval)
        {
        case GDK_Left:      case GDK_KP_Left:
          if (!is_all_selected (widget) && !is_at_left (widget))
            return FALSE;
          break;

        case GDK_Right:     case GDK_KP_Right:
          if (!is_all_selected (widget) && !is_at_right (widget))
            return FALSE;
          break;

        case GDK_Up:        case GDK_KP_Up:
        case GDK_Down:      case GDK_KP_Down:
          break;

        default:
          return FALSE;
        }
      break;

    default:
      return FALSE;
    }

  row = tree_view->priv->edited_row;
  column = tree_view->priv->edited_column;
  path = gtk_tree_path_new_from_indices (row, -1);

  pspp_sheet_view_stop_editing (tree_view, cancel);
  gtk_widget_grab_focus (GTK_WIDGET (tree_view));

  pspp_sheet_view_set_cursor (tree_view, path, column, FALSE);
  gtk_tree_path_free (path);

  handled = gtk_binding_set_activate (edit_bindings, keyval, event->state,
                                      G_OBJECT (tree_view));
  if (handled)
    g_signal_stop_emission_by_name (widget, "event");

  pspp_sheet_view_get_cursor (tree_view, &path, NULL);
  pspp_sheet_view_start_editing (tree_view, path);
  gtk_tree_path_free (path);

  return handled;
}

static void
pspp_sheet_view_override_cell_keypresses (GtkWidget *widget,
                                          gpointer data)
{
  PsppSheetView *sheet_view = data;

  g_signal_connect (widget, "event",
                    G_CALLBACK (pspp_sheet_view_event),
                    sheet_view);

  if (GTK_IS_CONTAINER (widget))
    gtk_container_foreach (GTK_CONTAINER (widget),
                           pspp_sheet_view_override_cell_keypresses,
                           data);
}

static void
pspp_sheet_view_real_start_editing (PsppSheetView       *tree_view,
				  PsppSheetViewColumn *column,
				  GtkTreePath       *path,
				  GtkCellEditable   *cell_editable,
				  GdkRectangle      *cell_area,
				  GdkEvent          *event,
				  guint              flags)
{
  PsppSheetSelectionMode mode = pspp_sheet_selection_get_mode (tree_view->priv->selection);
  gint pre_val = gtk_adjustment_get_value (tree_view->priv->vadjustment);
  GtkRequisition requisition;
  gint row;

  g_return_if_fail (gtk_tree_path_get_depth (path) == 1);

  tree_view->priv->edited_column = column;
  _pspp_sheet_view_column_start_editing (column, GTK_CELL_EDITABLE (cell_editable));

  row = gtk_tree_path_get_indices (path)[0];
  tree_view->priv->edited_row = row;
  pspp_sheet_view_real_set_cursor (tree_view, path, FALSE, TRUE, 0);
  cell_area->y += pre_val - (int)gtk_adjustment_get_value (tree_view->priv->vadjustment);

  pspp_sheet_selection_unselect_all_columns (tree_view->priv->selection);
  pspp_sheet_selection_select_column (tree_view->priv->selection, column);
  tree_view->priv->anchor_column = column;

  gtk_widget_size_request (GTK_WIDGET (cell_editable), &requisition);

  PSPP_SHEET_VIEW_SET_FLAG (tree_view, PSPP_SHEET_VIEW_DRAW_KEYFOCUS);

  if (requisition.height < cell_area->height)
    {
      gint diff = cell_area->height - requisition.height;
      pspp_sheet_view_put (tree_view,
			 GTK_WIDGET (cell_editable),
			 cell_area->x, cell_area->y + diff/2,
			 cell_area->width, requisition.height);
    }
  else
    {
      pspp_sheet_view_put (tree_view,
			 GTK_WIDGET (cell_editable),
			 cell_area->x, cell_area->y,
			 cell_area->width, cell_area->height);
    }

  gtk_cell_editable_start_editing (GTK_CELL_EDITABLE (cell_editable),
				   (GdkEvent *)event);

  gtk_widget_grab_focus (GTK_WIDGET (cell_editable));
  g_signal_connect (cell_editable, "remove-widget",
		    G_CALLBACK (pspp_sheet_view_remove_widget), tree_view);
  if (mode == PSPP_SHEET_SELECTION_RECTANGLE && column->row_head &&
      GTK_IS_BUTTON (cell_editable))
    {
      g_signal_connect (cell_editable, "button-press-event",
                        G_CALLBACK (pspp_sheet_view_editable_button_press_event),
                        tree_view);
      g_object_set_data (G_OBJECT (cell_editable), "pspp-sheet-view-node",
                         GINT_TO_POINTER (row));
      g_signal_connect (cell_editable, "clicked",
                        G_CALLBACK (pspp_sheet_view_editable_clicked),
                        tree_view);
    }

  pspp_sheet_view_override_cell_keypresses (GTK_WIDGET (cell_editable),
                                            tree_view);
}

void
pspp_sheet_view_stop_editing (PsppSheetView *tree_view,
                              gboolean     cancel_editing)
{
  PsppSheetViewColumn *column;
  GtkCellRenderer *cell;

  if (tree_view->priv->edited_column == NULL)
    return;

  /*
   * This is very evil. We need to do this, because
   * gtk_cell_editable_editing_done may trigger pspp_sheet_view_row_changed
   * later on. If pspp_sheet_view_row_changed notices
   * tree_view->priv->edited_column != NULL, it'll call
   * pspp_sheet_view_stop_editing again. Bad things will happen then.
   *
   * Please read that again if you intend to modify anything here.
   */

  column = tree_view->priv->edited_column;
  tree_view->priv->edited_column = NULL;

  cell = _pspp_sheet_view_column_get_edited_cell (column);
  gtk_cell_renderer_stop_editing (cell, cancel_editing);

  if (!cancel_editing)
    gtk_cell_editable_editing_done (column->editable_widget);

  tree_view->priv->edited_column = column;

  gtk_cell_editable_remove_widget (column->editable_widget);
}


/**
 * pspp_sheet_view_set_hover_selection:
 * @tree_view: a #PsppSheetView
 * @hover: %TRUE to enable hover selection mode
 *
 * Enables of disables the hover selection mode of @tree_view.
 * Hover selection makes the selected row follow the pointer.
 * Currently, this works only for the selection modes 
 * %PSPP_SHEET_SELECTION_SINGLE and %PSPP_SHEET_SELECTION_BROWSE.
 * 
 * Since: 2.6
 **/
void     
pspp_sheet_view_set_hover_selection (PsppSheetView *tree_view,
				   gboolean     hover)
{
  hover = hover != FALSE;

  if (hover != tree_view->priv->hover_selection)
    {
      tree_view->priv->hover_selection = hover;

      g_object_notify (G_OBJECT (tree_view), "hover-selection");
    }
}

/**
 * pspp_sheet_view_get_hover_selection:
 * @tree_view: a #PsppSheetView
 * 
 * Returns whether hover selection mode is turned on for @tree_view.
 * 
 * Return value: %TRUE if @tree_view is in hover selection mode
 *
 * Since: 2.6 
 **/
gboolean 
pspp_sheet_view_get_hover_selection (PsppSheetView *tree_view)
{
  return tree_view->priv->hover_selection;
}

/**
 * pspp_sheet_view_set_rubber_banding:
 * @tree_view: a #PsppSheetView
 * @enable: %TRUE to enable rubber banding
 *
 * Enables or disables rubber banding in @tree_view.  If the selection mode is
 * #PSPP_SHEET_SELECTION_MULTIPLE or #PSPP_SHEET_SELECTION_RECTANGLE, rubber
 * banding will allow the user to select multiple rows by dragging the mouse.
 * 
 * Since: 2.10
 **/
void
pspp_sheet_view_set_rubber_banding (PsppSheetView *tree_view,
				  gboolean     enable)
{
  enable = enable != FALSE;

  if (enable != tree_view->priv->rubber_banding_enable)
    {
      tree_view->priv->rubber_banding_enable = enable;

      g_object_notify (G_OBJECT (tree_view), "rubber-banding");
    }
}

/**
 * pspp_sheet_view_get_rubber_banding:
 * @tree_view: a #PsppSheetView
 * 
 * Returns whether rubber banding is turned on for @tree_view.  If the
 * selection mode is #PSPP_SHEET_SELECTION_MULTIPLE or
 * #PSPP_SHEET_SELECTION_RECTANGLE, rubber banding will allow the user to
 * select multiple rows by dragging the mouse.
 * 
 * Return value: %TRUE if rubber banding in @tree_view is enabled.
 *
 * Since: 2.10
 **/
gboolean
pspp_sheet_view_get_rubber_banding (PsppSheetView *tree_view)
{
  return tree_view->priv->rubber_banding_enable;
}

/**
 * pspp_sheet_view_is_rubber_banding_active:
 * @tree_view: a #PsppSheetView
 * 
 * Returns whether a rubber banding operation is currently being done
 * in @tree_view.
 *
 * Return value: %TRUE if a rubber banding operation is currently being
 * done in @tree_view.
 *
 * Since: 2.12
 **/
gboolean
pspp_sheet_view_is_rubber_banding_active (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), FALSE);

  if (tree_view->priv->rubber_banding_enable
      && tree_view->priv->rubber_band_status == RUBBER_BAND_ACTIVE)
    return TRUE;

  return FALSE;
}

static void
pspp_sheet_view_grab_notify (GtkWidget *widget,
			   gboolean   was_grabbed)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);

  tree_view->priv->in_grab = !was_grabbed;

  if (!was_grabbed)
    {
      tree_view->priv->pressed_button = -1;

      if (tree_view->priv->rubber_band_status)
	pspp_sheet_view_stop_rubber_band (tree_view);
    }
}

static void
pspp_sheet_view_state_changed (GtkWidget      *widget,
		 	     GtkStateType    previous_state)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);

  if (gtk_widget_get_realized (widget))
    {
      GtkStyle *style = gtk_widget_get_style (widget);
      gdk_window_set_background (tree_view->priv->bin_window, &style->base[gtk_widget_get_state (widget)]);
    }

  gtk_widget_queue_draw (widget);
}

/**
 * pspp_sheet_view_get_grid_lines:
 * @tree_view: a #PsppSheetView
 *
 * Returns which grid lines are enabled in @tree_view.
 *
 * Return value: a #PsppSheetViewGridLines value indicating which grid lines
 * are enabled.
 *
 * Since: 2.10
 */
PsppSheetViewGridLines
pspp_sheet_view_get_grid_lines (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), 0);

  return tree_view->priv->grid_lines;
}

/**
 * pspp_sheet_view_set_grid_lines:
 * @tree_view: a #PsppSheetView
 * @grid_lines: a #PsppSheetViewGridLines value indicating which grid lines to
 * enable.
 *
 * Sets which grid lines to draw in @tree_view.
 *
 * Since: 2.10
 */
void
pspp_sheet_view_set_grid_lines (PsppSheetView           *tree_view,
			      PsppSheetViewGridLines   grid_lines)
{
  PsppSheetViewPrivate *priv;
  PsppSheetViewGridLines old_grid_lines;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  priv = tree_view->priv;

  old_grid_lines = priv->grid_lines;
  priv->grid_lines = grid_lines;
  
  if (old_grid_lines != grid_lines)
    {
      gtk_widget_queue_draw (GTK_WIDGET (tree_view));
      
      g_object_notify (G_OBJECT (tree_view), "enable-grid-lines");
    }
}

/**
 * pspp_sheet_view_get_special_cells:
 * @tree_view: a #PsppSheetView
 *
 * Returns which grid lines are enabled in @tree_view.
 *
 * Return value: a #PsppSheetViewSpecialCells value indicating whether rows in
 * the sheet view contain special cells.
 */
PsppSheetViewSpecialCells
pspp_sheet_view_get_special_cells (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), 0);

  return tree_view->priv->special_cells;
}

/**
 * pspp_sheet_view_set_special_cells:
 * @tree_view: a #PsppSheetView
 * @special_cells: a #PsppSheetViewSpecialCells value indicating whether rows in
 * the sheet view contain special cells.
 *
 * Sets whether rows in the sheet view contain special cells, controlling the
 * rendering of row selections.
 */
void
pspp_sheet_view_set_special_cells (PsppSheetView           *tree_view,
			      PsppSheetViewSpecialCells   special_cells)
{
  PsppSheetViewPrivate *priv;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  priv = tree_view->priv;

  if (priv->special_cells != special_cells)
    {
      priv->special_cells = special_cells;
      gtk_widget_queue_draw (GTK_WIDGET (tree_view));
      g_object_notify (G_OBJECT (tree_view), "special-cells");
    }
}

int
pspp_sheet_view_get_fixed_height (const PsppSheetView *tree_view)
{
  /* XXX (re)calculate fixed_height if necessary */
  return tree_view->priv->fixed_height;
}

void
pspp_sheet_view_set_fixed_height (PsppSheetView *tree_view,
                                  int fixed_height)
{
  g_return_if_fail (fixed_height > 0);

  if (tree_view->priv->fixed_height != fixed_height)
    {
      tree_view->priv->fixed_height = fixed_height;
      g_object_notify (G_OBJECT (tree_view), "fixed-height");
    }
  if (!tree_view->priv->fixed_height_set)
    {
      tree_view->priv->fixed_height_set = TRUE;
      g_object_notify (G_OBJECT (tree_view), "fixed-height-set");
    }
}

/**
 * pspp_sheet_view_set_tooltip_row:
 * @tree_view: a #PsppSheetView
 * @tooltip: a #GtkTooltip
 * @path: a #GtkTreePath
 *
 * Sets the tip area of @tooltip to be the area covered by the row at @path.
 * See also pspp_sheet_view_set_tooltip_column() for a simpler alternative.
 * See also gtk_tooltip_set_tip_area().
 *
 * Since: 2.12
 */
void
pspp_sheet_view_set_tooltip_row (PsppSheetView *tree_view,
			       GtkTooltip  *tooltip,
			       GtkTreePath *path)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (GTK_IS_TOOLTIP (tooltip));

  pspp_sheet_view_set_tooltip_cell (tree_view, tooltip, path, NULL, NULL);
}

/**
 * pspp_sheet_view_set_tooltip_cell:
 * @tree_view: a #PsppSheetView
 * @tooltip: a #GtkTooltip
 * @path: (allow-none): a #GtkTreePath or %NULL
 * @column: (allow-none): a #PsppSheetViewColumn or %NULL
 * @cell: (allow-none): a #GtkCellRenderer or %NULL
 *
 * Sets the tip area of @tooltip to the area @path, @column and @cell have
 * in common.  For example if @path is %NULL and @column is set, the tip
 * area will be set to the full area covered by @column.  See also
 * gtk_tooltip_set_tip_area().
 *
 * See also pspp_sheet_view_set_tooltip_column() for a simpler alternative.
 *
 * Since: 2.12
 */
void
pspp_sheet_view_set_tooltip_cell (PsppSheetView       *tree_view,
				GtkTooltip        *tooltip,
				GtkTreePath       *path,
				PsppSheetViewColumn *column,
				GtkCellRenderer   *cell)
{
  GdkRectangle rect;

  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));
  g_return_if_fail (GTK_IS_TOOLTIP (tooltip));
  g_return_if_fail (column == NULL || PSPP_IS_SHEET_VIEW_COLUMN (column));
  g_return_if_fail (cell == NULL || GTK_IS_CELL_RENDERER (cell));

  /* Determine x values. */
  if (column && cell)
    {
      GdkRectangle tmp;
      gint start, width;

      pspp_sheet_view_get_cell_area (tree_view, path, column, &tmp);
      pspp_sheet_view_column_cell_get_position (column, cell, &start, &width);

      pspp_sheet_view_convert_bin_window_to_widget_coords (tree_view,
							 tmp.x + start, 0,
							 &rect.x, NULL);
      rect.width = width;
    }
  else if (column)
    {
      GdkRectangle tmp;

      pspp_sheet_view_get_background_area (tree_view, NULL, column, &tmp);
      pspp_sheet_view_convert_bin_window_to_widget_coords (tree_view,
							 tmp.x, 0,
							 &rect.x, NULL);
      rect.width = tmp.width;
    }
  else
    {
      GtkAllocation allocation;
      gtk_widget_get_allocation (GTK_WIDGET (tree_view), &allocation);
      rect.x = 0;
      rect.width = allocation.width;
    }

  /* Determine y values. */
  if (path)
    {
      GdkRectangle tmp;

      pspp_sheet_view_get_background_area (tree_view, path, NULL, &tmp);
      pspp_sheet_view_convert_bin_window_to_widget_coords (tree_view,
							 0, tmp.y,
							 NULL, &rect.y);
      rect.height = tmp.height;
    }
  else
    {
      rect.y = 0;
      rect.height = gtk_adjustment_get_page_size (tree_view->priv->vadjustment);
    }

  gtk_tooltip_set_tip_area (tooltip, &rect);
}

/**
 * pspp_sheet_view_get_tooltip_context:
 * @tree_view: a #PsppSheetView
 * @x: the x coordinate (relative to widget coordinates)
 * @y: the y coordinate (relative to widget coordinates)
 * @keyboard_tip: whether this is a keyboard tooltip or not
 * @model: (allow-none): a pointer to receive a #GtkTreeModel or %NULL
 * @path: (allow-none): a pointer to receive a #GtkTreePath or %NULL
 * @iter: (allow-none): a pointer to receive a #GtkTreeIter or %NULL
 *
 * This function is supposed to be used in a #GtkWidget::query-tooltip
 * signal handler for #PsppSheetView.  The @x, @y and @keyboard_tip values
 * which are received in the signal handler, should be passed to this
 * function without modification.
 *
 * The return value indicates whether there is a tree view row at the given
 * coordinates (%TRUE) or not (%FALSE) for mouse tooltips.  For keyboard
 * tooltips the row returned will be the cursor row.  When %TRUE, then any of
 * @model, @path and @iter which have been provided will be set to point to
 * that row and the corresponding model.  @x and @y will always be converted
 * to be relative to @tree_view's bin_window if @keyboard_tooltip is %FALSE.
 *
 * Return value: whether or not the given tooltip context points to a row.
 *
 * Since: 2.12
 */
gboolean
pspp_sheet_view_get_tooltip_context (PsppSheetView   *tree_view,
				   gint          *x,
				   gint          *y,
				   gboolean       keyboard_tip,
				   GtkTreeModel **model,
				   GtkTreePath  **path,
				   GtkTreeIter   *iter)
{
  GtkTreePath *tmppath = NULL;

  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), FALSE);
  g_return_val_if_fail (x != NULL, FALSE);
  g_return_val_if_fail (y != NULL, FALSE);

  if (keyboard_tip)
    {
      pspp_sheet_view_get_cursor (tree_view, &tmppath, NULL);

      if (!tmppath)
	return FALSE;
    }
  else
    {
      pspp_sheet_view_convert_widget_to_bin_window_coords (tree_view, *x, *y,
							 x, y);

      if (!pspp_sheet_view_get_path_at_pos (tree_view, *x, *y,
					  &tmppath, NULL, NULL, NULL))
	return FALSE;
    }

  if (model)
    *model = pspp_sheet_view_get_model (tree_view);

  if (iter)
    gtk_tree_model_get_iter (pspp_sheet_view_get_model (tree_view),
			     iter, tmppath);

  if (path)
    *path = tmppath;
  else
    gtk_tree_path_free (tmppath);

  return TRUE;
}

static gboolean
pspp_sheet_view_set_tooltip_query_cb (GtkWidget  *widget,
				    gint        x,
				    gint        y,
				    gboolean    keyboard_tip,
				    GtkTooltip *tooltip,
				    gpointer    data)
{
  GValue value = { 0, };
  GValue transformed = { 0, };
  GtkTreeIter iter;
  GtkTreePath *path;
  GtkTreeModel *model;
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);

  if (!pspp_sheet_view_get_tooltip_context (PSPP_SHEET_VIEW (widget),
					  &x, &y,
					  keyboard_tip,
					  &model, &path, &iter))
    return FALSE;

  gtk_tree_model_get_value (model, &iter,
                            tree_view->priv->tooltip_column, &value);

  g_value_init (&transformed, G_TYPE_STRING);

  if (!g_value_transform (&value, &transformed))
    {
      g_value_unset (&value);
      gtk_tree_path_free (path);

      return FALSE;
    }

  g_value_unset (&value);

  if (!g_value_get_string (&transformed))
    {
      g_value_unset (&transformed);
      gtk_tree_path_free (path);

      return FALSE;
    }

  gtk_tooltip_set_markup (tooltip, g_value_get_string (&transformed));
  pspp_sheet_view_set_tooltip_row (tree_view, tooltip, path);

  gtk_tree_path_free (path);
  g_value_unset (&transformed);

  return TRUE;
}

/**
 * pspp_sheet_view_set_tooltip_column:
 * @tree_view: a #PsppSheetView
 * @column: an integer, which is a valid column number for @tree_view's model
 *
 * If you only plan to have simple (text-only) tooltips on full rows, you
 * can use this function to have #PsppSheetView handle these automatically
 * for you. @column should be set to the column in @tree_view's model
 * containing the tooltip texts, or -1 to disable this feature.
 *
 * When enabled, #GtkWidget::has-tooltip will be set to %TRUE and
 * @tree_view will connect a #GtkWidget::query-tooltip signal handler.
 *
 * Note that the signal handler sets the text with gtk_tooltip_set_markup(),
 * so &amp;, &lt;, etc have to be escaped in the text.
 *
 * Since: 2.12
 */
void
pspp_sheet_view_set_tooltip_column (PsppSheetView *tree_view,
			          gint         column)
{
  g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  if (column == tree_view->priv->tooltip_column)
    return;

  if (column == -1)
    {
      g_signal_handlers_disconnect_by_func (tree_view,
	  				    pspp_sheet_view_set_tooltip_query_cb,
					    NULL);
      gtk_widget_set_has_tooltip (GTK_WIDGET (tree_view), FALSE);
    }
  else
    {
      if (tree_view->priv->tooltip_column == -1)
        {
          g_signal_connect (tree_view, "query-tooltip",
		            G_CALLBACK (pspp_sheet_view_set_tooltip_query_cb), NULL);
          gtk_widget_set_has_tooltip (GTK_WIDGET (tree_view), TRUE);
        }
    }

  tree_view->priv->tooltip_column = column;
  g_object_notify (G_OBJECT (tree_view), "tooltip-column");
}

/**
 * pspp_sheet_view_get_tooltip_column:
 * @tree_view: a #PsppSheetView
 *
 * Returns the column of @tree_view's model which is being used for
 * displaying tooltips on @tree_view's rows.
 *
 * Return value: the index of the tooltip column that is currently being
 * used, or -1 if this is disabled.
 *
 * Since: 2.12
 */
gint
pspp_sheet_view_get_tooltip_column (PsppSheetView *tree_view)
{
  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), 0);

  return tree_view->priv->tooltip_column;
}

gboolean
_gtk_boolean_handled_accumulator (GSignalInvocationHint *ihint,
                                  GValue                *return_accu,
                                  const GValue          *handler_return,
                                  gpointer               dummy)
{
  gboolean continue_emission;
  gboolean signal_handled;
  
  signal_handled = g_value_get_boolean (handler_return);
  g_value_set_boolean (return_accu, signal_handled);
  continue_emission = !signal_handled;
  
  return continue_emission;
}


GType
pspp_sheet_view_grid_lines_get_type (void)
{
    static GType etype = 0;
    if (G_UNLIKELY(etype == 0)) {
        static const GEnumValue values[] = {
            { PSPP_SHEET_VIEW_GRID_LINES_NONE, "PSPP_SHEET_VIEW_GRID_LINES_NONE", "none" },
            { PSPP_SHEET_VIEW_GRID_LINES_HORIZONTAL, "PSPP_SHEET_VIEW_GRID_LINES_HORIZONTAL", "horizontal" },
            { PSPP_SHEET_VIEW_GRID_LINES_VERTICAL, "PSPP_SHEET_VIEW_GRID_LINES_VERTICAL", "vertical" },
            { PSPP_SHEET_VIEW_GRID_LINES_BOTH, "PSPP_SHEET_VIEW_GRID_LINES_BOTH", "both" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static (g_intern_static_string ("PsppSheetViewGridLines"), values);
    }
    return etype;
}

GType
pspp_sheet_view_special_cells_get_type (void)
{
    static GType etype = 0;
    if (G_UNLIKELY(etype == 0)) {
        static const GEnumValue values[] = {
            { PSPP_SHEET_VIEW_SPECIAL_CELLS_DETECT, "PSPP_SHEET_VIEW_SPECIAL_CELLS_DETECT", "detect" },
            { PSPP_SHEET_VIEW_SPECIAL_CELLS_YES, "PSPP_SHEET_VIEW_SPECIAL_CELLS_YES", "yes" },
            { PSPP_SHEET_VIEW_SPECIAL_CELLS_NO, "PSPP_SHEET_VIEW_SPECIAL_CELLS_NO", "no" },
            { 0, NULL, NULL }
        };
        etype = g_enum_register_static (g_intern_static_string ("PsppSheetViewSpecialCells"), values);
    }
    return etype;
}
