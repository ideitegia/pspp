/* This version of GtkSheet has been heavily modified, for the specific
   requirements of PSPPIRE. */


/* GtkSheet widget for Gtk+.
 * Copyright (C) 1999-2001 Adrian E. Feiguin <adrian@ifir.ifir.edu.ar>
 *
 * Based on GtkClist widget by Jay Painter, but major changes.
 * Memory allocation routines inspired on SC (Spreadsheet Calculator)
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

#ifndef __GTK_SHEET_H__
#define __GTK_SHEET_H__

#include <gtk/gtk.h>

#include "gtkextra-sheet.h"
#include "gsheetmodel.h"
#include "gsheet-column-iface.h"
#include "gsheet-row-iface.h"


G_BEGIN_DECLS


typedef enum
{
  GTK_SHEET_FOREGROUND,
  GTK_SHEET_BACKGROUND,
  GTK_SHEET_FONT,
  GTK_SHEET_JUSTIFICATION,
  GTK_SHEET_BORDER,
  GTK_SHEET_BORDER_COLOR,
  GTK_SHEET_IS_EDITABLE,
  GTK_SHEET_IS_VISIBLE
} GtkSheetAttrType;

/* sheet->state */

enum
{
  GTK_SHEET_NORMAL,
  GTK_SHEET_ROW_SELECTED,
  GTK_SHEET_COLUMN_SELECTED,
  GTK_SHEET_RANGE_SELECTED
};


#define GTK_TYPE_SHEET_RANGE (gtk_sheet_range_get_type ())
#define GTK_TYPE_SHEET (gtk_sheet_get_type ())

#define GTK_SHEET(obj)          GTK_CHECK_CAST (obj, gtk_sheet_get_type (), GtkSheet)
#define GTK_SHEET_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, gtk_sheet_get_type (), GtkSheetClass)
#define GTK_IS_SHEET(obj)       GTK_CHECK_TYPE (obj, gtk_sheet_get_type ())




typedef struct _GtkSheetClass GtkSheetClass;
typedef struct _GtkSheetCellAttr     GtkSheetCellAttr;
typedef struct _GtkSheetCell GtkSheetCell;
typedef struct _GtkSheetHoverTitle GtkSheetHoverTitle;


struct _GtkSheetCellAttr
{
  GtkJustification justification;
  const PangoFontDescription *font_desc;
  GdkColor foreground;
  GdkColor background;
  GtkSheetCellBorder border;
  gboolean is_editable;
  gboolean is_visible;
};

struct _GtkSheetCell
{
  gint row;
  gint col;
};


struct _GtkSheetHoverTitle
{
  GtkWidget *window;
  GtkWidget *label;
  gint row, column;
};

enum
  {
    BG_COLOR,
    GRID_COLOR,
    n_COLORS
  };

struct _GtkSheet
{
  GtkBin parent;

  gboolean dispose_has_run;
  GSheetColumn *column_geometry;
  GSheetRow *row_geometry;

  guint16 flags;

  GSheetModel *model;

  GtkSelectionMode selection_mode;
  gboolean autoresize;

  /* Component colors */
  GdkColor color[n_COLORS];
  gboolean show_grid;

  /* allocation rectangle after the container_border_width
     and the width of the shadow border */
  GdkRectangle internal_allocation;

  gint16 column_requisition;
  gint16 row_requisition;

  gboolean rows_resizable;
  gboolean columns_resizable;

  /* active cell */
  GtkSheetCell active_cell;

  /* The GtkEntry used for editing the cells */
  GtkWidget *entry_widget;

  /* The widget containing entry_widget, or
     entry_widget itself if no container */
  GtkWidget *entry_container;

  /* The type of entry_widget */
  GtkType entry_type;

  /* expanding selection */
  GtkSheetCell selection_cell;

  /* global selection button */
  GtkWidget *button;

  /* sheet state */
  gint state;

  /* selected range */
  GtkSheetRange range;

