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
				      gint col, gint n_columns);

  /* Virtual Table */
  gint (* get_width) (const GSheetColumn *gcolumn, gint col);
  void (* set_width) (GSheetColumn *gcolumn, gint col, gint width);

  gboolean (* get_visibility) (const GSheetColumn *gcolumn, gint col);
  gboolean (* get_sensitivity) (const GSheetColumn *gcolumn, gint col);
  const GtkSheetButton * (* get_button) (const GSheetColumn *gcolumn, gint col);
  GtkJustification (* get_justification) (const GSheetColumn *gcolumn, gint col);

  gint  (*get_left_text_column) (const GSheetColumn *gcolumn,
				 gint col);

  gint  (*get_right_text_column) (const GSheetColumn *gcolumn,
				  gint col);

  void (* set_left_text_column) (const GSheetColumn *gcolumn,
				 gint col, gint i);

  void (* set_right_text_column) (const GSheetColumn *gcolumn,
				  gint col, gint i);

  gint  (* get_column_count) (const GSheetColumn *geo);


  GtkStateType  (*get_button_state)(const GSheetColumn *geo, gint col);
  gchar * (*get_button_label)(const GSheetColumn *geo, gint col);
  gboolean      (*get_button_visibility)(const GSheetColumn *geo,
					gint col);
  const GtkSheetChild * (*get_button_child)(const GSheetColumn *geo,
					   gint col);
  GtkJustification * (*get_button_justification)(const GSheetColumn *geo,
						gint col);
};


inline GType g_sheet_column_get_type   (void) G_GNUC_CONST;


inline gint  g_sheet_column_get_width (const GSheetColumn *gcolumn,
				       gint col);


inline void  g_sheet_column_set_width (GSheetColumn *gcolumn,
				       gint col, gint size);


inline gboolean  g_sheet_column_get_visibility(const GSheetColumn *gcolumn,
					    gint col);

inline gboolean  g_sheet_column_get_sensitivity(const GSheetColumn *gcolumn,
					     gint col);


inline GtkSheetButton *g_sheet_column_get_button(const GSheetColumn *gcolumn,
					     gint col);

inline GtkJustification g_sheet_column_get_justification(const GSheetColumn *gcolumn, gint col);


inline gint  g_sheet_column_get_left_text_column (const GSheetColumn *gcolumn,
					gint col);

inline gint  g_sheet_column_get_right_text_column (const GSheetColumn *gcolumn,
					gint col);

inline void g_sheet_column_set_left_text_column (const GSheetColumn *gcolumn,
					gint col, gint i);


inline void g_sheet_column_set_right_text_column (const GSheetColumn *gcolumn,
					gint col, gint i);


inline gint  g_sheet_column_get_column_count(const GSheetColumn *geo);

inline gint  g_sheet_column_start_pixel(const GSheetColumn *geo, gint col);

inline void g_sheet_column_columns_changed(GSheetColumn *geo,
					   gint first, gint n_columns);

G_END_DECLS

#endif /* __G_SHEET_COLUMN_IFACE_H__ */
