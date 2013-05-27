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

/* gtktreeview.h
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

#ifndef __PSPP_SHEET_VIEW_H__
#define __PSPP_SHEET_VIEW_H__

#include <gtk/gtk.h>
#include "ui/gui/pspp-sheet-view-column.h"

G_BEGIN_DECLS


typedef enum
{
  PSPP_SHEET_VIEW_GRID_LINES_NONE,
  PSPP_SHEET_VIEW_GRID_LINES_HORIZONTAL,
  PSPP_SHEET_VIEW_GRID_LINES_VERTICAL,
  PSPP_SHEET_VIEW_GRID_LINES_BOTH
} PsppSheetViewGridLines;

GType pspp_sheet_view_grid_lines_get_type (void) G_GNUC_CONST;
#define PSPP_TYPE_SHEET_VIEW_GRID_LINES (pspp_sheet_view_grid_lines_get_type ())

/* A "special cell" is a cell that is editable or activatable.  When a row that
 * contains a special cell is selected, the cursor is drawn around a single
 * cell; when other rows are selected, the cursor is drawn around the entire
 * row.
 *
 * With the default of "detect", whether a given row contains a special cell is
 * detected automatically.  This is the best choice most of the time.  For
 * sheet views that contain more than 100 columns, an explicit "yes" or "no"
 * improves performance. */
typedef enum
{
  PSPP_SHEET_VIEW_SPECIAL_CELLS_DETECT,
  PSPP_SHEET_VIEW_SPECIAL_CELLS_YES,
  PSPP_SHEET_VIEW_SPECIAL_CELLS_NO,
} PsppSheetViewSpecialCells;

GType pspp_sheet_view_special_cells_get_type (void) G_GNUC_CONST;
#define PSPP_TYPE_SHEET_VIEW_SPECIAL_CELLS (pspp_sheet_view_special_cells_get_type ())

typedef enum
{
  /* drop before/after this row */
  PSPP_SHEET_VIEW_DROP_BEFORE,
  PSPP_SHEET_VIEW_DROP_AFTER,
  /* drop as a child of this row (with fallback to before or after
   * if into is not possible)
   */
  PSPP_SHEET_VIEW_DROP_INTO_OR_BEFORE,
  PSPP_SHEET_VIEW_DROP_INTO_OR_AFTER
} PsppSheetViewDropPosition;

typedef enum
{
  PSPP_SHEET_SELECT_MODE_TOGGLE = 1 << 0,
  PSPP_SHEET_SELECT_MODE_EXTEND = 1 << 1
}
PsppSheetSelectMode;

#define PSPP_TYPE_SHEET_VIEW		(pspp_sheet_view_get_type ())
#define PSPP_SHEET_VIEW(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPP_TYPE_SHEET_VIEW, PsppSheetView))
#define PSPP_SHEET_VIEW_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), PSPP_TYPE_SHEET_VIEW, PsppSheetViewClass))
#define PSPP_IS_SHEET_VIEW(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPP_TYPE_SHEET_VIEW))
#define PSPP_IS_SHEET_VIEW_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), PSPP_TYPE_SHEET_VIEW))
#define PSPP_SHEET_VIEW_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), PSPP_TYPE_SHEET_VIEW, PsppSheetViewClass))

typedef struct _PsppSheetView           PsppSheetView;
typedef struct _PsppSheetViewClass      PsppSheetViewClass;
typedef struct _PsppSheetViewPrivate    PsppSheetViewPrivate;
typedef struct _PsppSheetSelection      PsppSheetSelection;
typedef struct _PsppSheetSelectionClass PsppSheetSelectionClass;

struct _PsppSheetView
{
  GtkContainer parent;

  PsppSheetViewPrivate *PSEAL (priv);

  gboolean dispose_has_run ;
};

struct _PsppSheetViewClass
{
  GtkContainerClass parent_class;

