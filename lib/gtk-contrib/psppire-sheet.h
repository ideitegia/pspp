/*
   Copyright (C) 2006, 2008 Free Software Foundation

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.


 This file is derived from the gtksheet.c and extensively modified for the
 requirements of PSPPIRE.  The changes are copyright by the
 Free Software Foundation.  The copyright notice for the original work is
 below.


 GtkSheet widget for Gtk+.
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

#ifndef __PSPPIRE_SHEET_H__
#define __PSPPIRE_SHEET_H__

#include <gtk/gtk.h>

#include "gtkextra-sheet.h"
#include <ui/gui/sheet/psppire-sheetmodel.h>
#include <ui/gui/sheet/psppire-axis.h>

G_BEGIN_DECLS

/* sheet->select_status */
enum
{
  PSPPIRE_SHEET_NORMAL,
  PSPPIRE_SHEET_ROW_SELECTED,
  PSPPIRE_SHEET_COLUMN_SELECTED,
  PSPPIRE_SHEET_RANGE_SELECTED
};


#define PSPPIRE_TYPE_SHEET_RANGE (psppire_sheet_range_get_type ())
#define PSPPIRE_TYPE_SHEET_CELL (psppire_sheet_cell_get_type ())
#define PSPPIRE_TYPE_SHEET (psppire_sheet_get_type ())

#define PSPPIRE_SHEET(obj)          GTK_CHECK_CAST (obj, psppire_sheet_get_type (), PsppireSheet)
#define PSPPIRE_SHEET_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, psppire_sheet_get_type (), PsppireSheetClass)
#define PSPPIRE_IS_SHEET(obj)       GTK_CHECK_TYPE (obj, psppire_sheet_get_type ())


typedef struct _PsppireSheetClass PsppireSheetClass;
typedef struct _PsppireSheetCellAttr     PsppireSheetCellAttr;

typedef struct _PsppireSheetHoverTitle PsppireSheetHoverTitle;


struct _PsppireSheetCellAttr
{
  GtkJustification justification;
  GdkColor foreground;
  GdkColor background;
  PsppireSheetCellBorder border;
};

struct _PsppireSheetHoverTitle
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

struct _PsppireSheet
{
  GtkBin parent;

  gboolean dispose_has_run;
  PsppireAxis *haxis;
  PsppireAxis *vaxis;

  guint16 flags;

  PsppireSheetModel *model;

  GtkSelectionMode selection_mode;

  /* Component colors */
  GdkColor color[n_COLORS];
  gboolean show_grid;

  /* active cell */
  PsppireSheetCell active_cell;

  /* The GtkEntry used for editing the cells */
  GtkWidget *entry_widget;

  /* The type of entry_widget */
  GtkType entry_type;

  /* global selection button */
  GtkWidget *button;

  /* sheet state */
  gint select_status;

  /* selected range */
  PsppireSheetRange range;

  /* The space between a cell's contents and its border */
  GtkBorder *cell_padding;

  /* the scrolling window and its height and width to
   * make things a little speedier */
  GdkWindow *sheet_window;

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
  PsppireSheetCell drag_cell;
  /* current range being dragged */
  PsppireSheetRange drag_range;

  /* Used for the subtitle (popups) */
  gint motion_timer;
  PsppireSheetHoverTitle *hover_window;

  gulong update_handler_id;
};

struct _PsppireSheetClass
{
  GtkBinClass parent_class;

 gboolean (*set_scroll_adjustments) (PsppireSheet *sheet,
				 GtkAdjustment *hadjustment,
				 GtkAdjustment *vadjustment);

 void (*select_row) 		(PsppireSheet *sheet, gint row);

 void (*select_column) 		(PsppireSheet *sheet, gint column);

 void (*select_range) 		(PsppireSheet *sheet, PsppireSheetRange *range);

 void (*resize_range)		(PsppireSheet *sheet,
	                	PsppireSheetRange *old_range,
                        	PsppireSheetRange *new_range);

 void (*move_range)    		(PsppireSheet *sheet,
	                	PsppireSheetRange *old_range,
                        	PsppireSheetRange *new_range);

 gboolean (*traverse)       	(PsppireSheet *sheet,
                         	gint row, gint column,
                         	gint *new_row, gint *new_column);

 gboolean (*activate) 		(PsppireSheet *sheet,
	                	gint row, gint column);

 void (*changed) 		(PsppireSheet *sheet,
	          		gint row, gint column);
};

GType psppire_sheet_get_type (void);
GtkType psppire_sheet_range_get_type (void);


/* create a new sheet */
GtkWidget * psppire_sheet_new (PsppireSheetModel *model);

/* create a new sheet with custom entry */
GtkWidget *
psppire_sheet_new_with_custom_entry (GtkType entry_type);

/* Change entry */
void psppire_sheet_change_entry		(PsppireSheet *sheet, GtkType entry_type);

GtkEntry *psppire_sheet_get_entry    (PsppireSheet *sheet);


void psppire_sheet_get_selected_range (PsppireSheet *sheet,
					 PsppireSheetRange *range);

void psppire_sheet_show_grid	  (PsppireSheet *sheet,
					 gboolean show);

gboolean psppire_sheet_grid_visible   (PsppireSheet *sheet);


/* scroll the viewing area of the sheet to the given column
 * and row; row_align and col_align are between 0-1 representing the
 * location the row should appear on the screen, 0.0 being top or left,
 * 1.0 being bottom or right; if row or column is negative then there
 * is no change */
void psppire_sheet_moveto (PsppireSheet *sheet,
		  gint row,
		  gint column,
	          gfloat row_align,
                  gfloat col_align);


void psppire_sheet_show_row_titles		(PsppireSheet *sheet);
void psppire_sheet_hide_row_titles		(PsppireSheet *sheet);
void psppire_sheet_show_column_titles       (PsppireSheet *sheet);
void psppire_sheet_hide_column_titles	(PsppireSheet *sheet);

/* select the row. The range is then highlighted, and the bounds are stored
 * in sheet->range  */
void psppire_sheet_select_row    (PsppireSheet * sheet,  gint row);

/* select the column. The range is then highlighted, and the bounds are stored
 * in sheet->range  */
void psppire_sheet_select_column (PsppireSheet * sheet,  gint column);

/* highlight the selected range and store bounds in sheet->range */
void psppire_sheet_select_range (PsppireSheet *sheet, const PsppireSheetRange *range);

void psppire_sheet_get_visible_range (PsppireSheet *sheet, PsppireSheetRange *range);


/* obvious */
void psppire_sheet_unselect_range		(PsppireSheet *sheet);

/* set active cell where the entry will be displayed */
void psppire_sheet_set_active_cell (PsppireSheet *sheet,
				gint row, gint column);

/* Sets *ROW and *COLUMN to be the coordinates of the active cell.
   ROW and/or COLUMN may be null if the caller is not interested in their
   values */
void psppire_sheet_get_active_cell (PsppireSheet *sheet,
					gint *row, gint *column);

/* get cell contents */
gchar *psppire_sheet_cell_get_text (const PsppireSheet *sheet, gint row, gint col);


void psppire_sheet_set_model (PsppireSheet *sheet,
				   PsppireSheetModel *model);

PsppireSheetModel * psppire_sheet_get_model (const PsppireSheet *sheet);


G_END_DECLS


#endif /* __PSPPIRE_SHEET_H__ */


