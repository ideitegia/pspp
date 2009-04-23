/*
   Copyright (C) 2006, 2008, 2009 Free Software Foundation

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
*/

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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * SECTION:psppiresheet
 * @short_description: spreadsheet widget for gtk2
 *
 * PsppireSheet is a matrix widget for GTK+. It consists of an scrollable grid of
 * cells where you can allocate text. Cell contents can be edited interactively
 * through a specially designed entry, GtkItemEntry.
 *
 */
#include <config.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkadjustment.h>
#include <gtk/gtktypeutils.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkcontainer.h>
#include <pango/pango.h>
#include "psppire-sheet.h"
#include <ui/gui/psppire-marshal.h>
#include <ui/gui/sheet/psppire-sheetmodel.h>
#include <ui/gui/sheet/psppire-axis.h>
#include <libpspp/misc.h>

#include <math.h>

/* sheet flags */
enum
  {
    PSPPIRE_SHEET_IN_XDRAG = 1 << 1,
    PSPPIRE_SHEET_IN_YDRAG = 1 << 2,
    PSPPIRE_SHEET_IN_DRAG = 1 << 3,
    PSPPIRE_SHEET_IN_SELECTION = 1 << 4,
    PSPPIRE_SHEET_IN_RESIZE = 1 << 5
  };

#define PSPPIRE_SHEET_FLAGS(sheet) (PSPPIRE_SHEET (sheet)->flags)
#define PSPPIRE_SHEET_SET_FLAGS(sheet,flag) (PSPPIRE_SHEET_FLAGS (sheet) |= (flag))
#define PSPPIRE_SHEET_UNSET_FLAGS(sheet,flag) (PSPPIRE_SHEET_FLAGS (sheet) &= ~ (flag))

#define PSPPIRE_SHEET_IN_XDRAG(sheet) (PSPPIRE_SHEET_FLAGS (sheet) & PSPPIRE_SHEET_IN_XDRAG)
#define PSPPIRE_SHEET_IN_YDRAG(sheet) (PSPPIRE_SHEET_FLAGS (sheet) & PSPPIRE_SHEET_IN_YDRAG)
#define PSPPIRE_SHEET_IN_DRAG(sheet) (PSPPIRE_SHEET_FLAGS (sheet) & PSPPIRE_SHEET_IN_DRAG)
#define PSPPIRE_SHEET_IN_SELECTION(sheet) (PSPPIRE_SHEET_FLAGS (sheet) & PSPPIRE_SHEET_IN_SELECTION)
#define PSPPIRE_SHEET_IN_RESIZE(sheet) (PSPPIRE_SHEET_FLAGS (sheet) & PSPPIRE_SHEET_IN_RESIZE)

#define CELL_SPACING 1

#define TIMEOUT_HOVER 300
#define COLUMN_MIN_WIDTH 10
#define COLUMN_TITLES_HEIGHT 4
#define DEFAULT_COLUMN_WIDTH 80
#define DEFAULT_ROW_HEIGHT 25

static void set_entry_widget_font (PsppireSheet *sheet);

static void psppire_sheet_update_primary_selection (PsppireSheet *sheet);
static void draw_column_title_buttons_range (PsppireSheet *sheet, gint first, gint n);
static void draw_row_title_buttons_range (PsppireSheet *sheet, gint first, gint n);
static void redraw_range (PsppireSheet *sheet, PsppireSheetRange *range);


static void set_row_height (PsppireSheet *sheet,
			    gint row,
			    gint height);

static void destroy_hover_window (PsppireSheetHoverTitle *);
static PsppireSheetHoverTitle *create_hover_window (void);

static GtkStateType psppire_sheet_cell_get_state (PsppireSheet *sheet, gint row, gint col);


static inline  void
dispose_string (const PsppireSheet *sheet, gchar *text)
{
  PsppireSheetModel *model = psppire_sheet_get_model (sheet);

  if ( ! model )
    return;

  if (psppire_sheet_model_free_strings (model))
    g_free (text);
}


/* FIXME: Why bother with these two ? */

/* returns the column index from a pixel location */
static inline gint
column_from_xpixel (const PsppireSheet *sheet, gint pixel)
{
  return psppire_axis_unit_at_pixel (sheet->haxis, pixel);
}

static inline gint
row_from_ypixel (const PsppireSheet *sheet, gint pixel)
{
  return psppire_axis_unit_at_pixel (sheet->vaxis, pixel);
}


/* Return the lowest row number which is wholly or partially on
   the visible range of the sheet */
static inline glong
min_visible_row (const PsppireSheet *sheet)
{
  return row_from_ypixel (sheet, sheet->vadjustment->value);
}

static inline glong
min_fully_visible_row (const PsppireSheet *sheet)
{
  glong row = min_visible_row (sheet);

  if ( psppire_axis_start_pixel (sheet->vaxis, row) < sheet->vadjustment->value)
    row++;

  return row;
}

static inline glong
max_visible_row (const PsppireSheet *sheet)
{
  return row_from_ypixel (sheet, sheet->vadjustment->value + sheet->vadjustment->page_size);
}


static inline glong
max_fully_visible_row (const PsppireSheet *sheet)
{
  glong row = max_visible_row (sheet);

  if ( psppire_axis_start_pixel (sheet->vaxis, row)
       +
       psppire_axis_unit_size (sheet->vaxis, row)
       > sheet->vadjustment->value)
    row--;

  return row;
}


/* Returns the lowest column number which is wholly or partially
   on the sheet */
static inline glong
min_visible_column (const PsppireSheet *sheet)
{
  return column_from_xpixel (sheet, sheet->hadjustment->value);
}

static inline glong
min_fully_visible_column (const PsppireSheet *sheet)
{
  glong col = min_visible_column (sheet);

  if ( psppire_axis_start_pixel (sheet->haxis, col) < sheet->hadjustment->value)
    col++;

  return col;
}


/* Returns the highest column number which is wholly or partially
   on the sheet */
static inline glong
max_visible_column (const PsppireSheet *sheet)
{
  return column_from_xpixel (sheet, sheet->hadjustment->value + sheet->hadjustment->page_size);
}

static inline glong
max_fully_visible_column (const PsppireSheet *sheet)
{
  glong col = max_visible_column (sheet);

  if ( psppire_axis_start_pixel (sheet->haxis, col)
       +
       psppire_axis_unit_size (sheet->haxis, col)
       > sheet->hadjustment->value)
    col--;

  return col;
}



/* The size of the region (in pixels) around the row/column boundaries
   where the height/width may be grabbed to change size */
#define DRAG_WIDTH 6

static gboolean
on_column_boundary (const PsppireSheet *sheet, gint x, gint *column)
{
  gint col;
  gint pixel;

  x += sheet->hadjustment->value;

  if ( x < 0)
    return FALSE;

  col = column_from_xpixel (sheet, x);

  pixel = x - DRAG_WIDTH / 2;
  if (pixel < 0)
    pixel = 0;

  if ( column_from_xpixel (sheet, pixel) < col )
    {
      *column = col - 1;
      return TRUE;
    }

  if  ( column_from_xpixel (sheet, x + DRAG_WIDTH / 2) > col )
    {
      *column = col;
      return TRUE;
    }

  return FALSE;
}

static gboolean
on_row_boundary (const PsppireSheet *sheet, gint y, gint *row)
{
  gint r;
  gint pixel;

  y += sheet->vadjustment->value;

  if ( y < 0)
    return FALSE;

  r = row_from_ypixel (sheet, y);

  pixel = y - DRAG_WIDTH / 2;
  if (pixel < 0)
    pixel = 0;

  if ( row_from_ypixel (sheet, pixel) < r )
    {
      *row = r - 1;
      return TRUE;
    }

  if  ( row_from_ypixel (sheet, y + DRAG_WIDTH / 2) > r )
    {
      *row = r;
      return TRUE;
    }

  return FALSE;
}


static inline gboolean
POSSIBLE_DRAG (const PsppireSheet *sheet, gint x, gint y,
	       gint *drag_row, gint *drag_column)
{
  gint ydrag, xdrag;

  /* Can't drag if nothing is selected */
  if ( sheet->range.row0 < 0 || sheet->range.rowi < 0 ||
       sheet->range.col0 < 0 || sheet->range.coli < 0 )
    return FALSE;

  *drag_column = column_from_xpixel (sheet, x);
  *drag_row = row_from_ypixel (sheet, y);

  if (x >= psppire_axis_start_pixel (sheet->haxis, sheet->range.col0) - DRAG_WIDTH / 2 &&
      x <= psppire_axis_start_pixel (sheet->haxis, sheet->range.coli) +
      psppire_axis_unit_size (sheet->haxis, sheet->range.coli) + DRAG_WIDTH / 2)
    {
      ydrag = psppire_axis_start_pixel (sheet->vaxis, sheet->range.row0);
      if (y >= ydrag - DRAG_WIDTH / 2 && y <= ydrag + DRAG_WIDTH / 2)
	{
	  *drag_row = sheet->range.row0;
	  return TRUE;
	}
      ydrag = psppire_axis_start_pixel (sheet->vaxis, sheet->range.rowi) +
	psppire_axis_unit_size (sheet->vaxis, sheet->range.rowi);
      if (y >= ydrag - DRAG_WIDTH / 2 && y <= ydrag + DRAG_WIDTH / 2)
	{
	  *drag_row = sheet->range.rowi;
	  return TRUE;
	}
    }

  if (y >= psppire_axis_start_pixel (sheet->vaxis, sheet->range.row0) - DRAG_WIDTH / 2 &&
      y <= psppire_axis_start_pixel (sheet->vaxis, sheet->range.rowi) +
      psppire_axis_unit_size (sheet->vaxis, sheet->range.rowi) + DRAG_WIDTH / 2)
    {
      xdrag = psppire_axis_start_pixel (sheet->haxis, sheet->range.col0);
      if (x >= xdrag - DRAG_WIDTH / 2 && x <= xdrag + DRAG_WIDTH / 2)
	{
	  *drag_column = sheet->range.col0;
	  return TRUE;
	}
      xdrag = psppire_axis_start_pixel (sheet->haxis, sheet->range.coli) +
	psppire_axis_unit_size (sheet->haxis, sheet->range.coli);
      if (x >= xdrag - DRAG_WIDTH / 2 && x <= xdrag + DRAG_WIDTH / 2)
	{
	  *drag_column = sheet->range.coli;
	  return TRUE;
	}
    }

  return FALSE;
}

static inline gboolean
POSSIBLE_RESIZE (const PsppireSheet *sheet, gint x, gint y,
		 gint *drag_row, gint *drag_column)
{
  gint xdrag, ydrag;

  /* Can't drag if nothing is selected */
  if ( sheet->range.row0 < 0 || sheet->range.rowi < 0 ||
       sheet->range.col0 < 0 || sheet->range.coli < 0 )
    return FALSE;

  xdrag = psppire_axis_start_pixel (sheet->haxis, sheet->range.coli)+
    psppire_axis_unit_size (sheet->haxis, sheet->range.coli);

  ydrag = psppire_axis_start_pixel (sheet->vaxis, sheet->range.rowi) +
    psppire_axis_unit_size (sheet->vaxis, sheet->range.rowi);

  if (sheet->state == PSPPIRE_SHEET_COLUMN_SELECTED)
    ydrag = psppire_axis_start_pixel (sheet->vaxis, min_visible_row (sheet));

  if (sheet->state == PSPPIRE_SHEET_ROW_SELECTED)
    xdrag = psppire_axis_start_pixel (sheet->haxis, min_visible_column (sheet));

  *drag_column = column_from_xpixel (sheet, x);
  *drag_row = row_from_ypixel (sheet, y);

  if (x >= xdrag - DRAG_WIDTH / 2 && x <= xdrag + DRAG_WIDTH / 2 &&
      y >= ydrag - DRAG_WIDTH / 2 && y <= ydrag + DRAG_WIDTH / 2) return TRUE;

  return FALSE;
}


static gboolean
rectangle_from_range (PsppireSheet *sheet, const PsppireSheetRange *range,
		      GdkRectangle *r)
{
  g_return_val_if_fail (range, FALSE);

  r->x = psppire_axis_start_pixel (sheet->haxis, range->col0);
  r->x -= round (sheet->hadjustment->value);

  r->y = psppire_axis_start_pixel (sheet->vaxis, range->row0);
  r->y -= round (sheet->vadjustment->value);

  r->width = psppire_axis_start_pixel (sheet->haxis, range->coli) -
    psppire_axis_start_pixel (sheet->haxis, range->col0) +
    psppire_axis_unit_size (sheet->haxis, range->coli);

  r->height = psppire_axis_start_pixel (sheet->vaxis, range->rowi) -
    psppire_axis_start_pixel (sheet->vaxis, range->row0) +
    psppire_axis_unit_size (sheet->vaxis, range->rowi);

  if ( sheet->column_titles_visible)
    {
      r->y += sheet->column_title_area.height;
    }

  if ( sheet->row_titles_visible)
    {
      r->x += sheet->row_title_area.width;
    }

  return TRUE;
}

static gboolean
rectangle_from_cell (PsppireSheet *sheet, gint row, gint col,
		     GdkRectangle *r)
{
  PsppireSheetRange range;
  g_return_val_if_fail (row >= 0, FALSE);
  g_return_val_if_fail (col >= 0, FALSE);

  range.row0 = range.rowi = row;
  range.col0 = range.coli = col;

  return rectangle_from_range (sheet, &range, r);
}


static void psppire_sheet_class_init 		 (PsppireSheetClass *klass);
static void psppire_sheet_init 			 (PsppireSheet *sheet);
static void psppire_sheet_dispose 			 (GObject *object);
static void psppire_sheet_finalize 			 (GObject *object);
static void psppire_sheet_style_set 		 (GtkWidget *widget,
						  GtkStyle *previous_style);
static void psppire_sheet_realize 			 (GtkWidget *widget);
static void psppire_sheet_unrealize 		 (GtkWidget *widget);
static void psppire_sheet_map 			 (GtkWidget *widget);
static void psppire_sheet_unmap 			 (GtkWidget *widget);
static gint psppire_sheet_expose 			 (GtkWidget *widget,
						  GdkEventExpose *event);

static void psppire_sheet_forall 			 (GtkContainer *container,
						  gboolean include_internals,
						  GtkCallback callback,
						  gpointer callback_data);

static gboolean psppire_sheet_set_scroll_adjustments  (PsppireSheet *sheet,
						  GtkAdjustment *hadjustment,
						  GtkAdjustment *vadjustment);

static gint psppire_sheet_button_press 		 (GtkWidget *widget,
						  GdkEventButton *event);
static gint psppire_sheet_button_release 		 (GtkWidget *widget,
						  GdkEventButton *event);
static gint psppire_sheet_motion 			 (GtkWidget *widget,
						  GdkEventMotion *event);
static gboolean psppire_sheet_crossing_notify           (GtkWidget *widget,
						     GdkEventCrossing *event);
static gint psppire_sheet_entry_key_press		 (GtkWidget *widget,
						  GdkEventKey *key);
static gboolean psppire_sheet_key_press		 (GtkWidget *widget,
						  GdkEventKey *key);
static void psppire_sheet_size_request 		 (GtkWidget *widget,
						  GtkRequisition *requisition);
static void psppire_sheet_size_allocate 		 (GtkWidget *widget,
						  GtkAllocation *allocation);

static gboolean psppire_sheet_focus_in               (GtkWidget     *widget,
						      GdkEventFocus *event);

/* Sheet queries */

static gboolean psppire_sheet_range_isvisible (const PsppireSheet *sheet,
					   const PsppireSheetRange *range);
static gboolean psppire_sheet_cell_isvisible  (PsppireSheet *sheet,
					   gint row, gint column);
/* Drawing Routines */

/* draw cell */
static void psppire_sheet_cell_draw (PsppireSheet *sheet, gint row, gint column);


/* draw visible part of range. */
static void draw_sheet_region (PsppireSheet *sheet, GdkRegion *region);


/* highlight the visible part of the selected range */
static void psppire_sheet_range_draw_selection	 (PsppireSheet *sheet,
						  PsppireSheetRange range);

/* Selection */

static void psppire_sheet_real_select_range 	 (PsppireSheet *sheet,
						  const PsppireSheetRange *range);
static void psppire_sheet_real_unselect_range 	 (PsppireSheet *sheet,
						  const PsppireSheetRange *range);
static void psppire_sheet_extend_selection		 (PsppireSheet *sheet,
						  gint row, gint column);
static void psppire_sheet_new_selection		 (PsppireSheet *sheet,
						  PsppireSheetRange *range);
static void psppire_sheet_draw_border 		 (PsppireSheet *sheet,
						  PsppireSheetRange range);

/* Active Cell handling */

static void psppire_sheet_hide_entry_widget		 (PsppireSheet *sheet);
static void change_active_cell  (PsppireSheet *sheet, gint row, gint col);
static gboolean psppire_sheet_draw_active_cell	 (PsppireSheet *sheet);
static void psppire_sheet_show_entry_widget		 (PsppireSheet *sheet);
static gboolean psppire_sheet_click_cell		 (PsppireSheet *sheet,
						  gint row,
						  gint column);


/* Scrollbars */

static void adjust_scrollbars 			 (PsppireSheet *sheet);
static void vadjustment_value_changed 		 (GtkAdjustment *adjustment,
						  gpointer data);
static void hadjustment_value_changed 		 (GtkAdjustment *adjustment,
						  gpointer data);


static void draw_xor_vline 			 (PsppireSheet *sheet);
static void draw_xor_hline 			 (PsppireSheet *sheet);
static void draw_xor_rectangle			 (PsppireSheet *sheet,
						  PsppireSheetRange range);

/* Sheet Button */

static void create_global_button		 (PsppireSheet *sheet);
static void global_button_clicked		 (GtkWidget *widget,
						  gpointer data);
/* Sheet Entry */

static void create_sheet_entry			 (PsppireSheet *sheet);
static void psppire_sheet_size_allocate_entry	 (PsppireSheet *sheet);

/* Sheet button gadgets */

static void draw_column_title_buttons 	 (PsppireSheet *sheet);
static void draw_row_title_buttons 	 (PsppireSheet *sheet);


static void size_allocate_global_button 	 (PsppireSheet *sheet);
static void psppire_sheet_button_size_request	 (PsppireSheet *sheet,
						  const PsppireSheetButton *button,
						  GtkRequisition *requisition);

static void psppire_sheet_real_cell_clear 		 (PsppireSheet *sheet,
						  gint row,
						  gint column);


/* Signals */
enum
  {
    SELECT_ROW,
    SELECT_COLUMN,
    DOUBLE_CLICK_ROW,
    DOUBLE_CLICK_COLUMN,
    BUTTON_EVENT_ROW,
    BUTTON_EVENT_COLUMN,
    SELECT_RANGE,
    RESIZE_RANGE,
    MOVE_RANGE,
    TRAVERSE,
    ACTIVATE,
    LAST_SIGNAL
  };

static GtkContainerClass *parent_class = NULL;
static guint sheet_signals[LAST_SIGNAL] = { 0 };


GType
psppire_sheet_get_type ()
{
  static GType sheet_type = 0;

  if (!sheet_type)
    {
      static const GTypeInfo sheet_info =
	{
	  sizeof (PsppireSheetClass),
	  NULL,
	  NULL,
	  (GClassInitFunc) psppire_sheet_class_init,
	  NULL,
	  NULL,
	  sizeof (PsppireSheet),
	  0,
	  (GInstanceInitFunc) psppire_sheet_init,
	  NULL,
	};

      sheet_type =
	g_type_register_static (GTK_TYPE_BIN, "PsppireSheet",
				&sheet_info, 0);
    }
  return sheet_type;
}



static PsppireSheetRange*
psppire_sheet_range_copy (const PsppireSheetRange *range)
{
  PsppireSheetRange *new_range;

  g_return_val_if_fail (range != NULL, NULL);

  new_range = g_new (PsppireSheetRange, 1);

  *new_range = *range;

  return new_range;
}

static void
psppire_sheet_range_free (PsppireSheetRange *range)
{
  g_return_if_fail (range != NULL);

  g_free (range);
}

GType
psppire_sheet_range_get_type (void)
{
  static GType sheet_range_type = 0;

  if (!sheet_range_type)
    {
      sheet_range_type =
	g_boxed_type_register_static ("PsppireSheetRange",
				      (GBoxedCopyFunc) psppire_sheet_range_copy,
				      (GBoxedFreeFunc) psppire_sheet_range_free);
    }

  return sheet_range_type;
}

static PsppireSheetCell*
psppire_sheet_cell_copy (const PsppireSheetCell *cell)
{
  PsppireSheetCell *new_cell;

  g_return_val_if_fail (cell != NULL, NULL);

  new_cell = g_new (PsppireSheetCell, 1);

  *new_cell = *cell;

  return new_cell;
}