  /*the scrolling window and it's height and width to
   * make things a little speedier */
  GdkWindow *sheet_window;
  guint sheet_window_width;
  guint sheet_window_height;

  /* sheet backing pixmap */
  GdkPixmap *pixmap;

  /* border shadow style */
  GtkShadowType shadow_type;

  /* Column Titles */
  GdkRectangle column_title_area;
  GdkWindow *column_title_window;
  gboolean column_titles_visible;
  /* TRUE if the cursor is over the column title window */
  gboolean column_title_under;

  /* Row Titles */
  GdkRectangle row_title_area;
  GdkWindow *row_title_window;
  gboolean row_titles_visible;
  /* TRUE if the cursor is over the row title window */
  gboolean row_title_under;

  /*scrollbars*/
  GtkAdjustment *hadjustment;
  GtkAdjustment *vadjustment;

  /* xor GC for the verticle drag line */
  GdkGC *xor_gc;

  /* gc for drawing unselected cells */
  GdkGC *fg_gc;
  GdkGC *bg_gc;

  /* cursor used to indicate dragging */
  GdkCursor *cursor_drag;

  /* the current x-pixel location of the xor-drag vline */
  gint x_drag;

  /* the current y-pixel location of the xor-drag hline */
  gint y_drag;

  /* current cell being dragged */
  GtkSheetCell drag_cell;
  /* current range being dragged */
  GtkSheetRange drag_range;

  /* Used for the subtitle (popups) */
  gint motion_timer;
  GtkSheetHoverTitle *hover_window;
};

struct _GtkSheetClass
{
  GtkBinClass parent_class;

 void (*set_scroll_adjustments) (GtkSheet *sheet,
				 GtkAdjustment *hadjustment,
				 GtkAdjustment *vadjustment);

 void (*select_row) 		(GtkSheet *sheet, gint row);

 void (*select_column) 		(GtkSheet *sheet, gint column);

 void (*select_range) 		(GtkSheet *sheet, GtkSheetRange *range);

 void (*resize_range)		(GtkSheet *sheet,
	                	GtkSheetRange *old_range,
                        	GtkSheetRange *new_range);

 void (*move_range)    		(GtkSheet *sheet,
	                	GtkSheetRange *old_range,
                        	GtkSheetRange *new_range);

 gboolean (*traverse)       	(GtkSheet *sheet,
                         	gint row, gint column,
                         	gint *new_row, gint *new_column);

 gboolean (*deactivate)	 	(GtkSheet *sheet,
	                  	gint row, gint column);

 gboolean (*activate) 		(GtkSheet *sheet,
	                	gint row, gint column);

 void (*changed) 		(GtkSheet *sheet,
	          		gint row, gint column);
};

GType gtk_sheet_get_type (void);
GtkType gtk_sheet_range_get_type (void);


/* create a new sheet */
GtkWidget * gtk_sheet_new (GSheetRow *vgeo, GSheetColumn *hgeo,
			   GSheetModel *model);

/* create a new sheet with custom entry */
GtkWidget *
gtk_sheet_new_with_custom_entry 	(GSheetRow *vgeo,
					 GSheetColumn *hgeo,
                                 	 GtkType entry_type);
void
gtk_sheet_construct_with_custom_entry	(GtkSheet *sheet,
					 GSheetRow *vgeo,
					 GSheetColumn *hgeo,
					 GtkType entry_type);
/* Change entry */
void gtk_sheet_change_entry		(GtkSheet *sheet, GtkType entry_type);

GtkWidget *gtk_sheet_get_entry    (GtkSheet *sheet);


guint gtk_sheet_get_columns_count (GtkSheet *sheet);

guint gtk_sheet_get_rows_count 	  (GtkSheet *sheet);

void gtk_sheet_get_selected_range (GtkSheet *sheet,
					 GtkSheetRange *range);

void gtk_sheet_set_selection_mode (GtkSheet *sheet, gint mode);