  void     (* set_scroll_adjustments)     (PsppSheetView       *tree_view,
				           GtkAdjustment     *hadjustment,
				           GtkAdjustment     *vadjustment);
  void     (* row_activated)              (PsppSheetView       *tree_view,
				           GtkTreePath       *path,
					   PsppSheetViewColumn *column);
  void     (* columns_changed)            (PsppSheetView       *tree_view);
  void     (* cursor_changed)             (PsppSheetView       *tree_view);

  /* Key Binding signals */
  gboolean (* move_cursor)                (PsppSheetView       *tree_view,
				           GtkMovementStep    step,
				           gint               count);
  gboolean (* select_all)                 (PsppSheetView       *tree_view);
  gboolean (* unselect_all)               (PsppSheetView       *tree_view);
  gboolean (* select_cursor_row)          (PsppSheetView       *tree_view,
					   gboolean           start_editing,
                                           PsppSheetSelectMode mode);
  gboolean (* toggle_cursor_row)          (PsppSheetView       *tree_view);
  gboolean (* select_cursor_parent)       (PsppSheetView       *tree_view);
  gboolean (* start_interactive_search)   (PsppSheetView       *tree_view);

  /* Padding for future expansion */
  void (*_gtk_reserved0) (void);
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};


typedef gboolean (* PsppSheetViewColumnDropFunc) (PsppSheetView             *tree_view,
						PsppSheetViewColumn       *column,
						PsppSheetViewColumn       *prev_column,
						PsppSheetViewColumn       *next_column,
						gpointer                 data);
typedef void     (* PsppSheetViewMappingFunc)    (PsppSheetView             *tree_view,
						GtkTreePath             *path,
						gpointer                 user_data);
typedef gboolean (*PsppSheetViewSearchEqualFunc) (GtkTreeModel            *model,
						gint                     column,
						const gchar             *key,
						GtkTreeIter             *iter,
						gpointer                 search_data);
typedef void     (*PsppSheetViewSearchPositionFunc) (PsppSheetView  *tree_view,
						   GtkWidget    *search_dialog,
						   gpointer      user_data);


/* Creators */
GType                  pspp_sheet_view_get_type                      (void) G_GNUC_CONST;
GtkWidget             *pspp_sheet_view_new                           (void);
GtkWidget             *pspp_sheet_view_new_with_model                (GtkTreeModel              *model);

/* Accessors */
GtkTreeModel          *pspp_sheet_view_get_model                     (PsppSheetView               *tree_view);
void                   pspp_sheet_view_set_model                     (PsppSheetView               *tree_view,
								    GtkTreeModel              *model);
PsppSheetSelection      *pspp_sheet_view_get_selection                 (PsppSheetView               *tree_view);
GtkAdjustment         *pspp_sheet_view_get_hadjustment               (PsppSheetView               *tree_view);
void                   pspp_sheet_view_set_hadjustment               (PsppSheetView               *tree_view,
								    GtkAdjustment             *adjustment);
GtkAdjustment         *pspp_sheet_view_get_vadjustment               (PsppSheetView               *tree_view);
void                   pspp_sheet_view_set_vadjustment               (PsppSheetView               *tree_view,
								    GtkAdjustment             *adjustment);
gboolean               pspp_sheet_view_get_headers_visible           (PsppSheetView               *tree_view);
void                   pspp_sheet_view_set_headers_visible           (PsppSheetView               *tree_view,
								    gboolean                   headers_visible);
void                   pspp_sheet_view_columns_autosize              (PsppSheetView               *tree_view);
gboolean               pspp_sheet_view_get_headers_clickable         (PsppSheetView *tree_view);
void                   pspp_sheet_view_set_headers_clickable         (PsppSheetView               *tree_view,
								    gboolean                   setting);
void                   pspp_sheet_view_set_rules_hint                (PsppSheetView               *tree_view,
								    gboolean                   setting);
gboolean               pspp_sheet_view_get_rules_hint                (PsppSheetView               *tree_view);