static void
psppire_sheet_cell_free (PsppireSheetCell *cell)
{
  g_return_if_fail (cell != NULL);

  g_free (cell);
}

GType
psppire_sheet_cell_get_type (void)
{
  static GType sheet_cell_type = 0;

  if (!sheet_cell_type)
    {
      sheet_cell_type =
	g_boxed_type_register_static ("PsppireSheetCell",
				      (GBoxedCopyFunc) psppire_sheet_cell_copy,
				      (GBoxedFreeFunc) psppire_sheet_cell_free);
    }

  return sheet_cell_type;
}


/* Properties */
enum
  {
    PROP_0,
    PROP_VAXIS,
    PROP_HAXIS,
    PROP_CELL_PADDING,
    PROP_MODEL
  };

static void
resize_column (PsppireSheet *sheet, gint unit, glong size)
{
  PsppireSheetRange range;
  range.col0 = unit;
  range.coli = max_visible_column (sheet);
  range.row0 = min_visible_row (sheet);
  range.rowi = max_visible_row (sheet);

  redraw_range (sheet, &range);

  draw_column_title_buttons_range (sheet, range.col0, range.coli);
}


static void
psppire_sheet_set_horizontal_axis (PsppireSheet *sheet, PsppireAxis *a)
{
  if ( sheet->haxis )
    g_object_unref (sheet->haxis);

  sheet->haxis = a;
  g_signal_connect_swapped (a, "resize-unit", G_CALLBACK (resize_column), sheet);

  if ( sheet->haxis )
    g_object_ref (sheet->haxis);
}

static void
resize_row (PsppireSheet *sheet, gint unit, glong size)
{
  PsppireSheetRange range;
  range.col0 = min_visible_column (sheet);
  range.coli = max_visible_column (sheet);
  range.row0 = unit;
  range.rowi = max_visible_row (sheet);

  redraw_range (sheet, &range);

  draw_row_title_buttons_range (sheet, range.row0, range.rowi);
}

static void
psppire_sheet_set_vertical_axis (PsppireSheet *sheet, PsppireAxis *a)
{
  if ( sheet->vaxis )
    g_object_unref (sheet->vaxis);

  sheet->vaxis = a;

  g_signal_connect_swapped (a, "resize-unit", G_CALLBACK (resize_row), sheet);

  if ( sheet->vaxis )
    g_object_ref (sheet->vaxis);
}

static const GtkBorder default_cell_padding = {3, 3, 3, 3};

static void
psppire_sheet_set_property (GObject         *object,
			guint            prop_id,
			const GValue    *value,
			GParamSpec      *pspec)

{
  PsppireSheet *sheet = PSPPIRE_SHEET (object);

  switch (prop_id)
    {
    case PROP_CELL_PADDING:
      if ( sheet->cell_padding)
	g_boxed_free (GTK_TYPE_BORDER, sheet->cell_padding);

      sheet->cell_padding = g_value_dup_boxed (value);

     if (NULL == sheet->cell_padding)
       sheet->cell_padding = g_boxed_copy (GTK_TYPE_BORDER,
					   &default_cell_padding);

     if (sheet->vaxis)
       g_object_set (sheet->vaxis, "padding",
		     sheet->cell_padding->top + sheet->cell_padding->bottom,
		     NULL);

     if (sheet->haxis)
       g_object_set (sheet->haxis, "padding",
		     sheet->cell_padding->left + sheet->cell_padding->right,
		     NULL);
      break;
    case PROP_VAXIS:
      psppire_sheet_set_vertical_axis (sheet, g_value_get_pointer (value));
      g_object_set (sheet->vaxis, "padding",
		    sheet->cell_padding->top + sheet->cell_padding->bottom,
		    NULL);
      break;
    case PROP_HAXIS:
      psppire_sheet_set_horizontal_axis (sheet, g_value_get_pointer (value));
      g_object_set (sheet->haxis, "padding",
		    sheet->cell_padding->left + sheet->cell_padding->right,
		    NULL);
      break;
    case PROP_MODEL:
      psppire_sheet_set_model (sheet, g_value_get_pointer (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void
psppire_sheet_get_property (GObject         *object,
			guint            prop_id,
			GValue          *value,
			GParamSpec      *pspec)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (object);

  switch (prop_id)
    {
    case PROP_CELL_PADDING:
      g_value_set_boxed (value, sheet->cell_padding);
      break;
    case PROP_VAXIS:
      g_value_set_pointer (value, sheet->vaxis);
      break;
    case PROP_HAXIS:
      g_value_set_pointer (value, sheet->haxis);
      break;
    case PROP_MODEL:
      g_value_set_pointer (value, sheet->model);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_sheet_class_init (PsppireSheetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  GParamSpec *haxis_spec ;
  GParamSpec *vaxis_spec ;
  GParamSpec *model_spec ;
  GParamSpec *cell_padding_spec ;

  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  /**
   * PsppireSheet::select-row
   * @sheet: the sheet widget that emitted the signal
   * @row: the newly selected row index
   *
   * A row has been selected.
   */
  sheet_signals[SELECT_ROW] =
    g_signal_new ("select-row",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (PsppireSheetClass, select_row),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  /**
   * PsppireSheet::select - column
   * @sheet: the sheet widget that emitted the signal
   * @column: the newly selected column index
   *
   * A column has been selected.
   */
  sheet_signals[SELECT_COLUMN] =
    g_signal_new ("select-column",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (PsppireSheetClass, select_column),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  /**
   * PsppireSheet::double-click-row
   * @sheet: the sheet widget that emitted the signal
   * @row: the row that was double clicked.
   *
   * A row's title button has been double clicked
   */
  sheet_signals[DOUBLE_CLICK_ROW] =
    g_signal_new ("double-click-row",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  /**
   * PsppireSheet::double-click-column
   * @sheet: the sheet widget that emitted the signal
   * @column: the column that was double clicked.
   *
   * A column's title button has been double clicked
   */
  sheet_signals[DOUBLE_CLICK_COLUMN] =
    g_signal_new ("double-click-column",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  /**
   * PsppireSheet::button-event-column
   * @sheet: the sheet widget that emitted the signal
   * @column: the column on which the event occured.
   *
   * A button event occured on a column title button
   */
  sheet_signals[BUTTON_EVENT_COLUMN] =
    g_signal_new ("button-event-column",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  0,
		  NULL, NULL,
		  psppire_marshal_VOID__INT_POINTER,
		  G_TYPE_NONE,
		  2,
		  G_TYPE_INT,
		  G_TYPE_POINTER
		  );


  /**
   * PsppireSheet::button-event-row
   * @sheet: the sheet widget that emitted the signal
   * @column: the column on which the event occured.
   *
   * A button event occured on a row title button
   */
  sheet_signals[BUTTON_EVENT_ROW] =
    g_signal_new ("button-event-row",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  0,
		  NULL, NULL,
		  psppire_marshal_VOID__INT_POINTER,
		  G_TYPE_NONE,
		  2,
		  G_TYPE_INT,
		  G_TYPE_POINTER
		  );


  sheet_signals[SELECT_RANGE] =
    g_signal_new ("select-range",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (PsppireSheetClass, select_range),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__BOXED,
		  G_TYPE_NONE,
		  1,
		  PSPPIRE_TYPE_SHEET_RANGE);


  sheet_signals[RESIZE_RANGE] =
    g_signal_new ("resize-range",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (PsppireSheetClass, resize_range),
		  NULL, NULL,
		  psppire_marshal_VOID__BOXED_BOXED,
		  G_TYPE_NONE,
		  2,
		  PSPPIRE_TYPE_SHEET_RANGE, PSPPIRE_TYPE_SHEET_RANGE
		  );

  sheet_signals[MOVE_RANGE] =
    g_signal_new ("move-range",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (PsppireSheetClass, move_range),
		  NULL, NULL,
		  psppire_marshal_VOID__BOXED_BOXED,
		  G_TYPE_NONE,
		  2,
		  PSPPIRE_TYPE_SHEET_RANGE, PSPPIRE_TYPE_SHEET_RANGE
		  );

  sheet_signals[TRAVERSE] =
    g_signal_new ("traverse",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (PsppireSheetClass, traverse),
		  NULL, NULL,
		  psppire_marshal_BOOLEAN__BOXED_POINTER,
		  G_TYPE_BOOLEAN, 2,
		  PSPPIRE_TYPE_SHEET_CELL,
		  G_TYPE_POINTER);


  sheet_signals[ACTIVATE] =
    g_signal_new ("activate",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (PsppireSheetClass, activate),
		  NULL, NULL,
		  psppire_marshal_VOID__INT_INT_INT_INT,
		  G_TYPE_NONE, 4,
		  G_TYPE_INT, G_TYPE_INT,
		  G_TYPE_INT, G_TYPE_INT);

  widget_class->set_scroll_adjustments_signal =
    g_signal_new ("set-scroll-adjustments",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (PsppireSheetClass, set_scroll_adjustments),
		  NULL, NULL,
		  psppire_marshal_VOID__OBJECT_OBJECT,
		  G_TYPE_NONE, 2, GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);


  container_class->add = NULL;
  container_class->remove = NULL;
  container_class->forall = psppire_sheet_forall;
  container_class->set_focus_child = NULL;

  object_class->dispose = psppire_sheet_dispose;
  object_class->finalize = psppire_sheet_finalize;

  cell_padding_spec =
    g_param_spec_boxed ("cell-padding",
			"Cell Padding",
			"The space between a cell's contents and its border",
			GTK_TYPE_BORDER,
			G_PARAM_CONSTRUCT | G_PARAM_READABLE | G_PARAM_WRITABLE);

  vaxis_spec =
    g_param_spec_pointer ("vertical-axis",
			  "Vertical Axis",
			  "A pointer to the PsppireAxis object for the rows",
			  G_PARAM_READABLE | G_PARAM_WRITABLE );

  haxis_spec =
    g_param_spec_pointer ("horizontal-axis",
			  "Horizontal Axis",
			  "A pointer to the PsppireAxis object for the columns",
			  G_PARAM_READABLE | G_PARAM_WRITABLE );

  model_spec =
    g_param_spec_pointer ("model",
			  "Model",
			  "A pointer to the data model",
			  G_PARAM_READABLE | G_PARAM_WRITABLE );


  object_class->set_property = psppire_sheet_set_property;
  object_class->get_property = psppire_sheet_get_property;

  g_object_class_install_property (object_class,
                                   PROP_VAXIS,
                                   vaxis_spec);

  g_object_class_install_property (object_class,
                                   PROP_HAXIS,
                                   haxis_spec);

  g_object_class_install_property (object_class,
                                   PROP_CELL_PADDING,
                                   cell_padding_spec);

  g_object_class_install_property (object_class,
                                   PROP_MODEL,
                                   model_spec);


  widget_class->realize = psppire_sheet_realize;
  widget_class->unrealize = psppire_sheet_unrealize;
  widget_class->map = psppire_sheet_map;
  widget_class->unmap = psppire_sheet_unmap;
  widget_class->style_set = psppire_sheet_style_set;
  widget_class->button_press_event = psppire_sheet_button_press;
  widget_class->button_release_event = psppire_sheet_button_release;
  widget_class->motion_notify_event = psppire_sheet_motion;
  widget_class->enter_notify_event = psppire_sheet_crossing_notify;
  widget_class->leave_notify_event = psppire_sheet_crossing_notify;
  widget_class->key_press_event = psppire_sheet_key_press;
  widget_class->expose_event = psppire_sheet_expose;
  widget_class->size_request = psppire_sheet_size_request;
  widget_class->size_allocate = psppire_sheet_size_allocate;
  widget_class->focus_in_event = psppire_sheet_focus_in;
  widget_class->focus_out_event = NULL;

  klass->set_scroll_adjustments = psppire_sheet_set_scroll_adjustments;
  klass->select_row = NULL;
  klass->select_column = NULL;
  klass->select_range = NULL;
  klass->resize_range = NULL;
  klass->move_range = NULL;
  klass->traverse = NULL;
  klass->activate = NULL;
  klass->changed = NULL;
}

static void
psppire_sheet_init (PsppireSheet *sheet)
{
  sheet->model = NULL;
  sheet->haxis = NULL;
  sheet->vaxis = NULL;

  sheet->flags = 0;
  sheet->selection_mode = GTK_SELECTION_NONE;
  sheet->state = PSPPIRE_SHEET_NORMAL;

  GTK_WIDGET_UNSET_FLAGS (sheet, GTK_NO_WINDOW);
  GTK_WIDGET_SET_FLAGS (sheet, GTK_CAN_FOCUS);

  sheet->column_title_window = NULL;
  sheet->column_title_area.x = 0;
  sheet->column_title_area.y = 0;
  sheet->column_title_area.width = 0;
  sheet->column_title_area.height = DEFAULT_ROW_HEIGHT;

  sheet->row_title_window = NULL;
  sheet->row_title_area.x = 0;
  sheet->row_title_area.y = 0;
  sheet->row_title_area.width = DEFAULT_COLUMN_WIDTH;
  sheet->row_title_area.height = 0;


  sheet->active_cell.row = 0;
  sheet->active_cell.col = 0;
  sheet->selection_cell.row = 0;
  sheet->selection_cell.col = 0;

  sheet->range.row0 = 0;
  sheet->range.rowi = 0;
  sheet->range.col0 = 0;
  sheet->range.coli = 0;

  sheet->state = PSPPIRE_SHEET_NORMAL;

  sheet->sheet_window = NULL;
  sheet->entry_widget = NULL;
  sheet->button = NULL;

  sheet->hadjustment = NULL;
  sheet->vadjustment = NULL;

  sheet->cursor_drag = NULL;

  sheet->xor_gc = NULL;
  sheet->fg_gc = NULL;
  sheet->bg_gc = NULL;
  sheet->x_drag = 0;
  sheet->y_drag = 0;
  sheet->show_grid = TRUE;

  sheet->motion_timer = 0;

  sheet->row_titles_visible = TRUE;
  sheet->row_title_area.width = DEFAULT_COLUMN_WIDTH;

  sheet->column_titles_visible = TRUE;


  /* create sheet entry */
  sheet->entry_type = GTK_TYPE_ENTRY;
  create_sheet_entry (sheet);

  /* create global selection button */
  create_global_button (sheet);
}


/* Cause RANGE to be redrawn. If RANGE is null, then the
   entire visible range will be redrawn.
 */
static void
redraw_range (PsppireSheet *sheet, PsppireSheetRange *range)
{
  GdkRectangle rect;

  if ( ! GTK_WIDGET_REALIZED (sheet))
    return;

  if ( NULL != range )
    rectangle_from_range (sheet, range, &rect);
  else
    {
      GdkRegion *r = gdk_drawable_get_visible_region (sheet->sheet_window);
      gdk_region_get_clipbox (r, &rect);

      if ( sheet->column_titles_visible)
	{
	  rect.y += sheet->column_title_area.height;
	  rect.height -= sheet->column_title_area.height;
	}

      if ( sheet->row_titles_visible)
	{
	  rect.x += sheet->row_title_area.width;
	  rect.width -= sheet->row_title_area.width;
	}
    }

  gdk_window_invalidate_rect (sheet->sheet_window, &rect, FALSE);
}


/* Callback which occurs whenever columns are inserted / deleted in the model */
static void
columns_inserted_deleted_callback (PsppireSheetModel *model, gint first_column,
				   gint n_columns,
				   gpointer data)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (data);

  PsppireSheetRange range;
  gint model_columns = psppire_sheet_model_get_column_count (model);


  /* Need to update all the columns starting from the first column and onwards.
   * Previous column are unchanged, so don't need to be updated.
   */
  range.col0 = first_column;
  range.row0 = 0;
  range.coli = psppire_axis_unit_count (sheet->haxis) - 1;
  range.rowi = psppire_axis_unit_count (sheet->vaxis) - 1;

  adjust_scrollbars (sheet);

  if (sheet->active_cell.col >= model_columns)
    change_active_cell (sheet, sheet->active_cell.row, model_columns - 1);

  draw_column_title_buttons_range (sheet,
				   first_column, max_visible_column (sheet));


  redraw_range (sheet, &range);
}




/* Callback which occurs whenever rows are inserted / deleted in the model */
static void
rows_inserted_deleted_callback (PsppireSheetModel *model, gint first_row,
				gint n_rows, gpointer data)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (data);

  PsppireSheetRange range;

  gint model_rows = psppire_sheet_model_get_row_count (model);

  /* Need to update all the rows starting from the first row and onwards.
   * Previous rows are unchanged, so don't need to be updated.
   */
  range.row0 = first_row;
  range.col0 = 0;
  range.rowi = psppire_axis_unit_count (sheet->vaxis) - 1;
  range.coli = psppire_axis_unit_count (sheet->haxis) - 1;

  adjust_scrollbars (sheet);

  if (sheet->active_cell.row >= model_rows)
    change_active_cell (sheet, model_rows - 1, sheet->active_cell.col);

  draw_row_title_buttons_range (sheet, first_row, max_visible_row (sheet));

  redraw_range (sheet, &range);
}

/*
  If row0 or rowi are negative, then all rows will be updated.
  If col0 or coli are negative, then all columns will be updated.
*/
static void
range_update_callback (PsppireSheetModel *m, gint row0, gint col0,
		       gint rowi, gint coli, gpointer data)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (data);

  PsppireSheetRange range;

  range.row0 = row0;
  range.col0 = col0;
  range.rowi = rowi;
  range.coli = coli;

  if ( !GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    return;

  if ( ( row0 < 0 && col0 < 0 ) || ( rowi < 0 && coli < 0 ) )
    {
      redraw_range (sheet, NULL);
      adjust_scrollbars (sheet);

      draw_row_title_buttons_range (sheet, min_visible_row (sheet),
				       max_visible_row (sheet));

      draw_column_title_buttons_range (sheet, min_visible_column (sheet),
				       max_visible_column (sheet));

      return;
    }
  else if ( row0 < 0 || rowi < 0 )
    {
      range.row0 = min_visible_row (sheet);
      range.rowi = max_visible_row (sheet);
    }
  else if ( col0 < 0 || coli < 0 )
    {
      range.col0 = min_visible_column (sheet);
      range.coli = max_visible_column (sheet);
    }

  redraw_range (sheet, &range);
}


/**
 * psppire_sheet_new:
 * @rows: initial number of rows
 * @columns: initial number of columns
 * @title: sheet title
 * @model: the model to use for the sheet data
 *
 * Creates a new sheet widget with the given number of rows and columns.
 *
 * Returns: the new sheet widget
 */
GtkWidget *
psppire_sheet_new (PsppireSheetModel *model)
{
  GtkWidget *widget = g_object_new (PSPPIRE_TYPE_SHEET,
				    "model", model,
				    NULL);
  return widget;
}


/**
 * psppire_sheet_set_model
 * @sheet: the sheet to set the model for
 * @model: the model to use for the sheet data
 *
 * Sets the model for a PsppireSheet
 *
 */
void
psppire_sheet_set_model (PsppireSheet *sheet, PsppireSheetModel *model)
{
  g_return_if_fail (PSPPIRE_IS_SHEET (sheet));

  if (sheet->model ) g_object_unref (sheet->model);

  sheet->model = model;

  if ( model)
    {
      g_object_ref (model);

      sheet->update_handler_id = g_signal_connect (model, "range_changed",
						   G_CALLBACK (range_update_callback),
						   sheet);

      g_signal_connect (model, "rows_inserted",
			G_CALLBACK (rows_inserted_deleted_callback), sheet);

      g_signal_connect (model, "rows_deleted",
			G_CALLBACK (rows_inserted_deleted_callback), sheet);

      g_signal_connect (model, "columns_inserted",
			G_CALLBACK (columns_inserted_deleted_callback), sheet);

      g_signal_connect (model, "columns_deleted",
			G_CALLBACK (columns_inserted_deleted_callback), sheet);
    }
}


void
psppire_sheet_change_entry (PsppireSheet *sheet, GtkType entry_type)
{
  gint state;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (sheet));

  state = sheet->state;

  if (sheet->state == PSPPIRE_SHEET_NORMAL)
    psppire_sheet_hide_entry_widget (sheet);

  sheet->entry_type = entry_type;

  create_sheet_entry (sheet);

  if (state == PSPPIRE_SHEET_NORMAL)
    {
      psppire_sheet_show_entry_widget (sheet);
    }

}

void
psppire_sheet_show_grid (PsppireSheet *sheet, gboolean show)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (sheet));

  if (show == sheet->show_grid) return;

  sheet->show_grid = show;

  redraw_range (sheet, NULL);
}

gboolean
psppire_sheet_grid_visible (PsppireSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (PSPPIRE_IS_SHEET (sheet), 0);

  return sheet->show_grid;
}

guint
psppire_sheet_get_columns_count (PsppireSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (PSPPIRE_IS_SHEET (sheet), 0);

  return psppire_axis_unit_count (sheet->haxis);
}

static void set_column_width (PsppireSheet *sheet,
			      gint column,
			      gint width);


void
psppire_sheet_show_column_titles (PsppireSheet *sheet)
{
  if (sheet->column_titles_visible) return;

  sheet->column_titles_visible = TRUE;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    return;

  gdk_window_show (sheet->column_title_window);
  gdk_window_move_resize (sheet->column_title_window,
			  sheet->column_title_area.x,
			  sheet->column_title_area.y,
			  sheet->column_title_area.width,
			  sheet->column_title_area.height);

  adjust_scrollbars (sheet);

  if (sheet->vadjustment)
    g_signal_emit_by_name (sheet->vadjustment,
			   "value_changed");

  size_allocate_global_button (sheet);

  if ( sheet->row_titles_visible)
    gtk_widget_show (sheet->button);
}


void
psppire_sheet_show_row_titles (PsppireSheet *sheet)
{
  if (sheet->row_titles_visible) return;

  sheet->row_titles_visible = TRUE;


  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      gdk_window_show (sheet->row_title_window);
      gdk_window_move_resize (sheet->row_title_window,
			      sheet->row_title_area.x,
			      sheet->row_title_area.y,
			      sheet->row_title_area.width,
			      sheet->row_title_area.height);

      adjust_scrollbars (sheet);
    }

  if (sheet->hadjustment)
    g_signal_emit_by_name (sheet->hadjustment,
			   "value_changed");

  size_allocate_global_button (sheet);

  if ( sheet->column_titles_visible)
    gtk_widget_show (sheet->button);
}

void
psppire_sheet_hide_column_titles (PsppireSheet *sheet)
{
  if (!sheet->column_titles_visible) return;

  sheet->column_titles_visible = FALSE;

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      if (sheet->column_title_window)
	gdk_window_hide (sheet->column_title_window);

      gtk_widget_hide (sheet->button);

      adjust_scrollbars (sheet);
    }

  if (sheet->vadjustment)
    g_signal_emit_by_name (sheet->vadjustment,
			   "value_changed");
}

void
psppire_sheet_hide_row_titles (PsppireSheet *sheet)
{
  if (!sheet->row_titles_visible) return;

  sheet->row_titles_visible = FALSE;

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      if (sheet->row_title_window)
	gdk_window_hide (sheet->row_title_window);

      gtk_widget_hide (sheet->button);

      adjust_scrollbars (sheet);
    }

  if (sheet->hadjustment)
    g_signal_emit_by_name (sheet->hadjustment,
			   "value_changed");
}


/* Scroll the sheet so that the cell ROW, COLUMN is visible.
   If {ROW,COL}_ALIGN is zero, then the cell will be placed
   at the {top,left} of the sheet.  If it's 1, then it'll
   be placed at the {bottom,right}.
   ROW or COL may be -1, in which case scrolling in that dimension
   does not occur.
 */
void
psppire_sheet_moveto (PsppireSheet *sheet,
		  gint row,
		  gint col,
		  gfloat row_align,
		  gfloat col_align)
{
  gint width, height;

  g_return_if_fail (row_align >= 0);
  g_return_if_fail (col_align >= 0);

  g_return_if_fail (row_align <= 1);
  g_return_if_fail (col_align <= 1);

  g_return_if_fail (col <
		    psppire_axis_unit_count (sheet->haxis));
  g_return_if_fail (row <
		    psppire_axis_unit_count (sheet->vaxis));

  gdk_drawable_get_size (sheet->sheet_window, &width, &height);


  if (row >= 0)
    {
      gint y =  psppire_axis_start_pixel (sheet->vaxis, row);

      gtk_adjustment_set_value (sheet->vadjustment, y - height * row_align);
    }


  if (col >= 0)
    {
      gint x =  psppire_axis_start_pixel (sheet->haxis, col);

      gtk_adjustment_set_value (sheet->hadjustment, x - width * col_align);
    }
}


void
psppire_sheet_select_row (PsppireSheet *sheet, gint row)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (sheet));

  if (row < 0 || row >= psppire_axis_unit_count (sheet->vaxis))
    return;

  if (sheet->state != PSPPIRE_SHEET_NORMAL)
    psppire_sheet_real_unselect_range (sheet, NULL);

  sheet->state = PSPPIRE_SHEET_ROW_SELECTED;
  sheet->range.row0 = row;
  sheet->range.col0 = 0;
  sheet->range.rowi = row;
  sheet->range.coli = psppire_axis_unit_count (sheet->haxis) - 1;

  g_signal_emit (sheet, sheet_signals[SELECT_ROW], 0, row);
  psppire_sheet_real_select_range (sheet, NULL);
}


