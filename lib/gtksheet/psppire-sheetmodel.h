/* PsppireSheetModel --- an abstract model for the PsppireSheet widget.
 * Copyright (C) 2006, 2008 Free Software Foundation

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

#ifndef __PSPPIRE_SHEET_MODEL_H__
#define __PSPPIRE_SHEET_MODEL_H__


/* This file provides an abstract interface or the data displayed by the
   PsppireSheet widget */

#include <glib-object.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include "gtkextra-sheet.h"

G_BEGIN_DECLS

#define PSPPIRE_TYPE_SHEET_MODEL            (psppire_sheet_model_get_type ())
#define PSPPIRE_SHEET_MODEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_TYPE_SHEET_MODEL, PsppireSheetModel))
#define PSPPIRE_IS_SHEET_MODEL(obj)	       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_SHEET_MODEL))
#define PSPPIRE_SHEET_MODEL_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), PSPPIRE_TYPE_SHEET_MODEL, PsppireSheetModelIface))

typedef enum
{
  PSPPIRE_SHEET_LEFT_BORDER     = 1 << 0,
  PSPPIRE_SHEET_RIGHT_BORDER    = 1 << 1,
  PSPPIRE_SHEET_TOP_BORDER      = 1 << 2,
  PSPPIRE_SHEET_BOTTOM_BORDER   = 1 << 3
} PsppireSheetBorderType ;


typedef struct _PsppireSheetModel        PsppireSheetModel; /* Dummy typedef */
typedef struct _PsppireSheetModelIface   PsppireSheetModelIface;
typedef struct _PsppireSheetRange PsppireSheetRange;
typedef struct _PsppireSheetCellBorder     PsppireSheetCellBorder;

struct _PsppireSheetRange
{
  gint row0, col0; /* upper-left cell */
  gint rowi, coli; /* lower-right cell */
};

struct _PsppireSheetCellBorder
{
  PsppireSheetBorderType mask;
  guint width;
  GdkLineStyle line_style;
  GdkCapStyle cap_style;
  GdkJoinStyle join_style;
  GdkColor color;
};



struct _PsppireSheetModelIface
{
  GTypeInterface g_iface;

  gboolean free_strings;

  /* Signals */
  void         (* range_changed)    (PsppireSheetModel *sheet_model,
				     glong row0, glong col0,
				     glong rowi, glong coli);

  void         (* rows_inserted)    (PsppireSheetModel *sheet_model,
				     glong row, glong n_rows);

  void         (* rows_deleted)     (PsppireSheetModel *sheet_model,
				     glong row, glong n_rows);

  void         (* columns_inserted)    (PsppireSheetModel *sheet_model,
					glong column, glong n_columns);

  void         (* columns_deleted)     (PsppireSheetModel *sheet_model,
					glong column, glong n_columns);



  /* Virtual Table */

  gchar *      (* get_string)      (const PsppireSheetModel *sheet_model,
				    glong row, glong column);

  gboolean  (* set_string) (PsppireSheetModel *sheet_model,
			    const gchar *s, glong row, glong column);

  gboolean  (* clear_datum) (PsppireSheetModel *sheet_model,
			     glong row, glong column);

  gboolean (* is_editable) (const PsppireSheetModel *sheet_model, glong row, glong column);

  GdkColor *  (* get_foreground) (const PsppireSheetModel *sheet_model,
				  glong row, glong column);

  GdkColor *  (* get_background) (const PsppireSheetModel *sheet_model,
				  glong row, glong column);

  const GtkJustification *  (* get_justification) (const PsppireSheetModel *sheet_model,
						   glong row, glong column);

  /* column related metadata */

  gchar * (*get_column_title) (const PsppireSheetModel *, gint col);
  gchar * (*get_column_subtitle) (const PsppireSheetModel *, gint col);
  gboolean (*get_column_sensitivity) (const PsppireSheetModel *, gint col);
  GtkJustification (*get_column_justification) (const PsppireSheetModel *mode, gint col);
  const PsppireSheetButton * (* get_button) (const PsppireSheetModel *model, gint col);

  glong (*get_column_count) (const PsppireSheetModel *model);


  /* row related metadata */
  gchar * (*get_row_title) (const PsppireSheetModel *, gint row);
  gchar * (*get_row_subtitle) (const PsppireSheetModel *, gint row);
  glong (*get_row_count) (const PsppireSheetModel *model);
  gboolean (*get_row_sensitivity) (const PsppireSheetModel *, gint row);
};



GType              psppire_sheet_model_get_type   (void) G_GNUC_CONST;


gchar * psppire_sheet_model_get_string (const PsppireSheetModel *sheet_model,
				  glong row, glong column);

gboolean  psppire_sheet_model_set_string (PsppireSheetModel *sheet_model,
				    const gchar *s,
				    glong row, glong column);

gboolean psppire_sheet_model_datum_clear    (PsppireSheetModel *sheet_model,
				       glong row, glong column);


void psppire_sheet_model_range_changed (PsppireSheetModel *sheet_model,
				  glong row0, glong col0,
				  glong rowi, glong coli);

void psppire_sheet_model_rows_deleted (PsppireSheetModel *sheet_model,
				 glong row, glong n_rows);

void psppire_sheet_model_rows_inserted (PsppireSheetModel *sheet_model,
				  glong row, glong n_rows);

void psppire_sheet_model_columns_inserted (PsppireSheetModel *sheet_model,
				     glong column, glong n_columns);

void psppire_sheet_model_columns_deleted (PsppireSheetModel *sheet_model,
				    glong column, glong n_columns);


gboolean psppire_sheet_model_is_editable (const PsppireSheetModel *model,
				    glong row, glong column);

gboolean psppire_sheet_model_is_visible
 (const PsppireSheetModel *model, glong row, glong column);


GdkColor *psppire_sheet_model_get_foreground
 (const PsppireSheetModel *model, glong row, glong column);

GdkColor *psppire_sheet_model_get_background
 (const PsppireSheetModel *model, glong row, glong column);

const GtkJustification *psppire_sheet_model_get_justification
 (const PsppireSheetModel *model, glong row, glong column);

const PsppireSheetCellBorder * psppire_sheet_model_get_cell_border
 (const PsppireSheetModel *model, glong row, glong column);

gboolean psppire_sheet_model_free_strings (const PsppireSheetModel *sheet_model);

glong psppire_sheet_model_get_column_count (const PsppireSheetModel *sheet_model);

gint psppire_sheet_model_get_row_count (const PsppireSheetModel *sheet_model);



gboolean psppire_sheet_model_get_column_sensitivity (const PsppireSheetModel *model,
					       gint col);

gchar * psppire_sheet_model_get_column_subtitle (const PsppireSheetModel *model,
					    gint col);

PsppireSheetButton * psppire_sheet_model_get_column_button (const PsppireSheetModel *, gint);

GtkJustification psppire_sheet_model_get_column_justification (const PsppireSheetModel *,
							 gint);



gboolean psppire_sheet_model_get_row_sensitivity (const PsppireSheetModel *model,
					    gint row);


gchar * psppire_sheet_model_get_row_subtitle (const PsppireSheetModel *model,
					    gint row);


PsppireSheetButton * psppire_sheet_model_get_row_button (const PsppireSheetModel *, gint);




G_END_DECLS

#endif /* __PSPPIRE_SHEET_MODEL_H__ */
