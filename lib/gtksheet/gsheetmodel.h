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
  gint row0,col0; /* upper-left cell */
  gint rowi,coli; /* lower-right cell */
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
				     gint row0, gint col0, 
				     gint rowi, gint coli);

  void         (* rows_inserted)    (GSheetModel *sheet_model,
				     gint row, gint n_rows);

  void         (* rows_deleted)     (GSheetModel *sheet_model,
				     gint row, gint n_rows);


  /* Virtual Table */

  gchar *      (* get_string)      (const GSheetModel *sheet_model, 
					       gint row, gint column);

  gboolean  (* set_string) (GSheetModel *sheet_model, 
			    const gchar *s, gint row, gint column);

  gboolean  (* clear_datum) (GSheetModel *sheet_model, 
			     gint row, gint column);

  gboolean (* is_visible) (const GSheetModel *sheet_model, gint row, gint column);
  gboolean (* is_editable) (const GSheetModel *sheet_model, gint row, gint column);

  const GdkColor *  (* get_foreground) (const GSheetModel *sheet_model, 
				    gint row, gint column);

  const GdkColor *  (* get_background) (const GSheetModel *sheet_model, 
				    gint row, gint column);

  const GtkJustification *  (* get_justification) (const GSheetModel *sheet_model, 
						   gint row, gint column);

  const PangoFontDescription *  (* get_font_desc) (const GSheetModel *sheet_model, 
						   gint row, gint column);

  const GtkSheetCellBorder *  (* get_cell_border) (const GSheetModel *sheet_model, 
						   gint row, gint column);

};



GType              g_sheet_model_get_type   (void) G_GNUC_CONST;


inline  gchar * g_sheet_model_get_string (const GSheetModel *sheet_model, 
					       gint row, gint column);

inline gboolean  g_sheet_model_set_string (GSheetModel *sheet_model, 
				      const gchar *s, 
				      gint row, gint column);

inline gboolean g_sheet_model_datum_clear    (GSheetModel *sheet_model, 
					 gint row, gint column);


inline void g_sheet_model_range_changed (GSheetModel *sheet_model,
				    gint row0, gint col0,
				    gint rowi, gint coli);

inline void g_sheet_model_rows_deleted (GSheetModel *sheet_model,
				   gint row, gint n_rows);

inline void g_sheet_model_rows_inserted (GSheetModel *sheet_model,
				    gint row, gint n_rows);

inline gboolean g_sheet_model_is_editable (const GSheetModel *model, 
				      gint row, gint column);

inline gboolean g_sheet_model_is_visible 
                   (const GSheetModel *model, gint row, gint column);


inline const GdkColor *g_sheet_model_get_foreground 
                   (const GSheetModel *model, gint row, gint column);

inline const GdkColor *g_sheet_model_get_background 
                   (const GSheetModel *model, gint row, gint column);


inline const GtkJustification *g_sheet_model_get_justification 
                   (const GSheetModel *model, gint row, gint column);


inline const PangoFontDescription *g_sheet_model_get_font_desc
                   (const GSheetModel *model, gint row, gint column);

inline const GtkSheetCellBorder * g_sheet_model_get_cell_border 
                   (const GSheetModel *model, gint row, gint column);

inline  gboolean g_sheet_model_free_strings (const GSheetModel *sheet_model);



G_END_DECLS

#endif /* __G_SHEET_MODEL_H__ */