void
psppire_sheet_select_column (PsppireSheet *sheet, gint column)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (sheet));

  if (column < 0 || column >= psppire_axis_unit_count (sheet->haxis))
    return;

  if (sheet->state != PSPPIRE_SHEET_NORMAL)
    psppire_sheet_real_unselect_range (sheet, NULL);

  sheet->state = PSPPIRE_SHEET_COLUMN_SELECTED;
  sheet->range.row0 = 0;
  sheet->range.col0 = column;
  sheet->range.rowi = psppire_axis_unit_count (sheet->vaxis) - 1;
  sheet->range.coli = column;

  g_signal_emit (sheet, sheet_signals[SELECT_COLUMN], 0, column);
  psppire_sheet_real_select_range (sheet, NULL);
}




static gboolean
psppire_sheet_range_isvisible (const PsppireSheet *sheet,
			   const PsppireSheetRange *range)
{
  g_return_val_if_fail (sheet != NULL, FALSE);

  if (range->row0 < 0 || range->row0 >= psppire_axis_unit_count (sheet->vaxis))
    return FALSE;

  if (range->rowi < 0 || range->rowi >= psppire_axis_unit_count (sheet->vaxis))
    return FALSE;

  if (range->col0 < 0 || range->col0 >= psppire_axis_unit_count (sheet->haxis))
    return FALSE;

  if (range->coli < 0 || range->coli >= psppire_axis_unit_count (sheet->haxis))
    return FALSE;

  if (range->rowi < min_visible_row (sheet))
    return FALSE;

  if (range->row0 > max_visible_row (sheet))
    return FALSE;

  if (range->coli < min_visible_column (sheet))
    return FALSE;

  if (range->col0 > max_visible_column (sheet))
    return FALSE;

  return TRUE;
}

static gboolean
psppire_sheet_cell_isvisible (PsppireSheet *sheet,
			  gint row, gint column)
{
  PsppireSheetRange range;

  range.row0 = row;
  range.col0 = column;
  range.rowi = row;
  range.coli = column;

  return psppire_sheet_range_isvisible (sheet, &range);
}

void
psppire_sheet_get_visible_range (PsppireSheet *sheet, PsppireSheetRange *range)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (sheet)) ;
  g_return_if_fail (range != NULL);

  range->row0 = min_visible_row (sheet);
  range->col0 = min_visible_column (sheet);
  range->rowi = max_visible_row (sheet);
  range->coli = max_visible_column (sheet);
}


static gboolean
psppire_sheet_set_scroll_adjustments (PsppireSheet *sheet,
				  GtkAdjustment *hadjustment,
				  GtkAdjustment *vadjustment)
{
  if ( sheet->vadjustment != vadjustment )
    {
      if (sheet->vadjustment)
	g_object_unref (sheet->vadjustment);
      sheet->vadjustment = vadjustment;

      if ( vadjustment)
	{
	  g_object_ref (vadjustment);

	  g_signal_connect (sheet->vadjustment, "value_changed",
			    G_CALLBACK (vadjustment_value_changed),
			    sheet);
	}
    }

  if ( sheet->hadjustment != hadjustment )
    {
      if (sheet->hadjustment)
	g_object_unref (sheet->hadjustment);

      sheet->hadjustment = hadjustment;

      if ( hadjustment)
	{
	  g_object_ref (hadjustment);

	  g_signal_connect (sheet->hadjustment, "value_changed",
			    G_CALLBACK (hadjustment_value_changed),
			    sheet);
	}
    }
  return TRUE;
}

static void
psppire_sheet_finalize (GObject *object)
{
  PsppireSheet *sheet;

  g_return_if_fail (object != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (object));

  sheet = PSPPIRE_SHEET (object);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    (*G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
psppire_sheet_dispose  (GObject *object)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (object);

  g_return_if_fail (object != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (object));

  if ( sheet->dispose_has_run )
    return ;

  sheet->dispose_has_run = TRUE;

  if ( sheet->cell_padding)
    g_boxed_free (GTK_TYPE_BORDER, sheet->cell_padding);

  if (sheet->model) g_object_unref (sheet->model);
  if (sheet->vaxis) g_object_unref (sheet->vaxis);
  if (sheet->haxis) g_object_unref (sheet->haxis);

  g_object_unref (sheet->button);
  sheet->button = NULL;

  /* unref adjustments */
  if (sheet->hadjustment)
    {
      g_signal_handlers_disconnect_matched (sheet->hadjustment,
					    G_SIGNAL_MATCH_DATA,
					    0, 0, 0, 0,
					    sheet);

      g_object_unref (sheet->hadjustment);
      sheet->hadjustment = NULL;
    }

  if (sheet->vadjustment)
    {
      g_signal_handlers_disconnect_matched (sheet->vadjustment,
					    G_SIGNAL_MATCH_DATA,
					    0, 0, 0, 0,
					    sheet);

      g_object_unref (sheet->vadjustment);

      sheet->vadjustment = NULL;
    }

  if (G_OBJECT_CLASS (parent_class)->dispose)
    (*G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
psppire_sheet_style_set (GtkWidget *widget,
		     GtkStyle *previous_style)
{
  PsppireSheet *sheet;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (widget));

  if (GTK_WIDGET_CLASS (parent_class)->style_set)
    (*GTK_WIDGET_CLASS (parent_class)->style_set) (widget, previous_style);

  sheet = PSPPIRE_SHEET (widget);

  if (GTK_WIDGET_REALIZED (widget))
    {
      gtk_style_set_background (widget->style, widget->window, widget->state);
    }

  set_entry_widget_font (sheet);
}


static void
psppire_sheet_realize (GtkWidget *widget)
{
  PsppireSheet *sheet;
  GdkWindowAttr attributes;
  const gint attributes_mask =
    GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP | GDK_WA_CURSOR;

  GdkGCValues values;
  GdkColormap *colormap;
  GdkDisplay *display;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (widget));

  sheet = PSPPIRE_SHEET (widget);

  colormap = gtk_widget_get_colormap (widget);
  display = gtk_widget_get_display (widget);

  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = widget->allocation.x;
  attributes.y = widget->allocation.y;
  attributes.width = widget->allocation.width;
  attributes.height = widget->allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;

  attributes.visual = gtk_widget_get_visual (widget);
  attributes.colormap = colormap;

  attributes.event_mask = gtk_widget_get_events (widget);
  attributes.event_mask |= (GDK_EXPOSURE_MASK |
			    GDK_BUTTON_PRESS_MASK |
			    GDK_BUTTON_RELEASE_MASK |
			    GDK_KEY_PRESS_MASK |
			    GDK_ENTER_NOTIFY_MASK |
			    GDK_LEAVE_NOTIFY_MASK |
			    GDK_POINTER_MOTION_MASK |
			    GDK_POINTER_MOTION_HINT_MASK);

  attributes.cursor = gdk_cursor_new_for_display (display, GDK_TOP_LEFT_ARROW);

  /* main window */
  widget->window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);

  gdk_window_set_user_data (widget->window, sheet);

  widget->style = gtk_style_attach (widget->style, widget->window);

  gtk_style_set_background (widget->style, widget->window, GTK_STATE_NORMAL);

  gdk_color_parse ("white", &sheet->color[BG_COLOR]);
  gdk_colormap_alloc_color (colormap, &sheet->color[BG_COLOR], FALSE,
			    TRUE);
  gdk_color_parse ("gray", &sheet->color[GRID_COLOR]);
  gdk_colormap_alloc_color (colormap, &sheet->color[GRID_COLOR], FALSE,
			    TRUE);

  attributes.x = 0;
  attributes.y = 0;
  attributes.width = sheet->column_title_area.width;
  attributes.height = sheet->column_title_area.height;


  /* column - title window */
  sheet->column_title_window =
    gdk_window_new (widget->window, &attributes, attributes_mask);
  gdk_window_set_user_data (sheet->column_title_window, sheet);
  gtk_style_set_background (widget->style, sheet->column_title_window,
			    GTK_STATE_NORMAL);


  attributes.x = 0;
  attributes.y = 0;
  attributes.width = sheet->row_title_area.width;
  attributes.height = sheet->row_title_area.height;

  /* row - title window */
  sheet->row_title_window = gdk_window_new (widget->window,
					    &attributes, attributes_mask);
  gdk_window_set_user_data (sheet->row_title_window, sheet);
  gtk_style_set_background (widget->style, sheet->row_title_window,
			    GTK_STATE_NORMAL);

  /* sheet - window */
  attributes.cursor = gdk_cursor_new_for_display (display, GDK_PLUS);

  attributes.x = 0;
  attributes.y = 0;

  sheet->sheet_window = gdk_window_new (widget->window,
					&attributes, attributes_mask);
  gdk_window_set_user_data (sheet->sheet_window, sheet);

  gdk_cursor_unref (attributes.cursor);

  gdk_window_set_background (sheet->sheet_window, &widget->style->white);
  gdk_window_show (sheet->sheet_window);

  /* GCs */
  sheet->fg_gc = gdk_gc_new (widget->window);
  sheet->bg_gc = gdk_gc_new (widget->window);

  values.foreground = widget->style->white;
  values.function = GDK_INVERT;
  values.subwindow_mode = GDK_INCLUDE_INFERIORS;
  values.line_width = MAX (sheet->cell_padding->left,
			   MAX (sheet->cell_padding->right,
				MAX (sheet->cell_padding->top,
				     sheet->cell_padding->bottom)));

  sheet->xor_gc = gdk_gc_new_with_values (widget->window,
					  &values,
					  GDK_GC_FOREGROUND |
					  GDK_GC_FUNCTION |
					  GDK_GC_SUBWINDOW |
					  GDK_GC_LINE_WIDTH
					  );

  gtk_widget_set_parent_window (sheet->entry_widget, sheet->sheet_window);
  gtk_widget_set_parent (sheet->entry_widget, GTK_WIDGET (sheet));

  gtk_widget_set_parent_window (sheet->button, sheet->sheet_window);
  gtk_widget_set_parent (sheet->button, GTK_WIDGET (sheet));

  sheet->button->style = gtk_style_attach (sheet->button->style,
                                          sheet->sheet_window);


  sheet->cursor_drag = gdk_cursor_new_for_display (display, GDK_PLUS);

  if (sheet->column_titles_visible)
    gdk_window_show (sheet->column_title_window);
  if (sheet->row_titles_visible)
    gdk_window_show (sheet->row_title_window);

  sheet->hover_window = create_hover_window ();

  draw_row_title_buttons (sheet);
  draw_column_title_buttons (sheet);

  psppire_sheet_update_primary_selection (sheet);


  GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);
}

static void
create_global_button (PsppireSheet *sheet)
{
  sheet->button = gtk_button_new_with_label (" ");

  GTK_WIDGET_UNSET_FLAGS(sheet->button, GTK_CAN_FOCUS);

  g_object_ref_sink (sheet->button);

  g_signal_connect (sheet->button,
		    "pressed",
		    G_CALLBACK (global_button_clicked),
		    sheet);
}

static void
size_allocate_global_button (PsppireSheet *sheet)
{
  GtkAllocation allocation;

  if (!sheet->column_titles_visible) return;
  if (!sheet->row_titles_visible) return;

  gtk_widget_size_request (sheet->button, NULL);

  allocation.x = 0;
  allocation.y = 0;
  allocation.width = sheet->row_title_area.width;
  allocation.height = sheet->column_title_area.height;

  gtk_widget_size_allocate (sheet->button, &allocation);
}

static void
global_button_clicked (GtkWidget *widget, gpointer data)
{
  psppire_sheet_click_cell (PSPPIRE_SHEET (data), -1, -1);
}


static void
psppire_sheet_unrealize (GtkWidget *widget)
{
  PsppireSheet *sheet;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (widget));

  sheet = PSPPIRE_SHEET (widget);

  gdk_cursor_unref (sheet->cursor_drag);
  sheet->cursor_drag = NULL;

  gdk_colormap_free_colors (gtk_widget_get_colormap (widget),
			    sheet->color, n_COLORS);

  g_object_unref (sheet->xor_gc);
  g_object_unref (sheet->fg_gc);
  g_object_unref (sheet->bg_gc);

  destroy_hover_window (sheet->hover_window);

  gdk_window_destroy (sheet->sheet_window);
  gdk_window_destroy (sheet->column_title_window);
  gdk_window_destroy (sheet->row_title_window);

  gtk_widget_unparent (sheet->entry_widget);
  if (sheet->button != NULL)
    gtk_widget_unparent (sheet->button);

  if (GTK_WIDGET_CLASS (parent_class)->unrealize)
    (* GTK_WIDGET_CLASS (parent_class)->unrealize) (widget);
}

static void
psppire_sheet_map (GtkWidget *widget)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (widget);

  g_return_if_fail (widget != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (widget));

  if (!GTK_WIDGET_MAPPED (widget))
    {
      GTK_WIDGET_SET_FLAGS (widget, GTK_MAPPED);

      gdk_window_show (widget->window);
      gdk_window_show (sheet->sheet_window);

      if (sheet->column_titles_visible)
	{
	  draw_column_title_buttons (sheet);
	  gdk_window_show (sheet->column_title_window);
	}
      if (sheet->row_titles_visible)
	{
	  draw_row_title_buttons (sheet);
	  gdk_window_show (sheet->row_title_window);
	}

      if (!GTK_WIDGET_MAPPED (sheet->entry_widget)
	  && sheet->active_cell.row >= 0
	  && sheet->active_cell.col >= 0 )
	{
	  gtk_widget_show (sheet->entry_widget);
	  gtk_widget_map (sheet->entry_widget);
	}

      if (!GTK_WIDGET_MAPPED (sheet->button))
	{
	  gtk_widget_show (sheet->button);
	  gtk_widget_map (sheet->button);
	}

      redraw_range (sheet, NULL);
      change_active_cell (sheet,
		     sheet->active_cell.row,
		     sheet->active_cell.col);
    }
}

static void
psppire_sheet_unmap (GtkWidget *widget)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (widget);

  if (!GTK_WIDGET_MAPPED (widget))
    return;

  GTK_WIDGET_UNSET_FLAGS (widget, GTK_MAPPED);

  gdk_window_hide (sheet->sheet_window);
  if (sheet->column_titles_visible)
    gdk_window_hide (sheet->column_title_window);
  if (sheet->row_titles_visible)
    gdk_window_hide (sheet->row_title_window);
  gdk_window_hide (widget->window);

  gtk_widget_unmap (sheet->entry_widget);
  gtk_widget_unmap (sheet->button);
  gtk_widget_unmap (sheet->hover_window->window);
}

/* get cell attributes of the given cell */
/* TRUE means that the cell is currently allocated */
static gboolean psppire_sheet_get_attributes (const PsppireSheet *sheet,
					      gint row, gint col,
					      PsppireSheetCellAttr *attributes);



