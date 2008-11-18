/* GtkSheet widget for Gtk+.
 * Copyright (C) 2006 Free Software Foundation

 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __G_SHEET_HETERO_COLUMN_H__
#define __G_SHEET_HETERO_COLUMN_H__

#include <glib-object.h>
#include <glib.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define G_TYPE_SHEET_HETERO_COLUMN (g_sheet_hetero_column_get_type ())

#define G_SHEET_HETERO_COLUMN(obj)    G_TYPE_CHECK_INSTANCE_CAST (obj, G_TYPE_SHEET_HETERO_COLUMN, GSheetHeteroColumn )
#define G_SHEET_HETERO_COLUMN_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, g_sheet_hetero_column_get_type (), GSheetHeteroColumnClass)
#define G_IS_SHEET_HETERO_COLUMN(obj)  G_TYPE_CHECK_INSTANCE_TYPE (obj, G_TYPE_SHEET_HETERO_COLUMN)


  struct GSheetHeteroColumnUnit
  {
    GtkSheetButton button;

    gint width;
    gboolean is_sensitive;
  };


  struct _GSheetHeteroColumn{
    GObject parent;

    gint n_columns;
    gint default_width;

    struct GSheetHeteroColumnUnit *col;

  };

  struct _GSheetHeteroColumnClass
  {
    GObjectClass parent_class;
  };




  /* create a new column */
  GObject * g_sheet_hetero_column_new (gint default_width, gint n_columns);

  GType g_sheet_hetero_column_get_type (void);


  typedef struct _GSheetHeteroColumn GSheetHeteroColumn;
  typedef struct _GSheetHeteroColumnClass GSheetHeteroColumnClass;


  void g_sheet_hetero_column_set_button_label (GSheetHeteroColumn *geo,
						glong i, const gchar *label);

  void g_sheet_hetero_column_set_width (GSheetHeteroColumn *geo,
					     glong i, gint size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __G_SHEET_HETERO_COLUMN_H__ */


