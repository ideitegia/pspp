/* GSheetRow --- an abstract model of the row geometry of a 
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

#ifndef __G_SHEET_ROW_IFACE_H__
#define __G_SHEET_ROW_IFACE_H__

#include <glib-object.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "gtkextra-sheet.h"


G_BEGIN_DECLS

#define G_TYPE_SHEET_ROW            (g_sheet_row_get_type ())
#define G_SHEET_ROW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_TYPE_SHEET_ROW, GSheetRow))
#define G_IS_SHEET_ROW(obj)	       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_TYPE_SHEET_ROW))
#define G_SHEET_ROW_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), G_TYPE_SHEET_ROW, GSheetRowIface))




typedef struct _GSheetRow        GSheetRow; 
typedef struct _GSheetRowIface   GSheetRowIface;

struct _GSheetRowIface
{
  GTypeInterface g_iface;


  /* Signals */
  void         (* rows_changed)     (GSheetRow *geo,
				      gint row, gint n_rows);

  /* Virtual Table */
  gint (* get_height) (const GSheetRow *grow, gint row, const GtkSheet *);
  void (* set_height) (GSheetRow *grow, gint row, gint height, 
		       const GtkSheet *);

  gboolean (* get_visibility) (const GSheetRow *grow, gint row, 
			       const GtkSheet *);

  gboolean (* get_sensitivity) (const GSheetRow *grow, gint row, 
				const GtkSheet *);

  const GtkSheetButton * (* get_button) (const GSheetRow *grow, gint row, 
					 const GtkSheet *);

  gint  (* get_row_count) (const GSheetRow *geo, const GtkSheet *);


  GtkStateType  (*get_button_state)(const GSheetRow *geo, gint row, 
				    const GtkSheet *);

  gchar * (*get_button_label)(const GSheetRow *geo, gint row, 
			      const GtkSheet *);

  gboolean      (*get_button_visibility)(const GSheetRow *geo, 
					gint row, const GtkSheet *);

  const GtkSheetChild * (*get_button_child)(const GSheetRow *geo, 
					   gint row, const GtkSheet *);

  guint (*top_ypixel)(const GSheetRow *geo, gint row, const GtkSheet *);
  gint (*pixel_to_row)(const GSheetRow *geo, guint pixel, const GtkSheet *);
};


GType g_sheet_row_get_type   (void) G_GNUC_CONST;


gint  g_sheet_row_get_height (const GSheetRow *grow,
					gint row, const GtkSheet *sheet);


void  g_sheet_row_set_height (GSheetRow *grow,
					gint row, gint size, const GtkSheet *sheet);


gboolean  g_sheet_row_get_visibility(const GSheetRow *grow,
					    gint row, const GtkSheet *sheet);

gboolean  g_sheet_row_get_sensitivity(const GSheetRow *grow,
					     gint row, const GtkSheet *sheet);


GtkSheetButton *g_sheet_row_get_button(const GSheetRow *grow,
					     gint row, const GtkSheet *sheet);


gint  g_sheet_row_get_row_count(const GSheetRow *geo, const GtkSheet *sheet);

/* Return the top pixel of row ROW */
gint  g_sheet_row_start_pixel(const GSheetRow *geo, gint row, 
			      const GtkSheet *sheet);

/* Return the row contained by pixel PIXEL */
gint  g_sheet_row_pixel_to_row(const GSheetRow *geo, gint pixel, 
			       const GtkSheet *sheet);


void g_sheet_row_rows_deleted(GSheetRow *geo, 
				      gint first, gint n_rows);


G_END_DECLS

#endif /* __G_SHEET_ROW_IFACE_H__ */