static void
psppire_sheet_cell_draw (PsppireSheet *sheet, gint row, gint col)
{
  PangoLayout *layout;
  PangoRectangle text;
  PangoFontDescription *font_desc = GTK_WIDGET (sheet)->style->font_desc;
  gint font_height;

  gchar *label;

  PsppireSheetCellAttr attributes;
  GdkRectangle area;

  g_return_if_fail (sheet != NULL);

  /* bail now if we aren't yet drawable */
  if (!GTK_WIDGET_DRAWABLE (sheet)) return;

  if (row < 0 ||
      row >= psppire_axis_unit_count (sheet->vaxis))
    return;

  if (col < 0 ||
      col >= psppire_axis_unit_count (sheet->haxis))
    return;

  psppire_sheet_get_attributes (sheet, row, col, &attributes);

  /* select GC for background rectangle */
  gdk_gc_set_foreground (sheet->fg_gc, &attributes.foreground);
  gdk_gc_set_foreground (sheet->bg_gc, &attributes.background);

  rectangle_from_cell (sheet, row, col, &area);

  gdk_gc_set_line_attributes (sheet->fg_gc, 1, 0, 0, 0);

  if (sheet->show_grid)
    {
      gdk_gc_set_foreground (sheet->bg_gc, &sheet->color[GRID_COLOR]);

      gdk_draw_rectangle (sheet->sheet_window,
			  sheet->bg_gc,
			  FALSE,
			  area.x, area.y,
			  area.width, area.height);
    }


  label = psppire_sheet_cell_get_text (sheet, row, col);
  if (NULL == label)
    return;


  layout = gtk_widget_create_pango_layout (GTK_WIDGET (sheet), label);
  dispose_string (sheet, label);


  pango_layout_set_font_description (layout, font_desc);

  pango_layout_get_pixel_extents (layout, NULL, &text);

  gdk_gc_set_clip_rectangle (sheet->fg_gc, &area);

  font_height = pango_font_description_get_size (font_desc);
  if ( !pango_font_description_get_size_is_absolute (font_desc))
    font_height /= PANGO_SCALE;


  if ( sheet->cell_padding )
    {
      area.x += sheet->cell_padding->left;
      area.width -= sheet->cell_padding->right
	+ sheet->cell_padding->left;

      area.y += sheet->cell_padding->top;
      area.height -= sheet->cell_padding->bottom
	+
	sheet->cell_padding->top;
    }

  /* Centre the text vertically */
  area.y += (area.height - font_height) / 2.0;

  switch (attributes.justification)
    {
    case GTK_JUSTIFY_RIGHT:
      area.x += area.width - text.width;
      break;
    case GTK_JUSTIFY_CENTER:
      area.x += (area.width - text.width) / 2.0;
      break;
    case GTK_JUSTIFY_LEFT:
      /* Do nothing */
      break;
    default:
      g_critical ("Unhandled justification %d in column %d\n",
		 attributes.justification, col);
      break;
    }

  gdk_draw_layout (sheet->sheet_window, sheet->fg_gc,
		   area.x,
		   area.y,
		   layout);

  gdk_gc_set_clip_rectangle (sheet->fg_gc, NULL);
  g_object_unref (layout);
}


static void
draw_sheet_region (PsppireSheet *sheet, GdkRegion *region)
{
  PsppireSheetRange range;
  GdkRectangle area;
  gint y, x;
  gint i, j;

  PsppireSheetRange drawing_range;

  gdk_region_get_clipbox (region, &area);

  y = area.y + sheet->vadjustment->value;
  x = area.x + sheet->hadjustment->value;

  if ( sheet->column_titles_visible)
    y -= sheet->column_title_area.height;

  if ( sheet->row_titles_visible)
    x -= sheet->row_title_area.width;

  maximize_int (&x, 0);
  maximize_int (&y, 0);

  range.row0 = row_from_ypixel (sheet, y);
  range.rowi = row_from_ypixel (sheet, y + area.height);

  range.col0 = column_from_xpixel (sheet, x);
  range.coli = column_from_xpixel (sheet, x + area.width);

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (PSPPIRE_SHEET (sheet));

  if (!GTK_WIDGET_DRAWABLE (GTK_WIDGET (sheet))) return;
  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;
  if (!GTK_WIDGET_MAPPED (GTK_WIDGET (sheet))) return;


  drawing_range.row0 = MAX (range.row0, min_visible_row (sheet));
  drawing_range.col0 = MAX (range.col0, min_visible_column (sheet));
  drawing_range.rowi = MIN (range.rowi, max_visible_row (sheet));
  drawing_range.coli = MIN (range.coli, max_visible_column (sheet));

  g_return_if_fail (drawing_range.rowi >= drawing_range.row0);
  g_return_if_fail (drawing_range.coli >= drawing_range.col0);

  for (i = drawing_range.row0; i <= drawing_range.rowi; i++)
    {
      for (j = drawing_range.col0; j <= drawing_range.coli; j++)
	psppire_sheet_cell_draw (sheet, i, j);
    }

  if (sheet->state != PSPPIRE_SHEET_NORMAL &&
      psppire_sheet_range_isvisible (sheet, &sheet->range))
    psppire_sheet_range_draw_selection (sheet, drawing_range);


  if (sheet->state == GTK_STATE_NORMAL &&
      sheet->active_cell.row >= drawing_range.row0 &&
      sheet->active_cell.row <= drawing_range.rowi &&
      sheet->active_cell.col >= drawing_range.col0 &&
      sheet->active_cell.col <= drawing_range.coli)
    psppire_sheet_show_entry_widget (sheet);
}


static void
psppire_sheet_range_draw_selection (PsppireSheet *sheet, PsppireSheetRange range)
{
  GdkRectangle area;
  gint i, j;
  PsppireSheetRange aux;

  if (range.col0 > sheet->range.coli || range.coli < sheet->range.col0 ||
      range.row0 > sheet->range.rowi || range.rowi < sheet->range.row0)
    return;

  if (!psppire_sheet_range_isvisible (sheet, &range)) return;
  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;

  aux = range;

  range.col0 = MAX (sheet->range.col0, range.col0);
  range.coli = MIN (sheet->range.coli, range.coli);
  range.row0 = MAX (sheet->range.row0, range.row0);
  range.rowi = MIN (sheet->range.rowi, range.rowi);

  range.col0 = MAX (range.col0, min_visible_column (sheet));
  range.coli = MIN (range.coli, max_visible_column (sheet));
  range.row0 = MAX (range.row0, min_visible_row (sheet));
  range.rowi = MIN (range.rowi, max_visible_row (sheet));

  for (i = range.row0; i <= range.rowi; i++)
    {
      for (j = range.col0; j <= range.coli; j++)
	{
	  if (psppire_sheet_cell_get_state (sheet, i, j) == GTK_STATE_SELECTED)
	    {
	      rectangle_from_cell (sheet, i, j, &area);

	      if (i == sheet->range.row0)
		{
		  area.y = area.y + 2;
		  area.height = area.height - 2;
		}
	      if (i == sheet->range.rowi) area.height = area.height - 3;
	      if (j == sheet->range.col0)
		{
		  area.x = area.x + 2;
		  area.width = area.width - 2;
		}
	      if (j == sheet->range.coli) area.width = area.width - 3;

	      if (i != sheet->active_cell.row || j != sheet->active_cell.col)
		{
		  gdk_draw_rectangle (sheet->sheet_window,
				      sheet->xor_gc,
				      TRUE,
				      area.x + 1, area.y + 1,
				      area.width, area.height);
		}
	    }

	}
    }

  psppire_sheet_draw_border (sheet, sheet->range);
}

static inline gint
safe_strcmp (const gchar *s1, const gchar *s2)
{
  if ( !s1 && !s2) return 0;
  if ( !s1) return -1;
  if ( !s2) return +1;
  return strcmp (s1, s2);
}

static void
psppire_sheet_set_cell (PsppireSheet *sheet, gint row, gint col,
		    GtkJustification justification,
		    const gchar *text)
{
  PsppireSheetModel *model ;
  gchar *old_text ;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (sheet));

  if (col >= psppire_axis_unit_count (sheet->haxis)
      || row >= psppire_axis_unit_count (sheet->vaxis))
    return;

  if (col < 0 || row < 0) return;

  model = psppire_sheet_get_model (sheet);

  old_text = psppire_sheet_model_get_string (model, row, col);

  if (0 != safe_strcmp (old_text, text))
    {
      g_signal_handler_block    (sheet->model, sheet->update_handler_id);
      psppire_sheet_model_set_string (model, text, row, col);
      g_signal_handler_unblock  (sheet->model, sheet->update_handler_id);
    }

  if ( psppire_sheet_model_free_strings (model))
    g_free (old_text);
}


void
psppire_sheet_cell_clear (PsppireSheet *sheet, gint row, gint column)
{
  PsppireSheetRange range;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (sheet));
  if (column >= psppire_axis_unit_count (sheet->haxis) ||
      row >= psppire_axis_unit_count (sheet->vaxis)) return;

  if (column < 0 || row < 0) return;

  range.row0 = row;
  range.rowi = row;
  range.col0 = min_visible_column (sheet);
  range.coli = max_visible_column (sheet);

  psppire_sheet_real_cell_clear (sheet, row, column);

  redraw_range (sheet, &range);
}

static void
psppire_sheet_real_cell_clear (PsppireSheet *sheet, gint row, gint column)
{
  PsppireSheetModel *model = psppire_sheet_get_model (sheet);

  gchar *old_text = psppire_sheet_cell_get_text (sheet, row, column);

  if (old_text && strlen (old_text) > 0 )
    {
      psppire_sheet_model_datum_clear (model, row, column);
    }

  dispose_string (sheet, old_text);
}

gchar *
psppire_sheet_cell_get_text (const PsppireSheet *sheet, gint row, gint col)
{
  PsppireSheetModel *model;
  g_return_val_if_fail (sheet != NULL, NULL);
  g_return_val_if_fail (PSPPIRE_IS_SHEET (sheet), NULL);

  if (col >= psppire_axis_unit_count (sheet->haxis) || row >= psppire_axis_unit_count (sheet->vaxis))
    return NULL;
  if (col < 0 || row < 0) return NULL;

  model = psppire_sheet_get_model (sheet);

  if ( !model )
    return NULL;

  return psppire_sheet_model_get_string (model, row, col);
}


static GtkStateType
psppire_sheet_cell_get_state (PsppireSheet *sheet, gint row, gint col)
{
  gint state;
  PsppireSheetRange *range;

  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (PSPPIRE_IS_SHEET (sheet), 0);
  if (col >= psppire_axis_unit_count (sheet->haxis) || row >= psppire_axis_unit_count (sheet->vaxis)) return 0;
  if (col < 0 || row < 0) return 0;

  state = sheet->state;
  range = &sheet->range;

  switch (state)
    {
    case PSPPIRE_SHEET_NORMAL:
      return GTK_STATE_NORMAL;
      break;
    case PSPPIRE_SHEET_ROW_SELECTED:
      if (row >= range->row0 && row <= range->rowi)
	return GTK_STATE_SELECTED;
      break;
    case PSPPIRE_SHEET_COLUMN_SELECTED:
      if (col >= range->col0 && col <= range->coli)
	return GTK_STATE_SELECTED;
      break;
    case PSPPIRE_SHEET_RANGE_SELECTED:
      if (row >= range->row0 && row <= range->rowi && \
	  col >= range->col0 && col <= range->coli)
	return GTK_STATE_SELECTED;
      break;
    }
  return GTK_STATE_NORMAL;
}

/* Convert X, Y (in pixels) to *ROW, *COLUMN
   If the function returns FALSE, then the results will be unreliable.
*/
static gboolean
psppire_sheet_get_pixel_info (PsppireSheet *sheet,
			  gint x,
			  gint y,
			  gint *row,
			  gint *column)
{
  gint trow, tcol;
  *row = -G_MAXINT;
  *column = -G_MAXINT;

  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (PSPPIRE_IS_SHEET (sheet), 0);

  /* bounds checking, return false if the user clicked
     on a blank area */
  if (y < 0)
    return FALSE;

  if (x < 0)
    return FALSE;

  if ( sheet->column_titles_visible)
    y -= sheet->column_title_area.height;

  y += sheet->vadjustment->value;

  if ( y < 0 && sheet->column_titles_visible)
    {
      trow = -1;
    }
  else
    {
      trow = row_from_ypixel (sheet, y);
      if (trow > psppire_axis_unit_count (sheet->vaxis))
	return FALSE;
    }

  *row = trow;

  if ( sheet->row_titles_visible)
    x -= sheet->row_title_area.width;

  x += sheet->hadjustment->value;

  if ( x < 0 && sheet->row_titles_visible)
    {
      tcol = -1;
    }
  else
    {
      tcol = column_from_xpixel (sheet, x);
      if (tcol > psppire_axis_unit_count (sheet->haxis))
	return FALSE;
    }

  *column = tcol;

  return TRUE;
}

gboolean
psppire_sheet_get_cell_area (PsppireSheet *sheet,
			 gint row,
			 gint column,
			 GdkRectangle *area)
{
  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (PSPPIRE_IS_SHEET (sheet), 0);

  if (row >= psppire_axis_unit_count (sheet->vaxis) || column >= psppire_axis_unit_count (sheet->haxis))
    return FALSE;

  area->x = (column == -1) ? 0 : psppire_axis_start_pixel (sheet->haxis, column);
  area->y = (row == -1)    ? 0 : psppire_axis_start_pixel (sheet->vaxis, row);

  area->width= (column == -1) ? sheet->row_title_area.width
    : psppire_axis_unit_size (sheet->haxis, column);

  area->height= (row == -1) ? sheet->column_title_area.height
    : psppire_axis_unit_size (sheet->vaxis, row);

  return TRUE;
}

void
psppire_sheet_set_active_cell (PsppireSheet *sheet, gint row, gint col)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (sheet));

  if (row < -1 || col < -1)
    return;

  if (row >= psppire_axis_unit_count (sheet->vaxis)
      ||
      col >= psppire_axis_unit_count (sheet->haxis))
    return;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    return;

  if ( row == -1 || col == -1)
    {
      psppire_sheet_hide_entry_widget (sheet);
      return;
    }

  change_active_cell (sheet, row, col);
}

void
psppire_sheet_get_active_cell (PsppireSheet *sheet, gint *row, gint *column)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (sheet));

  if ( row ) *row = sheet->active_cell.row;
  if (column) *column = sheet->active_cell.col;
}

static void
entry_load_text (PsppireSheet *sheet)
{
  gint row, col;
  const char *text;
  GtkJustification justification;
  PsppireSheetCellAttr attributes;

  if (!GTK_WIDGET_VISIBLE (sheet->entry_widget)) return;
  if (sheet->state != GTK_STATE_NORMAL) return;

  row = sheet->active_cell.row;
  col = sheet->active_cell.col;

  if (row < 0 || col < 0) return;

  text = gtk_entry_get_text (psppire_sheet_get_entry (sheet));

  if (text && strlen (text) > 0)
    {
      psppire_sheet_get_attributes (sheet, row, col, &attributes);
      justification = attributes.justification;
      psppire_sheet_set_cell (sheet, row, col, justification, text);
    }
}


static void
psppire_sheet_hide_entry_widget (PsppireSheet *sheet)
{
  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    return;

  if (sheet->active_cell.row < 0 ||
      sheet->active_cell.col < 0) return;

  gtk_widget_hide (sheet->entry_widget);
  gtk_widget_unmap (sheet->entry_widget);

  GTK_WIDGET_UNSET_FLAGS (GTK_WIDGET (sheet->entry_widget), GTK_VISIBLE);
}

static void
change_active_cell (PsppireSheet *sheet, gint row, gint col)
{
  gint old_row, old_col;

  g_return_if_fail (PSPPIRE_IS_SHEET (sheet));

  if (row < 0 || col < 0)
    return;

  if ( row > psppire_axis_unit_count (sheet->vaxis)
       || col > psppire_axis_unit_count (sheet->haxis))
    return;

  if (sheet->state != PSPPIRE_SHEET_NORMAL)
    {
      sheet->state = PSPPIRE_SHEET_NORMAL;
      psppire_sheet_real_unselect_range (sheet, NULL);
    }

  old_row = sheet->active_cell.row;
  old_col = sheet->active_cell.col;

  entry_load_text (sheet);

  /* Erase the old cell border */
  psppire_sheet_draw_active_cell (sheet);

  sheet->range.row0 = row;
  sheet->range.col0 = col;
  sheet->range.rowi = row;
  sheet->range.coli = col;
  sheet->active_cell.row = row;
  sheet->active_cell.col = col;
  sheet->selection_cell.row = row;
  sheet->selection_cell.col = col;

  PSPPIRE_SHEET_UNSET_FLAGS (sheet, PSPPIRE_SHEET_IN_SELECTION);

  GTK_WIDGET_UNSET_FLAGS (sheet->entry_widget, GTK_HAS_FOCUS);

  psppire_sheet_draw_active_cell (sheet);
  psppire_sheet_show_entry_widget (sheet);

  GTK_WIDGET_SET_FLAGS (sheet->entry_widget, GTK_HAS_FOCUS);

  g_signal_emit (sheet, sheet_signals [ACTIVATE], 0,
		 row, col, old_row, old_col);

}

static void
psppire_sheet_show_entry_widget (PsppireSheet *sheet)
{
  GtkEntry *sheet_entry;
  PsppireSheetCellAttr attributes;

  gint row, col;

  g_return_if_fail (PSPPIRE_IS_SHEET (sheet));

  row = sheet->active_cell.row;
  col = sheet->active_cell.col;

  /* Don't show the active cell, if there is no active cell: */
  if (! (row >= 0 && col >= 0)) /* e.g row or coll == -1. */
    return;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;
  if (sheet->state != PSPPIRE_SHEET_NORMAL) return;
  if (PSPPIRE_SHEET_IN_SELECTION (sheet)) return;

  GTK_WIDGET_SET_FLAGS (GTK_WIDGET (sheet->entry_widget), GTK_VISIBLE);

  sheet_entry = psppire_sheet_get_entry (sheet);

  psppire_sheet_get_attributes (sheet, row, col, &attributes);

  if (GTK_IS_ENTRY (sheet_entry))
    {
      gchar *text = psppire_sheet_cell_get_text (sheet, row, col);
      const gchar *old_text = gtk_entry_get_text (GTK_ENTRY (sheet_entry));

      if ( ! text )
	text = g_strdup ("");

      if (strcmp (old_text, text) != 0)
	gtk_entry_set_text (sheet_entry, text);
      
      dispose_string (sheet, text);

	{
	  switch (attributes.justification)
	    {
	    case GTK_JUSTIFY_RIGHT:
	      gtk_entry_set_alignment (GTK_ENTRY (sheet_entry), 1.0);
	      break;
	    case GTK_JUSTIFY_CENTER:
	      gtk_entry_set_alignment (GTK_ENTRY (sheet_entry), 0.5);
	      break;
	    case GTK_JUSTIFY_LEFT:
	    default:
	      gtk_entry_set_alignment (GTK_ENTRY (sheet_entry), 0.0);
	      break;
	    }
	}
    }

  psppire_sheet_size_allocate_entry (sheet);

  gtk_widget_set_sensitive (GTK_WIDGET (sheet_entry),
			    psppire_sheet_model_is_editable (sheet->model,
						       row, col));
  gtk_widget_map (sheet->entry_widget);
}

static gboolean
psppire_sheet_draw_active_cell (PsppireSheet *sheet)
{
  gint row, col;
  PsppireSheetRange range;

  row = sheet->active_cell.row;
  col = sheet->active_cell.col;

  if (row < 0 || col < 0) return FALSE;

  if (!psppire_sheet_cell_isvisible (sheet, row, col))
    return FALSE;

  range.col0 = range.coli = col;
  range.row0 = range.rowi = row;

  psppire_sheet_draw_border (sheet, range);

  return FALSE;
}



