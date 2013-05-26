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

/* gtktreeviewcolumn.h
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

#ifndef __PSPP_SHEET_VIEW_COLUMN_H__
#define __PSPP_SHEET_VIEW_COLUMN_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PSPP_TYPE_SHEET_VIEW_COLUMN	     (pspp_sheet_view_column_get_type ())
#define PSPP_SHEET_VIEW_COLUMN(obj)	     (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPP_TYPE_SHEET_VIEW_COLUMN, PsppSheetViewColumn))
#define PSPP_SHEET_VIEW_COLUMN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPP_TYPE_SHEET_VIEW_COLUMN, PsppSheetViewColumnClass))
#define PSPP_IS_SHEET_VIEW_COLUMN(obj)	     (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPP_TYPE_SHEET_VIEW_COLUMN))
#define PSPP_IS_SHEET_VIEW_COLUMN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPP_TYPE_SHEET_VIEW_COLUMN))
#define PSPP_SHEET_VIEW_COLUMN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), PSPP_TYPE_SHEET_VIEW_COLUMN, PsppSheetViewColumnClass))

typedef struct _PsppSheetViewColumn      PsppSheetViewColumn;
typedef struct _PsppSheetViewColumnClass PsppSheetViewColumnClass;

typedef void (* PsppSheetCellDataFunc) (PsppSheetViewColumn *tree_column,
                                        GtkCellRenderer   *cell,
                                        GtkTreeModel      *tree_model,
                                        GtkTreeIter       *iter,
                                        gpointer           data);


struct _PsppSheetViewColumn
{
  GtkObject parent;

  GtkWidget *PSEAL (tree_view);
  GtkWidget *PSEAL (button);
  GtkWidget *PSEAL (child);
  GtkWidget *PSEAL (arrow);
  GtkWidget *PSEAL (alignment);
  GdkWindow *PSEAL (window);
  GtkCellEditable *PSEAL (editable_widget);
  gfloat PSEAL (xalign);
  guint PSEAL (property_changed_signal);
  gint PSEAL (spacing);
  GtkAllocation PSEAL (allocation);

  /* Sizing fields */
  /* see gtk+/doc/tree-column-sizing.txt for more information on them */
  gint PSEAL (requested_width);
  gint PSEAL (button_request);
  gint PSEAL (resized_width);
  gint PSEAL (width);
  gint PSEAL (fixed_width);
  gint PSEAL (min_width);
  gint PSEAL (max_width);

  /* dragging columns */
  gint PSEAL (drag_x);
  gint PSEAL (drag_y);

  gchar *PSEAL (title);
  GList *PSEAL (cell_list);

  /* Sorting */
  guint PSEAL (sort_clicked_signal);
  guint PSEAL (sort_column_changed_signal);
  gint PSEAL (sort_column_id);
  GtkSortType PSEAL (sort_order);

  /* Flags */
  guint PSEAL (visible)             : 1;
  guint PSEAL (resizable)           : 1;
  guint PSEAL (clickable)           : 1;
  guint PSEAL (dirty)               : 1;
  guint PSEAL (show_sort_indicator) : 1;
  guint PSEAL (maybe_reordered)     : 1;
  guint PSEAL (reorderable)         : 1;
  guint PSEAL (use_resized_width)   : 1;
  guint PSEAL (expand)              : 1;
  guint PSEAL (quick_edit)          : 1;
  guint PSEAL (selected)            : 1;
  guint PSEAL (selectable)          : 1;
  guint PSEAL (row_head)            : 1;
  guint PSEAL (tabbable)            : 1;
  guint PSEAL (need_button)         : 1;
};

struct _PsppSheetViewColumnClass
{
  GtkObjectClass parent_class;

  gboolean (*clicked) (PsppSheetViewColumn *tree_column);
  gboolean (*button_press_event) (PsppSheetViewColumn *,
                                  GdkEventButton *);

  /* Padding for future expansion */
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};

GType                   pspp_sheet_view_column_get_type            (void) G_GNUC_CONST;
PsppSheetViewColumn      *pspp_sheet_view_column_new                 (void);
PsppSheetViewColumn      *pspp_sheet_view_column_new_with_attributes (const gchar             *title,
								  GtkCellRenderer         *cell,
								  ...) G_GNUC_NULL_TERMINATED;
void                    pspp_sheet_view_column_pack_start          (PsppSheetViewColumn       *tree_column,
								  GtkCellRenderer         *cell,
								  gboolean                 expand);
void                    pspp_sheet_view_column_pack_end            (PsppSheetViewColumn       *tree_column,
								  GtkCellRenderer         *cell,
								  gboolean                 expand);
void                    pspp_sheet_view_column_clear               (PsppSheetViewColumn       *tree_column);

GList                  *pspp_sheet_view_column_get_cell_renderers  (PsppSheetViewColumn       *tree_column);

void                    pspp_sheet_view_column_add_attribute       (PsppSheetViewColumn       *tree_column,
								  GtkCellRenderer         *cell_renderer,
								  const gchar             *attribute,
								  gint                     column);
void                    pspp_sheet_view_column_set_attributes      (PsppSheetViewColumn       *tree_column,
								  GtkCellRenderer         *cell_renderer,
								  ...) G_GNUC_NULL_TERMINATED;
void                    pspp_sheet_view_column_set_cell_data_func  (PsppSheetViewColumn       *tree_column,
								  GtkCellRenderer         *cell_renderer,
								  PsppSheetCellDataFunc      func,
								  gpointer                 func_data,
								  GDestroyNotify           destroy);
void                    pspp_sheet_view_column_clear_attributes    (PsppSheetViewColumn       *tree_column,
								  GtkCellRenderer         *cell_renderer);
void                    pspp_sheet_view_column_set_spacing         (PsppSheetViewColumn       *tree_column,
								  gint                     spacing);
