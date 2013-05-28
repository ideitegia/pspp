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

/* gtktreeprivate.h
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

#ifndef __GTK_TREE_PRIVATE_H__
#define __GTK_TREE_PRIVATE_H__


#include <gtk/gtk.h>
#include "libpspp/range-tower.h"
#include "ui/gui/pspp-sheet-view.h"
#include "ui/gui/pspp-sheet-view-column.h"

#define TREE_VIEW_DRAG_WIDTH 6

typedef enum
{
  PSPP_SHEET_VIEW_IN_COLUMN_RESIZE = 1 << 2,
  PSPP_SHEET_VIEW_HEADERS_VISIBLE = 1 << 4,
  PSPP_SHEET_VIEW_DRAW_KEYFOCUS = 1 << 5,
  PSPP_SHEET_VIEW_MODEL_SETUP = 1 << 6,
  PSPP_SHEET_VIEW_IN_COLUMN_DRAG = 1 << 7
} PsppSheetViewFlags;

enum
{
  DRAG_COLUMN_WINDOW_STATE_UNSET = 0,
  DRAG_COLUMN_WINDOW_STATE_ORIGINAL = 1,
  DRAG_COLUMN_WINDOW_STATE_ARROW = 2,
  DRAG_COLUMN_WINDOW_STATE_ARROW_LEFT = 3,
  DRAG_COLUMN_WINDOW_STATE_ARROW_RIGHT = 4
};

enum
{
  RUBBER_BAND_OFF = 0,
  RUBBER_BAND_MAYBE_START = 1,
  RUBBER_BAND_ACTIVE = 2
};

#define PSPP_SHEET_VIEW_SET_FLAG(tree_view, flag)   G_STMT_START{ (tree_view->priv->flags|=flag); }G_STMT_END
#define PSPP_SHEET_VIEW_UNSET_FLAG(tree_view, flag) G_STMT_START{ (tree_view->priv->flags&=~(flag)); }G_STMT_END
#define PSPP_SHEET_VIEW_FLAG_SET(tree_view, flag)   ((tree_view->priv->flags&flag)==flag)
#define TREE_VIEW_HEADER_HEIGHT(tree_view)        (PSPP_SHEET_VIEW_FLAG_SET (tree_view, PSPP_SHEET_VIEW_HEADERS_VISIBLE)?tree_view->priv->header_height:0)
#define TREE_VIEW_COLUMN_REQUESTED_WIDTH(column)  (CLAMP (column->requested_width, (column->min_width!=-1)?column->min_width:column->requested_width, (column->max_width!=-1)?column->max_width:column->requested_width))

 /* This lovely little value is used to determine how far away from the title bar
  * you can move the mouse and still have a column drag work.
  */
#define TREE_VIEW_COLUMN_DRAG_DEAD_MULTIPLIER(tree_view) (10*TREE_VIEW_HEADER_HEIGHT(tree_view))

typedef struct _PsppSheetViewColumnReorder PsppSheetViewColumnReorder;
struct _PsppSheetViewColumnReorder
{
  gint left_align;
  gint right_align;
  PsppSheetViewColumn *left_column;
  PsppSheetViewColumn *right_column;
};

struct _PsppSheetViewPrivate
{
  GtkTreeModel *model;

  guint flags;
  /* tree information */
  gint row_count;
  struct range_tower *selected;

  /* Container info */
  GList *children;
  gint width;
  gint height;

  /* Adjustments */
  GtkAdjustment *hadjustment;
  GtkAdjustment *vadjustment;

  /* Sub windows */
  GdkWindow *bin_window;
  GdkWindow *header_window;

  /* Scroll position state keeping */
  GtkTreeRowReference *top_row;
  gint top_row_dy;
  /* dy == y pos of top_row + top_row_dy */
  /* we cache it for simplicity of the code */
  gint dy;

  guint presize_handler_timer;
  guint validate_rows_timer;
  guint scroll_sync_timer;

  /* Indentation and expander layout */
  gint expander_size;

  /* Key navigation (focus), selection */
  gint cursor_offset;