static void
psppire_sheet_new_selection (PsppireSheet *sheet, PsppireSheetRange *range)
{
  gint i, j, mask1, mask2;
  gint state, selected;
  gint x, y, width, height;
  PsppireSheetRange new_range, aux_range;

  g_return_if_fail (sheet != NULL);

  if (range == NULL) range=&sheet->range;

  new_range=*range;

  range->row0 = MIN (range->row0, sheet->range.row0);
  range->rowi = MAX (range->rowi, sheet->range.rowi);
  range->col0 = MIN (range->col0, sheet->range.col0);
  range->coli = MAX (range->coli, sheet->range.coli);

  range->row0 = MAX (range->row0, min_visible_row (sheet));
  range->rowi = MIN (range->rowi, max_visible_row (sheet));
  range->col0 = MAX (range->col0, min_visible_column (sheet));
  range->coli = MIN (range->coli, max_visible_column (sheet));

  aux_range.row0 = MAX (new_range.row0, min_visible_row (sheet));
  aux_range.rowi = MIN (new_range.rowi, max_visible_row (sheet));
  aux_range.col0 = MAX (new_range.col0, min_visible_column (sheet));
  aux_range.coli = MIN (new_range.coli, max_visible_column (sheet));

  for (i = range->row0; i <= range->rowi; i++)
    {
      for (j = range->col0; j <= range->coli; j++)
	{

	  state = psppire_sheet_cell_get_state (sheet, i, j);
	  selected= (i <= new_range.rowi && i >= new_range.row0 &&
		     j <= new_range.coli && j >= new_range.col0) ? TRUE : FALSE;

	  if (state == GTK_STATE_SELECTED && selected &&
	      (i == sheet->range.row0 || i == sheet->range.rowi ||
	       j == sheet->range.col0 || j == sheet->range.coli ||
	       i == new_range.row0 || i == new_range.rowi ||
	       j == new_range.col0 || j == new_range.coli))
	    {

	      mask1 = i == sheet->range.row0 ? 1 : 0;
	      mask1 = i == sheet->range.rowi ? mask1 + 2 : mask1;
	      mask1 = j == sheet->range.col0 ? mask1 + 4 : mask1;
	      mask1 = j == sheet->range.coli ? mask1 + 8 : mask1;

	      mask2 = i == new_range.row0 ? 1 : 0;
	      mask2 = i == new_range.rowi ? mask2 + 2 : mask2;
	      mask2 = j == new_range.col0 ? mask2 + 4 : mask2;
	      mask2 = j == new_range.coli ? mask2 + 8 : mask2;

	      if (mask1 != mask2)
		{
		  x = psppire_axis_start_pixel (sheet->haxis, j);
		  y = psppire_axis_start_pixel (sheet->vaxis, i);
		  width = psppire_axis_start_pixel (sheet->haxis, j)- x+
		    psppire_axis_unit_size (sheet->haxis, j);
		  height = psppire_axis_start_pixel (sheet->vaxis, i) - y + psppire_axis_unit_size (sheet->vaxis, i);

		  if (i == sheet->range.row0)
		    {
		      y = y - 3;
		      height = height + 3;
		    }
		  if (i == sheet->range.rowi) height = height + 3;
		  if (j == sheet->range.col0)
		    {
		      x = x - 3;
		      width = width + 3;
		    }
		  if (j == sheet->range.coli) width = width + 3;

		  if (i != sheet->active_cell.row || j != sheet->active_cell.col)
		    {
		      x = psppire_axis_start_pixel (sheet->haxis, j);
		      y = psppire_axis_start_pixel (sheet->vaxis, i);
		      width = psppire_axis_start_pixel (sheet->haxis, j)- x+
			psppire_axis_unit_size (sheet->haxis, j);

		      height = psppire_axis_start_pixel (sheet->vaxis, i) - y + psppire_axis_unit_size (sheet->vaxis, i);

		      if (i == new_range.row0)
			{
			  y = y+2;
			  height = height - 2;
			}
		      if (i == new_range.rowi) height = height - 3;
		      if (j == new_range.col0)
			{
			  x = x+2;
			  width = width - 2;
			}
		      if (j == new_range.coli) width = width - 3;

		      gdk_draw_rectangle (sheet->sheet_window,
					  sheet->xor_gc,
					  TRUE,
					  x + 1, y + 1,
					  width, height);
		    }
		}
	    }
	}
    }

  for (i = range->row0; i <= range->rowi; i++)
    {
      for (j = range->col0; j <= range->coli; j++)
	{

	  state = psppire_sheet_cell_get_state (sheet, i, j);
	  selected= (i <= new_range.rowi && i >= new_range.row0 &&
		     j <= new_range.coli && j >= new_range.col0) ? TRUE : FALSE;

	  if (state == GTK_STATE_SELECTED && !selected)
	    {

	      x = psppire_axis_start_pixel (sheet->haxis, j);
	      y = psppire_axis_start_pixel (sheet->vaxis, i);
	      width = psppire_axis_start_pixel (sheet->haxis, j) - x + psppire_axis_unit_size (sheet->haxis, j);
	      height = psppire_axis_start_pixel (sheet->vaxis, i) - y + psppire_axis_unit_size (sheet->vaxis, i);

	      if (i == sheet->range.row0)
		{
		  y = y - 3;
		  height = height + 3;
		}
	      if (i == sheet->range.rowi) height = height + 3;
	      if (j == sheet->range.col0)
		{
		  x = x - 3;
		  width = width + 3;
		}
	      if (j == sheet->range.coli) width = width + 3;

	    }
	}
    }

  for (i = range->row0; i <= range->rowi; i++)
    {
      for (j = range->col0; j <= range->coli; j++)
	{

	  state = psppire_sheet_cell_get_state (sheet, i, j);
	  selected= (i <= new_range.rowi && i >= new_range.row0 &&
		     j <= new_range.coli && j >= new_range.col0) ? TRUE : FALSE;

	  if (state != GTK_STATE_SELECTED && selected &&
	      (i != sheet->active_cell.row || j != sheet->active_cell.col))
	    {

	      x = psppire_axis_start_pixel (sheet->haxis, j);
	      y = psppire_axis_start_pixel (sheet->vaxis, i);
	      width = psppire_axis_start_pixel (sheet->haxis, j) - x + psppire_axis_unit_size (sheet->haxis, j);
	      height = psppire_axis_start_pixel (sheet->vaxis, i) - y + psppire_axis_unit_size (sheet->vaxis, i);

	      if (i == new_range.row0)
		{
		  y = y+2;
		  height = height - 2;
		}
	      if (i == new_range.rowi) height = height - 3;
	      if (j == new_range.col0)
		{
		  x = x+2;
		  width = width - 2;
		}
	      if (j == new_range.coli) width = width - 3;

	      gdk_draw_rectangle (sheet->sheet_window,
				  sheet->xor_gc,
				  TRUE,
				  x + 1, y + 1,
				  width, height);

	    }

	}
    }

  for (i = aux_range.row0; i <= aux_range.rowi; i++)
    {
      for (j = aux_range.col0; j <= aux_range.coli; j++)
	{
	  state = psppire_sheet_cell_get_state (sheet, i, j);

	  mask1 = i == sheet->range.row0 ? 1 : 0;
	  mask1 = i == sheet->range.rowi ? mask1 + 2 : mask1;
	  mask1 = j == sheet->range.col0 ? mask1 + 4 : mask1;
	  mask1 = j == sheet->range.coli ? mask1 + 8 : mask1;

	  mask2 = i == new_range.row0 ? 1 : 0;
	  mask2 = i == new_range.rowi ? mask2 + 2 : mask2;
	  mask2 = j == new_range.col0 ? mask2 + 4 : mask2;
	  mask2 = j == new_range.coli ? mask2 + 8 : mask2;
	  if (mask2 != mask1 || (mask2 == mask1 && state != GTK_STATE_SELECTED))
	    {
	      x = psppire_axis_start_pixel (sheet->haxis, j);
	      y = psppire_axis_start_pixel (sheet->vaxis, i);
	      width = psppire_axis_unit_size (sheet->haxis, j);
	      height = psppire_axis_unit_size (sheet->vaxis, i);
	      if (mask2 & 1)
		gdk_draw_rectangle (sheet->sheet_window,
				    sheet->xor_gc,
				    TRUE,
				    x + 1, y - 1,
				    width, 3);


	      if (mask2 & 2)
		gdk_draw_rectangle (sheet->sheet_window,
				    sheet->xor_gc,
				    TRUE,
				    x + 1, y + height - 1,
				    width, 3);

	      if (mask2 & 4)
		gdk_draw_rectangle (sheet->sheet_window,
				    sheet->xor_gc,
				    TRUE,
				    x - 1, y + 1,
				    3, height);


	      if (mask2 & 8)
		gdk_draw_rectangle (sheet->sheet_window,
				    sheet->xor_gc,
				    TRUE,
				    x + width - 1, y + 1,
				    3, height);
	    }
	}
    }

  *range = new_range;
}



static void
psppire_sheet_draw_border (PsppireSheet *sheet, PsppireSheetRange new_range)
{
  GdkRectangle area;

  rectangle_from_range (sheet, &new_range, &area);

  area.width ++;
  area.height ++;

  gdk_gc_set_clip_rectangle (sheet->xor_gc, &area);

  area.x += sheet->cell_padding->left / 2;
  area.y += sheet->cell_padding->top / 2;
  area.width -= (sheet->cell_padding->left + sheet->cell_padding->right ) / 2;
  area.height -= (sheet->cell_padding->top + sheet->cell_padding->bottom ) / 2;

  gdk_draw_rectangle (sheet->sheet_window,  sheet->xor_gc,
		      FALSE,
		      area.x,
		      area.y,
		      area.width,
		      area.height);

  gdk_gc_set_clip_rectangle (sheet->xor_gc, NULL);
}


static void
psppire_sheet_real_select_range (PsppireSheet *sheet,
			     const PsppireSheetRange *range)
{
  gint state;

  g_return_if_fail (sheet != NULL);

  if (range == NULL) range = &sheet->range;

  memcpy (&sheet->range, range, sizeof (*range));

  if (range->row0 < 0 || range->rowi < 0) return;
  if (range->col0 < 0 || range->coli < 0) return;

  state = sheet->state;

#if 0
  if (range->coli != sheet->range.coli || range->col0 != sheet->range.col0 ||
      range->rowi != sheet->range.rowi || range->row0 != sheet->range.row0)
    {
      psppire_sheet_new_selection (sheet, &sheet->range);
    }
  else
    {
      psppire_sheet_range_draw_selection (sheet, sheet->range);
    }
#endif

  psppire_sheet_update_primary_selection (sheet);

  g_signal_emit (sheet, sheet_signals[SELECT_RANGE], 0, &sheet->range);
}


void
psppire_sheet_get_selected_range (PsppireSheet *sheet, PsppireSheetRange *range)
{
  g_return_if_fail (sheet != NULL);
  *range = sheet->range;
}


void
psppire_sheet_select_range (PsppireSheet *sheet, const PsppireSheetRange *range)
{
  g_return_if_fail (sheet != NULL);

  if (range == NULL) range=&sheet->range;

  if (range->row0 < 0 || range->rowi < 0) return;
  if (range->col0 < 0 || range->coli < 0) return;


  if (sheet->state != PSPPIRE_SHEET_NORMAL)
    psppire_sheet_real_unselect_range (sheet, NULL);

  sheet->range.row0 = range->row0;
  sheet->range.rowi = range->rowi;
  sheet->range.col0 = range->col0;
  sheet->range.coli = range->coli;
  sheet->selection_cell.row = range->rowi;
  sheet->selection_cell.col = range->coli;

  sheet->state = PSPPIRE_SHEET_RANGE_SELECTED;
  psppire_sheet_real_select_range (sheet, NULL);
}

void
psppire_sheet_unselect_range (PsppireSheet *sheet)
{
  if (! GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    return;

  psppire_sheet_real_unselect_range (sheet, NULL);
  sheet->state = GTK_STATE_NORMAL;

  change_active_cell (sheet,
		 sheet->active_cell.row, sheet->active_cell.col);
}


static void
psppire_sheet_real_unselect_range (PsppireSheet *sheet,
			       const PsppireSheetRange *range)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)));

  if ( range == NULL)
    range = &sheet->range;

  if (range->row0 < 0 || range->rowi < 0) return;
  if (range->col0 < 0 || range->coli < 0) return;

  g_signal_emit (sheet, sheet_signals[SELECT_COLUMN], 0, -1);
  g_signal_emit (sheet, sheet_signals[SELECT_ROW], 0, -1);

  sheet->range.row0 = -1;
  sheet->range.rowi = -1;
  sheet->range.col0 = -1;
  sheet->range.coli = -1;
}


static gint
psppire_sheet_expose (GtkWidget *widget,
		  GdkEventExpose *event)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (widget);

  g_return_val_if_fail (event != NULL, FALSE);

  if (!GTK_WIDGET_DRAWABLE (widget))
    return FALSE;

  /* exposure events on the sheet */
  if (event->window == sheet->row_title_window &&
      sheet->row_titles_visible)
    {
      draw_row_title_buttons_range (sheet,
				    min_visible_row (sheet),
				    max_visible_row (sheet));
    }

  if (event->window == sheet->column_title_window &&
      sheet->column_titles_visible)
    {
      draw_column_title_buttons_range (sheet,
				       min_visible_column (sheet),
				       max_visible_column (sheet));
    }

  if (event->window == sheet->sheet_window)
    {
      draw_sheet_region (sheet, event->region);

#if 0
      if (sheet->state != PSPPIRE_SHEET_NORMAL)
	{
	  if (psppire_sheet_range_isvisible (sheet, &sheet->range))
	    psppire_sheet_range_draw (sheet, &sheet->range);

	  if (PSPPIRE_SHEET_IN_RESIZE (sheet) || PSPPIRE_SHEET_IN_DRAG (sheet))
	    psppire_sheet_range_draw (sheet, &sheet->drag_range);

	  if (psppire_sheet_range_isvisible (sheet, &sheet->range))
	    psppire_sheet_range_draw_selection (sheet, sheet->range);
	  if (PSPPIRE_SHEET_IN_RESIZE (sheet) || PSPPIRE_SHEET_IN_DRAG (sheet))
	    draw_xor_rectangle (sheet, sheet->drag_range);
	}
#endif

      if ((!PSPPIRE_SHEET_IN_XDRAG (sheet)) && (!PSPPIRE_SHEET_IN_YDRAG (sheet)))
	{
	  GdkRectangle rect;
	  PsppireSheetRange range;
	  range.row0 = range.rowi =  sheet->active_cell.row;
	  range.col0 = range.coli =  sheet->active_cell.col;

	  rectangle_from_range (sheet, &range, &rect);

	  if (GDK_OVERLAP_RECTANGLE_OUT !=
	      gdk_region_rect_in (event->region, &rect))
	    {
	      psppire_sheet_draw_active_cell (sheet);
	    }
	}

    }

  (* GTK_WIDGET_CLASS (parent_class)->expose_event) (widget, event);

  return FALSE;
}