/* Column funtions */
gint                   pspp_sheet_view_append_column                 (PsppSheetView               *tree_view,
								    PsppSheetViewColumn         *column);
gint                   pspp_sheet_view_remove_column                 (PsppSheetView               *tree_view,
								    PsppSheetViewColumn         *column);
gint                   pspp_sheet_view_insert_column                 (PsppSheetView               *tree_view,
								    PsppSheetViewColumn         *column,
								    gint                       position);
gint                   pspp_sheet_view_insert_column_with_attributes (PsppSheetView               *tree_view,
								    gint                       position,
								    const gchar               *title,
								    GtkCellRenderer           *cell,
								    ...) G_GNUC_NULL_TERMINATED;
gint                   pspp_sheet_view_insert_column_with_data_func  (PsppSheetView               *tree_view,
								    gint                       position,
								    const gchar               *title,
								    GtkCellRenderer           *cell,
                                                                    PsppSheetCellDataFunc        func,
                                                                    gpointer                   data,
                                                                    GDestroyNotify             dnotify);
PsppSheetViewColumn     *pspp_sheet_view_get_column                    (PsppSheetView               *tree_view,
								    gint                       n);
GList                 *pspp_sheet_view_get_columns                   (PsppSheetView               *tree_view);
void                   pspp_sheet_view_move_column_after             (PsppSheetView               *tree_view,
								    PsppSheetViewColumn         *column,
								    PsppSheetViewColumn         *base_column);
void                   pspp_sheet_view_set_column_drag_function      (PsppSheetView               *tree_view,
								    PsppSheetViewColumnDropFunc  func,
								    gpointer                   user_data,
								    GDestroyNotify             destroy);

/* Actions */
void                   pspp_sheet_view_scroll_to_point               (PsppSheetView               *tree_view,
								    gint                       tree_x,
								    gint                       tree_y);
void                   pspp_sheet_view_scroll_to_cell                (PsppSheetView               *tree_view,
								    GtkTreePath               *path,
								    PsppSheetViewColumn         *column,
								    gboolean                   use_align,
								    gfloat                     row_align,
								    gfloat                     col_align);
void                   pspp_sheet_view_row_activated                 (PsppSheetView               *tree_view,
								    GtkTreePath               *path,
								    PsppSheetViewColumn         *column);
void                   pspp_sheet_view_set_reorderable               (PsppSheetView               *tree_view,
								    gboolean                   reorderable);
gboolean               pspp_sheet_view_get_reorderable               (PsppSheetView               *tree_view);
void                   pspp_sheet_view_set_cursor                    (PsppSheetView               *tree_view,
								    GtkTreePath               *path,
								    PsppSheetViewColumn         *focus_column,
								    gboolean                   start_editing);
void                   pspp_sheet_view_set_cursor_on_cell            (PsppSheetView               *tree_view,
								    GtkTreePath               *path,
								    PsppSheetViewColumn         *focus_column,
								    GtkCellRenderer           *focus_cell,
								    gboolean                   start_editing);
void                   pspp_sheet_view_get_cursor                    (PsppSheetView               *tree_view,
								    GtkTreePath              **path,
								    PsppSheetViewColumn        **focus_column);


/* Layout information */
GdkWindow             *pspp_sheet_view_get_bin_window                (PsppSheetView               *tree_view);
gboolean               pspp_sheet_view_get_path_at_pos               (PsppSheetView               *tree_view,
								    gint                       x,
								    gint                       y,
								    GtkTreePath              **path,
								    PsppSheetViewColumn        **column,
								    gint                      *cell_x,
								    gint                      *cell_y);
void                   pspp_sheet_view_get_cell_area                 (PsppSheetView               *tree_view,
								    GtkTreePath               *path,
								    PsppSheetViewColumn         *column,
								    GdkRectangle              *rect);
void                   pspp_sheet_view_get_background_area           (PsppSheetView               *tree_view,
								    GtkTreePath               *path,
								    PsppSheetViewColumn         *column,
								    GdkRectangle              *rect);