  GtkTreeRowReference *anchor;
  GtkTreeRowReference *cursor;

  PsppSheetViewColumn *focus_column;

  /* Current pressed node, previously pressed, prelight */
  gint pressed_button;
  gint press_start_x;
  gint press_start_y;
  gint press_start_node;

  gint event_last_x;
  gint event_last_y;

  guint last_button_time;
  gint last_button_x;
  gint last_button_y;

  int prelight_node;

  /* Cell Editing */
  PsppSheetViewColumn *edited_column;
  gint edited_row;

  /* Selection information */
  PsppSheetSelection *selection;

  /* Header information */
  gint n_columns;
  GList *columns;
  gint header_height;
  gint n_selected_columns;

  PsppSheetViewColumnDropFunc column_drop_func;
  gpointer column_drop_func_data;
  GDestroyNotify column_drop_func_data_destroy;
  GList *column_drag_info;
  PsppSheetViewColumnReorder *cur_reorder;

  /* Interactive Header reordering */
  GdkWindow *drag_window;
  GdkWindow *drag_highlight_window;
  PsppSheetViewColumn *drag_column;
  gint drag_column_x;

  /* Interactive Header Resizing */
  gint drag_pos;
  gint x_drag;

  /* Non-interactive Header Resizing, expand flag support */
  gint prev_width;

  /* ATK Hack */
  PsppSheetDestroyCountFunc destroy_count_func;
  gpointer destroy_count_data;
  GDestroyNotify destroy_count_destroy;

  /* Scroll timeout (e.g. during dnd, rubber banding) */
  guint scroll_timeout;

  /* Row drag-and-drop */
  GtkTreeRowReference *drag_dest_row;
  PsppSheetViewDropPosition drag_dest_pos;
  guint open_dest_timeout;

  /* Rubber banding */
  gint rubber_band_status;
  gint rubber_band_x;
  gint rubber_band_y;
  gint rubber_band_shift;
  gint rubber_band_ctrl;

  int rubber_band_start_node;

  int rubber_band_end_node;

  /* Rectangular selection. */
  PsppSheetViewColumn *anchor_column; /* XXX needs to be a weak pointer? */

  /* fixed height */
  gint fixed_height;
  gboolean fixed_height_set;

  /* Scroll-to functionality when unrealized */
  GtkTreeRowReference *scroll_to_path;
  PsppSheetViewColumn *scroll_to_column;
  gfloat scroll_to_row_align;
  gfloat scroll_to_col_align;

  /* Interactive search */
  gint selected_iter;
  gint search_column;
  PsppSheetViewSearchPositionFunc search_position_func;
  PsppSheetViewSearchEqualFunc search_equal_func;
  gpointer search_user_data;
  GDestroyNotify search_destroy;
  gpointer search_position_user_data;
  GDestroyNotify search_position_destroy;
  GtkWidget *search_window;
  GtkWidget *search_entry;
  guint search_entry_changed_id;
  guint typeselect_flush_timeout;

  /* Grid and tree lines */
  PsppSheetViewGridLines grid_lines;

  /* Special cells. */
  PsppSheetViewSpecialCells special_cells;

  /* Tooltip support */
  gint tooltip_column;

  /* Cached style for button facades in columns. */
  GtkStyle *button_style;

  /* Here comes the bitfield */
  guint scroll_to_use_align : 1;

  guint reorderable : 1;
  guint header_has_focus : 1;
  guint drag_column_window_state : 3;
  /* hint to display rows in alternating colors */
  guint has_rules : 1;

  /* for DnD */
  guint empty_view_drop : 1;

  guint init_hadjust_value : 1;

  guint in_top_row_to_dy : 1;

  /* interactive search */
  guint enable_search : 1;
  guint disable_popdown : 1;
  guint search_custom_entry_set : 1;
  
  guint hover_selection : 1;
  guint imcontext_changed : 1;

  guint rubber_banding_enable : 1;

  guint in_grab : 1;

  guint post_validation_flag : 1;

  /* Whether our key press handler is to avoid sending an unhandled binding to the search entry */
  guint search_entry_avoid_unhandled_binding : 1;
};