static gboolean
psppire_sheet_button_press (GtkWidget *widget,
			GdkEventButton *event)
{
  PsppireSheet *sheet;
  GdkModifierType mods;
  gint x, y;
  gint  row, column;


  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (PSPPIRE_IS_SHEET (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  sheet = PSPPIRE_SHEET (widget);

  /* Cancel any pending tooltips */
  if (sheet->motion_timer)
    {
      g_source_remove (sheet->motion_timer);
      sheet->motion_timer = 0;
    }

  gtk_widget_get_pointer (widget, &x, &y);
  psppire_sheet_get_pixel_info (sheet, x, y, &row, &column);


  if (event->window == sheet->column_title_window)
    {
      sheet->x_drag = event->x;
      g_signal_emit (sheet,
		     sheet_signals[BUTTON_EVENT_COLUMN], 0,
		     column, event);

      if (psppire_sheet_model_get_column_sensitivity (sheet->model, column))
	{
	  if ( event->type == GDK_2BUTTON_PRESS && event->button == 1)
	    g_signal_emit (sheet,
			   sheet_signals[DOUBLE_CLICK_COLUMN], 0, column);
	}
    }
  else if (event->window == sheet->row_title_window)
    {
      g_signal_emit (sheet,
		     sheet_signals[BUTTON_EVENT_ROW], 0,
		     row, event);

      if (psppire_sheet_model_get_row_sensitivity (sheet->model, row))
	{
	  if ( event->type == GDK_2BUTTON_PRESS && event->button == 1)
	    g_signal_emit (sheet,
			   sheet_signals[DOUBLE_CLICK_ROW], 0, row);
	}
    }

  gdk_window_get_pointer (widget->window, NULL, NULL, &mods);

  if (! (mods & GDK_BUTTON1_MASK)) return TRUE;


  /* press on resize windows */
  if (event->window == sheet->column_title_window)
    {
      sheet->x_drag = event->x;

      if (on_column_boundary (sheet, sheet->x_drag, &sheet->drag_cell.col))
	{
	  PSPPIRE_SHEET_SET_FLAGS (sheet, PSPPIRE_SHEET_IN_XDRAG);
	  gdk_pointer_grab (sheet->column_title_window, FALSE,
			    GDK_POINTER_MOTION_HINT_MASK |
			    GDK_BUTTON1_MOTION_MASK |
			    GDK_BUTTON_RELEASE_MASK,
			    NULL, NULL, event->time);

	  draw_xor_vline (sheet);
	  return TRUE;
	}
    }

  if (event->window == sheet->row_title_window)
    {
      sheet->y_drag = event->y;

      if (on_row_boundary (sheet, sheet->y_drag, &sheet->drag_cell.row))
	{
	  PSPPIRE_SHEET_SET_FLAGS (sheet, PSPPIRE_SHEET_IN_YDRAG);
	  gdk_pointer_grab (sheet->row_title_window, FALSE,
			    GDK_POINTER_MOTION_HINT_MASK |
			    GDK_BUTTON1_MOTION_MASK |
			    GDK_BUTTON_RELEASE_MASK,
			    NULL, NULL, event->time);

	  draw_xor_hline (sheet);
	  return TRUE;
	}
    }

  /* the sheet itself does not handle other than single click events */
  if (event->type != GDK_BUTTON_PRESS) return FALSE;

  /* selections on the sheet */
  if (event->window == sheet->sheet_window)
    {
      gtk_widget_get_pointer (widget, &x, &y);
      psppire_sheet_get_pixel_info (sheet, x, y, &row, &column);
      gdk_pointer_grab (sheet->sheet_window, FALSE,
			GDK_POINTER_MOTION_HINT_MASK |
			GDK_BUTTON1_MOTION_MASK |
			GDK_BUTTON_RELEASE_MASK,
			NULL, NULL, event->time);
      gtk_grab_add (GTK_WIDGET (sheet));

      if (psppire_sheet_click_cell (sheet, row, column))
	PSPPIRE_SHEET_SET_FLAGS (sheet, PSPPIRE_SHEET_IN_SELECTION);
    }

  if (event->window == sheet->column_title_window)
    {
      gtk_widget_get_pointer (widget, &x, &y);
      if ( sheet->row_titles_visible)
	x -= sheet->row_title_area.width;

      x += sheet->hadjustment->value;

      column = column_from_xpixel (sheet, x);

      if (psppire_sheet_model_get_column_sensitivity (sheet->model, column))
	{
	  gtk_grab_add (GTK_WIDGET (sheet));
	  PSPPIRE_SHEET_SET_FLAGS (sheet, PSPPIRE_SHEET_IN_SELECTION);
	}
    }

  if (event->window == sheet->row_title_window)
    {
      gtk_widget_get_pointer (widget, &x, &y);
      if ( sheet->column_titles_visible)
	y -= sheet->column_title_area.height;

      y += sheet->vadjustment->value;

      row = row_from_ypixel (sheet, y);
      if (psppire_sheet_model_get_row_sensitivity (sheet->model, row))
	{
	  gtk_grab_add (GTK_WIDGET (sheet));
	  PSPPIRE_SHEET_SET_FLAGS (sheet, PSPPIRE_SHEET_IN_SELECTION);
	}
    }

  return TRUE;
}

static gboolean
psppire_sheet_click_cell (PsppireSheet *sheet, gint row, gint column)
{
  PsppireSheetCell cell;
  gboolean forbid_move;

  cell.row = row;
  cell.col = column;

  if (row >= psppire_axis_unit_count (sheet->vaxis)
      || column >= psppire_axis_unit_count (sheet->haxis))
    {
      return FALSE;
    }

  g_signal_emit (sheet, sheet_signals[TRAVERSE], 0,
		 &sheet->active_cell,
		 &cell,
		 &forbid_move);

  if (forbid_move)
    {
      if (sheet->state == GTK_STATE_NORMAL)
	return FALSE;

      row = sheet->active_cell.row;
      column = sheet->active_cell.col;

      change_active_cell (sheet, row, column);
      return FALSE;
    }

  if (row == -1 && column >= 0)
    {
      psppire_sheet_select_column (sheet, column);
      return TRUE;
    }

  if (column == -1 && row >= 0)
    {
      psppire_sheet_select_row (sheet, row);
      return TRUE;
    }

  if (row == -1 && column == -1)
    {
      sheet->range.row0 = 0;
      sheet->range.col0 = 0;
      sheet->range.rowi = psppire_axis_unit_count (sheet->vaxis) - 1;
      sheet->range.coli =
	psppire_axis_unit_count (sheet->haxis) - 1;
      psppire_sheet_select_range (sheet, NULL);
      return TRUE;
    }

  if (sheet->state != PSPPIRE_SHEET_NORMAL)
    {
      sheet->state = PSPPIRE_SHEET_NORMAL;
      psppire_sheet_real_unselect_range (sheet, NULL);
    }
  else
    {
      change_active_cell (sheet, row, column);
    }

  sheet->selection_cell.row = row;
  sheet->selection_cell.col = column;
  sheet->range.row0 = row;
  sheet->range.col0 = column;
  sheet->range.rowi = row;
  sheet->range.coli = column;
  sheet->state = PSPPIRE_SHEET_NORMAL;
  PSPPIRE_SHEET_SET_FLAGS (sheet, PSPPIRE_SHEET_IN_SELECTION);

  gtk_widget_grab_focus (GTK_WIDGET (sheet->entry_widget));

  return TRUE;
}

static gint
psppire_sheet_button_release (GtkWidget *widget,
			  GdkEventButton *event)
{
  GdkDisplay *display = gtk_widget_get_display (widget);

  PsppireSheet *sheet = PSPPIRE_SHEET (widget);

  /* release on resize windows */
  if (PSPPIRE_SHEET_IN_XDRAG (sheet))
    {
      gint width;
      PSPPIRE_SHEET_UNSET_FLAGS (sheet, PSPPIRE_SHEET_IN_XDRAG);
      PSPPIRE_SHEET_UNSET_FLAGS (sheet, PSPPIRE_SHEET_IN_SELECTION);

      gdk_display_pointer_ungrab (display, event->time);
      draw_xor_vline (sheet);

      width = event->x -
	psppire_axis_start_pixel (sheet->haxis, sheet->drag_cell.col)
	+ sheet->hadjustment->value;

      set_column_width (sheet, sheet->drag_cell.col, width);

      return TRUE;
    }

  if (PSPPIRE_SHEET_IN_YDRAG (sheet))
    {
      gint height;
      PSPPIRE_SHEET_UNSET_FLAGS (sheet, PSPPIRE_SHEET_IN_YDRAG);
      PSPPIRE_SHEET_UNSET_FLAGS (sheet, PSPPIRE_SHEET_IN_SELECTION);

      gdk_display_pointer_ungrab (display, event->time);
      draw_xor_hline (sheet);

      height = event->y -
	psppire_axis_start_pixel (sheet->vaxis, sheet->drag_cell.row) +
	sheet->vadjustment->value;

      set_row_height (sheet, sheet->drag_cell.row, height);

      return TRUE;
    }

  if (PSPPIRE_SHEET_IN_DRAG (sheet))
    {
      PsppireSheetRange old_range;
      draw_xor_rectangle (sheet, sheet->drag_range);
      PSPPIRE_SHEET_UNSET_FLAGS (sheet, PSPPIRE_SHEET_IN_DRAG);
      gdk_display_pointer_ungrab (display, event->time);

      psppire_sheet_real_unselect_range (sheet, NULL);

      sheet->selection_cell.row = sheet->selection_cell.row +
	(sheet->drag_range.row0 - sheet->range.row0);
      sheet->selection_cell.col = sheet->selection_cell.col +
	(sheet->drag_range.col0 - sheet->range.col0);
      old_range = sheet->range;
      sheet->range = sheet->drag_range;
      sheet->drag_range = old_range;
      g_signal_emit (sheet, sheet_signals[MOVE_RANGE], 0,
		     &sheet->drag_range, &sheet->range);
      psppire_sheet_select_range (sheet, &sheet->range);
    }

  if (PSPPIRE_SHEET_IN_RESIZE (sheet))
    {
      PsppireSheetRange old_range;
      draw_xor_rectangle (sheet, sheet->drag_range);
      PSPPIRE_SHEET_UNSET_FLAGS (sheet, PSPPIRE_SHEET_IN_RESIZE);
      gdk_display_pointer_ungrab (display, event->time);

      psppire_sheet_real_unselect_range (sheet, NULL);

      if (sheet->drag_range.row0 < sheet->range.row0)
	sheet->selection_cell.row = sheet->drag_range.row0;
      if (sheet->drag_range.rowi >= sheet->range.rowi)
	sheet->selection_cell.row = sheet->drag_range.rowi;
      if (sheet->drag_range.col0 < sheet->range.col0)
	sheet->selection_cell.col = sheet->drag_range.col0;
      if (sheet->drag_range.coli >= sheet->range.coli)
	sheet->selection_cell.col = sheet->drag_range.coli;
      old_range = sheet->range;
      sheet->range = sheet->drag_range;
      sheet->drag_range = old_range;

      if (sheet->state == GTK_STATE_NORMAL) sheet->state = PSPPIRE_SHEET_RANGE_SELECTED;
      g_signal_emit (sheet, sheet_signals[RESIZE_RANGE], 0,
		     &sheet->drag_range, &sheet->range);
      psppire_sheet_select_range (sheet, &sheet->range);
    }

  if (sheet->state == PSPPIRE_SHEET_NORMAL && PSPPIRE_SHEET_IN_SELECTION (sheet))
    {
      PSPPIRE_SHEET_UNSET_FLAGS (sheet, PSPPIRE_SHEET_IN_SELECTION);
      gdk_display_pointer_ungrab (display, event->time);
      change_active_cell (sheet, sheet->active_cell.row,
			       sheet->active_cell.col);
    }

  if (PSPPIRE_SHEET_IN_SELECTION)
    gdk_display_pointer_ungrab (display, event->time);
  gtk_grab_remove (GTK_WIDGET (sheet));

  PSPPIRE_SHEET_UNSET_FLAGS (sheet, PSPPIRE_SHEET_IN_SELECTION);

  return TRUE;
}





/* Shamelessly lifted from gtktooltips */
static gboolean
psppire_sheet_subtitle_paint_window (GtkWidget *tip_window)
{
  GtkRequisition req;

  gtk_widget_size_request (tip_window, &req);
  gtk_paint_flat_box (tip_window->style, tip_window->window,
		      GTK_STATE_NORMAL, GTK_SHADOW_OUT,
		      NULL, GTK_WIDGET(tip_window), "tooltip",
		      0, 0, req.width, req.height);

  return FALSE;
}

static void
destroy_hover_window (PsppireSheetHoverTitle *h)
{
  gtk_widget_destroy (h->window);
  g_free (h);
}

static PsppireSheetHoverTitle *
create_hover_window (void)
{
  PsppireSheetHoverTitle *hw = g_malloc (sizeof (*hw));

  hw->window = gtk_window_new (GTK_WINDOW_POPUP);

#if GTK_CHECK_VERSION (2, 9, 0)
  gtk_window_set_type_hint (GTK_WINDOW (hw->window),
			    GDK_WINDOW_TYPE_HINT_TOOLTIP);
#endif

  gtk_widget_set_app_paintable (hw->window, TRUE);
  gtk_window_set_resizable (GTK_WINDOW (hw->window), FALSE);
  gtk_widget_set_name (hw->window, "gtk-tooltips");
  gtk_container_set_border_width (GTK_CONTAINER (hw->window), 4);

  g_signal_connect (hw->window,
		    "expose_event",
		    G_CALLBACK (psppire_sheet_subtitle_paint_window),
		    NULL);

  hw->label = gtk_label_new (NULL);


  gtk_label_set_line_wrap (GTK_LABEL (hw->label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (hw->label), 0.5, 0.5);

  gtk_container_add (GTK_CONTAINER (hw->window), hw->label);

  gtk_widget_show (hw->label);

  g_signal_connect (hw->window,
		    "destroy",
		    G_CALLBACK (gtk_widget_destroyed),
		    &hw->window);

  return hw;
}

#define HOVER_WINDOW_Y_OFFSET 2

static void
show_subtitle (PsppireSheet *sheet, gint row, gint column,
	       const gchar *subtitle)
{
  gint x, y;
  gint px, py;
  gint width;

  if ( ! subtitle )
    return;

  gtk_label_set_text (GTK_LABEL (sheet->hover_window->label),
		      subtitle);


  sheet->hover_window->row = row;
  sheet->hover_window->column = column;

  gdk_window_get_origin (GTK_WIDGET (sheet)->window, &x, &y);

  gtk_widget_get_pointer (GTK_WIDGET (sheet), &px, &py);

  gtk_widget_show (sheet->hover_window->window);

  width = GTK_WIDGET (sheet->hover_window->label)->allocation.width;

  if (row == -1 )
    {
      x += px;
      x -= width / 2;
      y += sheet->column_title_area.y;
      y += sheet->column_title_area.height;
      y += HOVER_WINDOW_Y_OFFSET;
    }

  if ( column == -1 )
    {
      y += py;
      x += sheet->row_title_area.x;
      x += sheet->row_title_area.width * 2 / 3.0;
    }

  gtk_window_move (GTK_WINDOW (sheet->hover_window->window),
		   x, y);
}

static gboolean
motion_timeout_callback (gpointer data)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (data);
  gint x, y;
  gint row, column;

  gdk_threads_enter ();
  gtk_widget_get_pointer (GTK_WIDGET (sheet), &x, &y);

  if ( psppire_sheet_get_pixel_info (sheet, x, y, &row, &column) )
    {
      if (sheet->row_title_under && row >= 0)
	{
	  gchar *text = psppire_sheet_model_get_row_subtitle (sheet->model, row);

	  show_subtitle (sheet, row, -1, text);
	  g_free (text);
	}

      if (sheet->column_title_under && column >= 0)
	{
	  gchar *text = psppire_sheet_model_get_column_subtitle (sheet->model,
							   column);

	  show_subtitle (sheet, -1, column, text);

	  g_free (text);
	}
    }

  gdk_threads_leave ();
  return FALSE;
}

static gboolean
psppire_sheet_motion (GtkWidget *widget,  GdkEventMotion *event)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (widget);
  GdkModifierType mods;
  GdkCursorType new_cursor;
  gint x, y;
  gint row, column;
  GdkDisplay *display;

  g_return_val_if_fail (event != NULL, FALSE);

  display = gtk_widget_get_display (widget);

  /* selections on the sheet */
  x = event->x;
  y = event->y;

  if (!GTK_WIDGET_VISIBLE (sheet->hover_window->window))
    {
      if ( sheet->motion_timer > 0 )
	g_source_remove (sheet->motion_timer);
      sheet->motion_timer =
	g_timeout_add (TIMEOUT_HOVER, motion_timeout_callback, sheet);
    }
  else
    {
      gint row, column;
      gint wx, wy;
      gtk_widget_get_pointer (widget, &wx, &wy);

      if ( psppire_sheet_get_pixel_info (sheet, wx, wy, &row, &column) )
	{
	  if ( row != sheet->hover_window->row ||
	       column != sheet->hover_window->column)
	    {
	      gtk_widget_hide (sheet->hover_window->window);
	    }
	}
    }

  if (event->window == sheet->column_title_window)
    {
      if (!PSPPIRE_SHEET_IN_SELECTION (sheet) &&
	  on_column_boundary (sheet, x, &column))
	{
	  new_cursor = GDK_SB_H_DOUBLE_ARROW;
	  if (new_cursor != sheet->cursor_drag->type)
	    {
	      gdk_cursor_unref (sheet->cursor_drag);
	      sheet->cursor_drag =
		gdk_cursor_new_for_display (display, new_cursor);

	      gdk_window_set_cursor (sheet->column_title_window,
				     sheet->cursor_drag);
	    }
	}
      else
	{
	  new_cursor = GDK_TOP_LEFT_ARROW;
	  if (!PSPPIRE_SHEET_IN_XDRAG (sheet) &&
	      new_cursor != sheet->cursor_drag->type)
	    {
	      gdk_cursor_unref (sheet->cursor_drag);
	      sheet->cursor_drag =
		gdk_cursor_new_for_display (display, new_cursor);
	      gdk_window_set_cursor (sheet->column_title_window,
				     sheet->cursor_drag);
	    }
	}
    }
  else if (event->window == sheet->row_title_window)
    {
      if (!PSPPIRE_SHEET_IN_SELECTION (sheet) &&
	  on_row_boundary (sheet, y, &row))
	{
	  new_cursor = GDK_SB_V_DOUBLE_ARROW;
	  if (new_cursor != sheet->cursor_drag->type)
	    {
	      gdk_cursor_unref (sheet->cursor_drag);
	      sheet->cursor_drag =
		gdk_cursor_new_for_display (display, new_cursor);
	      gdk_window_set_cursor (sheet->row_title_window,
				     sheet->cursor_drag);
	    }
	}
      else
	{
	  new_cursor = GDK_TOP_LEFT_ARROW;
	  if (!PSPPIRE_SHEET_IN_YDRAG (sheet) &&
	      new_cursor != sheet->cursor_drag->type)
	    {
	      gdk_cursor_unref (sheet->cursor_drag);
	      sheet->cursor_drag =
		gdk_cursor_new_for_display (display, new_cursor);
	      gdk_window_set_cursor (sheet->row_title_window,
				     sheet->cursor_drag);
	    }
	}
    }

  new_cursor = GDK_PLUS;
  if ( event->window == sheet->sheet_window &&
       !POSSIBLE_DRAG (sheet, x, y, &row, &column) &&
       !PSPPIRE_SHEET_IN_DRAG (sheet) &&
       !POSSIBLE_RESIZE (sheet, x, y, &row, &column) &&
       !PSPPIRE_SHEET_IN_RESIZE (sheet) &&
       new_cursor != sheet->cursor_drag->type)
    {
      gdk_cursor_unref (sheet->cursor_drag);
      sheet->cursor_drag = gdk_cursor_new_for_display (display, GDK_PLUS);
      gdk_window_set_cursor (sheet->sheet_window, sheet->cursor_drag);
    }

  new_cursor = GDK_TOP_LEFT_ARROW;
  if ( event->window == sheet->sheet_window &&
       ! (POSSIBLE_RESIZE (sheet, x, y, &row, &column) ||
	  PSPPIRE_SHEET_IN_RESIZE (sheet)) &&
       (POSSIBLE_DRAG (sheet, x, y, &row, &column) ||
	PSPPIRE_SHEET_IN_DRAG (sheet)) &&
       new_cursor != sheet->cursor_drag->type)
    {
      gdk_cursor_unref (sheet->cursor_drag);
      sheet->cursor_drag = gdk_cursor_new_for_display (display, GDK_TOP_LEFT_ARROW);
      gdk_window_set_cursor (sheet->sheet_window, sheet->cursor_drag);
    }

  new_cursor = GDK_SIZING;
  if ( event->window == sheet->sheet_window &&
       sheet->selection_mode != GTK_SELECTION_NONE &&
       !PSPPIRE_SHEET_IN_DRAG (sheet) &&
       (POSSIBLE_RESIZE (sheet, x, y, &row, &column) ||
	PSPPIRE_SHEET_IN_RESIZE (sheet)) &&
       new_cursor != sheet->cursor_drag->type)
    {
      gdk_cursor_unref (sheet->cursor_drag);
      sheet->cursor_drag = gdk_cursor_new_for_display (display, GDK_SIZING);
      gdk_window_set_cursor (sheet->sheet_window, sheet->cursor_drag);
    }


  gdk_window_get_pointer (widget->window, &x, &y, &mods);
  if (! (mods & GDK_BUTTON1_MASK)) return FALSE;

  if (PSPPIRE_SHEET_IN_XDRAG (sheet))
    {
      if (event->x != sheet->x_drag)
	{
	  draw_xor_vline (sheet);
	  sheet->x_drag = event->x;
	  draw_xor_vline (sheet);
	}

      return TRUE;
    }

  if (PSPPIRE_SHEET_IN_YDRAG (sheet))
    {
      if (event->y != sheet->y_drag)
	{
	  draw_xor_hline (sheet);
	  sheet->y_drag = event->y;
	  draw_xor_hline (sheet);
	}

      return TRUE;
    }

  if (PSPPIRE_SHEET_IN_DRAG (sheet))
    {
      PsppireSheetRange aux;
      column = column_from_xpixel (sheet, x)- sheet->drag_cell.col;
      row = row_from_ypixel (sheet, y) - sheet->drag_cell.row;
      if (sheet->state == PSPPIRE_SHEET_COLUMN_SELECTED) row = 0;
      if (sheet->state == PSPPIRE_SHEET_ROW_SELECTED) column = 0;
      sheet->x_drag = x;
      sheet->y_drag = y;
      aux = sheet->range;
      if (aux.row0 + row >= 0 && aux.rowi + row < psppire_axis_unit_count (sheet->vaxis) &&
	  aux.col0 + column >= 0 && aux.coli + column < psppire_axis_unit_count (sheet->haxis))
	{
	  aux = sheet->drag_range;
	  sheet->drag_range.row0 = sheet->range.row0 + row;
	  sheet->drag_range.col0 = sheet->range.col0 + column;
	  sheet->drag_range.rowi = sheet->range.rowi + row;
	  sheet->drag_range.coli = sheet->range.coli + column;
	  if (aux.row0 != sheet->drag_range.row0 ||
	      aux.col0 != sheet->drag_range.col0)
	    {
	      draw_xor_rectangle (sheet, aux);
	      draw_xor_rectangle (sheet, sheet->drag_range);
	    }
	}
      return TRUE;
    }

  if (PSPPIRE_SHEET_IN_RESIZE (sheet))
    {
      PsppireSheetRange aux;
      gint v_h, current_col, current_row, col_threshold, row_threshold;
      v_h = 1;
      if (abs (x - psppire_axis_start_pixel (sheet->haxis, sheet->drag_cell.col)) >
	  abs (y - psppire_axis_start_pixel (sheet->vaxis, sheet->drag_cell.row))) v_h = 2;

      current_col = column_from_xpixel (sheet, x);
      current_row = row_from_ypixel (sheet, y);
      column = current_col - sheet->drag_cell.col;
      row = current_row - sheet->drag_cell.row;

      /*use half of column width resp. row height as threshold to
	expand selection*/
      col_threshold = psppire_axis_start_pixel (sheet->haxis, current_col) +
	psppire_axis_unit_size (sheet->haxis, current_col) / 2;
      if (column > 0)
	{
	  if (x < col_threshold)
	    column -= 1;
	}
      else if (column < 0)
	{
	  if (x > col_threshold)
	    column +=1;
	}
      row_threshold = psppire_axis_start_pixel (sheet->vaxis, current_row) +
	psppire_axis_unit_size (sheet->vaxis, current_row)/2;
      if (row > 0)
	{
	  if (y < row_threshold)
	    row -= 1;
	}
      else if (row < 0)
	{
	  if (y > row_threshold)
	    row +=1;
	}

      if (sheet->state == PSPPIRE_SHEET_COLUMN_SELECTED) row = 0;
      if (sheet->state == PSPPIRE_SHEET_ROW_SELECTED) column = 0;
      sheet->x_drag = x;
      sheet->y_drag = y;
      aux = sheet->range;

      if (v_h == 1)
	column = 0;
      else
	row = 0;

      if (aux.row0 + row >= 0 && aux.rowi + row < psppire_axis_unit_count (sheet->vaxis) &&
	  aux.col0 + column >= 0 && aux.coli + column < psppire_axis_unit_count (sheet->haxis))
	{
	  aux = sheet->drag_range;
	  sheet->drag_range = sheet->range;

	  if (row < 0) sheet->drag_range.row0 = sheet->range.row0 + row;
	  if (row > 0) sheet->drag_range.rowi = sheet->range.rowi + row;
	  if (column < 0) sheet->drag_range.col0 = sheet->range.col0 + column;
	  if (column > 0) sheet->drag_range.coli = sheet->range.coli + column;

	  if (aux.row0 != sheet->drag_range.row0 ||
	      aux.rowi != sheet->drag_range.rowi ||
	      aux.col0 != sheet->drag_range.col0 ||
	      aux.coli != sheet->drag_range.coli)
	    {
	      draw_xor_rectangle (sheet, aux);
	      draw_xor_rectangle (sheet, sheet->drag_range);
	    }
	}
      return TRUE;
    }

  psppire_sheet_get_pixel_info (sheet, x, y, &row, &column);

  if (sheet->state == PSPPIRE_SHEET_NORMAL && row == sheet->active_cell.row &&
      column == sheet->active_cell.col) return TRUE;

  if (PSPPIRE_SHEET_IN_SELECTION (sheet) && mods&GDK_BUTTON1_MASK)
    psppire_sheet_extend_selection (sheet, row, column);

  return TRUE;
}

static gboolean
psppire_sheet_crossing_notify (GtkWidget *widget,
			   GdkEventCrossing *event)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (widget);

  if (event->window == sheet->column_title_window)
    sheet->column_title_under = event->type == GDK_ENTER_NOTIFY;
  else if (event->window == sheet->row_title_window)
    sheet->row_title_under = event->type == GDK_ENTER_NOTIFY;

  if (event->type == GDK_LEAVE_NOTIFY)
    gtk_widget_hide (sheet->hover_window->window);

  return TRUE;
}


static gboolean
psppire_sheet_focus_in (GtkWidget     *w,
			GdkEventFocus *event)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (w);

  gtk_widget_grab_focus (sheet->entry_widget);

  return TRUE;
}


