/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011 Free Software Foundation, Inc.

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

/* gtktreeselection.h
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

#ifndef __PSPP_SHEET_SELECTION_H__
#define __PSPP_SHEET_SELECTION_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS


#define PSPP_TYPE_SHEET_SELECTION			(pspp_sheet_selection_get_type ())
#define PSPP_SHEET_SELECTION(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPP_TYPE_SHEET_SELECTION, PsppSheetSelection))
#define PSPP_SHEET_SELECTION_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), PSPP_TYPE_SHEET_SELECTION, PsppSheetSelectionClass))
#define PSPP_IS_SHEET_SELECTION(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPP_TYPE_SHEET_SELECTION))
#define PSPP_IS_SHEET_SELECTION_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), PSPP_TYPE_SHEET_SELECTION))
#define PSPP_SHEET_SELECTION_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), PSPP_TYPE_SHEET_SELECTION, PsppSheetSelectionClass))

typedef enum
  {
    /* Same as GtkSelectionMode. */
    PSPP_SHEET_SELECTION_NONE = GTK_SELECTION_NONE,
    PSPP_SHEET_SELECTION_SINGLE = GTK_SELECTION_SINGLE,
    PSPP_SHEET_SELECTION_BROWSE = GTK_SELECTION_BROWSE,
    PSPP_SHEET_SELECTION_MULTIPLE = GTK_SELECTION_MULTIPLE,

    /* PsppSheetView extension. */
    PSPP_SHEET_SELECTION_RECTANGLE = 10
  }
PsppSheetSelectionMode;

typedef gboolean (* PsppSheetSelectionFunc)    (PsppSheetSelection  *selection,
					      GtkTreeModel      *model,
					      GtkTreePath       *path,
                                              gboolean           path_currently_selected,
					      gpointer           data);
typedef void (* PsppSheetSelectionForeachFunc) (GtkTreeModel      *model,
					      GtkTreePath       *path,
					      GtkTreeIter       *iter,
					      gpointer           data);

struct _PsppSheetSelection
{
  GObject parent;

  /*< private >*/

  PsppSheetView *PSEAL (tree_view);
  PsppSheetSelectionMode PSEAL (type);
};

struct _PsppSheetSelectionClass
{
  GObjectClass parent_class;

  void (* changed) (PsppSheetSelection *selection);

  /* Padding for future expansion */
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};


GType            pspp_sheet_selection_get_type            (void) G_GNUC_CONST;

void             pspp_sheet_selection_set_mode            (PsppSheetSelection            *selection,
							 PsppSheetSelectionMode             type);
PsppSheetSelectionMode pspp_sheet_selection_get_mode        (PsppSheetSelection            *selection);
void             pspp_sheet_selection_set_select_function (PsppSheetSelection            *selection,
							 PsppSheetSelectionFunc         func,
							 gpointer                     data,
							 GDestroyNotify               destroy);
gpointer         pspp_sheet_selection_get_user_data       (PsppSheetSelection            *selection);
PsppSheetView*     pspp_sheet_selection_get_tree_view       (PsppSheetSelection            *selection);

PsppSheetSelectionFunc pspp_sheet_selection_get_select_function (PsppSheetSelection        *selection);

/* Only meaningful if PSPP_SHEET_SELECTION_SINGLE or PSPP_SHEET_SELECTION_BROWSE is set */
/* Use selected_foreach or get_selected_rows for
   PSPP_SHEET_SELECTION_MULTIPLE */
gboolean         pspp_sheet_selection_get_selected        (PsppSheetSelection            *selection,
							 GtkTreeModel               **model,
							 GtkTreeIter                 *iter);
GList *          pspp_sheet_selection_get_selected_rows   (PsppSheetSelection            *selection,
                                                         GtkTreeModel               **model);
gint             pspp_sheet_selection_count_selected_rows (PsppSheetSelection            *selection);
void             pspp_sheet_selection_selected_foreach    (PsppSheetSelection            *selection,
							 PsppSheetSelectionForeachFunc  func,
							 gpointer                     data);
void             pspp_sheet_selection_select_path         (PsppSheetSelection            *selection,
							 GtkTreePath                 *path);
void             pspp_sheet_selection_unselect_path       (PsppSheetSelection            *selection,
							 GtkTreePath                 *path);
void             pspp_sheet_selection_select_iter         (PsppSheetSelection            *selection,
							 GtkTreeIter                 *iter);
void             pspp_sheet_selection_unselect_iter       (PsppSheetSelection            *selection,
							 GtkTreeIter                 *iter);
gboolean         pspp_sheet_selection_path_is_selected    (PsppSheetSelection            *selection,
							 GtkTreePath                 *path);
gboolean         pspp_sheet_selection_iter_is_selected    (PsppSheetSelection            *selection,
							 GtkTreeIter                 *iter);
void             pspp_sheet_selection_select_all          (PsppSheetSelection            *selection);
void             pspp_sheet_selection_unselect_all        (PsppSheetSelection            *selection);
void             pspp_sheet_selection_select_range        (PsppSheetSelection            *selection,
							 GtkTreePath                 *start_path,
							 GtkTreePath                 *end_path);
void             pspp_sheet_selection_unselect_range      (PsppSheetSelection            *selection,
                                                         GtkTreePath                 *start_path,
							 GtkTreePath                 *end_path);
struct range_set *pspp_sheet_selection_get_range_set (PsppSheetSelection *selection);


GList *          pspp_sheet_selection_get_selected_columns (PsppSheetSelection            *selection);
gint             pspp_sheet_selection_count_selected_columns (PsppSheetSelection            *selection);
void             pspp_sheet_selection_select_all_columns (PsppSheetSelection        *selection);
void             pspp_sheet_selection_unselect_all_columns (PsppSheetSelection        *selection);
void             pspp_sheet_selection_select_column        (PsppSheetSelection        *selection,
                                                            PsppSheetViewColumn       *column);
void             pspp_sheet_selection_select_column_range  (PsppSheetSelection        *selection,
                                                            PsppSheetViewColumn       *first,
                                                            PsppSheetViewColumn       *last);

G_END_DECLS

#endif /* __PSPP_SHEET_SELECTION_H__ */