#ifdef __GNUC__

#define TREE_VIEW_INTERNAL_ASSERT(expr, ret)     G_STMT_START{          \
     if (!(expr))                                                       \
       {                                                                \
         g_log (G_LOG_DOMAIN,                                           \
                G_LOG_LEVEL_CRITICAL,                                   \
		"%s (%s): assertion `%s' failed.\n"                     \
	        "There is a disparity between the internal view of the PsppSheetView,\n"    \
		"and the GtkTreeModel.  This generally means that the model has changed\n"\
		"without letting the view know.  Any display from now on is likely to\n"  \
		"be incorrect.\n",                                                        \
                G_STRLOC,                                               \
                G_STRFUNC,                                              \
                #expr);                                                 \
         return ret;                                                    \
       };                               }G_STMT_END

#define TREE_VIEW_INTERNAL_ASSERT_VOID(expr)     G_STMT_START{          \
     if (!(expr))                                                       \
       {                                                                \
         g_log (G_LOG_DOMAIN,                                           \
                G_LOG_LEVEL_CRITICAL,                                   \
		"%s (%s): assertion `%s' failed.\n"                     \
	        "There is a disparity between the internal view of the PsppSheetView,\n"    \
		"and the GtkTreeModel.  This generally means that the model has changed\n"\
		"without letting the view know.  Any display from now on is likely to\n"  \
		"be incorrect.\n",                                                        \
                G_STRLOC,                                               \
                G_STRFUNC,                                              \
                #expr);                                                 \
         return;                                                        \
       };                               }G_STMT_END

#else

#define TREE_VIEW_INTERNAL_ASSERT(expr, ret)     G_STMT_START{          \
     if (!(expr))                                                       \
       {                                                                \
         g_log (G_LOG_DOMAIN,                                           \
                G_LOG_LEVEL_CRITICAL,                                   \
		"file %s: line %d: assertion `%s' failed.\n"       \
	        "There is a disparity between the internal view of the PsppSheetView,\n"    \
		"and the GtkTreeModel.  This generally means that the model has changed\n"\
		"without letting the view know.  Any display from now on is likely to\n"  \
		"be incorrect.\n",                                                        \
                __FILE__,                                               \
                __LINE__,                                               \
                #expr);                                                 \
         return ret;                                                    \
       };                               }G_STMT_END

#define TREE_VIEW_INTERNAL_ASSERT_VOID(expr)     G_STMT_START{          \
     if (!(expr))                                                       \
       {                                                                \
         g_log (G_LOG_DOMAIN,                                           \
                G_LOG_LEVEL_CRITICAL,                                   \
		"file %s: line %d: assertion '%s' failed.\n"            \
	        "There is a disparity between the internal view of the PsppSheetView,\n"    \
		"and the GtkTreeModel.  This generally means that the model has changed\n"\
		"without letting the view know.  Any display from now on is likely to\n"  \
		"be incorrect.\n",                                                        \
                __FILE__,                                               \
                __LINE__,                                               \
                #expr);                                                 \
         return;                                                        \
       };                               }G_STMT_END
#endif


/* functions that shouldn't be exported */
void         _pspp_sheet_selection_internal_select_node (PsppSheetSelection  *selection,
						       int                node,
						       GtkTreePath       *path,
                                                       PsppSheetSelectMode  mode,
						       gboolean           override_browse_mode);
void         _pspp_sheet_selection_emit_changed         (PsppSheetSelection  *selection);
void         _pspp_sheet_view_find_node                 (PsppSheetView       *tree_view,
						       GtkTreePath       *path,
						       int              *node);
GtkTreePath *_pspp_sheet_view_find_path                 (PsppSheetView       *tree_view,
						       int                    node);
void         _pspp_sheet_view_child_move_resize         (PsppSheetView       *tree_view,
						       GtkWidget         *widget,
						       gint               x,
						       gint               y,
						       gint               width,
						       gint               height);
void         _pspp_sheet_view_queue_draw_node           (PsppSheetView       *tree_view,
						       int                    node,
						       const GdkRectangle *clip_rect);