void gtk_sheet_show_grid	  (GtkSheet *sheet,
					 gboolean show);

gboolean gtk_sheet_grid_visible   (GtkSheet *sheet);

/* set/get column title */
void gtk_sheet_set_column_title   (GtkSheet * sheet,
			    		gint column,
			    		const gchar * title);

const gchar *gtk_sheet_get_column_title  (GtkSheet * sheet,
			    		gint column);

/* set/get row title */
void gtk_sheet_set_row_title 	  (GtkSheet * sheet,
			    		gint row,
			    		const gchar * title);

const gchar *gtk_sheet_get_row_title (GtkSheet * sheet,
				      gint row);

/* set/get button label */
void gtk_sheet_row_button_add_label (GtkSheet *sheet,
					gint row, const gchar *label);

const gchar *gtk_sheet_row_button_get_label  (GtkSheet *sheet,
					      gint row);

void gtk_sheet_row_button_justify   (GtkSheet *sheet,
				     gint row, GtkJustification justification);

/* scroll the viewing area of the sheet to the given column
 * and row; row_align and col_align are between 0-1 representing the
 * location the row should appear on the screen, 0.0 being top or left,
 * 1.0 being bottom or right; if row or column is negative then there
 * is no change */
void gtk_sheet_moveto (GtkSheet *sheet,
		  gint row,
		  gint column,
	          gfloat row_align,
                  gfloat col_align);


void gtk_sheet_show_row_titles		(GtkSheet *sheet);
void gtk_sheet_hide_row_titles		(GtkSheet *sheet);
void gtk_sheet_show_column_titles       (GtkSheet *sheet);
void gtk_sheet_hide_column_titles	(GtkSheet *sheet);

gboolean gtk_sheet_row_titles_visible	(GtkSheet *sheet);


/* set row button sensitivity. If sensitivity is TRUE can be toggled,
 * otherwise it acts as a title */
void gtk_sheet_row_set_sensitivity      (GtkSheet *sheet,
					gint row,  gboolean sensitive);

/* set sensitivity for all row buttons */
void gtk_sheet_rows_set_sensitivity	(GtkSheet *sheet, gboolean sensitive);
void gtk_sheet_rows_set_resizable	(GtkSheet *sheet, gboolean resizable);
gboolean gtk_sheet_rows_resizable	(GtkSheet *sheet);

/* set row visibility. The default value is TRUE. If FALSE, the
 * row is hidden */
void gtk_sheet_row_set_visibility	(GtkSheet *sheet,
					 gint row, gboolean visible);

void gtk_sheet_row_label_set_visibility	(GtkSheet *sheet,
					 gint row, gboolean visible);

void gtk_sheet_rows_labels_set_visibility (GtkSheet *sheet, gboolean visible);


/* select the row. The range is then highlighted, and the bounds are stored
 * in sheet->range  */
void gtk_sheet_select_row    (GtkSheet * sheet,  gint row);

/* select the column. The range is then highlighted, and the bounds are stored
 * in sheet->range  */
void gtk_sheet_select_column (GtkSheet * sheet,  gint column);

/* highlight the selected range and store bounds in sheet->range */
void gtk_sheet_select_range (GtkSheet *sheet, const GtkSheetRange *range);

void gtk_sheet_get_visible_range (GtkSheet *sheet, GtkSheetRange *range);


/* obvious */
void gtk_sheet_unselect_range		(GtkSheet *sheet);

/* set active cell where the entry will be displayed
 * returns FALSE if current cell can't be deactivated or
 * requested cell can't be activated */
gboolean gtk_sheet_set_active_cell (GtkSheet *sheet,
					gint row, gint column);

/* Sets *ROW and *COLUMN to be the coordinates of the active cell.
   ROW and/or COLUMN may be null if the caller is not interested in their
   values */
void gtk_sheet_get_active_cell (GtkSheet *sheet,
					gint *row, gint *column);