void                   pspp_sheet_view_get_visible_rect              (PsppSheetView               *tree_view,
								    GdkRectangle              *visible_rect);

#ifndef GTK_DISABLE_DEPRECATED
void                   pspp_sheet_view_widget_to_tree_coords         (PsppSheetView               *tree_view,
								    gint                       wx,
								    gint                       wy,
								    gint                      *tx,
								    gint                      *ty);
void                   pspp_sheet_view_tree_to_widget_coords         (PsppSheetView               *tree_view,
								    gint                       tx,
								    gint                       ty,
								    gint                      *wx,
								    gint                      *wy);
#endif /* !GTK_DISABLE_DEPRECATED */
gboolean               pspp_sheet_view_get_visible_range             (PsppSheetView               *tree_view,
								    GtkTreePath              **start_path,
								    GtkTreePath              **end_path);

/* Drag-and-Drop support */
void                   pspp_sheet_view_enable_model_drag_source      (PsppSheetView               *tree_view,
								    GdkModifierType            start_button_mask,
								    const GtkTargetEntry      *targets,
								    gint                       n_targets,
								    GdkDragAction              actions);
void                   pspp_sheet_view_enable_model_drag_dest        (PsppSheetView               *tree_view,
								    const GtkTargetEntry      *targets,
								    gint                       n_targets,
								    GdkDragAction              actions);
void                   pspp_sheet_view_unset_rows_drag_source        (PsppSheetView               *tree_view);
void                   pspp_sheet_view_unset_rows_drag_dest          (PsppSheetView               *tree_view);


/* These are useful to implement your own custom stuff. */
void                   pspp_sheet_view_set_drag_dest_row             (PsppSheetView               *tree_view,
								    GtkTreePath               *path,
								    PsppSheetViewDropPosition    pos);
void                   pspp_sheet_view_get_drag_dest_row             (PsppSheetView               *tree_view,
								    GtkTreePath              **path,
								    PsppSheetViewDropPosition   *pos);
gboolean               pspp_sheet_view_get_dest_row_at_pos           (PsppSheetView               *tree_view,
								    gint                       drag_x,
								    gint                       drag_y,
								    GtkTreePath              **path,
								    PsppSheetViewDropPosition   *pos);

#if GTK3_TRANSITION
GdkPixmap             *pspp_sheet_view_create_row_drag_icon          (PsppSheetView               *tree_view,
								    GtkTreePath               *path);
#endif

/* Interactive search */
void                       pspp_sheet_view_set_enable_search     (PsppSheetView                *tree_view,
								gboolean                    enable_search);
gboolean                   pspp_sheet_view_get_enable_search     (PsppSheetView                *tree_view);
gint                       pspp_sheet_view_get_search_column     (PsppSheetView                *tree_view);
void                       pspp_sheet_view_set_search_column     (PsppSheetView                *tree_view,
								gint                        column);
PsppSheetViewSearchEqualFunc pspp_sheet_view_get_search_equal_func (PsppSheetView                *tree_view);
void                       pspp_sheet_view_set_search_equal_func (PsppSheetView                *tree_view,
								PsppSheetViewSearchEqualFunc  search_equal_func,
								gpointer                    search_user_data,
								GDestroyNotify              search_destroy);

GtkEntry                     *pspp_sheet_view_get_search_entry         (PsppSheetView                   *tree_view);
void                          pspp_sheet_view_set_search_entry         (PsppSheetView                   *tree_view,
								      GtkEntry                      *entry);
PsppSheetViewSearchPositionFunc pspp_sheet_view_get_search_position_func (PsppSheetView                   *tree_view);
void                          pspp_sheet_view_set_search_position_func (PsppSheetView                   *tree_view,
								      PsppSheetViewSearchPositionFunc  func,
								      gpointer                       data,
								      GDestroyNotify                 destroy);