static void
psppire_sheet_extend_selection (PsppireSheet *sheet, gint row, gint column)
{
  PsppireSheetRange range;
  gint state;
  gint r, c;

  if (row == sheet->selection_cell.row && column == sheet->selection_cell.col)
    return;

  if (sheet->selection_mode == GTK_SELECTION_SINGLE) return;

  gtk_widget_grab_focus (GTK_WIDGET (sheet));

  if (PSPPIRE_SHEET_IN_DRAG (sheet)) return;

  state = sheet->state;

  switch (sheet->state)
    {
    case PSPPIRE_SHEET_ROW_SELECTED:
      column = psppire_axis_unit_count (sheet->haxis) - 1;
      break;
    case PSPPIRE_SHEET_COLUMN_SELECTED:
      row = psppire_axis_unit_count (sheet->vaxis) - 1;
      break;
    case PSPPIRE_SHEET_NORMAL:
      sheet->state = PSPPIRE_SHEET_RANGE_SELECTED;
      r = sheet->active_cell.row;
      c = sheet->active_cell.col;
      sheet->range.col0 = c;
      sheet->range.row0 = r;
      sheet->range.coli = c;
      sheet->range.rowi = r;
      psppire_sheet_range_draw_selection (sheet, sheet->range);
    case PSPPIRE_SHEET_RANGE_SELECTED:
      sheet->state = PSPPIRE_SHEET_RANGE_SELECTED;
    }

  sheet->selection_cell.row = row;
  sheet->selection_cell.col = column;

  range.col0 = MIN (column, sheet->active_cell.col);
  range.coli = MAX (column, sheet->active_cell.col);
  range.row0 = MIN (row, sheet->active_cell.row);
  range.rowi = MAX (row, sheet->active_cell.row);

  if (range.row0 != sheet->range.row0 || range.rowi != sheet->range.rowi ||
      range.col0 != sheet->range.col0 || range.coli != sheet->range.coli ||
      state == PSPPIRE_SHEET_NORMAL)
    psppire_sheet_real_select_range (sheet, &range);

}

static gint
psppire_sheet_entry_key_press (GtkWidget *widget,
			   GdkEventKey *key)
{
  gboolean focus;
  g_signal_emit_by_name (widget, "key_press_event", key, &focus);
  return focus;
}


/* Number of rows in a step-increment */
#define ROWS_PER_STEP 1


static void
page_vertical (PsppireSheet *sheet, GtkScrollType dir)
{
  gint old_row = sheet->active_cell.row ;
  glong vpixel = psppire_axis_start_pixel (sheet->vaxis, old_row);

  gint new_row;

  vpixel -= psppire_axis_start_pixel (sheet->vaxis,
				     min_visible_row (sheet));

  switch ( dir)
    {
    case GTK_SCROLL_PAGE_DOWN:
      gtk_adjustment_set_value (sheet->vadjustment,
				sheet->vadjustment->value +
				sheet->vadjustment->page_increment);
      break;
    case GTK_SCROLL_PAGE_UP:
      gtk_adjustment_set_value (sheet->vadjustment,
				sheet->vadjustment->value -
				sheet->vadjustment->page_increment);

      break;
    default:
      g_assert_not_reached ();
      break;
    }


  vpixel += psppire_axis_start_pixel (sheet->vaxis,
				     min_visible_row (sheet));

  new_row =  row_from_ypixel (sheet, vpixel);

  change_active_cell (sheet, new_row,
			   sheet->active_cell.col);
}


static void
step_sheet (PsppireSheet *sheet, GtkScrollType dir)
{
  gint current_row = sheet->active_cell.row;
  gint current_col = sheet->active_cell.col;
  PsppireSheetCell new_cell ;
  gboolean forbidden = FALSE;

  new_cell.row = current_row;
  new_cell.col = current_col;

  switch ( dir)
    {
    case GTK_SCROLL_STEP_DOWN:
      new_cell.row++;
      break;
    case GTK_SCROLL_STEP_UP:
      new_cell.row--;
      break;
    case GTK_SCROLL_STEP_RIGHT:
      new_cell.col++;
      break;
    case GTK_SCROLL_STEP_LEFT:
      new_cell.col--;
      break;
    case GTK_SCROLL_STEP_FORWARD:
      new_cell.col++;
      if (new_cell.col >=
	  psppire_sheet_model_get_column_count (sheet->model))
	{
	  new_cell.col = 0;
	  new_cell.row++;
	}
      break;
    case GTK_SCROLL_STEP_BACKWARD:
      new_cell.col--;
      if (new_cell.col < 0)
	{
	  new_cell.col =
	    psppire_sheet_model_get_column_count (sheet->model) - 1;
	  new_cell.row--;
	}
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  g_signal_emit (sheet, sheet_signals[TRAVERSE], 0,
		 &sheet->active_cell,
		 &new_cell,
		 &forbidden);

  if (forbidden)
    return;


  maximize_int (&new_cell.row, 0);
  maximize_int (&new_cell.col, 0);

  minimize_int (&new_cell.row,
		psppire_axis_unit_count (sheet->vaxis) - 1);

  minimize_int (&new_cell.col,
		psppire_axis_unit_count (sheet->haxis) - 1);

  change_active_cell (sheet, new_cell.row, new_cell.col);


  if ( new_cell.col > max_fully_visible_column (sheet))
    {
      glong hpos  =
	psppire_axis_start_pixel (sheet->haxis,
				    new_cell.col + 1);
      hpos -= sheet->hadjustment->page_size;

      gtk_adjustment_set_value (sheet->hadjustment,
				hpos);
    }
  else if ( new_cell.col < min_fully_visible_column (sheet))
    {
      glong hpos  =
	psppire_axis_start_pixel (sheet->haxis,
				    new_cell.col);

      gtk_adjustment_set_value (sheet->hadjustment,
				hpos);
    }


  if ( new_cell.row > max_fully_visible_row (sheet))
    {
      glong vpos  =
	psppire_axis_start_pixel (sheet->vaxis,
				    new_cell.row + 1);
      vpos -= sheet->vadjustment->page_size;

      gtk_adjustment_set_value (sheet->vadjustment,
				vpos);
    }
  else if ( new_cell.row < min_fully_visible_row (sheet))
    {
      glong vpos  =
	psppire_axis_start_pixel (sheet->vaxis,
				    new_cell.row);

      gtk_adjustment_set_value (sheet->vadjustment,
				vpos);
    }

  gtk_widget_grab_focus (GTK_WIDGET (sheet->entry_widget));
}


static gboolean
psppire_sheet_key_press (GtkWidget *widget,
		     GdkEventKey *key)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (widget);

  PSPPIRE_SHEET_UNSET_FLAGS (sheet, PSPPIRE_SHEET_IN_SELECTION);

  switch (key->keyval)
    {
    case GDK_Tab:
      step_sheet (sheet, GTK_SCROLL_STEP_FORWARD);
      break;
    case GDK_Right:
      step_sheet (sheet, GTK_SCROLL_STEP_RIGHT);
      break;
    case GDK_ISO_Left_Tab:
      step_sheet (sheet, GTK_SCROLL_STEP_BACKWARD);
      break;
    case GDK_Left:
      step_sheet (sheet, GTK_SCROLL_STEP_LEFT);
      break;
    case GDK_Return:
    case GDK_Down:
      step_sheet (sheet, GTK_SCROLL_STEP_DOWN);
      break;
    case GDK_Up:
      step_sheet (sheet, GTK_SCROLL_STEP_UP);
      break;

    case GDK_Page_Down:
      page_vertical (sheet, GTK_SCROLL_PAGE_DOWN);
      break;
    case GDK_Page_Up:
      page_vertical (sheet, GTK_SCROLL_PAGE_UP);
      break;

    case GDK_Home:
      gtk_adjustment_set_value (sheet->vadjustment,
				sheet->vadjustment->lower);

      change_active_cell (sheet,  0,
			       sheet->active_cell.col);

      break;

    case GDK_End:
      gtk_adjustment_set_value (sheet->vadjustment,
				sheet->vadjustment->upper -
				sheet->vadjustment->page_size -
				sheet->vadjustment->page_increment);

      /*
	change_active_cellx (sheet,
	psppire_axis_unit_count (sheet->vaxis) - 1,
	sheet->active_cell.col);
      */
      break;
    case GDK_Delete:
      psppire_sheet_real_cell_clear (sheet, sheet->active_cell.row, sheet->active_cell.col);
      break;
    default:
      return FALSE;
      break;
    }

  return TRUE;
}

static void
psppire_sheet_size_request (GtkWidget *widget,
			GtkRequisition *requisition)
{
  PsppireSheet *sheet;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (widget));
  g_return_if_fail (requisition != NULL);

  sheet = PSPPIRE_SHEET (widget);

  requisition->width = 3 * DEFAULT_COLUMN_WIDTH;
  requisition->height = 3 * DEFAULT_ROW_HEIGHT;

  /* compute the size of the column title area */
  if (sheet->column_titles_visible)
    requisition->height += sheet->column_title_area.height;

  /* compute the size of the row title area */
  if (sheet->row_titles_visible)
    requisition->width += sheet->row_title_area.width;
}


static void
psppire_sheet_size_allocate (GtkWidget *widget,
			 GtkAllocation *allocation)
{
  PsppireSheet *sheet;
  GtkAllocation sheet_allocation;
  gint border_width;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (widget));
  g_return_if_fail (allocation != NULL);

  sheet = PSPPIRE_SHEET (widget);
  widget->allocation = *allocation;
  border_width = GTK_CONTAINER (widget)->border_width;

  if (GTK_WIDGET_REALIZED (widget))
    gdk_window_move_resize (widget->window,
			    allocation->x + border_width,
			    allocation->y + border_width,
			    allocation->width - 2 * border_width,
			    allocation->height - 2 * border_width);

  sheet_allocation.x = 0;
  sheet_allocation.y = 0;
  sheet_allocation.width = allocation->width - 2 * border_width;
  sheet_allocation.height = allocation->height - 2 * border_width;

  if (GTK_WIDGET_REALIZED (widget))
    gdk_window_move_resize (sheet->sheet_window,
			    sheet_allocation.x,
			    sheet_allocation.y,
			    sheet_allocation.width,
			    sheet_allocation.height);

  /* position the window which holds the column title buttons */
  sheet->column_title_area.x = 0;
  sheet->column_title_area.y = 0;
  sheet->column_title_area.width = sheet_allocation.width ;


  /* position the window which holds the row title buttons */
  sheet->row_title_area.x = 0;
  sheet->row_title_area.y = 0;
  sheet->row_title_area.height = sheet_allocation.height;

  if (sheet->row_titles_visible)
    sheet->column_title_area.x += sheet->row_title_area.width;

  if (sheet->column_titles_visible)
    sheet->row_title_area.y += sheet->column_title_area.height;

  if (GTK_WIDGET_REALIZED (widget) && sheet->column_titles_visible)
    gdk_window_move_resize (sheet->column_title_window,
			    sheet->column_title_area.x,
			    sheet->column_title_area.y,
			    sheet->column_title_area.width,
			    sheet->column_title_area.height);


  if (GTK_WIDGET_REALIZED (widget) && sheet->row_titles_visible)
    gdk_window_move_resize (sheet->row_title_window,
			    sheet->row_title_area.x,
			    sheet->row_title_area.y,
			    sheet->row_title_area.width,
			    sheet->row_title_area.height);

  size_allocate_global_button (sheet);

  if (sheet->haxis)
    {
      gint width = sheet->column_title_area.width;

      if ( sheet->row_titles_visible)
	width -= sheet->row_title_area.width;

      g_object_set (sheet->haxis,
		    "minimum-extent", width,
		    NULL);
    }


  if (sheet->vaxis)
    {
      gint height = sheet->row_title_area.height;

      if ( sheet->column_titles_visible)
	height -= sheet->column_title_area.height;

      g_object_set (sheet->vaxis,
		    "minimum-extent", height,
		    NULL);
    }


  /* set the scrollbars adjustments */
  adjust_scrollbars (sheet);
}

static void
draw_column_title_buttons (PsppireSheet *sheet)
{
  gint x, width;

  if (!sheet->column_titles_visible) return;
  if (!GTK_WIDGET_REALIZED (sheet))
    return;

  gdk_drawable_get_size (sheet->sheet_window, &width, NULL);
  x = 0;

  if (sheet->row_titles_visible)
    {
      x = sheet->row_title_area.width;
    }

  if (sheet->column_title_area.width != width || sheet->column_title_area.x != x)
    {
      sheet->column_title_area.width = width;
      sheet->column_title_area.x = x;
      gdk_window_move_resize (sheet->column_title_window,
			      sheet->column_title_area.x,
			      sheet->column_title_area.y,
			      sheet->column_title_area.width,
			      sheet->column_title_area.height);
    }

  if (max_visible_column (sheet) ==
      psppire_axis_unit_count (sheet->haxis) - 1)
    gdk_window_clear_area (sheet->column_title_window,
			   0, 0,
			   sheet->column_title_area.width,
			   sheet->column_title_area.height);

  if (!GTK_WIDGET_DRAWABLE (sheet)) return;

  draw_column_title_buttons_range (sheet, min_visible_column (sheet), 
				   max_visible_column (sheet));
}

static void
draw_row_title_buttons (PsppireSheet *sheet)
{
  gint y = 0;
  gint height;

  if (!sheet->row_titles_visible) return;
  if (!GTK_WIDGET_REALIZED (sheet))
    return;

  gdk_drawable_get_size (sheet->sheet_window, NULL, &height);

  if (sheet->column_titles_visible)
    {
      y = sheet->column_title_area.height;
    }

  if (sheet->row_title_area.height != height || sheet->row_title_area.y != y)
    {
      sheet->row_title_area.y = y;
      sheet->row_title_area.height = height;
      gdk_window_move_resize (sheet->row_title_window,
			      sheet->row_title_area.x,
			      sheet->row_title_area.y,
			      sheet->row_title_area.width,
			      sheet->row_title_area.height);
    }

  if (max_visible_row (sheet) == psppire_axis_unit_count (sheet->vaxis) - 1)
    gdk_window_clear_area (sheet->row_title_window,
			   0, 0,
			   sheet->row_title_area.width,
			   sheet->row_title_area.height);

  if (!GTK_WIDGET_DRAWABLE (sheet)) return;

  draw_row_title_buttons_range (sheet, min_visible_row (sheet),
				max_visible_row (sheet));
}


static void
psppire_sheet_size_allocate_entry (PsppireSheet *sheet)
{
  GtkAllocation entry_alloc;
  PsppireSheetCellAttr attributes = { 0 };
  GtkEntry *sheet_entry;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;
  if (!GTK_WIDGET_MAPPED (GTK_WIDGET (sheet))) return;

  sheet_entry = psppire_sheet_get_entry (sheet);

  if ( ! psppire_sheet_get_attributes (sheet, sheet->active_cell.row,
				   sheet->active_cell.col,
				   &attributes) )
    return ;

  if ( GTK_WIDGET_REALIZED (sheet->entry_widget) )
    {
      GtkStyle *style = GTK_WIDGET (sheet_entry)->style;

      style->bg[GTK_STATE_NORMAL] = attributes.background;
      style->fg[GTK_STATE_NORMAL] = attributes.foreground;
      style->text[GTK_STATE_NORMAL] = attributes.foreground;
      style->bg[GTK_STATE_ACTIVE] = attributes.background;
      style->fg[GTK_STATE_ACTIVE] = attributes.foreground;
      style->text[GTK_STATE_ACTIVE] = attributes.foreground;
    }

  rectangle_from_cell (sheet, sheet->active_cell.row,
		       sheet->active_cell.col, &entry_alloc);

  entry_alloc.x += sheet->cell_padding->left;
  entry_alloc.y += sheet->cell_padding->right;
  entry_alloc.width -= sheet->cell_padding->left + sheet->cell_padding->right;
  entry_alloc.height -= sheet->cell_padding->top + sheet->cell_padding->bottom;


  gtk_widget_set_size_request (sheet->entry_widget, entry_alloc.width,
			       entry_alloc.height);
  gtk_widget_size_allocate (sheet->entry_widget, &entry_alloc);
}


/* Copy the sheet's font to the entry widget */
static void
set_entry_widget_font (PsppireSheet *sheet)
{
  GtkRcStyle *style = gtk_widget_get_modifier_style (sheet->entry_widget);

  pango_font_description_free (style->font_desc);
  style->font_desc = pango_font_description_copy (GTK_WIDGET (sheet)->style->font_desc);

  gtk_widget_modify_style (sheet->entry_widget, style);
}

static void
create_sheet_entry (PsppireSheet *sheet)
{
  if (sheet->entry_widget)
    {
      gtk_widget_unparent (sheet->entry_widget);
    }

  sheet->entry_widget = g_object_new (sheet->entry_type, NULL);
  g_object_ref_sink (sheet->entry_widget);

  gtk_widget_size_request (sheet->entry_widget, NULL);

  if ( GTK_IS_ENTRY (sheet->entry_widget))
    {
      g_object_set (sheet->entry_widget,
		    "has-frame", FALSE,
		    NULL);
    }

  if (GTK_WIDGET_REALIZED (sheet))
    {
      gtk_widget_set_parent_window (sheet->entry_widget, sheet->sheet_window);
      gtk_widget_set_parent (sheet->entry_widget, GTK_WIDGET (sheet));
      gtk_widget_realize (sheet->entry_widget);
    }

  g_signal_connect_swapped (sheet->entry_widget, "key_press_event",
			    G_CALLBACK (psppire_sheet_entry_key_press),
			    sheet);

  set_entry_widget_font (sheet);

  gtk_widget_show (sheet->entry_widget);
}


/* Finds the last child widget that happens to be of type GtkEntry */
static void
find_entry (GtkWidget *w, gpointer user_data)
{
  GtkWidget **entry = user_data;
  if ( GTK_IS_ENTRY (w))
    {
      *entry = w;
    }
}


GtkEntry *
psppire_sheet_get_entry (PsppireSheet *sheet)
{
  GtkWidget *w = sheet->entry_widget;

  g_return_val_if_fail (sheet != NULL, NULL);
  g_return_val_if_fail (PSPPIRE_IS_SHEET (sheet), NULL);
  g_return_val_if_fail (sheet->entry_widget != NULL, NULL);

  while (! GTK_IS_ENTRY (w))
    {
      GtkWidget *entry = NULL;

      if (GTK_IS_CONTAINER (w))
	{
	  gtk_container_forall (GTK_CONTAINER (w), find_entry, &entry);

	  if (NULL == entry)
	    break;

	  w = entry;
	}
    }

  return GTK_ENTRY (w);
}


static void
draw_button (PsppireSheet *sheet, GdkWindow *window,
		       PsppireSheetButton *button, gboolean is_sensitive,
		       GdkRectangle allocation)
{
  GtkShadowType shadow_type;
  gint text_width = 0, text_height = 0;
  PangoAlignment align = PANGO_ALIGN_LEFT;

  gboolean rtl ;

  gint state = 0;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (button != NULL);


  rtl = gtk_widget_get_direction (GTK_WIDGET (sheet)) == GTK_TEXT_DIR_RTL;

  gdk_window_clear_area (window,
			 allocation.x, allocation.y,
			 allocation.width, allocation.height);

  gtk_widget_ensure_style (sheet->button);

  gtk_paint_box (sheet->button->style, window,
		 GTK_STATE_NORMAL, GTK_SHADOW_OUT,
		 &allocation,
		 GTK_WIDGET (sheet->button),
		 NULL,
		 allocation.x, allocation.y,
		 allocation.width, allocation.height);

  state = button->state;
  if (!is_sensitive) state = GTK_STATE_INSENSITIVE;

  if (state == GTK_STATE_ACTIVE)
    shadow_type = GTK_SHADOW_IN;
  else
    shadow_type = GTK_SHADOW_OUT;

  if (state != GTK_STATE_NORMAL && state != GTK_STATE_INSENSITIVE)
    gtk_paint_box (sheet->button->style, window,
		   button->state, shadow_type,
		   &allocation, GTK_WIDGET (sheet->button),
		   NULL,
		   allocation.x, allocation.y,
		   allocation.width, allocation.height);

  if ( button->overstruck)
    {
      GdkPoint points[2] = {
	{allocation.x,  allocation.y},
	{allocation.x + allocation.width,
	 allocation.y + allocation.height}
      };

      gtk_paint_polygon (sheet->button->style,
			 window,
			 button->state,
			 shadow_type,
			 NULL,
			 GTK_WIDGET (sheet),
			 NULL,
			 points,
			 2,
			 TRUE);
    }

  if (button->label_visible)
    {
      text_height = DEFAULT_ROW_HEIGHT -
	2 * COLUMN_TITLES_HEIGHT;

      gdk_gc_set_clip_rectangle (GTK_WIDGET (sheet)->style->fg_gc[button->state],
				 &allocation);
      gdk_gc_set_clip_rectangle (GTK_WIDGET (sheet)->style->white_gc,
				 &allocation);

      allocation.y += 2 * sheet->button->style->ythickness;

      if (button->label && strlen (button->label) > 0)
	{
	  PangoRectangle rect;
	  gchar *line = button->label;

	  PangoLayout *layout = NULL;
	  gint real_x = allocation.x;
	  gint real_y = allocation.y;

	  layout = gtk_widget_create_pango_layout (GTK_WIDGET (sheet), line);
	  pango_layout_get_extents (layout, NULL, &rect);

	  text_width = PANGO_PIXELS (rect.width);
	  switch (button->justification)
	    {
	    case GTK_JUSTIFY_LEFT:
	      real_x = allocation.x + COLUMN_TITLES_HEIGHT;
	      align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
	      break;
	    case GTK_JUSTIFY_RIGHT:
	      real_x = allocation.x + allocation.width - text_width - COLUMN_TITLES_HEIGHT;
	      align = rtl ? PANGO_ALIGN_LEFT : PANGO_ALIGN_RIGHT;
	      break;
	    case GTK_JUSTIFY_CENTER:
	    default:
	      real_x = allocation.x + (allocation.width - text_width)/2;
	      align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
	      pango_layout_set_justify (layout, TRUE);
	    }
	  pango_layout_set_alignment (layout, align);
	  gtk_paint_layout (GTK_WIDGET (sheet)->style,
			    window,
			    state,
			    FALSE,
			    &allocation,
			    GTK_WIDGET (sheet),
			    "label",
			    real_x, real_y,
			    layout);
	  g_object_unref (layout);
	}

      gdk_gc_set_clip_rectangle (GTK_WIDGET (sheet)->style->fg_gc[button->state],
				 NULL);
      gdk_gc_set_clip_rectangle (GTK_WIDGET (sheet)->style->white_gc, NULL);

    }

  psppire_sheet_button_free (button);
}