/* get cell contents */
gchar *gtk_sheet_cell_get_text (const GtkSheet *sheet, gint row, gint col);

/* clear cell contents */
void gtk_sheet_cell_clear      (GtkSheet *sheet, gint row, gint col);

/* clear range contents. If range==NULL the whole sheet will be cleared */
void gtk_sheet_range_clear	(GtkSheet *sheet,
					 const GtkSheetRange *range);

/* get cell state: GTK_STATE_NORMAL, GTK_STATE_SELECTED */
GtkStateType gtk_sheet_cell_get_state (GtkSheet *sheet, gint row, gint col);

/* get area of a given cell */
gboolean gtk_sheet_get_cell_area (GtkSheet *sheet,
                         gint row,
                         gint column,
                         GdkRectangle *area);

/* set row height */
void gtk_sheet_set_row_height (GtkSheet * sheet,
			  gint row,
			  guint height);


/* delete nrows rows starting in row */
void gtk_sheet_delete_rows    (GtkSheet *sheet, guint row, guint nrows);

/* append nrows row to the end of the sheet */
void gtk_sheet_add_row	      (GtkSheet *sheet, guint nrows);

/* insert nrows rows before the given row and pull right */
void gtk_sheet_insert_rows    (GtkSheet *sheet, guint row, guint nrows);

/* set abckground color of the given range */
void gtk_sheet_range_set_background  (GtkSheet *sheet,
					const GtkSheetRange *range,
					const GdkColor *color);

/* set foreground color (text color) of the given range */
void gtk_sheet_range_set_foreground  (GtkSheet *sheet,
					const GtkSheetRange *range,
					const GdkColor *color);

/* set text justification (GTK_JUSTIFY_LEFT, RIGHT, CENTER) of the given range.
 * The default value is GTK_JUSTIFY_LEFT. If autoformat is on, the
 * default justification for numbers is GTK_JUSTIFY_RIGHT */
void gtk_sheet_range_set_justification	(GtkSheet *sheet,
					const GtkSheetRange *range,
					GtkJustification justification);

void gtk_sheet_column_set_justification (GtkSheet *sheet,
                                        gint column,
                                        GtkJustification justification);

/* set if cell contents can be edited or not in the given range:
 * accepted values are TRUE or FALSE. */
void gtk_sheet_range_set_editable (GtkSheet *sheet,
					const GtkSheetRange *range,
					gint editable);

/* set if cell contents are visible or not in the given range:
 * accepted values are TRUE or FALSE.*/
void gtk_sheet_range_set_visible  (GtkSheet *sheet,
					const GtkSheetRange *range,
					gboolean visible);

/* set cell border style in the given range.
 * mask values are CELL_LEFT_BORDER, CELL_RIGHT_BORDER, CELL_TOP_BORDER,
 * CELL_BOTTOM_BORDER
 * width is the width of the border line in pixels
 * line_style is the line_style for the border line */
void gtk_sheet_range_set_border		(GtkSheet *sheet,
					const GtkSheetRange *range,
					gint mask,
					guint width,
					gint line_style);

/* set border color for the given range */
void gtk_sheet_range_set_border_color	(GtkSheet *sheet,
					const GtkSheetRange *range,
					const GdkColor *color);

/* set font for the given range */
void gtk_sheet_range_set_font		(GtkSheet *sheet,
					const GtkSheetRange *range,
					PangoFontDescription *font);

/* get cell attributes of the given cell */
/* TRUE means that the cell is currently allocated */
gboolean gtk_sheet_get_attributes       (const GtkSheet *sheet,
					gint row, gint col,
					GtkSheetCellAttr *attributes);


void           gtk_sheet_set_model (GtkSheet *sheet,
				   GSheetModel *model);

GSheetModel * gtk_sheet_get_model (const GtkSheet *sheet);


G_END_DECLS


#endif /* __GTK_SHEET_H__ */