/* Convert between the different coordinate systems */
void pspp_sheet_view_convert_widget_to_tree_coords       (PsppSheetView *tree_view,
							gint         wx,
							gint         wy,
							gint        *tx,
							gint        *ty);
void pspp_sheet_view_convert_tree_to_widget_coords       (PsppSheetView *tree_view,
							gint         tx,
							gint         ty,
							gint        *wx,
							gint        *wy);
void pspp_sheet_view_convert_widget_to_bin_window_coords (PsppSheetView *tree_view,
							gint         wx,
							gint         wy,
							gint        *bx,
							gint        *by);
void pspp_sheet_view_convert_bin_window_to_widget_coords (PsppSheetView *tree_view,
							gint         bx,
							gint         by,
							gint        *wx,
							gint        *wy);
void pspp_sheet_view_convert_tree_to_bin_window_coords   (PsppSheetView *tree_view,
							gint         tx,
							gint         ty,
							gint        *bx,
							gint        *by);
void pspp_sheet_view_convert_bin_window_to_tree_coords   (PsppSheetView *tree_view,
							gint         bx,
							gint         by,
							gint        *tx,
							gint        *ty);

/* This function should really never be used.  It is just for use by ATK.
 */
typedef void (* PsppSheetDestroyCountFunc)  (PsppSheetView             *tree_view,
					   GtkTreePath             *path,
					   gint                     children,
					   gpointer                 user_data);
void pspp_sheet_view_set_destroy_count_func (PsppSheetView             *tree_view,
					   PsppSheetDestroyCountFunc  func,
					   gpointer                 data,
					   GDestroyNotify           destroy);

void     pspp_sheet_view_set_hover_selection   (PsppSheetView          *tree_view,
					      gboolean              hover);
gboolean pspp_sheet_view_get_hover_selection   (PsppSheetView          *tree_view);
void     pspp_sheet_view_set_rubber_banding    (PsppSheetView          *tree_view,
					      gboolean              enable);
gboolean pspp_sheet_view_get_rubber_banding    (PsppSheetView          *tree_view);

gboolean pspp_sheet_view_is_rubber_banding_active (PsppSheetView       *tree_view);

PsppSheetViewGridLines        pspp_sheet_view_get_grid_lines         (PsppSheetView                *tree_view);
void                        pspp_sheet_view_set_grid_lines         (PsppSheetView                *tree_view,
								  PsppSheetViewGridLines        grid_lines);

PsppSheetViewSpecialCells pspp_sheet_view_get_special_cells (PsppSheetView                *tree_view);
void                        pspp_sheet_view_set_special_cells (PsppSheetView                *tree_view,
                                                               PsppSheetViewSpecialCells);

int           pspp_sheet_view_get_fixed_height (const PsppSheetView *);
void          pspp_sheet_view_set_fixed_height (PsppSheetView *,
                                                int fixed_height);

/* Convenience functions for setting tooltips */
void          pspp_sheet_view_set_tooltip_row    (PsppSheetView       *tree_view,
						GtkTooltip        *tooltip,
						GtkTreePath       *path);
void          pspp_sheet_view_set_tooltip_cell   (PsppSheetView       *tree_view,
						GtkTooltip        *tooltip,
						GtkTreePath       *path,
						PsppSheetViewColumn *column,
						GtkCellRenderer   *cell);
gboolean      pspp_sheet_view_get_tooltip_context(PsppSheetView       *tree_view,
						gint              *x,
						gint              *y,
						gboolean           keyboard_tip,
						GtkTreeModel     **model,
						GtkTreePath      **path,
						GtkTreeIter       *iter);
void          pspp_sheet_view_set_tooltip_column (PsppSheetView       *tree_view,
					        gint               column);
gint          pspp_sheet_view_get_tooltip_column (PsppSheetView       *tree_view);

void pspp_sheet_view_stop_editing (PsppSheetView *tree_view,
                                   gboolean     cancel_editing);

G_END_DECLS


#endif /* __PSPP_SHEET_VIEW_H__ */