/* Draw the column title buttons FIRST through to LAST */
static void
draw_column_title_buttons_range (PsppireSheet *sheet, gint first, gint last)
{
  GdkRectangle rect;
  gint col;
  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;

  if (!sheet->column_titles_visible) return;

  g_return_if_fail (first >= min_visible_column (sheet));
  g_return_if_fail (last <= max_visible_column (sheet));

  rect.y = 0;
  rect.height = sheet->column_title_area.height;
  rect.x = psppire_axis_start_pixel (sheet->haxis, first) + CELL_SPACING;
  rect.width = psppire_axis_start_pixel (sheet->haxis, last) + CELL_SPACING
    + psppire_axis_unit_size (sheet->haxis, last);

  rect.x -= sheet->hadjustment->value;

  minimize_int (&rect.width, sheet->column_title_area.width);
  maximize_int (&rect.x, 0);

  gdk_window_begin_paint_rect (sheet->column_title_window, &rect);

  for (col = first ; col <= last ; ++col)
    {
      GdkRectangle allocation;
      gboolean is_sensitive = FALSE;

      PsppireSheetButton *
	button = psppire_sheet_model_get_column_button (sheet->model, col);
      allocation.y = 0;
      allocation.x = psppire_axis_start_pixel (sheet->haxis, col)
	+ CELL_SPACING;
      allocation.x -= sheet->hadjustment->value;

      allocation.height = sheet->column_title_area.height;
      allocation.width = psppire_axis_unit_size (sheet->haxis, col);
      is_sensitive = psppire_sheet_model_get_column_sensitivity (sheet->model, col);

      draw_button (sheet, sheet->column_title_window,
		   button, is_sensitive, allocation);
    }

  gdk_window_end_paint (sheet->column_title_window);
}


static void
draw_row_title_buttons_range (PsppireSheet *sheet, gint first, gint last)
{
  GdkRectangle rect;
  gint row;
  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;

  if (!sheet->row_titles_visible) return;

  g_return_if_fail (first >= min_visible_row (sheet));
  g_return_if_fail (last <= max_visible_row (sheet));

  rect.x = 0;
  rect.width = sheet->row_title_area.width;
  rect.y = psppire_axis_start_pixel (sheet->vaxis, first) + CELL_SPACING;
  rect.height = psppire_axis_start_pixel (sheet->vaxis, last) + CELL_SPACING
    + psppire_axis_unit_size (sheet->vaxis, last);

  rect.y -= sheet->vadjustment->value;

  minimize_int (&rect.height, sheet->row_title_area.height);
  maximize_int (&rect.y, 0);

  gdk_window_begin_paint_rect (sheet->row_title_window, &rect);
  for (row = first; row <= last; ++row)
    {
      GdkRectangle allocation;

      gboolean is_sensitive = FALSE;

      PsppireSheetButton *button =
	psppire_sheet_model_get_row_button (sheet->model, row);
      allocation.x = 0;
      allocation.y = psppire_axis_start_pixel (sheet->vaxis, row)
	+ CELL_SPACING;
      allocation.y -= sheet->vadjustment->value;

      allocation.width = sheet->row_title_area.width;
      allocation.height = psppire_axis_unit_size (sheet->vaxis, row);
      is_sensitive = psppire_sheet_model_get_row_sensitivity (sheet->model, row);

      draw_button (sheet, sheet->row_title_window,
		   button, is_sensitive, allocation);
    }

  gdk_window_end_paint (sheet->row_title_window);
}

/* SCROLLBARS
 *
 * functions:
 * adjust_scrollbars
 * vadjustment_value_changed
 * hadjustment_value_changed */


static void
update_adjustment (GtkAdjustment *adj, PsppireAxis *axis, gint page_size)
{
  double position =
    (adj->value + adj->page_size)
    /
    (adj->upper - adj->lower);

  const glong last_item = psppire_axis_unit_count (axis) - 1;

  if (isnan (position) || position < 0)
    position = 0;

  adj->upper =
    psppire_axis_start_pixel (axis, last_item)
    +
    psppire_axis_unit_size (axis, last_item)
    ;

  adj->lower = 0;
  adj->page_size = page_size;

#if 0
  adj->value = position * (adj->upper - adj->lower) - adj->page_size;

  if ( adj->value < adj->lower)
    adj->value = adj->lower;
#endif

  gtk_adjustment_changed (adj);
}


static void
adjust_scrollbars (PsppireSheet *sheet)
{
  gint width, height;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    return;

  gdk_drawable_get_size (sheet->sheet_window, &width, &height);

  if ( sheet->row_titles_visible)
    width -= sheet->row_title_area.width;

  if (sheet->column_titles_visible)
    height -= sheet->column_title_area.height;

  if (sheet->vadjustment)
    {
      glong last_row = psppire_axis_unit_count (sheet->vaxis) - 1;

      sheet->vadjustment->step_increment =
	ROWS_PER_STEP *
	psppire_axis_unit_size (sheet->vaxis, last_row);

      sheet->vadjustment->page_increment =
	height -
	sheet->column_title_area.height -
	psppire_axis_unit_size (sheet->vaxis, last_row);

      update_adjustment (sheet->vadjustment, sheet->vaxis, height);
    }

  if (sheet->hadjustment)
    {
      gint last_col = psppire_axis_unit_count (sheet->haxis) - 1;
      sheet->hadjustment->step_increment = 1;

      sheet->hadjustment->page_increment = width;

      sheet->hadjustment->upper =
	psppire_axis_start_pixel (sheet->haxis, last_col)
	+
	psppire_axis_unit_size (sheet->haxis, last_col)
	;

      update_adjustment (sheet->hadjustment, sheet->haxis, width);
    }
}

/* Subtracts the region of WIDGET from REGION */
static void
subtract_widget_region (GdkRegion *region, GtkWidget *widget)
{
  GdkRectangle rect;
  GdkRectangle intersect;
  GdkRegion *region2;

  gdk_region_get_clipbox (region, &rect);
  gtk_widget_intersect (widget,
			&rect,
			&intersect);

  region2 = gdk_region_rectangle (&intersect);
  gdk_region_subtract (region, region2);
  gdk_region_destroy (region2);
}

static void
vadjustment_value_changed (GtkAdjustment *adjustment,
			   gpointer data)
{
  GdkRegion *region;
  PsppireSheet *sheet = PSPPIRE_SHEET (data);

  g_return_if_fail (adjustment != NULL);

  if ( ! GTK_WIDGET_REALIZED (sheet)) return;

  gtk_widget_hide (sheet->entry_widget);

  region =
    gdk_drawable_get_visible_region (GDK_DRAWABLE (sheet->sheet_window));

  subtract_widget_region (region, sheet->button);
  gdk_window_begin_paint_region (sheet->sheet_window, region);

  draw_sheet_region (sheet, region);

  draw_row_title_buttons (sheet);
  psppire_sheet_draw_active_cell (sheet);

  gdk_window_end_paint (sheet->sheet_window);
  gdk_region_destroy (region);
}


static void
hadjustment_value_changed (GtkAdjustment *adjustment,
			   gpointer data)
{
  GdkRegion *region;
  PsppireSheet *sheet = PSPPIRE_SHEET (data);

  g_return_if_fail (adjustment != NULL);

  if ( ! GTK_WIDGET_REALIZED (sheet)) return;

  gtk_widget_hide (sheet->entry_widget);


  region =
    gdk_drawable_get_visible_region (GDK_DRAWABLE (sheet->sheet_window));

  subtract_widget_region (region, sheet->button);
  gdk_window_begin_paint_region (sheet->sheet_window, region);

  draw_sheet_region (sheet, region);

  draw_column_title_buttons (sheet);

  psppire_sheet_draw_active_cell (sheet);

  gdk_window_end_paint (sheet->sheet_window);

  gdk_region_destroy (region);
}


/* COLUMN RESIZING */
static void
draw_xor_vline (PsppireSheet *sheet)
{
  gint height;
  gint xpos = sheet->x_drag;
  gdk_drawable_get_size (sheet->sheet_window,
			 NULL, &height);

  if (sheet->row_titles_visible)
    xpos += sheet->row_title_area.width;

  gdk_draw_line (GTK_WIDGET (sheet)->window, sheet->xor_gc,
		 xpos,
		 sheet->column_title_area.height,
		 xpos,
		 height + CELL_SPACING);
}

/* ROW RESIZING */
static void
draw_xor_hline (PsppireSheet *sheet)

{
  gint width;
  gint ypos = sheet->y_drag;

  gdk_drawable_get_size (sheet->sheet_window,
			 &width, NULL);


  if (sheet->column_titles_visible)
    ypos += sheet->column_title_area.height;

  gdk_draw_line (GTK_WIDGET (sheet)->window, sheet->xor_gc,
		 sheet->row_title_area.width,
		 ypos,
		 width + CELL_SPACING,
		 ypos);
}

/* SELECTED RANGE */
static void
draw_xor_rectangle (PsppireSheet *sheet, PsppireSheetRange range)
{
  gint i = 0;
  GdkRectangle clip_area, area;
  GdkGCValues values;

  area.x = psppire_axis_start_pixel (sheet->haxis, range.col0);
  area.y = psppire_axis_start_pixel (sheet->vaxis, range.row0);
  area.width = psppire_axis_start_pixel (sheet->haxis, range.coli)- area.x+
    psppire_axis_unit_size (sheet->haxis, range.coli);
  area.height = psppire_axis_start_pixel (sheet->vaxis, range.rowi)- area.y +
    psppire_axis_unit_size (sheet->vaxis, range.rowi);

  clip_area.x = sheet->row_title_area.width;
  clip_area.y = sheet->column_title_area.height;

  gdk_drawable_get_size (sheet->sheet_window,
			 &clip_area.width, &clip_area.height);

  if (!sheet->row_titles_visible) clip_area.x = 0;
  if (!sheet->column_titles_visible) clip_area.y = 0;

  if (area.x < 0)
    {
      area.width = area.width + area.x;
      area.x = 0;
    }
  if (area.width > clip_area.width) area.width = clip_area.width + 10;
  if (area.y < 0)
    {
      area.height = area.height + area.y;
      area.y = 0;
    }
  if (area.height > clip_area.height) area.height = clip_area.height + 10;

  clip_area.x--;
  clip_area.y--;
  clip_area.width += 3;
  clip_area.height += 3;

  gdk_gc_get_values (sheet->xor_gc, &values);

  gdk_gc_set_clip_rectangle (sheet->xor_gc, &clip_area);

  gdk_draw_rectangle (sheet->sheet_window,
		      sheet->xor_gc,
		      FALSE,
		      area.x + i, area.y + i,
		      area.width - 2 * i, area.height - 2 * i);


  gdk_gc_set_clip_rectangle (sheet->xor_gc, NULL);

  gdk_gc_set_foreground (sheet->xor_gc, &values.foreground);
}


static void
set_column_width (PsppireSheet *sheet,
		  gint column,
		  gint width)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (sheet));

  if (column < 0 || column >= psppire_axis_unit_count (sheet->haxis))
    return;

  if ( width <= 0)
    return;

  psppire_axis_resize (sheet->haxis, column,
		       width - sheet->cell_padding->left -
		       sheet->cell_padding->right);

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      draw_column_title_buttons (sheet);
      adjust_scrollbars (sheet);
      psppire_sheet_size_allocate_entry (sheet);
      redraw_range (sheet, NULL);
    }
}

static void
set_row_height (PsppireSheet *sheet,
		gint row,
		gint height)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (PSPPIRE_IS_SHEET (sheet));

  if (row < 0 || row >= psppire_axis_unit_count (sheet->vaxis))
    return;

  if (height <= 0)
    return;

  psppire_axis_resize (sheet->vaxis, row,
		       height - sheet->cell_padding->top -
		       sheet->cell_padding->bottom);

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)) )
    {
      draw_row_title_buttons (sheet);
      adjust_scrollbars (sheet);
      psppire_sheet_size_allocate_entry (sheet);
      redraw_range (sheet, NULL);
    }
}

static gboolean
psppire_sheet_get_attributes (const PsppireSheet *sheet, gint row, gint col,
			  PsppireSheetCellAttr *attr)
{
  GdkColor *fg, *bg;
  const GtkJustification *j ;
  GdkColormap *colormap;

  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (PSPPIRE_IS_SHEET (sheet), FALSE);

  if (row < 0 || col < 0) return FALSE;

  attr->foreground = GTK_WIDGET (sheet)->style->black;
  attr->background = sheet->color[BG_COLOR];

  attr->border.width = 0;
  attr->border.line_style = GDK_LINE_SOLID;
  attr->border.cap_style = GDK_CAP_NOT_LAST;
  attr->border.join_style = GDK_JOIN_MITER;
  attr->border.mask = 0;
  attr->border.color = GTK_WIDGET (sheet)->style->black;

  colormap = gtk_widget_get_colormap (GTK_WIDGET (sheet));
  fg = psppire_sheet_model_get_foreground (sheet->model, row, col);
  if ( fg )
    {
      gdk_colormap_alloc_color (colormap, fg, TRUE, TRUE);
      attr->foreground = *fg;
    }

  bg = psppire_sheet_model_get_background (sheet->model, row, col);
  if ( bg )
    {
      gdk_colormap_alloc_color (colormap, bg, TRUE, TRUE);
      attr->background = *bg;
    }

  attr->justification =
    psppire_sheet_model_get_column_justification (sheet->model, col);

  j = psppire_sheet_model_get_justification (sheet->model, row, col);
  if (j)
    attr->justification = *j;

  return TRUE;
}

static void
psppire_sheet_button_size_request	 (PsppireSheet *sheet,
				  const PsppireSheetButton *button,
				  GtkRequisition *button_requisition)
{
  GtkRequisition requisition;
  GtkRequisition label_requisition;

  label_requisition.height = DEFAULT_ROW_HEIGHT;
  label_requisition.width = COLUMN_MIN_WIDTH;

  requisition.height = DEFAULT_ROW_HEIGHT;
  requisition.width = COLUMN_MIN_WIDTH;


  *button_requisition = requisition;
  button_requisition->width = MAX (requisition.width, label_requisition.width);
  button_requisition->height = MAX (requisition.height, label_requisition.height);

}

static void
psppire_sheet_forall (GtkContainer *container,
		  gboolean include_internals,
		  GtkCallback callback,
		  gpointer callback_data)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (container);

  g_return_if_fail (callback != NULL);

  if (sheet->button && sheet->button->parent)
    (* callback) (sheet->button, callback_data);

  if (sheet->entry_widget && GTK_IS_CONTAINER (sheet->entry_widget))
    (* callback) (sheet->entry_widget, callback_data);
}


PsppireSheetModel *
psppire_sheet_get_model (const PsppireSheet *sheet)
{
  g_return_val_if_fail (PSPPIRE_IS_SHEET (sheet), NULL);

  return sheet->model;
}


PsppireSheetButton *
psppire_sheet_button_new (void)
{
  PsppireSheetButton *button = g_malloc (sizeof (PsppireSheetButton));

  button->state = GTK_STATE_NORMAL;
  button->label = NULL;
  button->label_visible = TRUE;
  button->justification = GTK_JUSTIFY_FILL;
  button->overstruck = FALSE;

  return button;
}


void
psppire_sheet_button_free (PsppireSheetButton *button)
{
  if (!button) return ;

  g_free (button->label);
  g_free (button);
}

static void
append_cell_text (GString *string, const PsppireSheet *sheet, gint r, gint c)
{
  gchar *celltext = psppire_sheet_cell_get_text (sheet, r, c);

  if ( NULL == celltext)
    return;

  g_string_append (string, celltext);
  g_free (celltext);
}


static GString *
range_to_text (const PsppireSheet *sheet)
{
  gint r, c;
  GString *string;

  if ( !psppire_sheet_range_isvisible (sheet, &sheet->range))
    return NULL;

  string = g_string_sized_new (80);

  for (r = sheet->range.row0; r <= sheet->range.rowi; ++r)
    {
      for (c = sheet->range.col0; c < sheet->range.coli; ++c)
	{
	  append_cell_text (string, sheet, r, c);
	  g_string_append (string, "\t");
	}
      append_cell_text (string, sheet, r, c);
      if ( r < sheet->range.rowi)
	g_string_append (string, "\n");
    }

  return string;
}

static GString *
range_to_html (const PsppireSheet *sheet)
{
  gint r, c;
  GString *string;

  if ( !psppire_sheet_range_isvisible (sheet, &sheet->range))
    return NULL;

  string = g_string_sized_new (480);

  g_string_append (string, "<html>\n");
  g_string_append (string, "<body>\n");
  g_string_append (string, "<table>\n");
  for (r = sheet->range.row0; r <= sheet->range.rowi; ++r)
    {
      g_string_append (string, "<tr>\n");
      for (c = sheet->range.col0; c <= sheet->range.coli; ++c)
	{
	  g_string_append (string, "<td>");
	  append_cell_text (string, sheet, r, c);
	  g_string_append (string, "</td>\n");
	}
      g_string_append (string, "</tr>\n");
    }
  g_string_append (string, "</table>\n");
  g_string_append (string, "</body>\n");
  g_string_append (string, "</html>\n");

  return string;
}

enum {
  SELECT_FMT_NULL,
  SELECT_FMT_TEXT,
  SELECT_FMT_HTML
};

static void
primary_get_cb (GtkClipboard     *clipboard,
		GtkSelectionData *selection_data,
		guint             info,
		gpointer          data)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (data);
  GString *string = NULL;

  switch (info)
    {
    case SELECT_FMT_TEXT:
      string = range_to_text (sheet);
      break;
    case SELECT_FMT_HTML:
      string = range_to_html (sheet);
      break;
    default:
      g_assert_not_reached ();
    }

  gtk_selection_data_set (selection_data, selection_data->target,
			  8,
			  (const guchar *) string->str, string->len);
  g_string_free (string, TRUE);
}

static void
primary_clear_cb (GtkClipboard *clipboard,
		  gpointer      data)
{
  PsppireSheet *sheet = PSPPIRE_SHEET (data);
  if ( ! GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    return;

  psppire_sheet_real_unselect_range (sheet, NULL);
}

static void
psppire_sheet_update_primary_selection (PsppireSheet *sheet)
{
  static const GtkTargetEntry targets[] = {
    { "UTF8_STRING",   0, SELECT_FMT_TEXT },
    { "STRING",        0, SELECT_FMT_TEXT },
    { "TEXT",          0, SELECT_FMT_TEXT },
    { "COMPOUND_TEXT", 0, SELECT_FMT_TEXT },
    { "text/plain;charset=utf-8", 0, SELECT_FMT_TEXT },
    { "text/plain",    0, SELECT_FMT_TEXT },
    { "text/html",     0, SELECT_FMT_HTML }
  };

  GtkClipboard *clipboard;

  if (!GTK_WIDGET_REALIZED (sheet))
    return;

  clipboard = gtk_widget_get_clipboard (GTK_WIDGET (sheet),
					GDK_SELECTION_PRIMARY);

  if (psppire_sheet_range_isvisible (sheet, &sheet->range))
    {
      if (!gtk_clipboard_set_with_owner (clipboard, targets,
					 G_N_ELEMENTS (targets),
					 primary_get_cb, primary_clear_cb,
					 G_OBJECT (sheet)))
	primary_clear_cb (clipboard, sheet);
    }
  else
    {
      if (gtk_clipboard_get_owner (clipboard) == G_OBJECT (sheet))
	gtk_clipboard_clear (clipboard);
    }
}