void _pspp_sheet_view_column_realize_button   (PsppSheetViewColumn *column);
void _pspp_sheet_view_column_unrealize_button (PsppSheetViewColumn *column);
void _pspp_sheet_view_column_set_tree_view    (PsppSheetViewColumn *column,
					     PsppSheetView       *tree_view);
void _pspp_sheet_view_column_unset_model      (PsppSheetViewColumn *column,
					     GtkTreeModel      *old_model);
void _pspp_sheet_view_column_unset_tree_view  (PsppSheetViewColumn *column);
void _pspp_sheet_view_column_set_width        (PsppSheetViewColumn *column,
					     gint               width);
void _pspp_sheet_view_column_start_drag       (PsppSheetView       *tree_view,
					     PsppSheetViewColumn *column);
gboolean _pspp_sheet_view_column_cell_event   (PsppSheetViewColumn  *tree_column,
					     GtkCellEditable   **editable_widget,
					     GdkEvent           *event,
					     gchar              *path_string,
					     const GdkRectangle *background_area,
					     const GdkRectangle *cell_area,
					     guint               flags);
void _pspp_sheet_view_column_start_editing (PsppSheetViewColumn *tree_column,
					  GtkCellEditable   *editable_widget);
void _pspp_sheet_view_column_stop_editing  (PsppSheetViewColumn *tree_column);
void _pspp_sheet_view_install_mark_rows_col_dirty (PsppSheetView *tree_view);
void             _pspp_sheet_view_column_autosize          (PsppSheetView       *tree_view,
							  PsppSheetViewColumn *column);

gboolean         _pspp_sheet_view_column_has_editable_cell (PsppSheetViewColumn *column);
GtkCellRenderer *_pspp_sheet_view_column_get_edited_cell   (PsppSheetViewColumn *column);
gint             _pspp_sheet_view_column_count_special_cells (PsppSheetViewColumn *column);
GtkCellRenderer *_pspp_sheet_view_column_get_cell_at_pos   (PsppSheetViewColumn *column,
							  gint               x);

PsppSheetSelection* _pspp_sheet_selection_new                (void);
PsppSheetSelection* _pspp_sheet_selection_new_with_tree_view (PsppSheetView      *tree_view);
void              _pspp_sheet_selection_set_tree_view      (PsppSheetSelection *selection,
                                                          PsppSheetView      *tree_view);

void		  _pspp_sheet_view_column_cell_render      (PsppSheetViewColumn  *tree_column,
							    cairo_t *cr,
							  const GdkRectangle *background_area,
							  const GdkRectangle *cell_area,
							  guint               flags);
void		  _pspp_sheet_view_column_get_focus_area   (PsppSheetViewColumn  *tree_column,
							  const GdkRectangle *background_area,
							  const GdkRectangle *cell_area,
							  GdkRectangle       *focus_area);
gboolean	  _pspp_sheet_view_column_cell_focus       (PsppSheetViewColumn  *tree_column,
							  gint                direction,
							  gboolean            left,
							  gboolean            right);
void		  _pspp_sheet_view_column_cell_draw_focus  (PsppSheetViewColumn  *tree_column,
							    cairo_t *cr,
							  const GdkRectangle *background_area,
							  const GdkRectangle *cell_area,
							  guint               flags);
void		  _pspp_sheet_view_column_cell_set_dirty	 (PsppSheetViewColumn  *tree_column);
void              _pspp_sheet_view_column_get_neighbor_sizes (PsppSheetViewColumn *column,
							    GtkCellRenderer   *cell,
							    gint              *left,
							    gint              *right);

gboolean pspp_sheet_view_node_is_selected (PsppSheetView *tree_view,
                                           int node);
void pspp_sheet_view_node_select (PsppSheetView *tree_view,
                                  int node);
void pspp_sheet_view_node_unselect (PsppSheetView *tree_view,
                                    int node);

gint
pspp_sheet_view_node_next (PsppSheetView *tree_view,
                           gint node);
gint
pspp_sheet_view_node_prev (PsppSheetView *tree_view,
                           gint node);

#endif /* __GTK_TREE_PRIVATE_H__ */