gint                    pspp_sheet_view_column_get_spacing         (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_visible         (PsppSheetViewColumn       *tree_column,
								  gboolean                 visible);
gboolean                pspp_sheet_view_column_get_visible         (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_resizable       (PsppSheetViewColumn       *tree_column,
								  gboolean                 resizable);
gboolean                pspp_sheet_view_column_get_resizable       (PsppSheetViewColumn       *tree_column);
gint                    pspp_sheet_view_column_get_width           (PsppSheetViewColumn       *tree_column);
gint                    pspp_sheet_view_column_get_fixed_width     (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_fixed_width     (PsppSheetViewColumn       *tree_column,
								  gint                     fixed_width);
void                    pspp_sheet_view_column_set_min_width       (PsppSheetViewColumn       *tree_column,
								  gint                     min_width);
gint                    pspp_sheet_view_column_get_min_width       (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_max_width       (PsppSheetViewColumn       *tree_column,
								  gint                     max_width);
gint                    pspp_sheet_view_column_get_max_width       (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_clicked             (PsppSheetViewColumn       *tree_column);



/* Options for manipulating the column headers
 */
void                    pspp_sheet_view_column_set_title           (PsppSheetViewColumn       *tree_column,
								  const gchar             *title);
const gchar   *         pspp_sheet_view_column_get_title           (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_expand          (PsppSheetViewColumn       *tree_column,
								  gboolean                 expand);
gboolean                pspp_sheet_view_column_get_expand          (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_clickable       (PsppSheetViewColumn       *tree_column,
								  gboolean                 clickable);
gboolean                pspp_sheet_view_column_get_clickable       (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_widget          (PsppSheetViewColumn       *tree_column,
								  GtkWidget               *widget);
GtkWidget              *pspp_sheet_view_column_get_widget          (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_alignment       (PsppSheetViewColumn       *tree_column,
								  gfloat                   xalign);
gfloat                  pspp_sheet_view_column_get_alignment       (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_reorderable     (PsppSheetViewColumn       *tree_column,
								  gboolean                 reorderable);
gboolean                pspp_sheet_view_column_get_reorderable     (PsppSheetViewColumn       *tree_column);

void                    pspp_sheet_view_column_set_quick_edit     (PsppSheetViewColumn       *tree_column,
								  gboolean                 quick_edit);
gboolean                pspp_sheet_view_column_get_quick_edit     (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_selected     (PsppSheetViewColumn       *tree_column,
								  gboolean                 selected);
gboolean                pspp_sheet_view_column_get_selected     (PsppSheetViewColumn       *tree_column);

void                    pspp_sheet_view_column_set_selectable     (PsppSheetViewColumn       *tree_column,
								  gboolean                 selectable);
gboolean                pspp_sheet_view_column_get_selectable     (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_row_head     (PsppSheetViewColumn       *tree_column,
								  gboolean                 row_head);
gboolean                pspp_sheet_view_column_get_row_head     (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_tabbable     (PsppSheetViewColumn       *tree_column,
                                                                 gboolean                 tabbable);
gboolean                pspp_sheet_view_column_get_tabbable     (PsppSheetViewColumn       *tree_column);



/* You probably only want to use pspp_sheet_view_column_set_sort_column_id.  The
 * other sorting functions exist primarily to let others do their own custom sorting.
 */
void                    pspp_sheet_view_column_set_sort_column_id  (PsppSheetViewColumn       *tree_column,
								  gint                     sort_column_id);
gint                    pspp_sheet_view_column_get_sort_column_id  (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_sort_indicator  (PsppSheetViewColumn       *tree_column,
								  gboolean                 setting);
gboolean                pspp_sheet_view_column_get_sort_indicator  (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_sort_order      (PsppSheetViewColumn       *tree_column,
								  GtkSortType              order);
GtkSortType             pspp_sheet_view_column_get_sort_order      (PsppSheetViewColumn       *tree_column);


/* These functions are meant primarily for interaction between the PsppSheetView and the column.
 */
void                    pspp_sheet_view_column_cell_set_cell_data  (PsppSheetViewColumn       *tree_column,
								  GtkTreeModel            *tree_model,
								  GtkTreeIter             *iter);
void                    pspp_sheet_view_column_cell_get_size       (PsppSheetViewColumn       *tree_column,
								  const GdkRectangle      *cell_area,
								  gint                    *x_offset,
								  gint                    *y_offset,
								  gint                    *width,
								  gint                    *height);
gboolean                pspp_sheet_view_column_cell_is_visible     (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_focus_cell          (PsppSheetViewColumn       *tree_column,
								  GtkCellRenderer         *cell);
gboolean                pspp_sheet_view_column_cell_get_position   (PsppSheetViewColumn       *tree_column,
					                          GtkCellRenderer         *cell_renderer,
					                          gint                    *start_pos,
					                          gint                    *width);
void                    pspp_sheet_view_column_queue_resize        (PsppSheetViewColumn       *tree_column);
GtkWidget              *pspp_sheet_view_column_get_tree_view       (PsppSheetViewColumn       *tree_column);

void                    pspp_sheet_view_column_size_request       (PsppSheetViewColumn       *tree_column,
                                                                    GtkRequisition             *requisition);

void                    pspp_sheet_view_column_size_allocate       (PsppSheetViewColumn       *tree_column,
                                                                    GtkAllocation             *allocation);
gboolean                pspp_sheet_view_column_can_focus           (PsppSheetViewColumn       *tree_column);
void                    pspp_sheet_view_column_set_need_button     (PsppSheetViewColumn       *tree_column,
                                                                    gboolean                   need_button);

G_END_DECLS


#endif /* __PSPP_SHEET_VIEW_COLUMN_H__ */
