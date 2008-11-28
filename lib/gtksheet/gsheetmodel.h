/* GSheetModel --- an abstract model for the GtkSheet widget.
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

#ifndef __G_SHEET_MODEL_H__
#define __G_SHEET_MODEL_H__


/* This file provides an abstract interface or the data displayed by the
   GtkSheet widget */

#include <glib-object.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include "gtkextra-sheet.h"

G_BEGIN_DECLS

#define G_TYPE_SHEET_MODEL            (g_sheet_model_get_type ())
#define G_SHEET_MODEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_TYPE_SHEET_MODEL, GSheetModel))
#define G_IS_SHEET_MODEL(obj)	       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_TYPE_SHEET_MODEL))
#define G_SHEET_MODEL_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), G_TYPE_SHEET_MODEL, GSheetModelIface))

typedef enum
{
  GTK_SHEET_LEFT_BORDER     = 1 << 0,
  GTK_SHEET_RIGHT_BORDER    = 1 << 1,
  GTK_SHEET_TOP_BORDER      = 1 << 2,
  GTK_SHEET_BOTTOM_BORDER   = 1 << 3
} GtkSheetBorderType ;


typedef struct _GSheetModel        GSheetModel; /* Dummy typedef */
typedef struct _GSheetModelIface   GSheetModelIface;
typedef struct _GtkSheetRange GtkSheetRange;
typedef struct _GtkSheetCellBorder     GtkSheetCellBorder;

struct _GtkSheetRange
{
  glong row0, col0; /* upper-left cell */
  glong rowi, coli; /* lower-right cell */
};

struct _GtkSheetCellBorder
{
  GtkSheetBorderType mask;
  guint width;
  GdkLineStyle line_style;
  GdkCapStyle cap_style;
  GdkJoinStyle join_style;
  GdkColor color;
};



struct _GSheetModelIface
{
  GTypeInterface g_iface;

  gboolean free_strings;

  /* Signals */
  void         (* range_changed)    (GSheetModel *sheet_model,
				     glong row0, glong col0,
				     glong rowi, glong coli);

  void         (* rows_inserted)    (GSheetModel *sheet_model,
				     glong row, glong n_rows);

  void         (* rows_deleted)     (GSheetModel *sheet_model,
				     glong row, glong n_rows);

  void         (* columns_inserted)    (GSheetModel *sheet_model,
					glong column, glong n_columns);

  void         (* columns_deleted)     (GSheetModel *sheet_model,
					glong column, glong n_columns);






  /* Virtual Table */

  gchar *      (* get_string)      (const GSheetModel *sheet_model,
				    glong row, glong column);

  gboolean  (* set_string) (GSheetModel *sheet_model,
			    const gchar *s, glong row, glong column);

  gboolean  (* clear_datum) (GSheetModel *sheet_model,
			     glong row, glong column);

  gboolean (* is_editable) (const GSheetModel *sheet_model, glong row, glong column);

  GdkColor *  (* get_foreground) (const GSheetModel *sheet_model,
				  glong row, glong column);

  GdkColor *  (* get_background) (const GSheetModel *sheet_model,
				  glong row, glong column);

  const GtkJustification *  (* get_justification) (const GSheetModel *sheet_model,
						   glong row, glong column);

  const PangoFontDescription *  (* get_font_desc) (const GSheetModel *sheet_model,
						   glong row, glong column);

  const GtkSheetCellBorder *  (* get_cell_border) (const GSheetModel *sheet_model,
						   glong row, glong column);



  /* column related metadata */

  gchar * (*get_column_title) (const GSheetModel *, gint col);
  gchar * (*get_column_subtitle) (const GSheetModel *, gint col);
  gboolean (*get_column_sensitivity) (const GSheetModel *, gint col);
  GtkJustification (*get_column_justification) (const GSheetModel *mode, gint col);
  const GtkSheetButton * (* get_button) (const GSheetModel *model, gint col);

  glong (*get_column_count) (const GSheetModel *model);


  /* row related metadata */
  gchar * (*get_row_title) (const GSheetModel *, gint row);
  gchar * (*get_row_subtitle) (const GSheetModel *, gint row);
  glong (*get_row_count) (const GSheetModel *model);
  gboolean (*get_row_sensitivity) (const GSheetModel *, gint row);
};



GType              g_sheet_model_get_type   (void) G_GNUC_CONST;


gchar * g_sheet_model_get_string (const GSheetModel *sheet_model,
				  glong row, glong column);

gboolean  g_sheet_model_set_string (GSheetModel *sheet_model,
				    const gchar *s,
				    glong row, glong column);

gboolean g_sheet_model_datum_clear    (GSheetModel *sheet_model,
				       glong row, glong column);


void g_sheet_model_range_changed (GSheetModel *sheet_model,
				  glong row0, glong col0,
				  glong rowi, glong coli);

void g_sheet_model_rows_deleted (GSheetModel *sheet_model,
				 glong row, glong n_rows);

void g_sheet_model_rows_inserted (GSheetModel *sheet_model,
				  glong row, glong n_rows);

void g_sheet_model_columns_inserted (GSheetModel *sheet_model,
				     glong column, glong n_columns);

void g_sheet_model_columns_deleted (GSheetModel *sheet_model,
				    glong column, glong n_columns);


gboolean g_sheet_model_is_editable (const GSheetModel *model,
				    glong row, glong column);

gboolean g_sheet_model_is_visible
 (const GSheetModel *model, glong row, glong column);


GdkColor *g_sheet_model_get_foreground
 (const GSheetModel *model, glong row, glong column);

GdkColor *g_sheet_model_get_background
 (const GSheetModel *model, glong row, glong column);


const GtkJustification *g_sheet_model_get_justification
 (const GSheetModel *model, glong row, glong column);


const PangoFontDescription *g_sheet_model_get_font_desc
 (const GSheetModel *model, glong row, glong column);

const GtkSheetCellBorder * g_sheet_model_get_cell_border
 (const GSheetModel *model, glong row, glong column);

gboolean g_sheet_model_free_strings (const GSheetModel *sheet_model);

glong g_sheet_model_get_column_count (const GSheetModel *sheet_model);

gint g_sheet_model_get_row_count (const GSheetModel *sheet_model);



gboolean g_sheet_model_get_column_sensitivity (const GSheetModel *model,
					       gint col);

gchar * g_sheet_model_get_column_subtitle (const GSheetModel *model,
					    gint col);

GtkSheetButton * g_sheet_model_get_column_button (const GSheetModel *, gint);

GtkJustification g_sheet_model_get_column_justification (const GSheetModel *,
							 gint);



gboolean g_sheet_model_get_row_sensitivity (const GSheetModel *model,
					    gint row);


gchar * g_sheet_model_get_row_subtitle (const GSheetModel *model,
					    gint row);


GtkSheetButton * g_sheet_model_get_row_button (const GSheetModel *, gint);




G_END_DECLS

#endif /* __G_SHEET_MODEL_H__ */
