/* GSheetColumn --- an abstract model of the column geometry of a
 * GSheet widget.
 * Copyright (C) 2006 Free Software Foundation
 *
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

#ifndef __GSHEET_COLUMN_IFACE_H
#define __GSHEET_COLUMN_IFACE_H

#include <glib-object.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "gtkextra-sheet.h"


G_BEGIN_DECLS

#define G_TYPE_SHEET_COLUMN            (g_sheet_column_get_type ())
#define G_SHEET_COLUMN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_TYPE_SHEET_COLUMN, GSheetColumn))
#define G_IS_SHEET_COLUMN(obj)	       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_TYPE_SHEET_COLUMN))
#define G_SHEET_COLUMN_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), G_TYPE_SHEET_COLUMN, GSheetColumnIface))


typedef struct _GSheetColumn        GSheetColumn;
typedef struct _GSheetColumnIface   GSheetColumnIface;
struct _GSheetColumnIface
{
  GTypeInterface g_iface;


  /* Signals */
  void         (* columns_changed)     (GSheetColumn *geo,
				      glong col, glong n_columns);

  /* Virtual Table */
  gint (* get_width) (const GSheetColumn *gcolumn, glong col);
  void (* set_width) (GSheetColumn *gcolumn, glong col, gint width);

  gboolean (* get_sensitivity) (const GSheetColumn *gcolumn, glong col);
  const GtkSheetButton * (* get_button) (const GSheetColumn *gcolumn, glong col);
  GtkJustification (* get_justification) (const GSheetColumn *gcolumn, glong col);

  glong  (* get_column_count) (const GSheetColumn *geo);


  GtkStateType  (*get_button_state)(const GSheetColumn *geo, glong col);
  gchar * (*get_button_label)(const GSheetColumn *geo, glong col);
  gchar * (*get_subtitle)(const GSheetColumn *geo, glong col);

  gboolean      (*get_button_visibility)(const GSheetColumn *geo,
					glong col);

  GtkJustification * (*get_button_justification)(const GSheetColumn *geo,
						glong col);
};


inline GType g_sheet_column_get_type   (void) G_GNUC_CONST;


inline gint  g_sheet_column_get_width (const GSheetColumn *gcolumn,
				       glong col);


inline void  g_sheet_column_set_width (GSheetColumn *gcolumn,
				       glong col, gint size);


inline gboolean  g_sheet_column_get_visibility (const GSheetColumn *gcolumn,
					    glong col);

inline gboolean  g_sheet_column_get_sensitivity (const GSheetColumn *gcolumn,
					     glong col);


inline GtkSheetButton *g_sheet_column_get_button (const GSheetColumn *gcolumn,
					     glong col);

gchar *g_sheet_column_get_subtitle (const GSheetColumn *, glong);

inline GtkJustification g_sheet_column_get_justification (const GSheetColumn *gcolumn, glong col);


inline glong  g_sheet_column_get_column_count (const GSheetColumn *geo);

inline gint  g_sheet_column_start_pixel (const GSheetColumn *geo, glong col);

inline void g_sheet_column_columns_changed (GSheetColumn *geo,
					   glong first, glong n_columns);

G_END_DECLS

#endif /* __G_SHEET_COLUMN_IFACE_H__ */
