/* This version of GtkSheet has been *heavily* modified, for the specific
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gtksheet
 * @short_description: spreadsheet widget for gtk2
 *
 * GtkSheet is a matrix widget for GTK+. It consists of an scrollable grid of
 * cells where you can allocate text. Cell contents can be edited interactively
 * through a specially designed entry, GtkItemEntry. It is also a container
 * subclass, allowing you to display buttons, curves, pixmaps and any other
 * widgets in it.
 *
 * You can also set many attributes as: border, foreground and background color,
 * text justification, and more.
 *
 * The testgtksheet program shows how easy is to create a spreadsheet-like GUI
 * using this widget.
 */
#include <config.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtksignal.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkadjustment.h>
#include <gtk/gtktable.h>
#include <gtk/gtkbox.h>
#include <gtk/gtkmain.h>
#include <gtk/gtktypeutils.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkcontainer.h>
#include <gtk/gtkpixmap.h>
#include <pango/pango.h>
#include "gtkitementry.h"
#include "gtksheet.h"
#include "gtkextra-marshal.h"
#include "gsheetmodel.h"

/* sheet flags */
enum
  {
    GTK_SHEET_IS_LOCKED = 1 << 0,
    GTK_SHEET_IS_FROZEN = 1 << 1,
    GTK_SHEET_IN_XDRAG = 1 << 2,
    GTK_SHEET_IN_YDRAG = 1 << 3,
    GTK_SHEET_IN_DRAG = 1 << 4,
    GTK_SHEET_IN_SELECTION = 1 << 5,
    GTK_SHEET_IN_RESIZE = 1 << 6,
    GTK_SHEET_REDRAW_PENDING = 1 << 7,
  };

#define GTK_SHEET_FLAGS(sheet) (GTK_SHEET (sheet)->flags)
#define GTK_SHEET_SET_FLAGS(sheet,flag) (GTK_SHEET_FLAGS (sheet) |= (flag))
#define GTK_SHEET_UNSET_FLAGS(sheet,flag) (GTK_SHEET_FLAGS (sheet) &= ~ (flag))

#define GTK_SHEET_IS_LOCKED(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_IS_LOCKED)


#define GTK_SHEET_IS_FROZEN(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_IS_FROZEN)
#define GTK_SHEET_IN_XDRAG(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_IN_XDRAG)
#define GTK_SHEET_IN_YDRAG(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_IN_YDRAG)
#define GTK_SHEET_IN_DRAG(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_IN_DRAG)
#define GTK_SHEET_IN_SELECTION(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_IN_SELECTION)
#define GTK_SHEET_IN_RESIZE(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_IN_RESIZE)
#define GTK_SHEET_REDRAW_PENDING(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_REDRAW_PENDING)

#define CELL_SPACING 1
#define DRAG_WIDTH 6
#define TIMEOUT_HOVER 300
#define TIME_INTERVAL 8
#define COLUMN_MIN_WIDTH 10
#define MINROWS 1
#define MINCOLS 1
#define MAXLENGTH 30
#define CELLOFFSET 4
#define DEFAULT_COLUMN_WIDTH 80


static void gtk_sheet_update_primary_selection (GtkSheet *sheet);


static void gtk_sheet_column_title_button_draw (GtkSheet *sheet, gint column);

static void gtk_sheet_row_title_button_draw (GtkSheet *sheet, gint row);


static gboolean gtk_sheet_cell_empty (const GtkSheet *sheet, gint row, gint col);

static inline
void dispose_string (const GtkSheet *sheet, gchar *text)
{
  GSheetModel *model = gtk_sheet_get_model (sheet);

  if ( ! model )
    return;

  if (g_sheet_model_free_strings (model))
    g_free (text);
}

static inline
guint DEFAULT_ROW_HEIGHT (GtkWidget *widget)
{
  if (!widget->style->font_desc) return 24;
  else
    {
      PangoContext *context = gtk_widget_get_pango_context (widget);
      PangoFontMetrics *metrics =
	pango_context_get_metrics (context,
				   widget->style->font_desc,
				   pango_context_get_language (context));
      guint val = pango_font_metrics_get_descent (metrics) +
	pango_font_metrics_get_ascent (metrics);
      pango_font_metrics_unref (metrics);
      return PANGO_PIXELS (val)+2 * CELLOFFSET;
    }
}

static inline
guint DEFAULT_FONT_ASCENT (GtkWidget *widget)
{
  if (!widget->style->font_desc) return 12;
  else
    {
      PangoContext *context = gtk_widget_get_pango_context (widget);
      PangoFontMetrics *metrics =
	pango_context_get_metrics (context,
				   widget->style->font_desc,
				   pango_context_get_language (context));
      guint val = pango_font_metrics_get_ascent (metrics);
      pango_font_metrics_unref (metrics);
      return PANGO_PIXELS (val);
    }
}

static inline
guint STRING_WIDTH (GtkWidget *widget,
		    const PangoFontDescription *font, const gchar *text)
{
  PangoRectangle rect;
  PangoLayout *layout;

  layout = gtk_widget_create_pango_layout (widget, text);
  pango_layout_set_font_description (layout, font);

  pango_layout_get_extents (layout, NULL, &rect);

  g_object_unref (G_OBJECT (layout));
  return PANGO_PIXELS (rect.width);
}

static inline
guint DEFAULT_FONT_DESCENT (GtkWidget *widget)
{
  if (!widget->style->font_desc) return 12;
  else
    {
      PangoContext *context = gtk_widget_get_pango_context (widget);
      PangoFontMetrics *metrics =
	pango_context_get_metrics (context,
				   widget->style->font_desc,
				   pango_context_get_language (context));
      guint val = pango_font_metrics_get_descent (metrics);
      pango_font_metrics_unref (metrics);
      return PANGO_PIXELS (val);
    }
}


static gint
yyy_row_is_visible (const GtkSheet *sheet, gint row)
{
  GSheetRow *row_geo = sheet->row_geometry;

  return g_sheet_row_get_visibility (row_geo, row, 0);
}


static gint
yyy_row_is_sensitive (const GtkSheet *sheet, gint row)
{
  GSheetRow *row_geo = sheet->row_geometry;

  return g_sheet_row_get_sensitivity (row_geo, row, 0);
}



static inline gint
yyy_row_count (const GtkSheet *sheet)
{
  GSheetRow *row_geo = sheet->row_geometry;

  return g_sheet_row_get_row_count (row_geo, 0);
}

static inline gint
yyy_row_height (const GtkSheet *sheet, gint row)
{
  GSheetRow *row_geo = sheet->row_geometry;

  return g_sheet_row_get_height (row_geo, row, 0);
}

static gint
yyy_row_top_ypixel (const GtkSheet *sheet, gint row)
{
  GSheetRow *geo = sheet->row_geometry;

  gint y = g_sheet_row_start_pixel (geo, row, 0);

  if ( sheet->column_titles_visible )
    y += sheet->column_title_area.height;

  return y;
}


/* Return the row containing pixel Y */
static gint
yyy_row_ypixel_to_row (const GtkSheet *sheet, gint y)
{
  GSheetRow *geo = sheet->row_geometry;

  gint cy = sheet->voffset;

  if (sheet->column_titles_visible)
    cy += sheet->column_title_area.height;

  if (y < cy) return 0;

  return g_sheet_row_pixel_to_row (geo, y - cy, 0);
}


/* gives the top pixel of the given row in context of
 * the sheet's voffset */
static inline gint
ROW_TOP_YPIXEL (const GtkSheet *sheet, gint row)
{
  return (sheet->voffset + yyy_row_top_ypixel (sheet, row));
}


/* returns the row index from a y pixel location in the
 * context of the sheet's voffset */
static inline gint
ROW_FROM_YPIXEL (const GtkSheet *sheet, gint y)
{
  return (yyy_row_ypixel_to_row (sheet, y));
}

static inline GtkSheetButton *
xxx_column_button (const GtkSheet *sheet, gint col)
{
  GSheetColumn *col_geo = sheet->column_geometry;
  if ( col < 0 ) return NULL ;

  return g_sheet_column_get_button (col_geo, col);
}


static inline gint
xxx_column_left_xpixel (const GtkSheet *sheet, gint col)
{
  GSheetColumn *geo = sheet->column_geometry;

  gint x = g_sheet_column_start_pixel (geo, col);

  if ( sheet->row_titles_visible )
    x += sheet->row_title_area.width;

  return x;
}

static inline gint
xxx_column_width (const GtkSheet *sheet, gint col)
{
  GSheetColumn *col_geo = sheet->column_geometry;

  return g_sheet_column_get_width (col_geo, col);
}


static inline void
xxx_set_column_width (GtkSheet *sheet, gint col, gint width)
{
  if ( sheet->column_geometry )
    g_sheet_column_set_width (sheet->column_geometry, col, width);
}

static inline void
xxx_column_set_left_column (GtkSheet *sheet, gint col, gint i)
{
  GSheetColumn *col_geo = sheet->column_geometry;

  g_sheet_column_set_left_text_column (col_geo, col, i);
}

static inline gint
xxx_column_left_column (const GtkSheet *sheet, gint col)
{
  GSheetColumn *col_geo = sheet->column_geometry;

  return g_sheet_column_get_left_text_column (col_geo, col);
}

static inline void
xxx_column_set_right_column (GtkSheet *sheet, gint col, gint i)
{
  GSheetColumn *col_geo = sheet->column_geometry;

  g_sheet_column_set_right_text_column (col_geo, col, i);
}

static inline gint
xxx_column_right_column (const GtkSheet *sheet, gint col)
{
  GSheetColumn *col_geo = sheet->column_geometry;

  return g_sheet_column_get_right_text_column (col_geo, col);
}

static inline GtkJustification
xxx_column_justification (const GtkSheet *sheet, gint col)
{
  GSheetColumn *col_geo = sheet->column_geometry;

  return g_sheet_column_get_justification (col_geo, col);
}

static inline gint
xxx_column_is_visible (const GtkSheet *sheet, gint col)
{
  GSheetColumn *col_geo = sheet->column_geometry;

  return g_sheet_column_get_visibility (col_geo, col);
}


static inline gint
xxx_column_is_sensitive (const GtkSheet *sheet, gint col)
{
  GSheetColumn *col_geo = sheet->column_geometry;

  return g_sheet_column_get_sensitivity (col_geo, col);
}


/* gives the left pixel of the given column in context of
 * the sheet's hoffset */
static inline gint
COLUMN_LEFT_XPIXEL (const GtkSheet *sheet, gint ncol)
{
  return (sheet->hoffset + xxx_column_left_xpixel (sheet, ncol));
}

static inline gint
xxx_column_count (const GtkSheet *sheet)
{
  GSheetColumn *col_geo = sheet->column_geometry;

  return g_sheet_column_get_column_count (col_geo);
}

/* returns the column index from a x pixel location in the
 * context of the sheet's hoffset */
static inline gint
COLUMN_FROM_XPIXEL (const GtkSheet * sheet,
		    gint x)
{
  gint i, cx;

  cx = sheet->hoffset;
  if ( sheet->row_titles_visible )
    cx += sheet->row_title_area.width;

  if (x < cx) return 0;
  for (i = 0; i < xxx_column_count (sheet); i++)
    {
      if (x >= cx && x <= (cx + xxx_column_width (sheet, i)) &&
	  xxx_column_is_visible (sheet, i))
	return i;
      if ( xxx_column_is_visible (sheet, i))
	cx += xxx_column_width (sheet, i);
    }

  /* no match */
  return xxx_column_count (sheet) - 1;
}

/* returns the total height of the sheet */
static inline gint SHEET_HEIGHT (GtkSheet *sheet)
{
  const gint n_rows = yyy_row_count (sheet);

  return yyy_row_top_ypixel (sheet, n_rows - 1) +
    yyy_row_height (sheet, n_rows - 1);
}


static inline GtkSheetButton *
yyy_row_button (GtkSheet *sheet, gint row)
{
  GSheetRow *row_geo = sheet->row_geometry;

  return g_sheet_row_get_button (row_geo, row, sheet);
}




static inline void
yyy_set_row_height (GtkSheet *sheet, gint row, gint height)
{
  if ( sheet->row_geometry )
    g_sheet_row_set_height (sheet->row_geometry, row, height, sheet);
}



/* returns the total width of the sheet */
static inline gint SHEET_WIDTH (GtkSheet *sheet)
{
  gint i, cx;

  cx = ( sheet->row_titles_visible ? sheet->row_title_area.width : 0);

  for (i = 0; i < xxx_column_count (sheet); i++)
    if (xxx_column_is_visible (sheet, i))
      cx += xxx_column_width (sheet, i);

  return cx;
}

#define MIN_VISIBLE_ROW(sheet) \
    ROW_FROM_YPIXEL (sheet, sheet->column_title_area.height + 1)

#define MAX_VISIBLE_ROW(sheet) \
    ROW_FROM_YPIXEL (sheet, sheet->sheet_window_height - 1)

#define MIN_VISIBLE_COLUMN(sheet) \
    COLUMN_FROM_XPIXEL (sheet, sheet->row_title_area.width + 1)

#define MAX_VISIBLE_COLUMN(sheet) \
    COLUMN_FROM_XPIXEL (sheet, sheet->sheet_window_width)



static inline gboolean
POSSIBLE_XDRAG (const GtkSheet *sheet, gint x, gint *drag_column)
{
  gint column, xdrag;

  column = COLUMN_FROM_XPIXEL (sheet, x);
  *drag_column = column;

  xdrag = COLUMN_LEFT_XPIXEL (sheet, column) + CELL_SPACING;
  if (x <= xdrag + DRAG_WIDTH / 2 && column != 0)
    {
      while (! xxx_column_is_visible (sheet, column - 1) && column > 0) column--;
      *drag_column = column - 1;
      return xxx_column_is_sensitive (sheet, column - 1);
    }

  xdrag += xxx_column_width (sheet, column);
  if (x >= xdrag - DRAG_WIDTH / 2 && x <= xdrag + DRAG_WIDTH / 2)
    return xxx_column_is_sensitive (sheet, column);

  return FALSE;
}

static inline gboolean
POSSIBLE_YDRAG (const GtkSheet *sheet, gint y, gint *drag_row)
{
  gint row, ydrag;
  row = ROW_FROM_YPIXEL (sheet, y);
  *drag_row = row;

  ydrag = ROW_TOP_YPIXEL (sheet, row)+CELL_SPACING;
  if (y <= ydrag + DRAG_WIDTH / 2 && row != 0)
    {
      while (!yyy_row_is_visible (sheet, row - 1) && row > 0) row--;
      *drag_row = row - 1;
      return yyy_row_is_sensitive (sheet, row - 1);
    }

  ydrag +=yyy_row_height (sheet, row);

  if (y >= ydrag - DRAG_WIDTH / 2 && y <= ydrag + DRAG_WIDTH / 2)
    return yyy_row_is_sensitive (sheet, row);


  return FALSE;
}

static inline gboolean
POSSIBLE_DRAG (const GtkSheet *sheet, gint x, gint y,
	       gint *drag_row, gint *drag_column)
{
  gint ydrag, xdrag;

  /* Can't drag if nothing is selected */
  if ( sheet->range.row0 < 0 || sheet->range.rowi < 0 ||
       sheet->range.col0 < 0 || sheet->range.coli < 0 )
    return FALSE;

  *drag_column = COLUMN_FROM_XPIXEL (sheet, x);
  *drag_row = ROW_FROM_YPIXEL (sheet, y);

  if (x >= COLUMN_LEFT_XPIXEL (sheet, sheet->range.col0) - DRAG_WIDTH / 2 &&
      x <= COLUMN_LEFT_XPIXEL (sheet, sheet->range.coli) +
      xxx_column_width (sheet, sheet->range.coli) + DRAG_WIDTH / 2)
    {
      ydrag = ROW_TOP_YPIXEL (sheet, sheet->range.row0);
      if (y >= ydrag - DRAG_WIDTH / 2 && y <= ydrag + DRAG_WIDTH / 2)
	{
	  *drag_row = sheet->range.row0;
	  return TRUE;
	}
      ydrag = ROW_TOP_YPIXEL (sheet, sheet->range.rowi) +
	yyy_row_height (sheet, sheet->range.rowi);
      if (y >= ydrag - DRAG_WIDTH / 2 && y <= ydrag + DRAG_WIDTH / 2)
	{
	  *drag_row = sheet->range.rowi;
	  return TRUE;
	}
    }

  if (y >= ROW_TOP_YPIXEL (sheet, sheet->range.row0) - DRAG_WIDTH / 2 &&
      y <= ROW_TOP_YPIXEL (sheet, sheet->range.rowi) +
      yyy_row_height (sheet, sheet->range.rowi) + DRAG_WIDTH / 2)
    {
      xdrag = COLUMN_LEFT_XPIXEL (sheet, sheet->range.col0);
      if (x >= xdrag - DRAG_WIDTH / 2 && x <= xdrag + DRAG_WIDTH / 2)
	{
	  *drag_column = sheet->range.col0;
	  return TRUE;
	}
      xdrag = COLUMN_LEFT_XPIXEL (sheet, sheet->range.coli) +
	xxx_column_width (sheet, sheet->range.coli);
      if (x >= xdrag - DRAG_WIDTH / 2 && x <= xdrag + DRAG_WIDTH / 2)
	{
	  *drag_column = sheet->range.coli;
	  return TRUE;
	}
    }

  return FALSE;
}

static inline gboolean
POSSIBLE_RESIZE (const GtkSheet *sheet, gint x, gint y,
		 gint *drag_row, gint *drag_column)
{
  gint xdrag, ydrag;

  /* Can't drag if nothing is selected */
  if ( sheet->range.row0 < 0 || sheet->range.rowi < 0 ||
       sheet->range.col0 < 0 || sheet->range.coli < 0 )
    return FALSE;

  xdrag = COLUMN_LEFT_XPIXEL (sheet, sheet->range.coli)+
    xxx_column_width (sheet, sheet->range.coli);

  ydrag = ROW_TOP_YPIXEL (sheet, sheet->range.rowi)+
    yyy_row_height (sheet, sheet->range.rowi);

  if (sheet->state == GTK_SHEET_COLUMN_SELECTED)
    ydrag = ROW_TOP_YPIXEL (sheet, MIN_VISIBLE_ROW (sheet));

  if (sheet->state == GTK_SHEET_ROW_SELECTED)
    xdrag = COLUMN_LEFT_XPIXEL (sheet, MIN_VISIBLE_COLUMN (sheet));

  *drag_column = COLUMN_FROM_XPIXEL (sheet, x);
  *drag_row = ROW_FROM_YPIXEL (sheet, y);

  if (x >= xdrag - DRAG_WIDTH / 2 && x <= xdrag + DRAG_WIDTH / 2 &&
      y >= ydrag - DRAG_WIDTH / 2 && y <= ydrag + DRAG_WIDTH / 2) return TRUE;

  return FALSE;
}

static void gtk_sheet_class_init 		 (GtkSheetClass * klass);
static void gtk_sheet_init 			 (GtkSheet * sheet);
static void gtk_sheet_destroy 			 (GtkObject * object);
static void gtk_sheet_finalize 			 (GObject * object);
static void gtk_sheet_style_set 		 (GtkWidget *widget,
						  GtkStyle *previous_style);
static void gtk_sheet_realize 			 (GtkWidget * widget);
static void gtk_sheet_unrealize 		 (GtkWidget * widget);
static void gtk_sheet_map 			 (GtkWidget * widget);
static void gtk_sheet_unmap 			 (GtkWidget * widget);
static gint gtk_sheet_expose 			 (GtkWidget * widget,
						  GdkEventExpose * event);
static void gtk_sheet_forall 			 (GtkContainer *container,
						  gboolean include_internals,
						  GtkCallback callback,
						  gpointer callback_data);

static void gtk_sheet_set_scroll_adjustments	 (GtkSheet *sheet,
						  GtkAdjustment *hadjustment,
						  GtkAdjustment *vadjustment);

static gint gtk_sheet_button_press 		 (GtkWidget * widget,
						  GdkEventButton * event);
static gint gtk_sheet_button_release 		 (GtkWidget * widget,
						  GdkEventButton * event);
static gint gtk_sheet_motion 			 (GtkWidget * widget,
						  GdkEventMotion * event);
static gint gtk_sheet_entry_key_press		 (GtkWidget *widget,
						  GdkEventKey *key);
static gint gtk_sheet_key_press			 (GtkWidget *widget,
						  GdkEventKey *key);
static void gtk_sheet_size_request 		 (GtkWidget * widget,
						  GtkRequisition * requisition);
static void gtk_sheet_size_allocate 		 (GtkWidget * widget,
						  GtkAllocation * allocation);

/* Sheet queries */

static gboolean gtk_sheet_range_isvisible (const GtkSheet * sheet,
					   GtkSheetRange range);
static gboolean gtk_sheet_cell_isvisible  (GtkSheet * sheet,
					   gint row, gint column);
/* Drawing Routines */

/* draw cell background and frame */
static void gtk_sheet_cell_draw_default 	 (GtkSheet *sheet,
						  gint row, gint column);

/* draw cell contents */
static void gtk_sheet_cell_draw_label 		 (GtkSheet *sheet,
						  gint row, gint column);

/* draw visible part of range. If range == NULL then draw the whole screen */
static void gtk_sheet_range_draw		 (GtkSheet *sheet,
						  const GtkSheetRange *range);

/* highlight the visible part of the selected range */
static void gtk_sheet_range_draw_selection	 (GtkSheet *sheet,
						  GtkSheetRange range);

/* Selection */

static gboolean gtk_sheet_move_query   		 (GtkSheet *sheet,
						  gint row, gint column);
static void gtk_sheet_real_select_range 	 (GtkSheet * sheet,
						  const GtkSheetRange * range);
static void gtk_sheet_real_unselect_range 	 (GtkSheet * sheet,
						  const GtkSheetRange * range);
static void gtk_sheet_extend_selection		 (GtkSheet *sheet,
						  gint row, gint column);
static void gtk_sheet_new_selection		 (GtkSheet *sheet,
						  GtkSheetRange *range);
static void gtk_sheet_draw_border 		 (GtkSheet *sheet,
						  GtkSheetRange range);
static void gtk_sheet_draw_corners		 (GtkSheet *sheet,
						  GtkSheetRange range);


/* Active Cell handling */

static void gtk_sheet_entry_changed		 (GtkWidget *widget,
						  gpointer data);
static gboolean gtk_sheet_deactivate_cell	 (GtkSheet *sheet);
static void gtk_sheet_hide_active_cell		 (GtkSheet *sheet);
static gboolean gtk_sheet_activate_cell		 (GtkSheet *sheet,
						  gint row, gint col);
static void gtk_sheet_draw_active_cell		 (GtkSheet *sheet);
static void gtk_sheet_show_active_cell		 (GtkSheet *sheet);
static void gtk_sheet_click_cell		 (GtkSheet *sheet,
						  gint row,
						  gint column,
						  gboolean *veto);

/* Backing Pixmap */

static void gtk_sheet_make_backing_pixmap 	 (GtkSheet *sheet,
						  guint width, guint height);
static void gtk_sheet_draw_backing_pixmap	 (GtkSheet *sheet,
						  GtkSheetRange range);
/* Scrollbars */

static void adjust_scrollbars 			 (GtkSheet * sheet);
static void vadjustment_value_changed 		 (GtkAdjustment * adjustment,
						  gpointer data);
static void hadjustment_value_changed 		 (GtkAdjustment * adjustment,
						  gpointer data);


static void draw_xor_vline 			 (GtkSheet * sheet);
static void draw_xor_hline 			 (GtkSheet * sheet);
static void draw_xor_rectangle			 (GtkSheet *sheet,
						  GtkSheetRange range);

static guint new_column_width 			 (GtkSheet * sheet,
						  gint column,
						  gint * x);
static guint new_row_height 			 (GtkSheet * sheet,
						  gint row,
						  gint * y);
/* Sheet Button */

static void create_global_button		 (GtkSheet *sheet);
static void global_button_clicked		 (GtkWidget *widget,
						  gpointer data);
/* Sheet Entry */

static void create_sheet_entry			 (GtkSheet *sheet);
static void gtk_sheet_size_allocate_entry	 (GtkSheet *sheet);
static void gtk_sheet_entry_set_max_size	 (GtkSheet *sheet);

/* Sheet button gadgets */

static void size_allocate_column_title_buttons 	 (GtkSheet * sheet);
static void size_allocate_row_title_buttons 	 (GtkSheet * sheet);


static void size_allocate_global_button 	 (GtkSheet *sheet);
static void gtk_sheet_button_size_request	 (GtkSheet *sheet,
						  const GtkSheetButton *button,
						  GtkRequisition *requisition);

/* Attributes routines */
static void init_attributes			 (const GtkSheet *sheet, gint col,
						  GtkSheetCellAttr *attributes);


/* Memory allocation routines */
static void gtk_sheet_real_range_clear 		 (GtkSheet *sheet,
						  const GtkSheetRange *range);

static void gtk_sheet_real_cell_clear 		 (GtkSheet *sheet,
						  gint row,
						  gint column);


/* Container Functions */
static void gtk_sheet_remove			 (GtkContainer *container,
						  GtkWidget *widget);
static void gtk_sheet_realize_child		 (GtkSheet *sheet,
						  GtkSheetChild *child);
static void gtk_sheet_position_child		 (GtkSheet *sheet,
						  GtkSheetChild *child);
static void gtk_sheet_position_children		 (GtkSheet *sheet);
static void gtk_sheet_child_show		 (GtkSheetChild *child);
static void gtk_sheet_child_hide		 (GtkSheetChild *child);
static void gtk_sheet_column_size_request (GtkSheet *sheet,
					   gint col,
					   guint *requisition);
static void gtk_sheet_row_size_request (GtkSheet *sheet,
					gint row,
					guint *requisition);


/* Signals */

extern void
_gtkextra_signal_emit (GtkObject *object, guint signal_id, ...);

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
    DEACTIVATE,
    ACTIVATE,
    SET_CELL,
    CLEAR_CELL,
    CHANGED,
    NEW_COL_WIDTH,
    NEW_ROW_HEIGHT,
    LAST_SIGNAL
  };

static GtkContainerClass *parent_class = NULL;
static guint sheet_signals[LAST_SIGNAL] = { 0 };


GType
gtk_sheet_get_type ()
{
  static GType sheet_type = 0;

  if (!sheet_type)
    {
      static const GTypeInfo sheet_info =
	{
	  sizeof (GtkSheetClass),
	  NULL,
	  NULL,
	  (GClassInitFunc) gtk_sheet_class_init,
	  NULL,
	  NULL,
	  sizeof (GtkSheet),
	  0,
	  (GInstanceInitFunc) gtk_sheet_init,
	  NULL,
	};
      sheet_type =
	g_type_register_static (GTK_TYPE_CONTAINER, "GtkSheet",
				&sheet_info, 0);
    }
  return sheet_type;
}

static GtkSheetRange*
gtk_sheet_range_copy (const GtkSheetRange *range)
{
  GtkSheetRange *new_range;

  g_return_val_if_fail (range != NULL, NULL);

  new_range = g_new (GtkSheetRange, 1);

  *new_range = *range;

  return new_range;
}

static void
gtk_sheet_range_free (GtkSheetRange *range)
{
  g_return_if_fail (range != NULL);

  g_free (range);
}

GType
gtk_sheet_range_get_type (void)
{
  static GType sheet_range_type = 0;

  if (!sheet_range_type)
    {
      sheet_range_type =
	g_boxed_type_register_static ("GtkSheetRange",
				      (GBoxedCopyFunc) gtk_sheet_range_copy,
				      (GBoxedFreeFunc) gtk_sheet_range_free);
    }

  return sheet_range_type;
}

static void
gtk_sheet_class_init (GtkSheetClass * klass)
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;
  GtkContainerClass *container_class;
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  object_class = (GtkObjectClass *) klass;
  widget_class = (GtkWidgetClass *) klass;
  container_class = (GtkContainerClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  /**
   * GtkSheet::select-row
   * @sheet: the sheet widget that emitted the signal
   * @row: the newly selected row index
   *
   * A row has been selected.
   */
  sheet_signals[SELECT_ROW] =
    g_signal_new ("select-row",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, select_row),
		  NULL, NULL,
		  gtkextra_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  /**
   * GtkSheet::select - column
   * @sheet: the sheet widget that emitted the signal
   * @column: the newly selected column index
   *
   * A column has been selected.
   */
  sheet_signals[SELECT_COLUMN] =
    g_signal_new ("select-column",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, select_column),
		  NULL, NULL,
		  gtkextra_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  /**
   * GtkSheet::double-click-row
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
		  gtkextra_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  /**
   * GtkSheet::double-click-column
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
		  gtkextra_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  /**
   * GtkSheet::button-event-column
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
		  gtkextra_VOID__INT_POINTER,
		  G_TYPE_NONE,
		  2,
		  G_TYPE_INT,
		  G_TYPE_POINTER
		  );


  /**
   * GtkSheet::button-event-row
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
		  gtkextra_VOID__INT_POINTER,
		  G_TYPE_NONE,
		  2,
		  G_TYPE_INT,
		  G_TYPE_POINTER
		  );


  sheet_signals[SELECT_RANGE] =
    g_signal_new ("select-range",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, select_range),
		  NULL, NULL,
		  gtkextra_VOID__BOXED,
		  G_TYPE_NONE,
		  1,
		  GTK_TYPE_SHEET_RANGE);


  sheet_signals[RESIZE_RANGE] =
    g_signal_new ("resize-range",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, resize_range),
		  NULL, NULL,
		  gtkextra_VOID__BOXED_BOXED,
		  G_TYPE_NONE,
		  2,
		  GTK_TYPE_SHEET_RANGE, GTK_TYPE_SHEET_RANGE
		  );

  sheet_signals[MOVE_RANGE] =
    g_signal_new ("move-range",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, move_range),
		  NULL, NULL,
		  gtkextra_VOID__BOXED_BOXED,
		  G_TYPE_NONE,
		  2,
		  GTK_TYPE_SHEET_RANGE, GTK_TYPE_SHEET_RANGE
		  );

  sheet_signals[TRAVERSE] =
    g_signal_new ("traverse",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, traverse),
		  NULL, NULL,
		  gtkextra_BOOLEAN__INT_INT_POINTER_POINTER,
		  G_TYPE_BOOLEAN, 4, G_TYPE_INT, G_TYPE_INT,
		  G_TYPE_POINTER, G_TYPE_POINTER);


  sheet_signals[DEACTIVATE] =
    g_signal_new ("deactivate",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, deactivate),
		  NULL, NULL,
		  gtkextra_BOOLEAN__INT_INT,
		  G_TYPE_BOOLEAN, 2, G_TYPE_INT, G_TYPE_INT);

  sheet_signals[ACTIVATE] =
    g_signal_new ("activate",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, activate),
		  NULL, NULL,
		  gtkextra_BOOLEAN__INT_INT,
		  G_TYPE_BOOLEAN, 2, G_TYPE_INT, G_TYPE_INT);

  sheet_signals[SET_CELL] =
    g_signal_new ("set-cell",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, set_cell),
		  NULL, NULL,
		  gtkextra_VOID__INT_INT,
		  G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);


  sheet_signals[CLEAR_CELL] =
    g_signal_new ("clear-cell",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, clear_cell),
		  NULL, NULL,
		  gtkextra_VOID__INT_INT,
		  G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

  sheet_signals[CHANGED] =
    g_signal_new ("changed",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, changed),
		  NULL, NULL,
		  gtkextra_VOID__INT_INT,
		  G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

  sheet_signals[NEW_COL_WIDTH] =
    g_signal_new ("new-column-width",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, new_column_width), /*!!!! */
		  NULL, NULL,
		  gtkextra_VOID__INT_INT,
		  G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

  sheet_signals[NEW_ROW_HEIGHT] =
    g_signal_new ("new-row-height",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, new_row_height), /*!!!! */
		  NULL, NULL,
		  gtkextra_VOID__INT_INT,
		  G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

  widget_class->set_scroll_adjustments_signal =
    g_signal_new ("set-scroll-adjustments",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, set_scroll_adjustments),
		  NULL, NULL,
		  gtkextra_VOID__OBJECT_OBJECT,
		  G_TYPE_NONE, 2, GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);


  container_class->add = NULL;
  container_class->remove = gtk_sheet_remove;
  container_class->forall = gtk_sheet_forall;

  object_class->destroy = gtk_sheet_destroy;
  gobject_class->finalize = gtk_sheet_finalize;

  widget_class->realize = gtk_sheet_realize;
  widget_class->unrealize = gtk_sheet_unrealize;
  widget_class->map = gtk_sheet_map;
  widget_class->unmap = gtk_sheet_unmap;
  widget_class->style_set = gtk_sheet_style_set;
  widget_class->button_press_event = gtk_sheet_button_press;
  widget_class->button_release_event = gtk_sheet_button_release;
  widget_class->motion_notify_event = gtk_sheet_motion;
  widget_class->key_press_event = gtk_sheet_key_press;
  widget_class->expose_event = gtk_sheet_expose;
  widget_class->size_request = gtk_sheet_size_request;
  widget_class->size_allocate = gtk_sheet_size_allocate;
  widget_class->focus_in_event = NULL;
  widget_class->focus_out_event = NULL;

  klass->set_scroll_adjustments = gtk_sheet_set_scroll_adjustments;
  klass->select_row = NULL;
  klass->select_column = NULL;
  klass->select_range = NULL;
  klass->resize_range = NULL;
  klass->move_range = NULL;
  klass->traverse = NULL;
  klass->deactivate = NULL;
  klass->activate = NULL;
  klass->set_cell = NULL;
  klass->clear_cell = NULL;
  klass->changed = NULL;
}

static void
gtk_sheet_init (GtkSheet *sheet)
{
  sheet->column_geometry = NULL;
  sheet->row_geometry = NULL;

  sheet->children = NULL;

  sheet->flags = 0;
  sheet->selection_mode = GTK_SELECTION_NONE;
  sheet->freeze_count = 0;
  sheet->state = GTK_SHEET_NORMAL;

  GTK_WIDGET_UNSET_FLAGS (sheet, GTK_NO_WINDOW);
  GTK_WIDGET_SET_FLAGS (sheet, GTK_CAN_FOCUS);

  sheet->column_title_window = NULL;
  sheet->column_title_area.x = 0;
  sheet->column_title_area.y = 0;
  sheet->column_title_area.width = 0;
  sheet->column_title_area.height = DEFAULT_ROW_HEIGHT (GTK_WIDGET (sheet));

  sheet->row_title_window = NULL;
  sheet->row_title_area.x = 0;
  sheet->row_title_area.y = 0;
  sheet->row_title_area.width = DEFAULT_COLUMN_WIDTH;
  sheet->row_title_area.height = 0;


  sheet->active_cell.row = 0;
  sheet->active_cell.col = 0;
  sheet->selection_cell.row = 0;
  sheet->selection_cell.col = 0;

  sheet->sheet_entry = NULL;
  sheet->pixmap = NULL;

  sheet->range.row0 = 0;
  sheet->range.rowi = 0;
  sheet->range.col0 = 0;
  sheet->range.coli = 0;

  sheet->state = GTK_SHEET_NORMAL;

  sheet->sheet_window = NULL;
  sheet->sheet_window_width = 0;
  sheet->sheet_window_height = 0;
  sheet->sheet_entry = NULL;
  sheet->button = NULL;

  sheet->hoffset = 0;
  sheet->voffset = 0;

  sheet->hadjustment = NULL;
  sheet->vadjustment = NULL;

  sheet->cursor_drag = gdk_cursor_new (GDK_PLUS);
  sheet->xor_gc = NULL;
  sheet->fg_gc = NULL;
  sheet->bg_gc = NULL;
  sheet->x_drag = 0;
  sheet->y_drag = 0;

  gdk_color_parse ("white", &sheet->bg_color);
  gdk_color_alloc (gdk_colormap_get_system (), &sheet->bg_color);
  gdk_color_parse ("gray", &sheet->grid_color);
  gdk_color_alloc (gdk_colormap_get_system (), &sheet->grid_color);
  sheet->show_grid = TRUE;

  sheet->motion_timer = 0;
}


/* Callback which occurs whenever columns are inserted / deleted in the model */
static void
columns_inserted_deleted_callback (GSheetModel *model, gint first_column,
				   gint n_columns,
				   gpointer data)
{
  gint i;
  GtkSheet *sheet = GTK_SHEET (data);

  GtkSheetRange range;
  gint model_columns = g_sheet_model_get_column_count (model);


  /* Need to update all the columns starting from the first column and onwards.
   * Previous column are unchanged, so don't need to be updated.
   */
  range.col0 = first_column;
  range.row0 = 0;
  range.coli = xxx_column_count (sheet) - 1;
  range.rowi = yyy_row_count (sheet) - 1;

  adjust_scrollbars (sheet);

  if (sheet->active_cell.col >= model_columns)
    gtk_sheet_activate_cell (sheet, sheet->active_cell.row, model_columns - 1);

  for (i = first_column; i <= MAX_VISIBLE_COLUMN (sheet); i++)
    gtk_sheet_column_title_button_draw (sheet, i);

  gtk_sheet_range_draw (sheet, &range);
}


/* Callback which occurs whenever rows are inserted / deleted in the model */
static void
rows_inserted_deleted_callback (GSheetModel *model, gint first_row,
				gint n_rows, gpointer data)
{
  gint i;
  GtkSheet *sheet = GTK_SHEET (data);

  GtkSheetRange range;

  gint model_rows = g_sheet_model_get_row_count (model);

  /* Need to update all the rows starting from the first row and onwards.
   * Previous rows are unchanged, so don't need to be updated.
   */
  range.row0 = first_row;
  range.col0 = 0;
  range.rowi = yyy_row_count (sheet) - 1;
  range.coli = xxx_column_count (sheet) - 1;

  adjust_scrollbars (sheet);

  if (sheet->active_cell.row >= model_rows)
    gtk_sheet_activate_cell (sheet, model_rows - 1, sheet->active_cell.col);

  for (i = first_row; i <= MAX_VISIBLE_ROW (sheet); i++)
    gtk_sheet_row_title_button_draw (sheet, i);

  gtk_sheet_range_draw (sheet, &range);
}

/*
  If row0 or rowi are negative, then all rows will be updated.
  If col0 or coli are negative, then all columns will be updated.
*/
static void
range_update_callback (GSheetModel *m, gint row0, gint col0,
		       gint rowi, gint coli, gpointer data)
{
  GtkSheet *sheet = GTK_SHEET (data);

  GtkSheetRange range;

  range.row0 = row0;
  range.col0 = col0;
  range.rowi = rowi;
  range.coli = coli;

  if ( ( row0 < 0 && col0 < 0 ) || ( rowi < 0 && coli < 0 ) )
    {
      gint i;
      gtk_sheet_range_draw (sheet, NULL);
      adjust_scrollbars (sheet);

      for (i = MIN_VISIBLE_ROW (sheet); i <= MAX_VISIBLE_ROW (sheet); i++)
	gtk_sheet_row_title_button_draw (sheet, i);

      for (i = MIN_VISIBLE_COLUMN (sheet);
	   i <= MAX_VISIBLE_COLUMN (sheet); i++)
	gtk_sheet_column_title_button_draw (sheet, i);

      return;
    }
  else if ( row0 < 0 || rowi < 0 )
    {
      range.row0 = MIN_VISIBLE_ROW (sheet);
      range.rowi = MAX_VISIBLE_ROW (sheet);
    }
  else if ( col0 < 0 || coli < 0 )
    {
      range.col0 = MIN_VISIBLE_COLUMN (sheet);
      range.coli = MAX_VISIBLE_COLUMN (sheet);
    }

  gtk_sheet_range_draw (sheet, &range);
}


static void gtk_sheet_construct	 (GtkSheet *sheet,
				  GSheetRow *vgeo,
				  GSheetColumn *hgeo,
				  const gchar *title);


/**
 * gtk_sheet_new:
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
gtk_sheet_new (GSheetRow *vgeo, GSheetColumn *hgeo, const gchar *title,
	       GSheetModel *model)
{
  GtkWidget *widget = g_object_new (GTK_TYPE_SHEET, NULL);

  gtk_sheet_construct (GTK_SHEET (widget), vgeo, hgeo, title);

  if (model)
    gtk_sheet_set_model (GTK_SHEET (widget), model);


  return widget;
}


/**
 * gtk_sheet_set_model
 * @sheet: the sheet to set the model for
 * @model: the model to use for the sheet data
 *
 * Sets the model for a GtkSheet
 *
 */
void
gtk_sheet_set_model (GtkSheet *sheet, GSheetModel *model)
{
  g_return_if_fail (GTK_IS_SHEET (sheet));
  g_return_if_fail (G_IS_SHEET_MODEL (model));

  sheet->model = model;

  g_signal_connect (model, "range_changed",
		    G_CALLBACK (range_update_callback), sheet);

  g_signal_connect (model, "rows_inserted",
		    G_CALLBACK (rows_inserted_deleted_callback), sheet);

  g_signal_connect (model, "rows_deleted",
		    G_CALLBACK (rows_inserted_deleted_callback), sheet);

  g_signal_connect (model, "columns_inserted",
		    G_CALLBACK (columns_inserted_deleted_callback), sheet);

  g_signal_connect (model, "columns_deleted",
		    G_CALLBACK (columns_inserted_deleted_callback), sheet);

}


/* Call back for when the column titles have changed.
   FIRST is the first column changed.
   N_COLUMNS is the number of columns which have changed, or - 1, which
   indicates that the column has changed to its right - most extremity
*/
static void
column_titles_changed (GtkWidget *w, gint first, gint n_columns, gpointer data)
{
  GtkSheet *sheet = GTK_SHEET (data);
  gboolean extremity = FALSE;

  if ( n_columns == -1 )
    {
      extremity = TRUE;
      n_columns = xxx_column_count (sheet) - 1 ;
    }

  if (!GTK_SHEET_IS_FROZEN (sheet))
    {
      gint i;
      for ( i = first ; i <= first + n_columns ; ++i )
	{
	  gtk_sheet_column_title_button_draw (sheet, i);
	  g_signal_emit (G_OBJECT (sheet), sheet_signals[CHANGED], 0, -1, i);
	}
    }

  if ( extremity)
    gtk_sheet_column_title_button_draw (sheet, -1);

}

static void
gtk_sheet_construct (GtkSheet *sheet,
		     GSheetRow *vgeo,
		     GSheetColumn *hgeo,
		     const gchar *title)
{
  g_return_if_fail (G_IS_SHEET_COLUMN (hgeo));
  g_return_if_fail (G_IS_SHEET_ROW (vgeo));

  sheet->column_geometry = hgeo;
  sheet->row_geometry = vgeo;


  sheet->columns_resizable = TRUE;
  sheet->rows_resizable = TRUE;

  sheet->row_titles_visible = TRUE;
  sheet->row_title_area.width = DEFAULT_COLUMN_WIDTH;

  sheet->column_titles_visible = TRUE;
  sheet->autoscroll = TRUE;
  sheet->justify_entry = TRUE;


  /* create sheet entry */
  sheet->entry_type = 0;
  create_sheet_entry (sheet);

  /* create global selection button */
  create_global_button (sheet);

  if (title)
    sheet->name = g_strdup (title);

  g_signal_connect (sheet->column_geometry, "columns_changed",
		    G_CALLBACK (column_titles_changed), sheet);

}


GtkWidget *
gtk_sheet_new_with_custom_entry (GSheetRow *rows, GSheetColumn *columns,
				 const gchar *title, GtkType entry_type)
{
  GtkWidget *widget = g_object_new (GTK_TYPE_SHEET, NULL);

  gtk_sheet_construct_with_custom_entry (GTK_SHEET (widget),
					 rows, columns, title, entry_type);

  return widget;
}

void
gtk_sheet_construct_with_custom_entry (GtkSheet *sheet,
				       GSheetRow *vgeo,
				       GSheetColumn *hgeo,
				       const gchar *title,
				       GtkType entry_type)
{
  gtk_sheet_construct (sheet, vgeo, hgeo, title);

  sheet->entry_type = entry_type;
  create_sheet_entry (sheet);
}



void
gtk_sheet_change_entry (GtkSheet *sheet, GtkType entry_type)
{
  gint state;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  state = sheet->state;

  if (sheet->state == GTK_SHEET_NORMAL)
    gtk_sheet_hide_active_cell (sheet);

  sheet->entry_type = entry_type;

  create_sheet_entry (sheet);

  if (state == GTK_SHEET_NORMAL)
    {
      gtk_sheet_show_active_cell (sheet);
      g_signal_connect (G_OBJECT (gtk_sheet_get_entry (sheet)),
			"changed",
			G_CALLBACK (gtk_sheet_entry_changed),
			sheet);
    }
}

void
gtk_sheet_show_grid (GtkSheet *sheet, gboolean show)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (show == sheet->show_grid) return;

  sheet->show_grid = show;

  if (!GTK_SHEET_IS_FROZEN (sheet))
    gtk_sheet_range_draw (sheet, NULL);
}

gboolean
gtk_sheet_grid_visible (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), 0);

  return sheet->show_grid;
}

void
gtk_sheet_set_background (GtkSheet *sheet, GdkColor *color)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (!color)
    {
      gdk_color_parse ("white", &sheet->bg_color);
      gdk_color_alloc (gdk_colormap_get_system (), &sheet->bg_color);
    }
  else
    sheet->bg_color = *color;

  if (!GTK_SHEET_IS_FROZEN (sheet))
    gtk_sheet_range_draw (sheet, NULL);
}

void
gtk_sheet_set_grid (GtkSheet *sheet, GdkColor *color)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (!color)
    {
      gdk_color_parse ("black", &sheet->grid_color);
      gdk_color_alloc (gdk_colormap_get_system (), &sheet->grid_color);
    }
  else
    sheet->grid_color = *color;

  if (!GTK_SHEET_IS_FROZEN (sheet))
    gtk_sheet_range_draw (sheet, NULL);
}

guint
gtk_sheet_get_columns_count (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), 0);

  return xxx_column_count (sheet);
}

guint
gtk_sheet_get_rows_count (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), 0);

  return yyy_row_count (sheet);
}

gint
gtk_sheet_get_state (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), 0);

  return (sheet->state);
}

void
gtk_sheet_set_selection_mode (GtkSheet *sheet, gint mode)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (GTK_WIDGET_REALIZED (sheet))
    gtk_sheet_real_unselect_range (sheet, NULL);

  sheet->selection_mode = mode;
}

void
gtk_sheet_set_autoresize (GtkSheet *sheet, gboolean autoresize)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  sheet->autoresize = autoresize;
}

gboolean
gtk_sheet_autoresize (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);

  return sheet->autoresize;
}

static void
gtk_sheet_set_column_width (GtkSheet * sheet,
			    gint column,
			    guint width);


static void
gtk_sheet_autoresize_column (GtkSheet *sheet, gint column)
{
  gint text_width = 0;
  gint row;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));
  if (column >= xxx_column_count (sheet) || column < 0) return;

  for (row = 0; row < yyy_row_count (sheet); row++)
    {
      gchar *text = gtk_sheet_cell_get_text (sheet, row, column);
      if (text && strlen (text) > 0)
	{
	  GtkSheetCellAttr attributes;

	  gtk_sheet_get_attributes (sheet, row, column, &attributes);
	  if (attributes.is_visible)
	    {
	      gint width = STRING_WIDTH (GTK_WIDGET (sheet),
					 attributes.font_desc,
					 text)
		+ 2 * CELLOFFSET + attributes.border.width;
	      text_width = MAX (text_width, width);
	    }
	}
      dispose_string (sheet, text);
    }

  if (text_width > xxx_column_width (sheet, column) )
    {
      gtk_sheet_set_column_width (sheet, column, text_width);
      GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_REDRAW_PENDING);
    }
}


void
gtk_sheet_set_autoscroll (GtkSheet *sheet, gboolean autoscroll)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  sheet->autoscroll = autoscroll;
}

gboolean
gtk_sheet_autoscroll (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);

  return sheet->autoscroll;
}


void
gtk_sheet_set_justify_entry (GtkSheet *sheet, gboolean justify)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  sheet->justify_entry = justify;
}

gboolean
gtk_sheet_justify_entry (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);

  return sheet->justify_entry;
}

void
gtk_sheet_set_locked (GtkSheet *sheet, gboolean locked)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if ( locked )
    {
      GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IS_LOCKED);
      gtk_widget_hide (sheet->sheet_entry);
      gtk_widget_unmap (sheet->sheet_entry);
    }
  else
    {
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IS_LOCKED);
      if (GTK_WIDGET_MAPPED (GTK_WIDGET (sheet)))
	{
	  gtk_widget_show (sheet->sheet_entry);
	  gtk_widget_map (sheet->sheet_entry);
	}
    }

  gtk_editable_set_editable (GTK_EDITABLE (sheet->sheet_entry), locked);

}

gboolean
gtk_sheet_locked (const GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);

  return GTK_SHEET_IS_LOCKED (sheet);
}

/* This routine has problems with gtk+- 1.2 related with the
   label / button drawing - I think it's a bug in gtk+- 1.2 */
void
gtk_sheet_set_title (GtkSheet *sheet, const gchar *title)
{
  GtkWidget *label;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (title != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (sheet->name)
    g_free (sheet->name);

  sheet->name = g_strdup (title);

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)) || !title) return;

  if (GTK_BIN (sheet->button)->child)
    label = GTK_BIN (sheet->button)->child;

  size_allocate_global_button (sheet);
}

void
gtk_sheet_freeze (GtkSheet *sheet)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  sheet->freeze_count++;
  GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IS_FROZEN);
}

void
gtk_sheet_thaw (GtkSheet *sheet)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (sheet->freeze_count == 0) return;

  sheet->freeze_count--;
  if (sheet->freeze_count > 0) return;

  adjust_scrollbars (sheet);

  GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IS_FROZEN);

  sheet->old_vadjustment = -1.;
  sheet->old_hadjustment = -1.;

  if (sheet->hadjustment)
    g_signal_emit_by_name (G_OBJECT (sheet->hadjustment),
			   "value_changed");
  if (sheet->vadjustment)
    g_signal_emit_by_name (G_OBJECT (sheet->vadjustment),
			   "value_changed");

  if (sheet->state == GTK_STATE_NORMAL)
    if (sheet->sheet_entry && GTK_WIDGET_MAPPED (sheet->sheet_entry))
      {
	gtk_sheet_activate_cell (sheet, sheet->active_cell.row,
				 sheet->active_cell.col);
      }

}

void
gtk_sheet_set_row_titles_width (GtkSheet *sheet, guint width)
{
  if (width < COLUMN_MIN_WIDTH) return;

  sheet->row_title_area.width = width;

  adjust_scrollbars (sheet);

  sheet->old_hadjustment = -1.;
  if (sheet->hadjustment)
    g_signal_emit_by_name (G_OBJECT (sheet->hadjustment),
			   "value_changed");
  size_allocate_global_button (sheet);
}

void
gtk_sheet_set_column_titles_height (GtkSheet *sheet, guint height)
{
  if (height < DEFAULT_ROW_HEIGHT (GTK_WIDGET (sheet))) return;

  sheet->column_title_area.height = height;

  adjust_scrollbars (sheet);

  sheet->old_vadjustment = -1.;
  if (sheet->vadjustment)
    g_signal_emit_by_name (G_OBJECT (sheet->vadjustment),
			   "value_changed");
  size_allocate_global_button (sheet);
}

void
gtk_sheet_show_column_titles (GtkSheet *sheet)
{
  gint col;

  if (sheet->column_titles_visible) return;

  sheet->column_titles_visible = TRUE;


  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      gdk_window_show (sheet->column_title_window);
      gdk_window_move_resize (sheet->column_title_window,
			      sheet->column_title_area.x,
			      sheet->column_title_area.y,
			      sheet->column_title_area.width,
			      sheet->column_title_area.height);

      for (col = MIN_VISIBLE_COLUMN (sheet);
	   col <= MAX_VISIBLE_COLUMN (sheet);
	   col++)
	{
	  GtkSheetButton *button = xxx_column_button (sheet, col);
	  GtkSheetChild *child = button->child;
	  if (child)
	    gtk_sheet_child_show (child);
	  gtk_sheet_button_free (button);
	}
      adjust_scrollbars (sheet);
    }

  sheet->old_vadjustment = -1.;
  if (sheet->vadjustment)
    g_signal_emit_by_name (G_OBJECT (sheet->vadjustment),
			   "value_changed");
  size_allocate_global_button (sheet);
}


void
gtk_sheet_show_row_titles (GtkSheet *sheet)
{
  gint row;

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

      for (row = MIN_VISIBLE_ROW (sheet);
	   row <= MAX_VISIBLE_ROW (sheet);
	   row++)
	{
	  const GtkSheetButton *button = yyy_row_button (sheet, row);
	  GtkSheetChild *child = button->child;

	  if (child)
	    {
	      gtk_sheet_child_show (child);
	    }
	}
      adjust_scrollbars (sheet);
    }

  sheet->old_hadjustment = -1.;
  if (sheet->hadjustment)
    g_signal_emit_by_name (G_OBJECT (sheet->hadjustment),
			   "value_changed");
  size_allocate_global_button (sheet);
}

void
gtk_sheet_hide_column_titles (GtkSheet *sheet)
{
  gint col;

  if (!sheet->column_titles_visible) return;

  sheet->column_titles_visible = FALSE;

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      if (sheet->column_title_window)
	gdk_window_hide (sheet->column_title_window);
      if (GTK_WIDGET_VISIBLE (sheet->button))
	gtk_widget_hide (sheet->button);

      for (col = MIN_VISIBLE_COLUMN (sheet);
	   col <= MAX_VISIBLE_COLUMN (sheet);
	   col++)
	{
	  GtkSheetButton *button = xxx_column_button (sheet, col);
	  GtkSheetChild *child = button->child;
	  if (child)
	    gtk_sheet_child_hide (child);
	  gtk_sheet_button_free (button);
	}
      adjust_scrollbars (sheet);
    }

  sheet->old_vadjustment = -1.;
  if (sheet->vadjustment)
    g_signal_emit_by_name (G_OBJECT (sheet->vadjustment),
			   "value_changed");
}

void
gtk_sheet_hide_row_titles (GtkSheet *sheet)
{
  gint row;

  if (!sheet->row_titles_visible) return;

  sheet->row_titles_visible = FALSE;


  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      if (sheet->row_title_window)
	gdk_window_hide (sheet->row_title_window);
      if (GTK_WIDGET_VISIBLE (sheet->button))
	gtk_widget_hide (sheet->button);
      for (row = MIN_VISIBLE_ROW (sheet);
	   row <= MAX_VISIBLE_ROW (sheet);
	   row++)
	{
	  const GtkSheetButton *button = yyy_row_button (sheet, row);
	  GtkSheetChild *child = button->child;

	  if (child)
	    gtk_sheet_child_hide (child);
	}
      adjust_scrollbars (sheet);
    }

  sheet->old_hadjustment = -1.;
  if (sheet->hadjustment)
    g_signal_emit_by_name (G_OBJECT (sheet->hadjustment),
			   "value_changed");
}

gboolean
gtk_sheet_column_titles_visible (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);
  return sheet->column_titles_visible;
}

gboolean
gtk_sheet_row_titles_visible (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);
  return sheet->row_titles_visible;
}

void
gtk_sheet_moveto (GtkSheet *sheet,
		  gint row,
		  gint column,
		  gfloat row_align,
		  gfloat col_align)
{
  gint x, y;
  guint width, height;
  gint adjust;
  gint min_row, min_col;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));
  g_return_if_fail (sheet->hadjustment != NULL);
  g_return_if_fail (sheet->vadjustment != NULL);

  if (row < 0 || row >= yyy_row_count (sheet))
    return;
  if (column < 0 || column >= xxx_column_count (sheet))
    return;

  height = sheet->sheet_window_height;
  width = sheet->sheet_window_width;

  /* adjust vertical scrollbar */
  if (row >= 0 && row_align >= 0.)
    {
      y = ROW_TOP_YPIXEL (sheet, row) - sheet->voffset
	- (gint) ( row_align*height + (1. - row_align)
		   * yyy_row_height (sheet, row));

      /* This forces the sheet to scroll when you don't see the entire cell */
      min_row = row;
      adjust = 0;
      if (row_align == 1.)
	{
	  while (min_row >= 0 && min_row > MIN_VISIBLE_ROW (sheet))
	    {
	      if (yyy_row_is_visible (sheet, min_row))
		adjust += yyy_row_height (sheet, min_row);
	      if (adjust >= height)
		{
		  break;
		}
	      min_row--;
	    }
	  min_row = MAX (min_row, 0);
	  y = ROW_TOP_YPIXEL (sheet, min_row) - sheet->voffset +
	    yyy_row_height (sheet, min_row) - 1;
	}

      if (y < 0)
	sheet->vadjustment->value = 0.0;
      else
	sheet->vadjustment->value = y;

      sheet->old_vadjustment = -1.;
      g_signal_emit_by_name (G_OBJECT (sheet->vadjustment),
			     "value_changed");

    }

  /* adjust horizontal scrollbar */
  if (column >= 0 && col_align >= 0.)
    {
      x = COLUMN_LEFT_XPIXEL (sheet, column) - sheet->hoffset
	- (gint) ( col_align*width + (1.- col_align)*
		   xxx_column_width (sheet, column));


      /* This forces the sheet to scroll when you don't see the entire cell */
      min_col = column;
      adjust = 0;
      if (col_align == 1.)
	{
	  while (min_col >= 0 && min_col > MIN_VISIBLE_COLUMN (sheet))
	    {
	      if (xxx_column_is_visible (sheet, min_col))
		adjust += xxx_column_width (sheet, min_col);

	      if (adjust >= width)
		{
		  break;
		}
	      min_col--;
	    }
	  min_col = MAX (min_col, 0);
	  x = COLUMN_LEFT_XPIXEL (sheet, min_col) - sheet->hoffset +
	    xxx_column_width (sheet, min_col) - 1;
	}

      if (x < 0)
	sheet->hadjustment->value = 0.0;
      else
	sheet->hadjustment->value = x;

      sheet->old_vadjustment = -1.;
      g_signal_emit_by_name (G_OBJECT (sheet->hadjustment),
			     "value_changed");

    }
}


void
gtk_sheet_columns_set_resizable (GtkSheet *sheet, gboolean resizable)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  sheet->columns_resizable = resizable;
}

gboolean
gtk_sheet_columns_resizable (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);

  return sheet->columns_resizable;
}


void
gtk_sheet_rows_set_resizable (GtkSheet *sheet, gboolean resizable)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  sheet->rows_resizable = resizable;
}

gboolean
gtk_sheet_rows_resizable (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);

  return sheet->rows_resizable;
}


void
gtk_sheet_select_row (GtkSheet * sheet,
		      gint row)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (row < 0 || row >= yyy_row_count (sheet))
    return;

  if (sheet->state != GTK_SHEET_NORMAL)
    gtk_sheet_real_unselect_range (sheet, NULL);
  else
    {
      gboolean veto = TRUE;
      veto = gtk_sheet_deactivate_cell (sheet);
      if (!veto) return;
    }

  sheet->state = GTK_SHEET_ROW_SELECTED;
  sheet->range.row0 = row;
  sheet->range.col0 = 0;
  sheet->range.rowi = row;
  sheet->range.coli = xxx_column_count (sheet) - 1;
  sheet->active_cell.row = row;
  sheet->active_cell.col = 0;

  g_signal_emit (G_OBJECT (sheet), sheet_signals[SELECT_ROW], 0, row);
  gtk_sheet_real_select_range (sheet, NULL);
}


void
gtk_sheet_select_column (GtkSheet * sheet, gint column)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (column < 0 || column >= xxx_column_count (sheet))
    return;

  if (sheet->state != GTK_SHEET_NORMAL)
    gtk_sheet_real_unselect_range (sheet, NULL);
  else
    {
      gboolean veto = TRUE;
      veto = gtk_sheet_deactivate_cell (sheet);
      if (!veto) return;
    }

  sheet->state = GTK_SHEET_COLUMN_SELECTED;
  sheet->range.row0 = 0;
  sheet->range.col0 = column;
  sheet->range.rowi = yyy_row_count (sheet) - 1;
  sheet->range.coli = column;
  sheet->active_cell.row = 0;
  sheet->active_cell.col = column;

  g_signal_emit (G_OBJECT (sheet), sheet_signals[SELECT_COLUMN], 0, column);
  gtk_sheet_real_select_range (sheet, NULL);
}




static gboolean
gtk_sheet_range_isvisible (const GtkSheet * sheet,
			   GtkSheetRange range)
{
  g_return_val_if_fail (sheet != NULL, FALSE);

  if (range.row0 < 0 || range.row0 >= yyy_row_count (sheet))
    return FALSE;

  if (range.rowi < 0 || range.rowi >= yyy_row_count (sheet))
    return FALSE;

  if (range.col0 < 0 || range.col0 >= xxx_column_count (sheet))
    return FALSE;

  if (range.coli < 0 || range.coli >= xxx_column_count (sheet))
    return FALSE;

  if (range.rowi < MIN_VISIBLE_ROW (sheet))
    return FALSE;

  if (range.row0 > MAX_VISIBLE_ROW (sheet))
    return FALSE;

  if (range.coli < MIN_VISIBLE_COLUMN (sheet))
    return FALSE;

  if (range.col0 > MAX_VISIBLE_COLUMN (sheet))
    return FALSE;

  return TRUE;
}

static gboolean
gtk_sheet_cell_isvisible (GtkSheet * sheet,
			  gint row, gint column)
{
  GtkSheetRange range;

  range.row0 = row;
  range.col0 = column;
  range.rowi = row;
  range.coli = column;

  return gtk_sheet_range_isvisible (sheet, range);
}

void
gtk_sheet_get_visible_range (GtkSheet *sheet, GtkSheetRange *range)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet)) ;
  g_return_if_fail (range != NULL);

  range->row0 = MIN_VISIBLE_ROW (sheet);
  range->col0 = MIN_VISIBLE_COLUMN (sheet);
  range->rowi = MAX_VISIBLE_ROW (sheet);
  range->coli = MAX_VISIBLE_COLUMN (sheet);
}

GtkAdjustment *
gtk_sheet_get_vadjustment (GtkSheet * sheet)
{
  g_return_val_if_fail (sheet != NULL, NULL);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), NULL);

  return sheet->vadjustment;
}

GtkAdjustment *
gtk_sheet_get_hadjustment (GtkSheet * sheet)
{
  g_return_val_if_fail (sheet != NULL, NULL);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), NULL);

  return sheet->hadjustment;
}

void
gtk_sheet_set_vadjustment (GtkSheet *sheet,
			   GtkAdjustment *adjustment)
{
  GtkAdjustment *old_adjustment;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));
  if (adjustment)
    g_return_if_fail (GTK_IS_ADJUSTMENT (adjustment));

  if (sheet->vadjustment == adjustment)
    return;

  old_adjustment = sheet->vadjustment;

  if (sheet->vadjustment)
    {
      g_signal_handlers_disconnect_matched (G_OBJECT (sheet->vadjustment),
					    G_SIGNAL_MATCH_DATA,
					    0, 0, 0, 0,
					    sheet);
      g_object_unref (G_OBJECT (sheet->vadjustment));
    }

  sheet->vadjustment = adjustment;

  if (sheet->vadjustment)
    {
      g_object_ref (G_OBJECT (sheet->vadjustment));
      g_object_ref_sink (G_OBJECT (sheet->vadjustment));

      g_signal_connect (G_OBJECT (sheet->vadjustment), "value_changed",
			G_CALLBACK (vadjustment_value_changed),
			sheet);
    }

  if (!sheet->vadjustment || !old_adjustment)
    {
      gtk_widget_queue_resize (GTK_WIDGET (sheet));
      return;
    }

  sheet->old_vadjustment = sheet->vadjustment->value;
}

void
gtk_sheet_set_hadjustment (GtkSheet *sheet,
			   GtkAdjustment *adjustment)
{
  GtkAdjustment *old_adjustment;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));
  if (adjustment)
    g_return_if_fail (GTK_IS_ADJUSTMENT (adjustment));

  if (sheet->hadjustment == adjustment)
    return;

  old_adjustment = sheet->hadjustment;

  if (sheet->hadjustment)
    {
      g_signal_handlers_disconnect_matched (G_OBJECT (sheet->hadjustment),
					    G_SIGNAL_MATCH_DATA,
					    0, 0, 0, 0,
					    sheet);
      g_object_unref (G_OBJECT (sheet->hadjustment));
    }

  sheet->hadjustment = adjustment;

  if (sheet->hadjustment)
    {
      g_object_ref (G_OBJECT (sheet->hadjustment));
      g_object_ref_sink (G_OBJECT (sheet->hadjustment));

      g_signal_connect (G_OBJECT (sheet->hadjustment), "value_changed",
			G_CALLBACK (hadjustment_value_changed),
			sheet);
    }

  if (!sheet->hadjustment || !old_adjustment)
    {
      gtk_widget_queue_resize (GTK_WIDGET (sheet));
      return;
    }

  sheet->old_hadjustment = sheet->hadjustment->value;
}

static void
gtk_sheet_set_scroll_adjustments (GtkSheet *sheet,
				  GtkAdjustment *hadjustment,
				  GtkAdjustment *vadjustment)
{
  if (sheet->hadjustment != hadjustment)
    gtk_sheet_set_hadjustment (sheet, hadjustment);

  if (sheet->vadjustment != vadjustment)
    gtk_sheet_set_vadjustment (sheet, vadjustment);
}

static void
gtk_sheet_finalize (GObject * object)
{
  GtkSheet *sheet;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GTK_IS_SHEET (object));

  sheet = GTK_SHEET (object);

  if (sheet->name)
    {
      g_free (sheet->name);
      sheet->name = NULL;
    }

  if (G_OBJECT_CLASS (parent_class)->finalize)
    (*G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
gtk_sheet_destroy (GtkObject * object)
{
  GtkSheet *sheet;
  GList *children;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GTK_IS_SHEET (object));

  sheet = GTK_SHEET (object);

  /* destroy the entry */
  if (sheet->sheet_entry && GTK_IS_WIDGET (sheet->sheet_entry))
    {
      gtk_widget_destroy (sheet->sheet_entry);
      sheet->sheet_entry = NULL;
    }

  /* destroy the global selection button */
  if (sheet->button && GTK_IS_WIDGET (sheet->button))
    {
      gtk_widget_destroy (sheet->button);
      sheet->button = NULL;
    }

  /* unref adjustments */
  if (sheet->hadjustment)
    {
      g_signal_handlers_disconnect_matched (G_OBJECT (sheet->hadjustment),
					    G_SIGNAL_MATCH_DATA,
					    0, 0, 0, 0,
					    sheet);

      g_object_unref (G_OBJECT (sheet->hadjustment));
      sheet->hadjustment = NULL;
    }

  if (sheet->vadjustment)
    {
      g_signal_handlers_disconnect_matched (G_OBJECT (sheet->vadjustment),
					    G_SIGNAL_MATCH_DATA,
					    0, 0, 0, 0,
					    sheet);

      g_object_unref (G_OBJECT (sheet->vadjustment));

      sheet->vadjustment = NULL;
    }

  children = sheet->children;
  while (children)
    {
      GtkSheetChild *child = (GtkSheetChild *)children->data;
      if (child && child->widget)
	gtk_sheet_remove (GTK_CONTAINER (sheet), child->widget);
      children = sheet->children;
    }
  sheet->children = NULL;

  if (GTK_OBJECT_CLASS (parent_class)->destroy)
    (*GTK_OBJECT_CLASS (parent_class)->destroy) (object);
}

static void
gtk_sheet_style_set (GtkWidget *widget,
		     GtkStyle *previous_style)
{
  GtkSheet *sheet;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SHEET (widget));

  if (GTK_WIDGET_CLASS (parent_class)->style_set)
    (*GTK_WIDGET_CLASS (parent_class)->style_set) (widget, previous_style);

  sheet = GTK_SHEET (widget);

  if (GTK_WIDGET_REALIZED (widget))
    {
      gtk_style_set_background (widget->style, widget->window, widget->state);
    }

}

static void
gtk_sheet_realize (GtkWidget * widget)
{
  GtkSheet *sheet;
  GdkWindowAttr attributes;
  gint attributes_mask;
  GdkGCValues values, auxvalues;
  GdkColormap *colormap;
  gchar *name;
  GtkSheetChild *child;
  GList *children;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SHEET (widget));

  sheet = GTK_SHEET (widget);

  GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = widget->allocation.x;
  attributes.y = widget->allocation.y;
  attributes.width = widget->allocation.width;
  attributes.height = widget->allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;

  attributes.visual = gtk_widget_get_visual (widget);
  attributes.colormap = gtk_widget_get_colormap (widget);

  attributes.event_mask = gtk_widget_get_events (widget);
  attributes.event_mask |= (GDK_EXPOSURE_MASK |
			    GDK_BUTTON_PRESS_MASK |
			    GDK_BUTTON_RELEASE_MASK |
			    GDK_KEY_PRESS_MASK |
			    GDK_POINTER_MOTION_MASK |
			    GDK_POINTER_MOTION_HINT_MASK);
  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP |
    GDK_WA_CURSOR;

  attributes.cursor = gdk_cursor_new (GDK_TOP_LEFT_ARROW);

  /* main window */
  widget->window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);

  gdk_window_set_user_data (widget->window, sheet);

  widget->style = gtk_style_attach (widget->style, widget->window);

  gtk_style_set_background (widget->style, widget->window, GTK_STATE_NORMAL);

  attributes.x = 0;
  if (sheet->row_titles_visible)
    attributes.x = sheet->row_title_area.width;
  attributes.y = 0;
  attributes.width = sheet->column_title_area.width;
  attributes.height = sheet->column_title_area.height;

  /* column - title window */
  sheet->column_title_window = gdk_window_new (widget->window, &attributes, attributes_mask);
  gdk_window_set_user_data (sheet->column_title_window, sheet);
  gtk_style_set_background (widget->style, sheet->column_title_window, GTK_STATE_NORMAL);

  attributes.x = 0;
  attributes.y = 0;
  if (sheet->column_titles_visible)
    attributes.y = sheet->column_title_area.height;
  attributes.width = sheet->row_title_area.width;
  attributes.height = sheet->row_title_area.height;

  /* row - title window */
  sheet->row_title_window = gdk_window_new (widget->window, &attributes, attributes_mask);
  gdk_window_set_user_data (sheet->row_title_window, sheet);
  gtk_style_set_background (widget->style, sheet->row_title_window, GTK_STATE_NORMAL);

  /* sheet - window */
  attributes.cursor = gdk_cursor_new (GDK_PLUS);

  attributes.x = 0;
  attributes.y = 0;
  attributes.width = sheet->sheet_window_width,
    attributes.height = sheet->sheet_window_height;

  sheet->sheet_window = gdk_window_new (widget->window, &attributes, attributes_mask);
  gdk_window_set_user_data (sheet->sheet_window, sheet);

  gdk_cursor_unref (attributes.cursor);

  gdk_window_set_background (sheet->sheet_window, &widget->style->white);
  gdk_window_show (sheet->sheet_window);

  /* backing_pixmap */
  gtk_sheet_make_backing_pixmap (sheet, 0, 0);

  /* GCs */
  if (sheet->fg_gc)
    gdk_gc_unref (sheet->fg_gc);
  if (sheet->bg_gc)
    gdk_gc_unref (sheet->bg_gc);
  sheet->fg_gc = gdk_gc_new (widget->window);
  sheet->bg_gc = gdk_gc_new (widget->window);

  colormap = gtk_widget_get_colormap (widget);

  gdk_color_white (colormap, &widget->style->white);
  gdk_color_black (colormap, &widget->style->black);

  gdk_gc_get_values (sheet->fg_gc, &auxvalues);

  values.foreground = widget->style->white;
  values.function = GDK_INVERT;
  values.subwindow_mode = GDK_INCLUDE_INFERIORS;
  if (sheet->xor_gc)
    gdk_gc_unref (sheet->xor_gc);
  sheet->xor_gc = gdk_gc_new_with_values (widget->window,
					  &values,
					  GDK_GC_FOREGROUND |
					  GDK_GC_FUNCTION |
					  GDK_GC_SUBWINDOW);

  if (sheet->sheet_entry->parent)
    {
      gtk_widget_ref (sheet->sheet_entry);
      gtk_widget_unparent (sheet->sheet_entry);
    }
  gtk_widget_set_parent_window (sheet->sheet_entry, sheet->sheet_window);
  gtk_widget_set_parent (sheet->sheet_entry, GTK_WIDGET (sheet));

  if (sheet->button && sheet->button->parent)
    {
      gtk_widget_ref (sheet->button);
      gtk_widget_unparent (sheet->button);
    }
  gtk_widget_set_parent_window (sheet->button, sheet->sheet_window);
  gtk_widget_set_parent (sheet->button, GTK_WIDGET (sheet));

  if (!sheet->cursor_drag)
    sheet->cursor_drag = gdk_cursor_new (GDK_PLUS);

  if (sheet->column_titles_visible)
    gdk_window_show (sheet->column_title_window);
  if (sheet->row_titles_visible)
    gdk_window_show (sheet->row_title_window);

  size_allocate_row_title_buttons (sheet);
  size_allocate_column_title_buttons (sheet);

  name = g_strdup (sheet->name);
  gtk_sheet_set_title (sheet, name);

  g_free (name);

  children = sheet->children;
  while (children)
    {
      child = children->data;
      children = children->next;

      gtk_sheet_realize_child (sheet, child);
    }

  gtk_sheet_update_primary_selection (sheet);
}

static void
create_global_button (GtkSheet *sheet)
{
  sheet->button = gtk_button_new_with_label (" ");

  g_signal_connect (G_OBJECT (sheet->button),
		    "pressed",
		    G_CALLBACK (global_button_clicked),
		    sheet);
}

static void
size_allocate_global_button (GtkSheet *sheet)
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
  gtk_widget_show (sheet->button);
}

static void
global_button_clicked (GtkWidget *widget, gpointer data)
{
  gboolean veto;

  gtk_sheet_click_cell (GTK_SHEET (data), - 1, - 1, &veto);
  gtk_widget_grab_focus (GTK_WIDGET (data));
}


static void
gtk_sheet_unrealize (GtkWidget * widget)
{
  GtkSheet *sheet;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SHEET (widget));

  sheet = GTK_SHEET (widget);

  gdk_cursor_destroy (sheet->cursor_drag);

  gdk_gc_destroy (sheet->xor_gc);
  gdk_gc_destroy (sheet->fg_gc);
  gdk_gc_destroy (sheet->bg_gc);

  gdk_window_destroy (sheet->sheet_window);
  gdk_window_destroy (sheet->column_title_window);
  gdk_window_destroy (sheet->row_title_window);

  if (sheet->pixmap)
    {
      g_object_unref (sheet->pixmap);
      sheet->pixmap = NULL;
    }

  sheet->column_title_window = NULL;
  sheet->sheet_window = NULL;
  sheet->cursor_drag = NULL;
  sheet->xor_gc = NULL;
  sheet->fg_gc = NULL;
  sheet->bg_gc = NULL;

  if (GTK_WIDGET_CLASS (parent_class)->unrealize)
    (* GTK_WIDGET_CLASS (parent_class)->unrealize) (widget);
}

static void
gtk_sheet_map (GtkWidget * widget)
{
  GtkSheet *sheet;
  GtkSheetChild *child;
  GList *children;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SHEET (widget));

  sheet = GTK_SHEET (widget);

  if (!GTK_WIDGET_MAPPED (widget))
    {
      GTK_WIDGET_SET_FLAGS (widget, GTK_MAPPED);

      if (!sheet->cursor_drag) sheet->cursor_drag = gdk_cursor_new (GDK_PLUS);

      gdk_window_show (widget->window);

      gdk_window_show (sheet->sheet_window);

      if (sheet->column_titles_visible)
	{
	  size_allocate_column_title_buttons (sheet);
	  gdk_window_show (sheet->column_title_window);
	}
      if (sheet->row_titles_visible)
	{
	  size_allocate_row_title_buttons (sheet);
	  gdk_window_show (sheet->row_title_window);
	}

      if (!GTK_WIDGET_MAPPED (sheet->sheet_entry)
	  && ! gtk_sheet_locked (sheet)
	  && sheet->active_cell.row >= 0
	  && sheet->active_cell.col >= 0 )
	{
	  gtk_widget_show (sheet->sheet_entry);
	  gtk_widget_map (sheet->sheet_entry);
	}

      if (GTK_WIDGET_VISIBLE (sheet->button) &&
	  !GTK_WIDGET_MAPPED (sheet->button))
	{
	  gtk_widget_show (sheet->button);
	  gtk_widget_map (sheet->button);
	}

      if (GTK_BIN (sheet->button)->child)
	if (GTK_WIDGET_VISIBLE (GTK_BIN (sheet->button)->child) &&
	    !GTK_WIDGET_MAPPED (GTK_BIN (sheet->button)->child))
	  gtk_widget_map (GTK_BIN (sheet->button)->child);

      gtk_sheet_range_draw (sheet, NULL);
      gtk_sheet_activate_cell (sheet,
			       sheet->active_cell.row,
			       sheet->active_cell.col);

      children = sheet->children;
      while (children)
	{
	  child = children->data;
	  children = children->next;

	  if (GTK_WIDGET_VISIBLE (child->widget) &&
	      !GTK_WIDGET_MAPPED (child->widget))
	    {
	      gtk_widget_map (child->widget);
	      gtk_sheet_position_child (sheet, child);
	    }
	}

    }
}

static void
gtk_sheet_unmap (GtkWidget * widget)
{
  GtkSheet *sheet;
  GtkSheetChild *child;
  GList *children;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SHEET (widget));

  sheet = GTK_SHEET (widget);

  if (GTK_WIDGET_MAPPED (widget))
    {
      GTK_WIDGET_UNSET_FLAGS (widget, GTK_MAPPED);

      gdk_window_hide (sheet->sheet_window);
      if (sheet->column_titles_visible)
	gdk_window_hide (sheet->column_title_window);
      if (sheet->row_titles_visible)
	gdk_window_hide (sheet->row_title_window);
      gdk_window_hide (widget->window);

      if (GTK_WIDGET_MAPPED (sheet->sheet_entry))
	gtk_widget_unmap (sheet->sheet_entry);

      if (GTK_WIDGET_MAPPED (sheet->button))
	gtk_widget_unmap (sheet->button);

      children = sheet->children;
      while (children)
	{
	  child = children->data;
	  children = children->next;

	  if (GTK_WIDGET_VISIBLE (child->widget) &&
	      GTK_WIDGET_MAPPED (child->widget))
	    {
	      gtk_widget_unmap (child->widget);
	    }
	}

    }
}


static void
gtk_sheet_cell_draw_default (GtkSheet *sheet, gint row, gint col)
{
  GtkWidget *widget;
  GdkGC *fg_gc, *bg_gc;
  GtkSheetCellAttr attributes;
  GdkRectangle area;

  g_return_if_fail (sheet != NULL);

  /* bail now if we arn't drawable yet */
  if (!GTK_WIDGET_DRAWABLE (sheet)) return;

  if (row < 0 || row >= yyy_row_count (sheet)) return;
  if (col < 0 || col >= xxx_column_count (sheet)) return;
  if (! xxx_column_is_visible (sheet, col)) return;
  if (! yyy_row_is_visible (sheet, row)) return;

  widget = GTK_WIDGET (sheet);

  gtk_sheet_get_attributes (sheet, row, col, &attributes);

  /* select GC for background rectangle */
  gdk_gc_set_foreground (sheet->fg_gc, &attributes.foreground);
  gdk_gc_set_foreground (sheet->bg_gc, &attributes.background);

  fg_gc = sheet->fg_gc;
  bg_gc = sheet->bg_gc;

  area.x = COLUMN_LEFT_XPIXEL (sheet, col);
  area.y = ROW_TOP_YPIXEL (sheet, row);
  area.width= xxx_column_width (sheet, col);
  area.height = yyy_row_height (sheet, row);

  gdk_draw_rectangle (sheet->pixmap,
		      bg_gc,
		      TRUE,
		      area.x,
		      area.y,
		      area.width,
		      area.height);

  gdk_gc_set_line_attributes (sheet->fg_gc, 1, 0, 0, 0);

  if (sheet->show_grid)
    {
      gdk_gc_set_foreground (sheet->bg_gc, &sheet->grid_color);

      gdk_draw_rectangle (sheet->pixmap,
			  sheet->bg_gc,
			  FALSE,
			  area.x, area.y,
			  area.width, area.height);
    }
}

static void
gtk_sheet_cell_draw_label (GtkSheet *sheet, gint row, gint col)
{
  GtkWidget *widget;
  GdkRectangle area;
  gint i;
  gint text_width, text_height, y;
  gint xoffset = 0;
  gint size, sizel, sizer;
  GdkGC *fg_gc, *bg_gc;
  GtkSheetCellAttr attributes;
  PangoLayout *layout;
  PangoRectangle rect;
  PangoRectangle logical_rect;
  PangoLayoutLine *line;
  PangoFontMetrics *metrics;
  PangoContext *context = gtk_widget_get_pango_context (GTK_WIDGET (sheet));
  gint ascent, descent, y_pos;

  gchar *label;

  g_return_if_fail (sheet != NULL);

  /* bail now if we aren't drawable yet */
  if (!GTK_WIDGET_DRAWABLE (sheet))
    return;

  label = gtk_sheet_cell_get_text (sheet, row, col);
  if (!label)
    return;

  if (row < 0 || row >= yyy_row_count (sheet)) return;
  if (col < 0 || col >= xxx_column_count (sheet)) return;
  if (! xxx_column_is_visible (sheet, col)) return;
  if (!yyy_row_is_visible (sheet, row)) return;


  widget = GTK_WIDGET (sheet);

  gtk_sheet_get_attributes (sheet, row, col, &attributes);

  /* select GC for background rectangle */
  gdk_gc_set_foreground (sheet->fg_gc, &attributes.foreground);
  gdk_gc_set_foreground (sheet->bg_gc, &attributes.background);

  fg_gc = sheet->fg_gc;
  bg_gc = sheet->bg_gc;

  area.x = COLUMN_LEFT_XPIXEL (sheet, col);
  area.y = ROW_TOP_YPIXEL (sheet, row);
  area.width = xxx_column_width (sheet, col);
  area.height = yyy_row_height (sheet, row);


  layout = gtk_widget_create_pango_layout (GTK_WIDGET (sheet), label);
  dispose_string (sheet, label);
  pango_layout_set_font_description (layout, attributes.font_desc);

  pango_layout_get_pixel_extents (layout, NULL, &rect);

  line = pango_layout_get_lines (layout)->data;
  pango_layout_line_get_extents (line, NULL, &logical_rect);

  metrics = pango_context_get_metrics (context,
				       attributes.font_desc,
				       pango_context_get_language (context));

  ascent = pango_font_metrics_get_ascent (metrics) / PANGO_SCALE;
  descent = pango_font_metrics_get_descent (metrics) / PANGO_SCALE;

  pango_font_metrics_unref (metrics);

  /* Align primarily for locale's ascent / descent */

  logical_rect.height /= PANGO_SCALE;
  logical_rect.y /= PANGO_SCALE;
  y_pos = area.height - logical_rect.height;

  if (logical_rect.height > area.height)
    y_pos = (logical_rect.height - area.height - 2 * CELLOFFSET) / 2;
  else if (y_pos < 0)
    y_pos = 0;
  else if (y_pos + logical_rect.height > area.height)
    y_pos = area.height - logical_rect.height;

  text_width = rect.width;
  text_height = rect.height;
  y = area.y + y_pos - CELLOFFSET;

  switch (attributes.justification)
    {
    case GTK_JUSTIFY_RIGHT:
      size = area.width;
      area.x +=area.width;
	{
	  for (i = col - 1; i >= MIN_VISIBLE_COLUMN (sheet); i--)
	    {
	      if ( !gtk_sheet_cell_empty (sheet, row, i)) break;
	      if (size >= text_width + CELLOFFSET) break;
	      size +=xxx_column_width (sheet, i);
	      xxx_column_set_right_column (sheet, i,
					   MAX (col,
						xxx_column_right_column (sheet, i)));
	    }
	  area.width = size;
	}
      area.x -= size;
      xoffset += area.width - text_width - 2 * CELLOFFSET -
	attributes.border.width / 2;
      break;
    case GTK_JUSTIFY_CENTER:
      sizel = area.width / 2;
      sizer = area.width / 2;
      area.x += area.width / 2;
	{
	  for (i = col + 1; i <= MAX_VISIBLE_COLUMN (sheet); i++)
	    {
	      if ( ! gtk_sheet_cell_empty (sheet, row, i)) break;
	      if (sizer >= text_width / 2) break;
	      sizer += xxx_column_width (sheet, i);
	      xxx_column_set_left_column (sheet, i,
					  MIN (
					       col,
					       xxx_column_left_column (sheet, i)));
	    }
	  for (i = col - 1; i >= MIN_VISIBLE_COLUMN (sheet); i--)
	    {
	      if ( ! gtk_sheet_cell_empty (sheet, row, i)) break;
	      if (sizel >= text_width / 2) break;
	      sizel +=xxx_column_width (sheet, i);
	      xxx_column_set_right_column (sheet, i,
					   MAX (col,
						xxx_column_right_column (sheet, i)));
	    }
	  size = MIN (sizel, sizer);
	}
      area.x -= sizel;
      xoffset += sizel - text_width / 2 - CELLOFFSET;
      area.width = sizel + sizer;
      break;
    case GTK_JUSTIFY_LEFT:
    default:
      size = area.width;
	{
	  for (i = col + 1; i <= MAX_VISIBLE_COLUMN (sheet); i++)
	    {
	      if (! gtk_sheet_cell_empty (sheet, row, i)) break;
	      if (size >= text_width + CELLOFFSET) break;
	      size +=xxx_column_width (sheet, i);
	      xxx_column_set_left_column (sheet, i,
					  MIN (
					       col,
					       xxx_column_left_column (sheet, i)));

	    }
	  area.width = size;
	}
      xoffset += attributes.border.width / 2;
      break;
    }

  gdk_gc_set_clip_rectangle (fg_gc, &area);


  gdk_draw_layout (sheet->pixmap, fg_gc,
		   area.x + xoffset + CELLOFFSET,
		   y,
		   layout);

  gdk_gc_set_clip_rectangle (fg_gc, NULL);
  g_object_unref (G_OBJECT (layout));

  gdk_draw_pixmap (sheet->sheet_window,
		   GTK_WIDGET (sheet)->style->fg_gc[GTK_STATE_NORMAL],
		   sheet->pixmap,
		   area.x,
		   area.y,
		   area.x,
		   area.y,
		   area.width,
		   area.height);

}

static void
gtk_sheet_range_draw (GtkSheet *sheet, const GtkSheetRange *range)
{
  gint i, j;
  GtkSheetRange drawing_range;
  GdkRectangle area;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_SHEET (sheet));

  if (!GTK_WIDGET_DRAWABLE (GTK_WIDGET (sheet))) return;
  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;
  if (!GTK_WIDGET_MAPPED (GTK_WIDGET (sheet))) return;

  if (range == NULL)
    {
      drawing_range.row0 = MIN_VISIBLE_ROW (sheet);
      drawing_range.col0 = MIN_VISIBLE_COLUMN (sheet);
      drawing_range.rowi = MIN (MAX_VISIBLE_ROW (sheet),
				yyy_row_count (sheet) - 1);
      drawing_range.coli = MAX_VISIBLE_COLUMN (sheet);


      gdk_draw_rectangle (sheet->pixmap,
			  GTK_WIDGET (sheet)->style->white_gc,
			  TRUE,
			  0, 0,
			  sheet->sheet_window_width,
			  sheet->sheet_window_height);
    }
  else
    {
      drawing_range.row0 = MAX (range->row0, MIN_VISIBLE_ROW (sheet));
      drawing_range.col0 = MAX (range->col0, MIN_VISIBLE_COLUMN (sheet));
      drawing_range.rowi = MIN (range->rowi, MAX_VISIBLE_ROW (sheet));
      drawing_range.coli = MIN (range->coli, MAX_VISIBLE_COLUMN (sheet));
    }

  if (drawing_range.coli == xxx_column_count (sheet) - 1)
    {
      area.x = COLUMN_LEFT_XPIXEL (sheet,
				   xxx_column_count (sheet) - 1) +
	xxx_column_width (sheet, xxx_column_count (sheet) - 1) + 1;

      area.y = 0;

      gdk_gc_set_foreground (sheet->fg_gc, &sheet->bg_color);

      gdk_draw_rectangle (sheet->pixmap,
			  sheet->fg_gc,
			  TRUE,
			  area.x, area.y,
			  sheet->sheet_window_width - area.x,
			  sheet->sheet_window_height);

      gdk_draw_pixmap (sheet->sheet_window,
		       GTK_WIDGET (sheet)->style->fg_gc[GTK_STATE_NORMAL],
		       sheet->pixmap,
		       area.x,
		       area.y,
		       area.x,
		       area.y,
		       sheet->sheet_window_width - area.x,
		       sheet->sheet_window_height);
    }

  if (drawing_range.rowi == yyy_row_count (sheet) - 1)
    {
      area.x = 0;
      area.y = ROW_TOP_YPIXEL (sheet,
			       yyy_row_count (sheet) - 1) +
	yyy_row_height (sheet, yyy_row_count (sheet) - 1) + 1;

      gdk_gc_set_foreground (sheet->fg_gc, &sheet->bg_color);

      gdk_draw_rectangle (sheet->pixmap,
			  sheet->fg_gc,
			  TRUE,
			  area.x, area.y,
			  sheet->sheet_window_width,
			  sheet->sheet_window_height - area.y);

      gdk_draw_pixmap (sheet->sheet_window,
		       GTK_WIDGET (sheet)->style->fg_gc[GTK_STATE_NORMAL],
		       sheet->pixmap,
		       area.x,
		       area.y,
		       area.x,
		       area.y,
		       sheet->sheet_window_width,
		       sheet->sheet_window_height - area.y);
    }

  for (i = drawing_range.row0; i <= drawing_range.rowi; i++)
    for (j = drawing_range.col0; j <= drawing_range.coli; j++)
      {
	gtk_sheet_cell_draw_default (sheet, i, j);
	gtk_sheet_cell_draw_label (sheet, i, j);
      }

  gtk_sheet_draw_backing_pixmap (sheet, drawing_range);

  if (sheet->state != GTK_SHEET_NORMAL &&
      gtk_sheet_range_isvisible (sheet, sheet->range))
    gtk_sheet_range_draw_selection (sheet, drawing_range);

  if (sheet->state == GTK_STATE_NORMAL &&
      sheet->active_cell.row >= drawing_range.row0 &&
      sheet->active_cell.row <= drawing_range.rowi &&
      sheet->active_cell.col >= drawing_range.col0 &&
      sheet->active_cell.col <= drawing_range.coli)
    gtk_sheet_show_active_cell (sheet);
}

static void
gtk_sheet_range_draw_selection (GtkSheet *sheet, GtkSheetRange range)
{
  GdkRectangle area;
  gint i, j;
  GtkSheetRange aux;

  if (range.col0 > sheet->range.coli || range.coli < sheet->range.col0 ||
      range.row0 > sheet->range.rowi || range.rowi < sheet->range.row0)
    return;

  if (!gtk_sheet_range_isvisible (sheet, range)) return;
  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;

  aux = range;

  range.col0 = MAX (sheet->range.col0, range.col0);
  range.coli = MIN (sheet->range.coli, range.coli);
  range.row0 = MAX (sheet->range.row0, range.row0);
  range.rowi = MIN (sheet->range.rowi, range.rowi);

  range.col0 = MAX (range.col0, MIN_VISIBLE_COLUMN (sheet));
  range.coli = MIN (range.coli, MAX_VISIBLE_COLUMN (sheet));
  range.row0 = MAX (range.row0, MIN_VISIBLE_ROW (sheet));
  range.rowi = MIN (range.rowi, MAX_VISIBLE_ROW (sheet));

  for (i = range.row0; i <= range.rowi; i++)
    {
      for (j = range.col0; j <= range.coli; j++)
	{

	  if (gtk_sheet_cell_get_state (sheet, i, j) == GTK_STATE_SELECTED &&
	      xxx_column_is_visible (sheet, j) && yyy_row_is_visible (sheet, i))
	    {

	      area.x = COLUMN_LEFT_XPIXEL (sheet, j);
	      area.y = ROW_TOP_YPIXEL (sheet, i);
	      area.width= xxx_column_width (sheet, j);
	      area.height = yyy_row_height (sheet, i);

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

  gtk_sheet_draw_border (sheet, sheet->range);
}

static void
gtk_sheet_draw_backing_pixmap (GtkSheet *sheet, GtkSheetRange range)
{
  gint x, y, width, height;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;

  x = COLUMN_LEFT_XPIXEL (sheet, range.col0);
  y = ROW_TOP_YPIXEL (sheet, range.row0);
  width = COLUMN_LEFT_XPIXEL (sheet, range.coli) - x +
    xxx_column_width (sheet, range.coli);

  height = ROW_TOP_YPIXEL (sheet, range.rowi)- y + yyy_row_height (sheet, range.rowi);

  if (range.row0 == sheet->range.row0)
    {
      y = y - 5;
      height = height + 5;
    }
  if (range.rowi == sheet->range.rowi) height = height + 5;
  if (range.col0 == sheet->range.col0)
    {
      x = x - 5;
      width = width + 5;
    }
  if (range.coli == sheet->range.coli) width = width + 5;

  width = MIN (width, sheet->sheet_window_width - x);
  height = MIN (height, sheet->sheet_window_height - y);

  x--;
  y--;
  width +=2;
  height +=2;

  x = (sheet->row_titles_visible)
    ? MAX (x, sheet->row_title_area.width) : MAX (x, 0);
  y = (sheet->column_titles_visible)
    ? MAX (y, sheet->column_title_area.height) : MAX (y, 0);

  if (range.coli == xxx_column_count (sheet) - 1)
    width = sheet->sheet_window_width - x;
  if (range.rowi == yyy_row_count (sheet) - 1)
    height = sheet->sheet_window_height - y;

  gdk_draw_pixmap (sheet->sheet_window,
		   GTK_WIDGET (sheet)->style->fg_gc[GTK_STATE_NORMAL],
		   sheet->pixmap,
		   x,
		   y,
		   x,
		   y,
		   width + 1,
		   height + 1);
}


void
gtk_sheet_set_cell_text (GtkSheet *sheet, gint row, gint col, const gchar *text)
{
  GtkSheetCellAttr attributes;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));
  if (col >= xxx_column_count (sheet) || row >= yyy_row_count (sheet)) return;
  if (col < 0 || row < 0) return;

  gtk_sheet_get_attributes (sheet, row, col, &attributes);
  gtk_sheet_set_cell (sheet, row, col, attributes.justification, text);
}

static inline gint
safe_strcmp (const gchar *s1, const gchar *s2)
{
  if ( !s1 && !s2) return 0;
  if ( !s1) return - 1;
  if ( !s2) return +1;
  return strcmp (s1, s2);
}

void
gtk_sheet_set_cell (GtkSheet *sheet, gint row, gint col,
		    GtkJustification justification,
		    const gchar *text)
{
  GSheetModel *model ;
  gboolean changed ;
  gchar *old_text ;

  GtkSheetRange range;
  gint text_width;
  GtkSheetCellAttr attributes;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));
  if (col >= xxx_column_count (sheet) || row >= yyy_row_count (sheet)) return;
  if (col < 0 || row < 0) return;

  gtk_sheet_get_attributes (sheet, row, col, &attributes);

  attributes.justification = justification;

  model = gtk_sheet_get_model (sheet);

  old_text = g_sheet_model_get_string (model, row, col);

  changed = FALSE;

  if (0 != safe_strcmp (old_text, text))
    changed = g_sheet_model_set_string (model, text, row, col);

  if ( g_sheet_model_free_strings (model))
    g_free (old_text);


  if (changed && attributes.is_visible)
    {
      gchar *s = gtk_sheet_cell_get_text (sheet, row, col);
      text_width = 0;
      if (s && strlen (s) > 0)
	{
	  text_width = STRING_WIDTH (GTK_WIDGET (sheet),
				     attributes.font_desc, text);
	}
      dispose_string (sheet, s);

      range.row0 = row;
      range.rowi = row;
      range.col0 = MIN_VISIBLE_COLUMN (sheet);
      range.coli = MAX_VISIBLE_COLUMN (sheet);

      if (gtk_sheet_autoresize (sheet) &&
	  text_width > xxx_column_width (sheet, col) -
	  2 * CELLOFFSET- attributes.border.width)
	{
	  gtk_sheet_set_column_width (sheet, col, text_width + 2 * CELLOFFSET
				      + attributes.border.width);
	  GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_REDRAW_PENDING);
	}
      else
	if (!GTK_SHEET_IS_FROZEN (sheet))
	  gtk_sheet_range_draw (sheet, &range);
    }

  if ( changed )
    g_signal_emit (G_OBJECT (sheet), sheet_signals[CHANGED], 0, row, col);

}


void
gtk_sheet_cell_clear (GtkSheet *sheet, gint row, gint column)
{
  GtkSheetRange range;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));
  if (column >= xxx_column_count (sheet) ||
      row >= yyy_row_count (sheet)) return;

  if (column < 0 || row < 0) return;

  range.row0 = row;
  range.rowi = row;
  range.col0 = MIN_VISIBLE_COLUMN (sheet);
  range.coli = MAX_VISIBLE_COLUMN (sheet);

  gtk_sheet_real_cell_clear (sheet, row, column);

  if (!GTK_SHEET_IS_FROZEN (sheet))
    {
      gtk_sheet_range_draw (sheet, &range);
    }
}

static void
gtk_sheet_real_cell_clear (GtkSheet *sheet, gint row, gint column)
{
  GSheetModel *model = gtk_sheet_get_model (sheet);

  gchar *old_text = gtk_sheet_cell_get_text (sheet, row, column);

  if (old_text && strlen (old_text) > 0 )
    {
      g_sheet_model_datum_clear (model, row, column);

      if (GTK_IS_OBJECT (sheet) && G_OBJECT (sheet)->ref_count > 0)
	g_signal_emit (G_OBJECT (sheet), sheet_signals[CLEAR_CELL], 0,
		       row, column);
    }

  dispose_string (sheet, old_text);
}

void
gtk_sheet_range_clear (GtkSheet *sheet, const GtkSheetRange *range)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  gtk_sheet_real_range_clear (sheet, range);
}

static void
gtk_sheet_real_range_clear (GtkSheet *sheet, const GtkSheetRange *range)
{
  gint i, j;
  GtkSheetRange clear;

  if (!range)
    {
      clear.row0 = 0;
      clear.rowi = yyy_row_count (sheet) - 1;
      clear.col0 = 0;
      clear.coli = xxx_column_count (sheet) - 1;
    }
  else
    clear=*range;

  clear.row0 = MAX (clear.row0, 0);
  clear.col0 = MAX (clear.col0, 0);
  clear.rowi = MIN (clear.rowi, yyy_row_count (sheet) - 1 );
  clear.coli = MIN (clear.coli, xxx_column_count (sheet) - 1 );

  for (i = clear.row0; i <= clear.rowi; i++)
    for (j = clear.col0; j <= clear.coli; j++)
      {
	gtk_sheet_real_cell_clear (sheet, i, j);
      }

  gtk_sheet_range_draw (sheet, NULL);
}


static gboolean
gtk_sheet_cell_empty (const GtkSheet *sheet, gint row, gint col)
{
  gboolean empty;
  char *text = gtk_sheet_cell_get_text (sheet, row, col);
  empty = (text == NULL );

  dispose_string (sheet, text);

  return empty;
}


gchar *
gtk_sheet_cell_get_text (const GtkSheet *sheet, gint row, gint col)
{
  GSheetModel *model;
  g_return_val_if_fail (sheet != NULL, NULL);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), NULL);

  if (col >= xxx_column_count (sheet) || row >= yyy_row_count (sheet))
    return NULL;
  if (col < 0 || row < 0) return NULL;

  model = gtk_sheet_get_model (sheet);

  if ( !model )
    return NULL;

  return g_sheet_model_get_string (model, row, col);
}


GtkStateType
gtk_sheet_cell_get_state (GtkSheet *sheet, gint row, gint col)
{
  gint state;
  GtkSheetRange *range;

  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), 0);
  if (col >= xxx_column_count (sheet) || row >= yyy_row_count (sheet)) return 0;
  if (col < 0 || row < 0) return 0;

  state = sheet->state;
  range = &sheet->range;

  switch (state)
    {
    case GTK_SHEET_NORMAL:
      return GTK_STATE_NORMAL;
      break;
    case GTK_SHEET_ROW_SELECTED:
      if (row >= range->row0 && row <= range->rowi)
	return GTK_STATE_SELECTED;
      break;
    case GTK_SHEET_COLUMN_SELECTED:
      if (col >= range->col0 && col <= range->coli)
	return GTK_STATE_SELECTED;
      break;
    case GTK_SHEET_RANGE_SELECTED:
      if (row >= range->row0 && row <= range->rowi && \
	  col >= range->col0 && col <= range->coli)
	return GTK_STATE_SELECTED;
      break;
    }
  return GTK_STATE_NORMAL;
}

/* Convert X, Y (in pixels) to *ROW, *COLUMN (in cell coords)
   -1 indicates the title buttons.
   If the function returns FALSE, then the results will be unreliable.
*/
gboolean
gtk_sheet_get_pixel_info (GtkSheet *sheet,
			  gint x,
			  gint y,
			  gint *row,
			  gint *column)
{
  gint trow, tcol;
  *row = -G_MAXINT;
  *column = -G_MAXINT;

  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), 0);

  /* bounds checking, return false if the user clicked
     on a blank area */
  if (y < 0)
    return FALSE;

  if (x < 0)
    return FALSE;

  if ( y < sheet->column_title_area.height + sheet->column_title_area.y)
    *row = -1;

  else
    {
      trow = ROW_FROM_YPIXEL (sheet, y);
      if (trow > yyy_row_count (sheet))
	return FALSE;

      *row = trow;
    }

  if ( x < sheet->row_title_area.width + sheet->row_title_area.x)
    *column = -1;
  else
    {
      tcol = COLUMN_FROM_XPIXEL (sheet, x);
      if (tcol > xxx_column_count (sheet))
	return FALSE;

      *column = tcol;
    }

  return TRUE;
}

gboolean
gtk_sheet_get_cell_area (GtkSheet * sheet,
			 gint row,
			 gint column,
			 GdkRectangle *area)
{
  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), 0);

  if (row >= yyy_row_count (sheet) || column >= xxx_column_count (sheet))
    return FALSE;

  area->x = (column == -1) ? 0 : (COLUMN_LEFT_XPIXEL (sheet, column) -
				  (sheet->row_titles_visible
				   ? sheet->row_title_area.width
				   : 0));
  area->y = (row == -1) ? 0 : (ROW_TOP_YPIXEL (sheet, row) -
			       (sheet->column_titles_visible
				? sheet->column_title_area.height
				: 0));
  area->width= (column == -1) ? sheet->row_title_area.width
    : xxx_column_width (sheet, column);

  area->height= (row == -1) ? sheet->column_title_area.height
    : yyy_row_height (sheet, row);

  return TRUE;
}

gboolean
gtk_sheet_set_active_cell (GtkSheet *sheet, gint row, gint column)
{
  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), 0);

  if (row < - 1 || column < - 1) return FALSE;
  if (row >= yyy_row_count (sheet) || column >= xxx_column_count (sheet))
    return FALSE;

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      if (!gtk_sheet_deactivate_cell (sheet)) return FALSE;
    }

  sheet->active_cell.row = row;
  sheet->active_cell.col = column;

  if ( row == -1 || column == -1)
    {
      gtk_sheet_hide_active_cell (sheet);
      return TRUE;
    }

  if (!gtk_sheet_activate_cell (sheet, row, column)) return FALSE;

  if (gtk_sheet_autoscroll (sheet))
    gtk_sheet_move_query (sheet, row, column);

  return TRUE;
}

void
gtk_sheet_get_active_cell (GtkSheet *sheet, gint *row, gint *column)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if ( row ) *row = sheet->active_cell.row;
  if (column) *column = sheet->active_cell.col;
}

static void
gtk_sheet_entry_changed (GtkWidget *widget, gpointer data)
{
  GtkSheet *sheet;
  gint row, col;
  const char *text;
  GtkJustification justification;
  GtkSheetCellAttr attributes;

  g_return_if_fail (data != NULL);
  g_return_if_fail (GTK_IS_SHEET (data));

  sheet = GTK_SHEET (data);

  if (!GTK_WIDGET_VISIBLE (widget)) return;
  if (sheet->state != GTK_STATE_NORMAL) return;

  row = sheet->active_cell.row;
  col = sheet->active_cell.col;

  if (row < 0 || col < 0) return;

  sheet->active_cell.row =- 1;
  sheet->active_cell.col =- 1;

  text = gtk_entry_get_text (GTK_ENTRY (gtk_sheet_get_entry (sheet)));

  GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IS_FROZEN);

  if (text && strlen (text) > 0)
    {
      gtk_sheet_get_attributes (sheet, row, col, &attributes);
      justification = attributes.justification;
      gtk_sheet_set_cell (sheet, row, col, justification, text);
    }

  if (sheet->freeze_count == 0)
    GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IS_FROZEN);

  sheet->active_cell.row = row;;
  sheet->active_cell.col = col;
}


static gboolean
gtk_sheet_deactivate_cell (GtkSheet *sheet)
{
  gboolean veto = TRUE;

  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return FALSE;
  if (sheet->state != GTK_SHEET_NORMAL) return FALSE;

  _gtkextra_signal_emit (GTK_OBJECT (sheet), sheet_signals[DEACTIVATE],
			 sheet->active_cell.row,
			 sheet->active_cell.col, &veto);

  if (!veto) return FALSE;

  if ( sheet->active_cell.row == -1 || sheet->active_cell.col == -1 )
    return TRUE;

  g_signal_handlers_disconnect_by_func (G_OBJECT (gtk_sheet_get_entry (sheet)),
					G_CALLBACK (gtk_sheet_entry_changed),
					sheet);

  gtk_sheet_hide_active_cell (sheet);
  sheet->active_cell.row = -1;
  sheet->active_cell.col = -1;

  if (GTK_SHEET_REDRAW_PENDING (sheet))
    {
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_REDRAW_PENDING);
      gtk_sheet_range_draw (sheet, NULL);
    }

  return TRUE;
}

static void
gtk_sheet_hide_active_cell (GtkSheet *sheet)
{
  const char *text;
  gint row, col;
  GtkJustification justification;
  GtkSheetCellAttr attributes;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;

  row = sheet->active_cell.row;
  col = sheet->active_cell.col;

  if (row < 0 || col < 0) return;

  if (sheet->freeze_count == 0)
    GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IS_FROZEN);

  text = gtk_entry_get_text (GTK_ENTRY (gtk_sheet_get_entry (sheet)));

  gtk_sheet_get_attributes (sheet, row, col, &attributes);
  justification = attributes.justification;

  if (text && strlen (text) != 0)
    {
      gtk_sheet_set_cell (sheet, row, col, justification, text);
      g_signal_emit (G_OBJECT (sheet), sheet_signals[SET_CELL], 0, row, col);
    }
  else
    {
      gtk_sheet_cell_clear (sheet, row, col);
    }

  row = sheet->active_cell.row;
  col = sheet->active_cell.col;

  gtk_widget_hide (sheet->sheet_entry);
  gtk_widget_unmap (sheet->sheet_entry);

  if (row != -1 && col != -1)
    gdk_draw_pixmap (sheet->sheet_window,
		     GTK_WIDGET (sheet)->style->fg_gc[GTK_STATE_NORMAL],
		     sheet->pixmap,
		     COLUMN_LEFT_XPIXEL (sheet, col)- 1,
		     ROW_TOP_YPIXEL (sheet, row)- 1,
		     COLUMN_LEFT_XPIXEL (sheet, col)- 1,
		     ROW_TOP_YPIXEL (sheet, row)- 1,
		     xxx_column_width (sheet, col) + 4,
		     yyy_row_height (sheet, row)+4);

  gtk_widget_grab_focus (GTK_WIDGET (sheet));

  GTK_WIDGET_UNSET_FLAGS (GTK_WIDGET (sheet->sheet_entry), GTK_VISIBLE);

}

static gboolean
gtk_sheet_activate_cell (GtkSheet *sheet, gint row, gint col)
{
  gboolean veto = TRUE;

  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);

  if (row < 0 || col < 0) return FALSE;
  if (row >= yyy_row_count (sheet) || col >= xxx_column_count (sheet))
    return FALSE;

  /* _gtkextra_signal_emit (GTK_OBJECT (sheet), sheet_signals[ACTIVATE], row, col, &veto);
     if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return veto;
  */

  if (!veto) return FALSE;
  if (sheet->state != GTK_SHEET_NORMAL)
    {
      sheet->state = GTK_SHEET_NORMAL;
      gtk_sheet_real_unselect_range (sheet, NULL);
    }

  sheet->range.row0 = row;
  sheet->range.col0 = col;
  sheet->range.rowi = row;
  sheet->range.coli = col;
  sheet->active_cell.row = row;
  sheet->active_cell.col = col;
  sheet->selection_cell.row = row;
  sheet->selection_cell.col = col;

  GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);

  gtk_sheet_show_active_cell (sheet);


  g_signal_connect (G_OBJECT (gtk_sheet_get_entry (sheet)),
		    "changed",
		    G_CALLBACK (gtk_sheet_entry_changed),
		    sheet);

  _gtkextra_signal_emit (GTK_OBJECT (sheet), sheet_signals[ACTIVATE], row, col, &veto);

  return TRUE;
}

static void
gtk_sheet_show_active_cell (GtkSheet *sheet)
{
  GtkEntry *sheet_entry;
  GtkSheetCellAttr attributes;
  gchar *text = NULL;
  const gchar *old_text;
  GtkJustification justification;
  gint row, col;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  row = sheet->active_cell.row;
  col = sheet->active_cell.col;

  /* Don't show the active cell, if there is no active cell: */
  if (! (row >= 0 && col >= 0)) /* e.g row or coll == -1. */
    return;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;
  if (sheet->state != GTK_SHEET_NORMAL) return;
  if (GTK_SHEET_IN_SELECTION (sheet)) return;

  GTK_WIDGET_SET_FLAGS (GTK_WIDGET (sheet->sheet_entry), GTK_VISIBLE);

  sheet_entry = GTK_ENTRY (gtk_sheet_get_entry (sheet));

  gtk_sheet_get_attributes (sheet, row, col, &attributes);

  justification = GTK_JUSTIFY_LEFT;

  if (gtk_sheet_justify_entry (sheet))
    justification = attributes.justification;

  text = gtk_sheet_cell_get_text (sheet, row, col);
  if ( ! text )
    text = g_strdup ("");

  gtk_entry_set_visibility (GTK_ENTRY (sheet_entry), attributes.is_visible);

  if (gtk_sheet_locked (sheet) || !attributes.is_editable)
    gtk_editable_set_editable (GTK_EDITABLE (sheet_entry), FALSE);
  else
    gtk_editable_set_editable (GTK_EDITABLE (sheet_entry), TRUE);

  /*** Added by John Gotts. Mar 25, 2005 *********/
  old_text = gtk_entry_get_text (GTK_ENTRY (sheet_entry));
  if (strcmp (old_text, text) != 0)
    {
      if (!GTK_IS_ITEM_ENTRY (sheet_entry))
	gtk_entry_set_text (GTK_ENTRY (sheet_entry), text);
      else
	gtk_item_entry_set_text (GTK_ITEM_ENTRY (sheet_entry), text, justification);
    }

  gtk_sheet_entry_set_max_size (sheet);
  gtk_sheet_size_allocate_entry (sheet);

  gtk_widget_map (sheet->sheet_entry);

  gtk_widget_grab_focus (GTK_WIDGET (sheet_entry));

  dispose_string (sheet, text);
}

static void
gtk_sheet_draw_active_cell (GtkSheet *sheet)
{
  gint row, col;
  GtkSheetRange range;

  if (!GTK_WIDGET_DRAWABLE (GTK_WIDGET (sheet))) return;
  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;

  row = sheet->active_cell.row;
  col = sheet->active_cell.col;

  if (row < 0 || col < 0) return;

  if (!gtk_sheet_cell_isvisible (sheet, row, col)) return;

  range.col0 = range.coli = col;
  range.row0 = range.rowi = row;

  gtk_sheet_draw_border (sheet, range);
}


static void
gtk_sheet_make_backing_pixmap (GtkSheet *sheet, guint width, guint height)
{
  gint pixmap_width, pixmap_height;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;

  if (width == 0 && height == 0)
    {
      width = sheet->sheet_window_width + 80;
      height = sheet->sheet_window_height + 80;
    }

  if (!sheet->pixmap)
    {
      /* allocate */
      sheet->pixmap = gdk_pixmap_new (sheet->sheet_window,
				      width, height,
				      - 1);
      if (!GTK_SHEET_IS_FROZEN (sheet)) gtk_sheet_range_draw (sheet, NULL);
    }
  else
    {
      /* reallocate if sizes don't match */
      gdk_window_get_size (sheet->pixmap,
			   &pixmap_width, &pixmap_height);
      if ( (pixmap_width != width) || (pixmap_height != height))
	{
	  g_object_unref (sheet->pixmap);
	  sheet->pixmap = gdk_pixmap_new (sheet->sheet_window,
					  width, height,
					  - 1);
	  if (!GTK_SHEET_IS_FROZEN (sheet)) gtk_sheet_range_draw (sheet, NULL);
	}
    }
}

static void
gtk_sheet_new_selection (GtkSheet *sheet, GtkSheetRange *range)
{
  gint i, j, mask1, mask2;
  gint state, selected;
  gint x, y, width, height;
  GtkSheetRange new_range, aux_range;

  g_return_if_fail (sheet != NULL);

  if (range == NULL) range=&sheet->range;

  new_range=*range;

  range->row0 = MIN (range->row0, sheet->range.row0);
  range->rowi = MAX (range->rowi, sheet->range.rowi);
  range->col0 = MIN (range->col0, sheet->range.col0);
  range->coli = MAX (range->coli, sheet->range.coli);

  range->row0 = MAX (range->row0, MIN_VISIBLE_ROW (sheet));
  range->rowi = MIN (range->rowi, MAX_VISIBLE_ROW (sheet));
  range->col0 = MAX (range->col0, MIN_VISIBLE_COLUMN (sheet));
  range->coli = MIN (range->coli, MAX_VISIBLE_COLUMN (sheet));

  aux_range.row0 = MAX (new_range.row0, MIN_VISIBLE_ROW (sheet));
  aux_range.rowi = MIN (new_range.rowi, MAX_VISIBLE_ROW (sheet));
  aux_range.col0 = MAX (new_range.col0, MIN_VISIBLE_COLUMN (sheet));
  aux_range.coli = MIN (new_range.coli, MAX_VISIBLE_COLUMN (sheet));

  for (i = range->row0; i <= range->rowi; i++)
    {
      for (j = range->col0; j <= range->coli; j++)
	{

	  state = gtk_sheet_cell_get_state (sheet, i, j);
	  selected= (i <= new_range.rowi && i >= new_range.row0 &&
		     j <= new_range.coli && j >= new_range.col0) ? TRUE : FALSE;

	  if (state == GTK_STATE_SELECTED && selected &&
	      xxx_column_is_visible (sheet, j) && yyy_row_is_visible (sheet, i) &&
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
		  x = COLUMN_LEFT_XPIXEL (sheet, j);
		  y = ROW_TOP_YPIXEL (sheet, i);
		  width = COLUMN_LEFT_XPIXEL (sheet, j)- x+
		    xxx_column_width (sheet, j);
		  height = ROW_TOP_YPIXEL (sheet, i)- y + yyy_row_height (sheet, i);

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

		  gdk_draw_pixmap (sheet->sheet_window,
				   GTK_WIDGET (sheet)->style->fg_gc[GTK_STATE_NORMAL],
				   sheet->pixmap,
				   x + 1,
				   y + 1,
				   x + 1,
				   y + 1,
				   width,
				   height);

		  if (i != sheet->active_cell.row || j != sheet->active_cell.col)
		    {
		      x = COLUMN_LEFT_XPIXEL (sheet, j);
		      y = ROW_TOP_YPIXEL (sheet, i);
		      width = COLUMN_LEFT_XPIXEL (sheet, j)- x+
			xxx_column_width (sheet, j);

		      height = ROW_TOP_YPIXEL (sheet, i)- y + yyy_row_height (sheet, i);

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

	  state = gtk_sheet_cell_get_state (sheet, i, j);
	  selected= (i <= new_range.rowi && i >= new_range.row0 &&
		     j <= new_range.coli && j >= new_range.col0) ? TRUE : FALSE;

	  if (state == GTK_STATE_SELECTED && !selected &&
	      xxx_column_is_visible (sheet, j) && yyy_row_is_visible (sheet, i))
	    {

	      x = COLUMN_LEFT_XPIXEL (sheet, j);
	      y = ROW_TOP_YPIXEL (sheet, i);
	      width = COLUMN_LEFT_XPIXEL (sheet, j)- x+ xxx_column_width (sheet, j);
	      height = ROW_TOP_YPIXEL (sheet, i)- y + yyy_row_height (sheet, i);

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

	      gdk_draw_pixmap (sheet->sheet_window,
			       GTK_WIDGET (sheet)->style->fg_gc[GTK_STATE_NORMAL],
			       sheet->pixmap,
			       x + 1,
			       y + 1,
			       x + 1,
			       y + 1,
			       width,
			       height);
	    }
	}
    }

  for (i = range->row0; i <= range->rowi; i++)
    {
      for (j = range->col0; j <= range->coli; j++)
	{

	  state = gtk_sheet_cell_get_state (sheet, i, j);
	  selected= (i <= new_range.rowi && i >= new_range.row0 &&
		     j <= new_range.coli && j >= new_range.col0) ? TRUE : FALSE;

	  if (state != GTK_STATE_SELECTED && selected &&
	      xxx_column_is_visible (sheet, j) && yyy_row_is_visible (sheet, i) &&
	      (i != sheet->active_cell.row || j != sheet->active_cell.col))
	    {

	      x = COLUMN_LEFT_XPIXEL (sheet, j);
	      y = ROW_TOP_YPIXEL (sheet, i);
	      width = COLUMN_LEFT_XPIXEL (sheet, j)- x+ xxx_column_width (sheet, j);
	      height = ROW_TOP_YPIXEL (sheet, i)- y + yyy_row_height (sheet, i);

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

	  if (xxx_column_is_visible (sheet, j) && yyy_row_is_visible (sheet, i))
	    {

	      state = gtk_sheet_cell_get_state (sheet, i, j);

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
		  x = COLUMN_LEFT_XPIXEL (sheet, j);
		  y = ROW_TOP_YPIXEL (sheet, i);
		  width = xxx_column_width (sheet, j);
		  height = yyy_row_height (sheet, i);
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
    }


  *range = new_range;
  gtk_sheet_draw_corners (sheet, new_range);

}

static void
gtk_sheet_draw_border (GtkSheet *sheet, GtkSheetRange new_range)
{
  GtkWidget *widget;
  GdkRectangle area;
  gint i;
  gint x, y, width, height;

  widget = GTK_WIDGET (sheet);

  x = COLUMN_LEFT_XPIXEL (sheet, new_range.col0);
  y = ROW_TOP_YPIXEL (sheet, new_range.row0);
  width = COLUMN_LEFT_XPIXEL (sheet, new_range.coli) - x +
    xxx_column_width (sheet, new_range.coli);

  height = ROW_TOP_YPIXEL (sheet, new_range.rowi) - y +
    yyy_row_height (sheet, new_range.rowi);

  area.x = COLUMN_LEFT_XPIXEL (sheet, MIN_VISIBLE_COLUMN (sheet));
  area.y = ROW_TOP_YPIXEL (sheet, MIN_VISIBLE_ROW (sheet));
  area.width = sheet->sheet_window_width;
  area.height = sheet->sheet_window_height;

  if (x < 0)
    {
      width = width + x;
      x = 0;
    }
  if (width > area.width) width = area.width + 10;
  if (y < 0)
    {
      height = height + y;
      y = 0;
    }
  if (height > area.height) height = area.height + 10;

  gdk_gc_set_clip_rectangle (sheet->xor_gc, &area);

  for (i =- 1; i <= 1; ++i)
    gdk_draw_rectangle (sheet->sheet_window,
			sheet->xor_gc,
			FALSE,
			x + i,
			y + i,
			width - 2 * i,
			height - 2 * i);

  gdk_gc_set_clip_rectangle (sheet->xor_gc, NULL);


  gtk_sheet_draw_corners (sheet, new_range);
}

static void
gtk_sheet_draw_corners (GtkSheet *sheet, GtkSheetRange range)
{
  gint x, y;
  guint width = 1;

  if (gtk_sheet_cell_isvisible (sheet, range.row0, range.col0))
    {
      x = COLUMN_LEFT_XPIXEL (sheet, range.col0);
      y = ROW_TOP_YPIXEL (sheet, range.row0);
      gdk_draw_pixmap (sheet->sheet_window,
		       GTK_WIDGET (sheet)->style->fg_gc[GTK_STATE_NORMAL],
		       sheet->pixmap,
		       x - 1,
		       y - 1,
		       x - 1,
		       y - 1,
		       3,
		       3);
      gdk_draw_rectangle (sheet->sheet_window,
			  sheet->xor_gc,
			  TRUE,
			  x - 1, y - 1,
			  3, 3);
    }

  if (gtk_sheet_cell_isvisible (sheet, range.row0, range.coli) ||
      sheet->state == GTK_SHEET_COLUMN_SELECTED)
    {
      x = COLUMN_LEFT_XPIXEL (sheet, range.coli)+
	xxx_column_width (sheet, range.coli);
      y = ROW_TOP_YPIXEL (sheet, range.row0);
      width = 1;
      if (sheet->state == GTK_SHEET_COLUMN_SELECTED)
	{
	  y = ROW_TOP_YPIXEL (sheet, MIN_VISIBLE_ROW (sheet))+3;
	  width = 3;
	}
      gdk_draw_pixmap (sheet->sheet_window,
		       GTK_WIDGET (sheet)->style->fg_gc[GTK_STATE_NORMAL],
		       sheet->pixmap,
		       x - width,
		       y - width,
		       x - width,
		       y - width,
		       2 * width + 1,
		       2 * width + 1);
      gdk_draw_rectangle (sheet->sheet_window,
			  sheet->xor_gc,
			  TRUE,
			  x - width + width / 2, y - width + width / 2,
			  2 + width, 2 + width);
    }

  if (gtk_sheet_cell_isvisible (sheet, range.rowi, range.col0) ||
      sheet->state == GTK_SHEET_ROW_SELECTED)
    {
      x = COLUMN_LEFT_XPIXEL (sheet, range.col0);
      y = ROW_TOP_YPIXEL (sheet, range.rowi)+
	yyy_row_height (sheet, range.rowi);
      width = 1;
      if (sheet->state == GTK_SHEET_ROW_SELECTED)
	{
	  x = COLUMN_LEFT_XPIXEL (sheet, MIN_VISIBLE_COLUMN (sheet))+3;
	  width = 3;
	}
      gdk_draw_pixmap (sheet->sheet_window,
		       GTK_WIDGET (sheet)->style->fg_gc[GTK_STATE_NORMAL],
		       sheet->pixmap,
		       x - width,
		       y - width,
		       x - width,
		       y - width,
		       2 * width + 1,
		       2 * width + 1);
      gdk_draw_rectangle (sheet->sheet_window,
			  sheet->xor_gc,
			  TRUE,
			  x - width + width / 2, y - width + width / 2,
			  2 + width, 2 + width);
    }

  if (gtk_sheet_cell_isvisible (sheet, range.rowi, range.coli))
    {
      x = COLUMN_LEFT_XPIXEL (sheet, range.coli)+
	xxx_column_width (sheet, range.coli);
      y = ROW_TOP_YPIXEL (sheet, range.rowi)+
	yyy_row_height (sheet, range.rowi);
      width = 1;
      if (sheet->state == GTK_SHEET_RANGE_SELECTED) width = 3;
      if (sheet->state == GTK_SHEET_NORMAL) width = 3;
      gdk_draw_pixmap (sheet->sheet_window,
		       GTK_WIDGET (sheet)->style->fg_gc[GTK_STATE_NORMAL],
		       sheet->pixmap,
		       x - width,
		       y - width,
		       x - width,
		       y - width,
		       2 * width + 1,
		       2 * width + 1);
      gdk_draw_rectangle (sheet->sheet_window,
			  sheet->xor_gc,
			  TRUE,
			  x - width + width / 2, y - width + width / 2,
			  2 + width, 2 + width);

    }

}


static void
gtk_sheet_real_select_range (GtkSheet * sheet,
			     const GtkSheetRange * range)
{
  gint state;

  g_return_if_fail (sheet != NULL);

  if (range == NULL) range = &sheet->range;

  memcpy (&sheet->range, range, sizeof (*range));

  if (range->row0 < 0 || range->rowi < 0) return;
  if (range->col0 < 0 || range->coli < 0) return;

  state = sheet->state;

  if (range->coli != sheet->range.coli || range->col0 != sheet->range.col0 ||
      range->rowi != sheet->range.rowi || range->row0 != sheet->range.row0)
    {
      gtk_sheet_new_selection (sheet, &sheet->range);
    }
  else
    {
      gtk_sheet_draw_backing_pixmap (sheet, sheet->range);
      gtk_sheet_range_draw_selection (sheet, sheet->range);
    }

  gtk_sheet_update_primary_selection (sheet);

  g_signal_emit (G_OBJECT (sheet), sheet_signals[SELECT_RANGE], 0, &sheet->range);
}


void
gtk_sheet_get_selected_range		(GtkSheet *sheet,
					 GtkSheetRange *range)
{
  g_return_if_fail (sheet != NULL);
  *range = sheet->range;
}


void
gtk_sheet_select_range (GtkSheet * sheet, const GtkSheetRange *range)
{
  g_return_if_fail (sheet != NULL);

  if (range == NULL) range=&sheet->range;

  if (range->row0 < 0 || range->rowi < 0) return;
  if (range->col0 < 0 || range->coli < 0) return;


  if ( gtk_sheet_locked (sheet)) return ;

  if (sheet->state != GTK_SHEET_NORMAL)
    gtk_sheet_real_unselect_range (sheet, NULL);
  else
    {
      gboolean veto = TRUE;
      veto = gtk_sheet_deactivate_cell (sheet);
      if (!veto) return;
    }

  sheet->range.row0 = range->row0;
  sheet->range.rowi = range->rowi;
  sheet->range.col0 = range->col0;
  sheet->range.coli = range->coli;
  sheet->active_cell.row = range->row0;
  sheet->active_cell.col = range->col0;
  sheet->selection_cell.row = range->rowi;
  sheet->selection_cell.col = range->coli;

  sheet->state = GTK_SHEET_RANGE_SELECTED;
  gtk_sheet_real_select_range (sheet, NULL);

}

void
gtk_sheet_unselect_range (GtkSheet * sheet)
{
  gtk_sheet_real_unselect_range (sheet, NULL);
  sheet->state = GTK_STATE_NORMAL;

  gtk_sheet_activate_cell (sheet,
			   sheet->active_cell.row, sheet->active_cell.col);
}


static void
gtk_sheet_real_unselect_range (GtkSheet * sheet,
			       const GtkSheetRange *range)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)));

  if ( range == NULL)
    range = &sheet->range;

  if (range->row0 < 0 || range->rowi < 0) return;
  if (range->col0 < 0 || range->coli < 0) return;

  g_signal_emit (G_OBJECT (sheet), sheet_signals[SELECT_COLUMN], 0, -1);
  g_signal_emit (G_OBJECT (sheet), sheet_signals[SELECT_ROW], 0, -1);

  if (gtk_sheet_range_isvisible (sheet, *range))
    gtk_sheet_draw_backing_pixmap (sheet, *range);

  sheet->range.row0 = -1;
  sheet->range.rowi = -1;
  sheet->range.col0 = -1;
  sheet->range.coli = -1;

  gtk_sheet_position_children (sheet);
}


static gint
gtk_sheet_expose (GtkWidget * widget,
		  GdkEventExpose * event)
{
  GtkSheet *sheet;
  GtkSheetRange range;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);


  sheet = GTK_SHEET (widget);

  if (GTK_WIDGET_DRAWABLE (widget))
    {
      range.row0 = ROW_FROM_YPIXEL (sheet, event->area.y);
      range.col0 = COLUMN_FROM_XPIXEL (sheet, event->area.x);
      range.rowi = ROW_FROM_YPIXEL (sheet, event->area.y + event->area.height);
      range.coli = COLUMN_FROM_XPIXEL (sheet, event->area.x + event->area.width);

      /* exposure events on the sheet */
      if (event->window == sheet->row_title_window &&
	  sheet->row_titles_visible)
	{
	  gint i;
	  for (i = MIN_VISIBLE_ROW (sheet); i <= MAX_VISIBLE_ROW (sheet); i++)
	    gtk_sheet_row_title_button_draw (sheet, i);
	}

      if (event->window == sheet->column_title_window &&
	  sheet->column_titles_visible)
	{
	  gint i;
	  for (i = MIN_VISIBLE_COLUMN (sheet); i <= MAX_VISIBLE_COLUMN (sheet); i++)
	    gtk_sheet_column_title_button_draw (sheet, i);
	}

      if (event->window == sheet->sheet_window)
	{
	  gtk_sheet_draw_backing_pixmap (sheet, range);

	  if (sheet->state != GTK_SHEET_NORMAL)
	    {
	      if (gtk_sheet_range_isvisible (sheet, sheet->range))
		gtk_sheet_draw_backing_pixmap (sheet, sheet->range);
	      if (GTK_SHEET_IN_RESIZE (sheet) || GTK_SHEET_IN_DRAG (sheet))
		gtk_sheet_draw_backing_pixmap (sheet, sheet->drag_range);

	      if (gtk_sheet_range_isvisible (sheet, sheet->range))
		gtk_sheet_range_draw_selection (sheet, sheet->range);
	      if (GTK_SHEET_IN_RESIZE (sheet) || GTK_SHEET_IN_DRAG (sheet))
		draw_xor_rectangle (sheet, sheet->drag_range);
	    }

	  if ((!GTK_SHEET_IN_XDRAG (sheet)) && (!GTK_SHEET_IN_YDRAG (sheet)))
	    {
	      if (sheet->state == GTK_SHEET_NORMAL)
		{
		  gtk_sheet_draw_active_cell (sheet);
		  if (!GTK_SHEET_IN_SELECTION (sheet))
		    gtk_widget_queue_draw (sheet->sheet_entry);
		}
	    }
	}
    }

  if (sheet->state != GTK_SHEET_NORMAL && GTK_SHEET_IN_SELECTION (sheet))
    gtk_widget_grab_focus (GTK_WIDGET (sheet));

  (* GTK_WIDGET_CLASS (parent_class)->expose_event) (widget, event);

  return FALSE;
}


static gboolean
gtk_sheet_button_press (GtkWidget * widget,
			GdkEventButton * event)
{
  GtkSheet *sheet;
  GdkModifierType mods;
  gint x, y, row, column;
  gboolean veto;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  sheet = GTK_SHEET (widget);

  /* Cancel any pending tooltips */
  if (sheet->motion_timer)
    {
      g_source_remove (sheet->motion_timer);
      sheet->motion_timer = 0;
    }

  gtk_widget_get_pointer (widget, &x, &y);
  gtk_sheet_get_pixel_info (sheet, x, y, &row, &column);


  if (event->window == sheet->column_title_window)
    {
      g_signal_emit (G_OBJECT (sheet),
		     sheet_signals[BUTTON_EVENT_COLUMN], 0,
		     column, event);

      if ( event->type == GDK_2BUTTON_PRESS && event->button == 1)
	g_signal_emit (G_OBJECT (sheet),
		       sheet_signals[DOUBLE_CLICK_COLUMN], 0, column);

    }
  else if (event->window == sheet->row_title_window)
    {
      g_signal_emit (G_OBJECT (sheet),
		     sheet_signals[BUTTON_EVENT_ROW], 0,
		     row, event);

      if ( event->type == GDK_2BUTTON_PRESS && event->button == 1)
	g_signal_emit (G_OBJECT (sheet),
		       sheet_signals[DOUBLE_CLICK_ROW], 0, row);
    }


  gdk_window_get_pointer (widget->window, NULL, NULL, &mods);

  if (! (mods & GDK_BUTTON1_MASK)) return TRUE;


  /* press on resize windows */
  if (event->window == sheet->column_title_window &&
      gtk_sheet_columns_resizable (sheet))
    {
      gtk_widget_get_pointer (widget, &sheet->x_drag, NULL);
      if (POSSIBLE_XDRAG (sheet, sheet->x_drag, &sheet->drag_cell.col))
	{
	  guint req;
	  if (event->type == GDK_2BUTTON_PRESS)
	    {
	      gtk_sheet_autoresize_column (sheet, sheet->drag_cell.col);
	      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_XDRAG);
	      return TRUE;
	    }
	  gtk_sheet_column_size_request (sheet, sheet->drag_cell.col, &req);
	  GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IN_XDRAG);
	  gdk_pointer_grab (sheet->column_title_window, FALSE,
			    GDK_POINTER_MOTION_HINT_MASK |
			    GDK_BUTTON1_MOTION_MASK |
			    GDK_BUTTON_RELEASE_MASK,
			    NULL, NULL, event->time);

	  draw_xor_vline (sheet);
	  return TRUE;
	}
    }

  if (event->window == sheet->row_title_window && gtk_sheet_rows_resizable (sheet))
    {
      gtk_widget_get_pointer (widget, NULL, &sheet->y_drag);

      if (POSSIBLE_YDRAG (sheet, sheet->y_drag, &sheet->drag_cell.row))
	{
	  guint req;
	  gtk_sheet_row_size_request (sheet, sheet->drag_cell.row, &req);
	  GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IN_YDRAG);
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
      gtk_sheet_get_pixel_info (sheet, x, y, &row, &column);
      gdk_pointer_grab (sheet->sheet_window, FALSE,
			GDK_POINTER_MOTION_HINT_MASK |
			GDK_BUTTON1_MOTION_MASK |
			GDK_BUTTON_RELEASE_MASK,
			NULL, NULL, event->time);
      gtk_grab_add (GTK_WIDGET (sheet));

      /* This seems to be a kludge to work around a problem where the sheet
	 scrolls to another position.  The timeout scrolls it back to its
	 original posn. 	 JMD 3 July 2007
      */
      gtk_widget_grab_focus (GTK_WIDGET (sheet));

      if (sheet->selection_mode != GTK_SELECTION_SINGLE &&
	  sheet->selection_mode != GTK_SELECTION_NONE &&
	  sheet->cursor_drag->type == GDK_SIZING &&
	  !GTK_SHEET_IN_SELECTION (sheet) && !GTK_SHEET_IN_RESIZE (sheet))
	{
	  if (sheet->state == GTK_STATE_NORMAL)
	    {
	      row = sheet->active_cell.row;
	      column = sheet->active_cell.col;
	      if (!gtk_sheet_deactivate_cell (sheet)) return FALSE;
	      sheet->active_cell.row = row;
	      sheet->active_cell.col = column;
	      sheet->drag_range = sheet->range;
	      sheet->state = GTK_SHEET_RANGE_SELECTED;
	      gtk_sheet_select_range (sheet, &sheet->drag_range);
	    }
	  sheet->x_drag = x;
	  sheet->y_drag = y;
	  if (row > sheet->range.rowi) row--;
	  if (column > sheet->range.coli) column--;
	  sheet->drag_cell.row = row;
	  sheet->drag_cell.col = column;
	  sheet->drag_range = sheet->range;
	  draw_xor_rectangle (sheet, sheet->drag_range);
	  GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IN_RESIZE);
	}
      else if (sheet->cursor_drag->type == GDK_TOP_LEFT_ARROW &&
	       !GTK_SHEET_IN_SELECTION (sheet)
	       && ! GTK_SHEET_IN_DRAG (sheet)
	       && ! gtk_sheet_locked (sheet)
	       && sheet->active_cell.row >= 0
	       && sheet->active_cell.col >= 0
	       )
	{
	  if (sheet->state == GTK_STATE_NORMAL)
	    {
	      row = sheet->active_cell.row;
	      column = sheet->active_cell.col;
	      if (!gtk_sheet_deactivate_cell (sheet)) return FALSE;
	      sheet->active_cell.row = row;
	      sheet->active_cell.col = column;
	      sheet->drag_range = sheet->range;
	      sheet->state = GTK_SHEET_RANGE_SELECTED;
	      gtk_sheet_select_range (sheet, &sheet->drag_range);
	    }
	  sheet->x_drag = x;
	  sheet->y_drag = y;
	  if (row < sheet->range.row0) row++;
	  if (row > sheet->range.rowi) row--;
	  if (column < sheet->range.col0) column++;
	  if (column > sheet->range.coli) column--;
	  sheet->drag_cell.row = row;
	  sheet->drag_cell.col = column;
	  sheet->drag_range = sheet->range;
	  draw_xor_rectangle (sheet, sheet->drag_range);
	  GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IN_DRAG);
	}
      else
	{
	  gtk_sheet_click_cell (sheet, row, column, &veto);
	  if (veto) GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);
	}
    }

  if (event->window == sheet->column_title_window)
    {
      gtk_widget_get_pointer (widget, &x, &y);
      column = COLUMN_FROM_XPIXEL (sheet, x);

      if (xxx_column_is_sensitive (sheet, column))
	{
	  gtk_sheet_click_cell (sheet, - 1, column, &veto);
	  gtk_grab_add (GTK_WIDGET (sheet));
	  gtk_widget_grab_focus (GTK_WIDGET (sheet));
	  GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);
	}
    }

  if (event->window == sheet->row_title_window)
    {
      gtk_widget_get_pointer (widget, &x, &y);
      row = ROW_FROM_YPIXEL (sheet, y);
      if (yyy_row_is_sensitive (sheet, row))
	{
	  gtk_sheet_click_cell (sheet, row, - 1, &veto);
	  gtk_grab_add (GTK_WIDGET (sheet));
	  gtk_widget_grab_focus (GTK_WIDGET (sheet));
	  GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);
	}
    }

  return TRUE;
}

#if 0
static gint
gtk_sheet_scroll (gpointer data)
{
  GtkSheet *sheet;
  gint x, y, row, column;
  gint move;

  sheet = GTK_SHEET (data);

  GDK_THREADS_ENTER ();

  gtk_widget_get_pointer (GTK_WIDGET (sheet), &x, &y);
  gtk_sheet_get_pixel_info (sheet, x, y, &row, &column);

  move = TRUE;

  if (GTK_SHEET_IN_SELECTION (sheet))
    gtk_sheet_extend_selection (sheet, row, column);

  if (GTK_SHEET_IN_DRAG (sheet) || GTK_SHEET_IN_RESIZE (sheet))
    {
      move = gtk_sheet_move_query (sheet, row, column);
      if (move) draw_xor_rectangle (sheet, sheet->drag_range);
    }

  GDK_THREADS_LEAVE ();

  return TRUE;
}
#endif

static void
gtk_sheet_click_cell (GtkSheet *sheet, gint row, gint column, gboolean *veto)
{
  *veto = TRUE;

  if (row >= yyy_row_count (sheet) || column >= xxx_column_count (sheet))
    {
      *veto = FALSE;
      return;
    }

  if (column >= 0 && row >= 0)
    if (! xxx_column_is_visible (sheet, column) || !yyy_row_is_visible (sheet, row))
      {
	*veto = FALSE;
	return;
      }

  _gtkextra_signal_emit (GTK_OBJECT (sheet), sheet_signals[TRAVERSE],
			 sheet->active_cell.row, sheet->active_cell.col,
			 &row, &column, veto);

  if (!*veto)
    {
      if (sheet->state == GTK_STATE_NORMAL) return;

      row = sheet->active_cell.row;
      column = sheet->active_cell.col;

      gtk_sheet_activate_cell (sheet, row, column);
      return;
    }

  if (row == -1 && column >= 0)
    {
      if (gtk_sheet_autoscroll (sheet))
	gtk_sheet_move_query (sheet, row, column);
      gtk_sheet_select_column (sheet, column);
      return;
    }
  if (column == -1 && row >= 0)
    {
      if (gtk_sheet_autoscroll (sheet))
	gtk_sheet_move_query (sheet, row, column);
      gtk_sheet_select_row (sheet, row);
      return;
    }

  if (row == - 1 && column == - 1)
    {
      sheet->range.row0 = 0;
      sheet->range.col0 = 0;
      sheet->range.rowi = yyy_row_count (sheet) - 1;
      sheet->range.coli = xxx_column_count (sheet) - 1;
      sheet->active_cell.row = 0;
      sheet->active_cell.col = 0;
      gtk_sheet_select_range (sheet, NULL);
      return;
    }

  if (row != -1 && column != -1)
    {
      if (sheet->state != GTK_SHEET_NORMAL)
	{
	  sheet->state = GTK_SHEET_NORMAL;
	  gtk_sheet_real_unselect_range (sheet, NULL);
	}
      else
	{
	  if (!gtk_sheet_deactivate_cell (sheet))
	    {
	      *veto = FALSE;
	      return;
	    }
	}

      if (gtk_sheet_autoscroll (sheet))
	gtk_sheet_move_query (sheet, row, column);
      sheet->active_cell.row = row;
      sheet->active_cell.col = column;
      sheet->selection_cell.row = row;
      sheet->selection_cell.col = column;
      sheet->range.row0 = row;
      sheet->range.col0 = column;
      sheet->range.rowi = row;
      sheet->range.coli = column;
      sheet->state = GTK_SHEET_NORMAL;
      GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);
      gtk_sheet_draw_active_cell (sheet);
      return;
    }

  g_assert_not_reached ();
  gtk_sheet_activate_cell (sheet, sheet->active_cell.row,
			   sheet->active_cell.col);
}

static gint
gtk_sheet_button_release (GtkWidget * widget,
			  GdkEventButton * event)
{
  GtkSheet *sheet;
  gint x, y;

  sheet = GTK_SHEET (widget);

  /* release on resize windows */
  if (GTK_SHEET_IN_XDRAG (sheet))
    {
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_XDRAG);
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);
      gtk_widget_get_pointer (widget, &x, NULL);
      gdk_pointer_ungrab (event->time);
      draw_xor_vline (sheet);

      gtk_sheet_set_column_width (sheet, sheet->drag_cell.col,
				  new_column_width (sheet, sheet->drag_cell.col, &x));
      sheet->old_hadjustment = -1.;
      g_signal_emit_by_name (G_OBJECT (sheet->hadjustment), "value_changed");
      return TRUE;
    }

  if (GTK_SHEET_IN_YDRAG (sheet))
    {
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_YDRAG);
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);
      gtk_widget_get_pointer (widget, NULL, &y);
      gdk_pointer_ungrab (event->time);
      draw_xor_hline (sheet);

      gtk_sheet_set_row_height (sheet, sheet->drag_cell.row, new_row_height (sheet, sheet->drag_cell.row, &y));
      sheet->old_vadjustment = -1.;
      g_signal_emit_by_name (G_OBJECT (sheet->vadjustment), "value_changed");
      return TRUE;
    }


  if (GTK_SHEET_IN_DRAG (sheet))
    {
      GtkSheetRange old_range;
      draw_xor_rectangle (sheet, sheet->drag_range);
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_DRAG);
      gdk_pointer_ungrab (event->time);

      gtk_sheet_real_unselect_range (sheet, NULL);

      sheet->active_cell.row = sheet->active_cell.row +
	(sheet->drag_range.row0 - sheet->range.row0);
      sheet->active_cell.col = sheet->active_cell.col +
	(sheet->drag_range.col0 - sheet->range.col0);
      sheet->selection_cell.row = sheet->selection_cell.row +
	(sheet->drag_range.row0 - sheet->range.row0);
      sheet->selection_cell.col = sheet->selection_cell.col +
	(sheet->drag_range.col0 - sheet->range.col0);
      old_range = sheet->range;
      sheet->range = sheet->drag_range;
      sheet->drag_range = old_range;
      g_signal_emit (G_OBJECT (sheet), sheet_signals[MOVE_RANGE], 0,
		     &sheet->drag_range, &sheet->range);
      gtk_sheet_select_range (sheet, &sheet->range);
    }

  if (GTK_SHEET_IN_RESIZE (sheet))
    {
      GtkSheetRange old_range;
      draw_xor_rectangle (sheet, sheet->drag_range);
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_RESIZE);
      gdk_pointer_ungrab (event->time);

      gtk_sheet_real_unselect_range (sheet, NULL);

      sheet->active_cell.row = sheet->active_cell.row +
	(sheet->drag_range.row0 - sheet->range.row0);
      sheet->active_cell.col = sheet->active_cell.col +
	(sheet->drag_range.col0 - sheet->range.col0);
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

      if (sheet->state == GTK_STATE_NORMAL) sheet->state = GTK_SHEET_RANGE_SELECTED;
      g_signal_emit (G_OBJECT (sheet), sheet_signals[RESIZE_RANGE], 0,
		     &sheet->drag_range, &sheet->range);
      gtk_sheet_select_range (sheet, &sheet->range);
    }

  if (sheet->state == GTK_SHEET_NORMAL && GTK_SHEET_IN_SELECTION (sheet))
    {
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);
      gdk_pointer_ungrab (event->time);
      gtk_sheet_activate_cell (sheet, sheet->active_cell.row,
			       sheet->active_cell.col);
    }

  if (GTK_SHEET_IN_SELECTION)
    gdk_pointer_ungrab (event->time);
  gtk_grab_remove (GTK_WIDGET (sheet));

  GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);

  return TRUE;
}

/* Shamelessly lifted from gtktooltips */
static gboolean
gtk_sheet_subtitle_paint_window (GtkWidget *tip_window)
{
  GtkRequisition req;

  gtk_widget_size_request (tip_window, &req);
  gtk_paint_flat_box (tip_window->style, tip_window->window,
		      GTK_STATE_NORMAL, GTK_SHADOW_OUT,
		      NULL, GTK_WIDGET(tip_window), "tooltip",
		      0, 0, req.width, req.height);

  return FALSE;
}

static GtkSheetHoverTitle *
create_hover_window (void)
{
  GtkSheetHoverTitle *hw = malloc (sizeof (*hw));

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
		    G_CALLBACK (gtk_sheet_subtitle_paint_window),
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
show_subtitle (GtkSheet *sheet, gint row, gint column, const gchar *subtitle)
{
  gint x, y;
  gint px, py;
  gint width;

  if ( ! subtitle )
    return;

  if ( ! sheet->hover_window)
    {
      sheet->hover_window = create_hover_window ();
      gtk_widget_add_events (GTK_WIDGET (sheet), GDK_LEAVE_NOTIFY_MASK);

      g_signal_connect_swapped (sheet, "leave-notify-event",
				G_CALLBACK (gtk_widget_hide),
				sheet->hover_window->window);
    }

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
  GtkSheet *sheet = GTK_SHEET (data);
  gint x, y;
  gint row, column;
  gtk_widget_get_pointer (GTK_WIDGET (sheet), &x, &y);

  if ( gtk_sheet_get_pixel_info (sheet, x, y, &row, &column) )
    {
      if ( column == -1 && row == -1 )
	return FALSE;

      if ( column == -1)
	{
	  GSheetRow *row_geo = sheet->row_geometry;
	  gchar *text;

	  text = g_sheet_row_get_subtitle (row_geo, row);

	  show_subtitle (sheet, row, column, text);
	  g_free (text);
	}

      if ( row == -1)
	{
	  GSheetColumn *col_geo = sheet->column_geometry;
	  gchar *text;

	  text = g_sheet_column_get_subtitle (col_geo, column);

	  show_subtitle (sheet, row, column, text );

	  g_free (text);
	}
    }

  return FALSE;
}

static gint
gtk_sheet_motion (GtkWidget * widget,
		  GdkEventMotion * event)
{
  GtkSheet *sheet;
  GdkModifierType mods;
  GdkCursorType new_cursor;
  gint x, y;
  gint row, column;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  sheet = GTK_SHEET (widget);

  /* selections on the sheet */
  x = event->x;
  y = event->y;

  if (!sheet->hover_window || ! GTK_WIDGET_VISIBLE (sheet->hover_window->window))
    {
      if ( sheet->motion_timer > 0 )
	g_source_remove (sheet->motion_timer);
      sheet->motion_timer = g_timeout_add (TIMEOUT_HOVER, motion_timeout_callback, sheet);
    }
  else
    {
      gint row, column;
      gint wx, wy;
      gtk_widget_get_pointer (widget, &wx, &wy);

      if ( gtk_sheet_get_pixel_info (sheet, wx, wy, &row, &column) )
	{
	  if ( row != sheet->hover_window->row || column != sheet->hover_window->column)
	    {
	      gtk_widget_hide (sheet->hover_window->window);
	    }
	}
    }

  if (event->window == sheet->column_title_window &&
      gtk_sheet_columns_resizable (sheet))
    {
      gtk_widget_get_pointer (widget, &x, &y);
      if (!GTK_SHEET_IN_SELECTION (sheet) &&
	  POSSIBLE_XDRAG (sheet, x, &column))
	{
	  new_cursor = GDK_SB_H_DOUBLE_ARROW;
	  if (new_cursor != sheet->cursor_drag->type)
	    {
	      gdk_cursor_destroy (sheet->cursor_drag);
	      sheet->cursor_drag = gdk_cursor_new (GDK_SB_H_DOUBLE_ARROW);
	      gdk_window_set_cursor (sheet->column_title_window,
				     sheet->cursor_drag);
	    }
	}
      else
	{
	  new_cursor = GDK_TOP_LEFT_ARROW;
	  if (!GTK_SHEET_IN_XDRAG (sheet) &&
	      new_cursor != sheet->cursor_drag->type)
	    {
	      gdk_cursor_destroy (sheet->cursor_drag);
	      sheet->cursor_drag = gdk_cursor_new (GDK_TOP_LEFT_ARROW);
	      gdk_window_set_cursor (sheet->column_title_window,
				     sheet->cursor_drag);
	    }
	}
    }

  if (event->window == sheet->row_title_window &&
      gtk_sheet_rows_resizable (sheet))
    {
      gtk_widget_get_pointer (widget, &x, &y);
      if (!GTK_SHEET_IN_SELECTION (sheet) && POSSIBLE_YDRAG (sheet, y, &column))
	{
	  new_cursor = GDK_SB_V_DOUBLE_ARROW;
	  if (new_cursor != sheet->cursor_drag->type)
	    {
	      gdk_cursor_destroy (sheet->cursor_drag);
	      sheet->cursor_drag = gdk_cursor_new (GDK_SB_V_DOUBLE_ARROW);
	      gdk_window_set_cursor (sheet->row_title_window, sheet->cursor_drag);
	    }
	}
      else
	{
	  new_cursor = GDK_TOP_LEFT_ARROW;
	  if (!GTK_SHEET_IN_YDRAG (sheet) &&
	      new_cursor != sheet->cursor_drag->type)
	    {
	      gdk_cursor_destroy (sheet->cursor_drag);
	      sheet->cursor_drag = gdk_cursor_new (GDK_TOP_LEFT_ARROW);
	      gdk_window_set_cursor (sheet->row_title_window, sheet->cursor_drag);
	    }
	}
    }

  new_cursor = GDK_PLUS;
  if ( event->window == sheet->sheet_window &&
       !POSSIBLE_DRAG (sheet, x, y, &row, &column) &&
       !GTK_SHEET_IN_DRAG (sheet) &&
       !POSSIBLE_RESIZE (sheet, x, y, &row, &column) &&
       !GTK_SHEET_IN_RESIZE (sheet) &&
       new_cursor != sheet->cursor_drag->type)
    {
      gdk_cursor_destroy (sheet->cursor_drag);
      sheet->cursor_drag = gdk_cursor_new (GDK_PLUS);
      gdk_window_set_cursor (sheet->sheet_window, sheet->cursor_drag);
    }

  new_cursor = GDK_TOP_LEFT_ARROW;
  if ( event->window == sheet->sheet_window &&
       ! (POSSIBLE_RESIZE (sheet, x, y, &row, &column) || GTK_SHEET_IN_RESIZE (sheet)) && (POSSIBLE_DRAG (sheet, x, y, &row, &column) || GTK_SHEET_IN_DRAG (sheet)) &&

       new_cursor != sheet->cursor_drag->type)
    {
      gdk_cursor_destroy (sheet->cursor_drag);
      sheet->cursor_drag = gdk_cursor_new (GDK_TOP_LEFT_ARROW);
      gdk_window_set_cursor (sheet->sheet_window, sheet->cursor_drag);
    }

  new_cursor = GDK_SIZING;
  if ( event->window == sheet->sheet_window &&
       sheet->selection_mode != GTK_SELECTION_NONE &&
       !GTK_SHEET_IN_DRAG (sheet) &&
       (POSSIBLE_RESIZE (sheet, x, y, &row, &column) ||
	GTK_SHEET_IN_RESIZE (sheet)) &&
       new_cursor != sheet->cursor_drag->type)
    {
      gdk_cursor_destroy (sheet->cursor_drag);
      sheet->cursor_drag = gdk_cursor_new (GDK_SIZING);
      gdk_window_set_cursor (sheet->sheet_window, sheet->cursor_drag);
    }


  gdk_window_get_pointer (widget->window, &x, &y, &mods);
  if (! (mods & GDK_BUTTON1_MASK)) return FALSE;

  if (GTK_SHEET_IN_XDRAG (sheet))
    {
      if (event->is_hint || event->window != widget->window)
	gtk_widget_get_pointer (widget, &x, NULL);
      else
	x = event->x;

      new_column_width (sheet, sheet->drag_cell.col, &x);
      if (x != sheet->x_drag)
	{
	  draw_xor_vline (sheet);
	  sheet->x_drag = x;
	  draw_xor_vline (sheet);
	}
      return TRUE;
    }

  if (GTK_SHEET_IN_YDRAG (sheet))
    {
      if (event->is_hint || event->window != widget->window)
	gtk_widget_get_pointer (widget, NULL, &y);
      else
	y = event->y;

      new_row_height (sheet, sheet->drag_cell.row, &y);
      if (y != sheet->y_drag)
	{
	  draw_xor_hline (sheet);
	  sheet->y_drag = y;
	  draw_xor_hline (sheet);
	}
      return TRUE;
    }

  if (GTK_SHEET_IN_DRAG (sheet))
    {
      GtkSheetRange aux;
      column = COLUMN_FROM_XPIXEL (sheet, x)- sheet->drag_cell.col;
      row = ROW_FROM_YPIXEL (sheet, y)- sheet->drag_cell.row;
      if (sheet->state == GTK_SHEET_COLUMN_SELECTED) row = 0;
      if (sheet->state == GTK_SHEET_ROW_SELECTED) column = 0;
      sheet->x_drag = x;
      sheet->y_drag = y;
      aux = sheet->range;
      if (aux.row0 + row >= 0 && aux.rowi + row < yyy_row_count (sheet) &&
	  aux.col0 + column >= 0 && aux.coli + column < xxx_column_count (sheet))
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

  if (GTK_SHEET_IN_RESIZE (sheet))
    {
      GtkSheetRange aux;
      gint v_h, current_col, current_row, col_threshold, row_threshold;
      v_h = 1;

      if (abs (x - COLUMN_LEFT_XPIXEL (sheet, sheet->drag_cell.col)) >
	  abs (y - ROW_TOP_YPIXEL (sheet, sheet->drag_cell.row))) v_h = 2;

      current_col = COLUMN_FROM_XPIXEL (sheet, x);
      current_row = ROW_FROM_YPIXEL (sheet, y);
      column = current_col - sheet->drag_cell.col;
      row = current_row - sheet->drag_cell.row;

      /*use half of column width resp. row height as threshold to
	expand selection*/
      col_threshold = COLUMN_LEFT_XPIXEL (sheet, current_col) +
	xxx_column_width (sheet, current_col) / 2;
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
      row_threshold = ROW_TOP_YPIXEL (sheet, current_row) +
	yyy_row_height (sheet, current_row)/2;
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

      if (sheet->state == GTK_SHEET_COLUMN_SELECTED) row = 0;
      if (sheet->state == GTK_SHEET_ROW_SELECTED) column = 0;
      sheet->x_drag = x;
      sheet->y_drag = y;
      aux = sheet->range;

      if (v_h == 1)
	column = 0;
      else
	row = 0;

      if (aux.row0 + row >= 0 && aux.rowi + row < yyy_row_count (sheet) &&
	  aux.col0 + column >= 0 && aux.coli + column < xxx_column_count (sheet))
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

  gtk_sheet_get_pixel_info (sheet, x, y, &row, &column);

  if (sheet->state == GTK_SHEET_NORMAL && row == sheet->active_cell.row &&
      column == sheet->active_cell.col) return TRUE;

  if (GTK_SHEET_IN_SELECTION (sheet) && mods&GDK_BUTTON1_MASK)
    gtk_sheet_extend_selection (sheet, row, column);

  return TRUE;
}

static gboolean
gtk_sheet_move_query (GtkSheet *sheet, gint row, gint column)
{
  gint row_move, column_move;
  gfloat row_align, col_align;
  guint height, width;
  gint new_row = row;
  gint new_col = column;

  row_move = FALSE;
  column_move = FALSE;
  row_align =- 1.;
  col_align =- 1.;

  height = sheet->sheet_window_height;
  width = sheet->sheet_window_width;

  if (row >= MAX_VISIBLE_ROW (sheet) && sheet->state != GTK_SHEET_COLUMN_SELECTED)
    {
      row_align = 1.;
      new_row = MIN (yyy_row_count (sheet), row + 1);
      row_move = TRUE;
      if (MAX_VISIBLE_ROW (sheet) == yyy_row_count (sheet) - 1 &&
	  ROW_TOP_YPIXEL (sheet, yyy_row_count (sheet)- 1) +
	  yyy_row_height (sheet, yyy_row_count (sheet)- 1) < height)
	{
	  row_move = FALSE;
	  row_align = -1.;
	}
    }
  if (row < MIN_VISIBLE_ROW (sheet) && sheet->state != GTK_SHEET_COLUMN_SELECTED)
    {
      row_align= 0.;
      row_move = TRUE;
    }
  if (column >= MAX_VISIBLE_COLUMN (sheet) && sheet->state != GTK_SHEET_ROW_SELECTED)
    {
      col_align = 1.;
      new_col = MIN (xxx_column_count (sheet) - 1, column + 1);
      column_move = TRUE;
      if (MAX_VISIBLE_COLUMN (sheet) == (xxx_column_count (sheet) - 1) &&
	  COLUMN_LEFT_XPIXEL (sheet, xxx_column_count (sheet) - 1) +
	  xxx_column_width (sheet, xxx_column_count (sheet) - 1) < width)
	{
	  column_move = FALSE;
	  col_align = -1.;
	}
    }
  if (column < MIN_VISIBLE_COLUMN (sheet) && sheet->state != GTK_SHEET_ROW_SELECTED)
    {
      col_align = 0.;
      column_move = TRUE;
    }

  if (row_move || column_move)
    {
      gtk_sheet_moveto (sheet, new_row, new_col, row_align, col_align);
    }

  return (row_move || column_move);
}

static void
gtk_sheet_extend_selection (GtkSheet *sheet, gint row, gint column)
{
  GtkSheetRange range;
  gint state;
  gint r, c;

  if (row == sheet->selection_cell.row && column == sheet->selection_cell.col)
    return;

  if (sheet->selection_mode == GTK_SELECTION_SINGLE) return;

  gtk_sheet_move_query (sheet, row, column);
  gtk_widget_grab_focus (GTK_WIDGET (sheet));

  if (GTK_SHEET_IN_DRAG (sheet)) return;

  state = sheet->state;

  switch (sheet->state)
    {
    case GTK_SHEET_ROW_SELECTED:
      column = xxx_column_count (sheet) - 1;
      break;
    case GTK_SHEET_COLUMN_SELECTED:
      row = yyy_row_count (sheet) - 1;
      break;
    case GTK_SHEET_NORMAL:
      sheet->state = GTK_SHEET_RANGE_SELECTED;
      r = sheet->active_cell.row;
      c = sheet->active_cell.col;
      sheet->range.col0 = c;
      sheet->range.row0 = r;
      sheet->range.coli = c;
      sheet->range.rowi = r;
      gdk_draw_pixmap (sheet->sheet_window,
		       GTK_WIDGET (sheet)->style->fg_gc[GTK_STATE_NORMAL],
		       sheet->pixmap,
		       COLUMN_LEFT_XPIXEL (sheet, c)- 1,
		       ROW_TOP_YPIXEL (sheet, r)- 1,
		       COLUMN_LEFT_XPIXEL (sheet, c)- 1,
		       ROW_TOP_YPIXEL (sheet, r)- 1,
		       xxx_column_width (sheet, c)+4,
		       yyy_row_height (sheet, r)+4);
      gtk_sheet_range_draw_selection (sheet, sheet->range);
    case GTK_SHEET_RANGE_SELECTED:
      sheet->state = GTK_SHEET_RANGE_SELECTED;
    }

  sheet->selection_cell.row = row;
  sheet->selection_cell.col = column;

  range.col0 = MIN (column, sheet->active_cell.col);
  range.coli = MAX (column, sheet->active_cell.col);
  range.row0 = MIN (row, sheet->active_cell.row);
  range.rowi = MAX (row, sheet->active_cell.row);

  if (range.row0 != sheet->range.row0 || range.rowi != sheet->range.rowi ||
      range.col0 != sheet->range.col0 || range.coli != sheet->range.coli ||
      state == GTK_SHEET_NORMAL)
    gtk_sheet_real_select_range (sheet, &range);

}

static gint
gtk_sheet_entry_key_press (GtkWidget *widget,
			   GdkEventKey *key)
{
  gboolean focus;
  g_signal_emit_by_name (G_OBJECT (widget), "key_press_event", key, &focus);
  return focus;
}

static gint
gtk_sheet_key_press (GtkWidget *widget,
		     GdkEventKey *key)
{
  GtkSheet *sheet;
  gint row, col;
  gint state;
  gboolean extend_selection = FALSE;
  gboolean force_move = FALSE;
  gboolean in_selection = FALSE;
  gboolean veto = TRUE;
  gint scroll = 1;

  sheet = GTK_SHEET (widget);

  if (key->state & GDK_CONTROL_MASK || key->keyval == GDK_Control_L ||
      key->keyval == GDK_Control_R) return FALSE;

  extend_selection = (key->state & GDK_SHIFT_MASK) || key->keyval == GDK_Shift_L
    || key->keyval == GDK_Shift_R;

  state = sheet->state;
  in_selection = GTK_SHEET_IN_SELECTION (sheet);
  GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);

  switch (key->keyval)
    {
    case GDK_Return: case GDK_KP_Enter:
      if (sheet->state == GTK_SHEET_NORMAL &&
	  !GTK_SHEET_IN_SELECTION (sheet))
	g_signal_stop_emission_by_name (gtk_sheet_get_entry (sheet),
					 "key-press-event");
      row = sheet->active_cell.row;
      col = sheet->active_cell.col;
      if (sheet->state == GTK_SHEET_COLUMN_SELECTED)
	row = MIN_VISIBLE_ROW (sheet)- 1;
      if (sheet->state == GTK_SHEET_ROW_SELECTED)
	col = MIN_VISIBLE_COLUMN (sheet);
      if (row < yyy_row_count (sheet) - 1)
	{
	  row = row + scroll;
	  while (!yyy_row_is_visible (sheet, row) && row < yyy_row_count (sheet)- 1)
	    row++;
	}
      gtk_sheet_click_cell (sheet, row, col, &veto);
      extend_selection = FALSE;
      break;
    case GDK_ISO_Left_Tab:
      row = sheet->active_cell.row;
      col = sheet->active_cell.col;
      if (sheet->state == GTK_SHEET_ROW_SELECTED)
	col = MIN_VISIBLE_COLUMN (sheet)- 1;
      if (sheet->state == GTK_SHEET_COLUMN_SELECTED)
	row = MIN_VISIBLE_ROW (sheet);
      if (col > 0)
	{
	  col = col - scroll;
	  while (! xxx_column_is_visible (sheet, col) && col > 0) col--;
	  col = MAX (0, col);
	}
      gtk_sheet_click_cell (sheet, row, col, &veto);
      extend_selection = FALSE;
      break;
    case GDK_Tab:
      row = sheet->active_cell.row;
      col = sheet->active_cell.col;
      if (sheet->state == GTK_SHEET_ROW_SELECTED)
	col = MIN_VISIBLE_COLUMN (sheet)- 1;
      if (sheet->state == GTK_SHEET_COLUMN_SELECTED)
	row = MIN_VISIBLE_ROW (sheet);
      if (col < xxx_column_count (sheet) - 1)
	{
	  col = col + scroll;
	  while (! xxx_column_is_visible (sheet, col) &&
		 col < xxx_column_count (sheet) - 1)
	    col++;
	}
      gtk_sheet_click_cell (sheet, row, col, &veto);
      extend_selection = FALSE;
      break;
    case GDK_Page_Up:
      scroll = MAX_VISIBLE_ROW (sheet)- MIN_VISIBLE_ROW (sheet)+1;
    case GDK_Up:
      if (extend_selection)
	{
	  if (state == GTK_STATE_NORMAL)
	    {
	      row = sheet->active_cell.row;
	      col = sheet->active_cell.col;
	      gtk_sheet_click_cell (sheet, row, col, &veto);
	      if (!veto) break;
	    }
	  if (sheet->selection_cell.row > 0)
	    {
	      row = sheet->selection_cell.row - scroll;
	      while (!yyy_row_is_visible (sheet, row) && row > 0) row--;
	      row = MAX (0, row);
	      gtk_sheet_extend_selection (sheet, row, sheet->selection_cell.col);
	    }
	  return TRUE;
	}
      col = sheet->active_cell.col;
      row = sheet->active_cell.row;
      if (state == GTK_SHEET_COLUMN_SELECTED)
	row = MIN_VISIBLE_ROW (sheet);
      if (state == GTK_SHEET_ROW_SELECTED)
	col = MIN_VISIBLE_COLUMN (sheet);
      row = row - scroll;
      while (!yyy_row_is_visible (sheet, row) && row > 0) row--;
      row = MAX (0, row);
      gtk_sheet_click_cell (sheet, row, col, &veto);
      extend_selection = FALSE;
      break;
    case GDK_Page_Down:
      scroll = MAX_VISIBLE_ROW (sheet)- MIN_VISIBLE_ROW (sheet)+1;
    case GDK_Down:
      if (extend_selection)
	{
	  if (state == GTK_STATE_NORMAL)
	    {
	      row = sheet->active_cell.row;
	      col = sheet->active_cell.col;
	      gtk_sheet_click_cell (sheet, row, col, &veto);
	      if (!veto) break;
	    }
	  if (sheet->selection_cell.row < yyy_row_count (sheet)- 1)
	    {
	      row = sheet->selection_cell.row + scroll;
	      while (!yyy_row_is_visible (sheet, row) && row < yyy_row_count (sheet)- 1) row++;
	      row = MIN (yyy_row_count (sheet)- 1, row);
	      gtk_sheet_extend_selection (sheet, row, sheet->selection_cell.col);
	    }
	  return TRUE;
	}
      col = sheet->active_cell.col;
      row = sheet->active_cell.row;
      if (sheet->active_cell.row < yyy_row_count (sheet)- 1)
	{
	  if (state == GTK_SHEET_COLUMN_SELECTED)
	    row = MIN_VISIBLE_ROW (sheet)- 1;
	  if (state == GTK_SHEET_ROW_SELECTED)
	    col = MIN_VISIBLE_COLUMN (sheet);
	  row = row + scroll;
	  while (!yyy_row_is_visible (sheet, row) && row < yyy_row_count (sheet)- 1) row++;
	  row = MIN (yyy_row_count (sheet)- 1, row);
	}
      gtk_sheet_click_cell (sheet, row, col, &veto);
      extend_selection = FALSE;
      break;
    case GDK_Right:
      if (extend_selection)
	{
	  if (state == GTK_STATE_NORMAL)
	    {
	      row = sheet->active_cell.row;
	      col = sheet->active_cell.col;
	      gtk_sheet_click_cell (sheet, row, col, &veto);
	      if (!veto) break;
	    }
	  if (sheet->selection_cell.col < xxx_column_count (sheet) - 1)
	    {
	      col = sheet->selection_cell.col + 1;
	      while (! xxx_column_is_visible (sheet, col) && col < xxx_column_count (sheet) - 1)
		col++;
	      gtk_sheet_extend_selection (sheet, sheet->selection_cell.row, col);
	    }
	  return TRUE;
	}
      col = sheet->active_cell.col;
      row = sheet->active_cell.row;
      if (sheet->active_cell.col < xxx_column_count (sheet) - 1)
	{
	  col ++;
	  if (state == GTK_SHEET_ROW_SELECTED)
	    col = MIN_VISIBLE_COLUMN (sheet)- 1;
	  if (state == GTK_SHEET_COLUMN_SELECTED)
	    row = MIN_VISIBLE_ROW (sheet);
	  while (! xxx_column_is_visible (sheet, col) && col < xxx_column_count (sheet) - 1) col++;
	  if (strlen (gtk_entry_get_text (GTK_ENTRY (gtk_sheet_get_entry (sheet)))) == 0
	      || force_move)
	    {
	      gtk_sheet_click_cell (sheet, row, col, &veto);
	    }
	  else
	    return FALSE;
	}
      extend_selection = FALSE;
      break;
    case GDK_Left:
      if (extend_selection)
	{
	  if (state == GTK_STATE_NORMAL)
	    {
	      row = sheet->active_cell.row;
	      col = sheet->active_cell.col;
	      gtk_sheet_click_cell (sheet, row, col, &veto);
	      if (!veto) break;
	    }
	  if (sheet->selection_cell.col > 0)
	    {
	      col = sheet->selection_cell.col - 1;
	      while (! xxx_column_is_visible (sheet, col) && col > 0) col--;
	      gtk_sheet_extend_selection (sheet, sheet->selection_cell.row, col);
	    }
	  return TRUE;
	}
      col = sheet->active_cell.col - 1;
      row = sheet->active_cell.row;
      if (state == GTK_SHEET_ROW_SELECTED)
	col = MIN_VISIBLE_COLUMN (sheet)- 1;
      if (state == GTK_SHEET_COLUMN_SELECTED)
	row = MIN_VISIBLE_ROW (sheet);
      while (! xxx_column_is_visible (sheet, col) && col > 0) col--;
      col = MAX (0, col);

      if (strlen (gtk_entry_get_text (GTK_ENTRY (gtk_sheet_get_entry (sheet)))) == 0
	  || force_move)
	{
	  gtk_sheet_click_cell (sheet, row, col, &veto);
	}
      else
	return FALSE;
      extend_selection = FALSE;
      break;
    case GDK_Home:
      row = 0;
      while (!yyy_row_is_visible (sheet, row) && row < yyy_row_count (sheet)- 1) row++;
      gtk_sheet_click_cell (sheet, row, sheet->active_cell.col, &veto);
      extend_selection = FALSE;
      break;
    case GDK_End:
      row = yyy_row_count (sheet)- 1;
      while (!yyy_row_is_visible (sheet, row) && row > 0) row--;
      gtk_sheet_click_cell (sheet, row, sheet->active_cell.col, &veto);
      extend_selection = FALSE;
      break;
    default:
      if (in_selection)
	{
	  GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);
	  if (extend_selection) return TRUE;
	}
      if (state == GTK_SHEET_ROW_SELECTED)
	sheet->active_cell.col = MIN_VISIBLE_COLUMN (sheet);
      if (state == GTK_SHEET_COLUMN_SELECTED)
	sheet->active_cell.row = MIN_VISIBLE_ROW (sheet);
      return FALSE;
    }

  if (extend_selection) return TRUE;

  gtk_sheet_activate_cell (sheet, sheet->active_cell.row,
			   sheet->active_cell.col);

  return TRUE;
}

static void
gtk_sheet_size_request (GtkWidget * widget,
			GtkRequisition * requisition)
{
  GtkSheet *sheet;
  GList *children;
  GtkSheetChild *child;
  GtkRequisition child_requisition;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SHEET (widget));
  g_return_if_fail (requisition != NULL);

  sheet = GTK_SHEET (widget);

  requisition->width = 3*DEFAULT_COLUMN_WIDTH;
  requisition->height = 3*DEFAULT_ROW_HEIGHT (widget);

  /* compute the size of the column title area */
  if (sheet->column_titles_visible)
    requisition->height += sheet->column_title_area.height;

  /* compute the size of the row title area */
  if (sheet->row_titles_visible)
    requisition->width += sheet->row_title_area.width;

  children = sheet->children;
  while (children)
    {
      child = children->data;
      children = children->next;

      gtk_widget_size_request (child->widget, &child_requisition);
    }
}


static void
gtk_sheet_size_allocate (GtkWidget * widget,
			 GtkAllocation * allocation)
{
  GtkSheet *sheet;
  GtkAllocation sheet_allocation;
  gint border_width;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SHEET (widget));
  g_return_if_fail (allocation != NULL);

  sheet = GTK_SHEET (widget);
  widget->allocation = *allocation;
  border_width = GTK_CONTAINER (widget)->border_width;

  if (GTK_WIDGET_REALIZED (widget))
    gdk_window_move_resize (widget->window,
			    allocation->x + border_width,
			    allocation->y + border_width,
			    allocation->width - 2 * border_width,
			    allocation->height - 2 * border_width);

  /* use internal allocation structure for all the math
   * because it's easier than always subtracting the container
   * border width */
  sheet->internal_allocation.x = 0;
  sheet->internal_allocation.y = 0;
  sheet->internal_allocation.width = allocation->width - 2 * border_width;
  sheet->internal_allocation.height = allocation->height - 2 * border_width;

  sheet_allocation.x = 0;
  sheet_allocation.y = 0;
  sheet_allocation.width = allocation->width - 2 * border_width;
  sheet_allocation.height = allocation->height - 2 * border_width;

  sheet->sheet_window_width = sheet_allocation.width;
  sheet->sheet_window_height = sheet_allocation.height;

  if (GTK_WIDGET_REALIZED (widget))
    gdk_window_move_resize (sheet->sheet_window,
			    sheet_allocation.x,
			    sheet_allocation.y,
			    sheet_allocation.width,
			    sheet_allocation.height);

  /* position the window which holds the column title buttons */
  sheet->column_title_area.x = 0;
  sheet->column_title_area.y = 0;
  if (sheet->row_titles_visible)
    sheet->column_title_area.x = sheet->row_title_area.width;
  sheet->column_title_area.width = sheet_allocation.width -
    sheet->column_title_area.x;
  if (GTK_WIDGET_REALIZED (widget) && sheet->column_titles_visible)
    gdk_window_move_resize (sheet->column_title_window,
			    sheet->column_title_area.x,
			    sheet->column_title_area.y,
			    sheet->column_title_area.width,
			    sheet->column_title_area.height);

  sheet->sheet_window_width = sheet_allocation.width;
  sheet->sheet_window_height = sheet_allocation.height;

  /* column button allocation */
  size_allocate_column_title_buttons (sheet);

  /* position the window which holds the row title buttons */
  sheet->row_title_area.x = 0;
  sheet->row_title_area.y = 0;
  if (sheet->column_titles_visible)
    sheet->row_title_area.y = sheet->column_title_area.height;
  sheet->row_title_area.height = sheet_allocation.height -
    sheet->row_title_area.y;

  if (GTK_WIDGET_REALIZED (widget) && sheet->row_titles_visible)
    gdk_window_move_resize (sheet->row_title_window,
			    sheet->row_title_area.x,
			    sheet->row_title_area.y,
			    sheet->row_title_area.width,
			    sheet->row_title_area.height);


  /* row button allocation */
  size_allocate_row_title_buttons (sheet);
  size_allocate_column_title_buttons (sheet);

  /* re - scale backing pixmap */
  gtk_sheet_make_backing_pixmap (sheet, 0, 0);
  gtk_sheet_position_children (sheet);

  /* set the scrollbars adjustments */
  adjust_scrollbars (sheet);
}

static void
size_allocate_column_title_buttons (GtkSheet * sheet)
{
  gint i;
  gint x, width;

  if (!sheet->column_titles_visible) return;
  if (!GTK_WIDGET_REALIZED (sheet))
    return;

  width = sheet->sheet_window_width;
  x = 0;

  if (sheet->row_titles_visible)
    {
      width -= sheet->row_title_area.width;
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


  if (MAX_VISIBLE_COLUMN (sheet) == xxx_column_count (sheet) - 1)
    gdk_window_clear_area (sheet->column_title_window,
			   0, 0,
			   sheet->column_title_area.width,
			   sheet->column_title_area.height);

  if (!GTK_WIDGET_DRAWABLE (sheet)) return;

  for (i = MIN_VISIBLE_COLUMN (sheet); i <= MAX_VISIBLE_COLUMN (sheet); i++)
    gtk_sheet_column_title_button_draw (sheet, i);
}

static void
size_allocate_row_title_buttons (GtkSheet * sheet)
{
  gint i;
  gint y, height;

  if (!sheet->row_titles_visible) return;
  if (!GTK_WIDGET_REALIZED (sheet))
    return;

  height = sheet->sheet_window_height;
  y = 0;

  if (sheet->column_titles_visible)
    {
      height -= sheet->column_title_area.height;
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
  if (MAX_VISIBLE_ROW (sheet) == yyy_row_count (sheet)- 1)
    gdk_window_clear_area (sheet->row_title_window,
			   0, 0,
			   sheet->row_title_area.width,
			   sheet->row_title_area.height);

  if (!GTK_WIDGET_DRAWABLE (sheet)) return;

  for (i = MIN_VISIBLE_ROW (sheet); i <= MAX_VISIBLE_ROW (sheet); i++)
    {
      if ( i >= yyy_row_count (sheet))
	break;
      gtk_sheet_row_title_button_draw (sheet, i);
    }
}


static void
gtk_sheet_size_allocate_entry (GtkSheet *sheet)
{
  GtkAllocation shentry_allocation;
  GtkSheetCellAttr attributes = { 0 };
  GtkEntry *sheet_entry;
  GtkStyle *style = NULL, *previous_style = NULL;
  gint row, col;
  gint size, max_size, text_size, column_width;
  const gchar *text;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;
  if (!GTK_WIDGET_MAPPED (GTK_WIDGET (sheet))) return;

  sheet_entry = GTK_ENTRY (gtk_sheet_get_entry (sheet));

  if ( ! gtk_sheet_get_attributes (sheet, sheet->active_cell.row,
				   sheet->active_cell.col,
				   &attributes) )
    return ;

  if ( GTK_WIDGET_REALIZED (sheet->sheet_entry) )
    {
      if (!GTK_WIDGET (sheet_entry)->style)
	gtk_widget_ensure_style (GTK_WIDGET (sheet_entry));

      previous_style = GTK_WIDGET (sheet_entry)->style;

      style = gtk_style_copy (previous_style);
      style->bg[GTK_STATE_NORMAL] = attributes.background;
      style->fg[GTK_STATE_NORMAL] = attributes.foreground;
      style->text[GTK_STATE_NORMAL] = attributes.foreground;
      style->bg[GTK_STATE_ACTIVE] = attributes.background;
      style->fg[GTK_STATE_ACTIVE] = attributes.foreground;
      style->text[GTK_STATE_ACTIVE] = attributes.foreground;

      pango_font_description_free (style->font_desc);
      g_assert (attributes.font_desc);
      style->font_desc = pango_font_description_copy (attributes.font_desc);

      GTK_WIDGET (sheet_entry)->style = style;
      gtk_widget_size_request (sheet->sheet_entry, NULL);
      GTK_WIDGET (sheet_entry)->style = previous_style;

      if (style != previous_style)
	{
	  if (!GTK_IS_ITEM_ENTRY (sheet->sheet_entry))
	    {
	      style->bg[GTK_STATE_NORMAL] = previous_style->bg[GTK_STATE_NORMAL];
	      style->fg[GTK_STATE_NORMAL] = previous_style->fg[GTK_STATE_NORMAL];
	      style->bg[GTK_STATE_ACTIVE] = previous_style->bg[GTK_STATE_ACTIVE];
	      style->fg[GTK_STATE_ACTIVE] = previous_style->fg[GTK_STATE_ACTIVE];
	    }
	  gtk_widget_set_style (GTK_WIDGET (sheet_entry), style);
	}
    }

  if (GTK_IS_ITEM_ENTRY (sheet_entry))
    max_size = GTK_ITEM_ENTRY (sheet_entry)->text_max_size;
  else
    max_size = 0;

  text_size = 0;
  text = gtk_entry_get_text (GTK_ENTRY (sheet_entry));
  if (text && strlen (text) > 0)
    text_size = STRING_WIDTH (GTK_WIDGET (sheet), attributes.font_desc, text);

  column_width = xxx_column_width (sheet, sheet->active_cell.col);

  size = MIN (text_size, max_size);
  size = MAX (size, column_width - 2 * CELLOFFSET);

  row = sheet->active_cell.row;
  col = sheet->active_cell.col;

  shentry_allocation.x = COLUMN_LEFT_XPIXEL (sheet, sheet->active_cell.col);
  shentry_allocation.y = ROW_TOP_YPIXEL (sheet, sheet->active_cell.row);
  shentry_allocation.width = column_width;
  shentry_allocation.height = yyy_row_height (sheet, sheet->active_cell.row);

  if (GTK_IS_ITEM_ENTRY (sheet->sheet_entry))
    {
      shentry_allocation.height -= 2 * CELLOFFSET;
      shentry_allocation.y += CELLOFFSET;
      shentry_allocation.width = size;

      switch (GTK_ITEM_ENTRY (sheet_entry)->justification)
	{
	case GTK_JUSTIFY_CENTER:
	  shentry_allocation.x += column_width / 2 - size / 2;
	  break;
	case GTK_JUSTIFY_RIGHT:
	  shentry_allocation.x += column_width - size - CELLOFFSET;
	  break;
	case GTK_JUSTIFY_LEFT:
	case GTK_JUSTIFY_FILL:
	  shentry_allocation.x += CELLOFFSET;
	  break;
	}
    }

  if (!GTK_IS_ITEM_ENTRY (sheet->sheet_entry))
    {
      shentry_allocation.x += 2;
      shentry_allocation.y += 2;
      shentry_allocation.width -= MIN (shentry_allocation.width, 3);
      shentry_allocation.height -= MIN (shentry_allocation.height, 3);
    }

  gtk_widget_size_allocate (sheet->sheet_entry, &shentry_allocation);

  if (previous_style == style) g_object_unref (previous_style);
}

static void
gtk_sheet_entry_set_max_size (GtkSheet *sheet)
{
  gint i;
  gint size = 0;
  gint sizel = 0, sizer = 0;
  gint row, col;
  GtkJustification justification;
  gchar *s = NULL;

  row = sheet->active_cell.row;
  col = sheet->active_cell.col;

  if ( ! GTK_IS_ITEM_ENTRY (sheet->sheet_entry) )
    return;

  justification = GTK_ITEM_ENTRY (sheet->sheet_entry)->justification;

  switch (justification)
    {
    case GTK_JUSTIFY_FILL:
    case GTK_JUSTIFY_LEFT:
      for (i = col + 1; i <= MAX_VISIBLE_COLUMN (sheet); i++)
	{
	  if ((s = gtk_sheet_cell_get_text (sheet, row, i)))
	    {
	      g_free (s);
	      break;
	    }
	  size +=xxx_column_width (sheet, i);
	}
      size = MIN (size, sheet->sheet_window_width - COLUMN_LEFT_XPIXEL (sheet, col));
      break;
    case GTK_JUSTIFY_RIGHT:
      for (i = col - 1; i >= MIN_VISIBLE_COLUMN (sheet); i--)
	{
	  if ((s = gtk_sheet_cell_get_text (sheet, row, i)))
	    {
	      g_free (s);
	      break;
	    }
	  size +=xxx_column_width (sheet, i);
	}
      break;
    case GTK_JUSTIFY_CENTER:
      for (i = col + 1; i <= MAX_VISIBLE_COLUMN (sheet); i++)
	{
	  sizer += xxx_column_width (sheet, i);
	}
      for (i = col - 1; i >= MIN_VISIBLE_COLUMN (sheet); i--)
	{
	  if ((s = gtk_sheet_cell_get_text (sheet, row, i)))
	    {
	      g_free (s);
	      break;
	    }
	  sizel +=xxx_column_width (sheet, i);
	}
      size = 2 * MIN (sizel, sizer);
      break;
    }

  if (size != 0)
    size += xxx_column_width (sheet, col);
  GTK_ITEM_ENTRY (sheet->sheet_entry)->text_max_size = size;
}


static void
create_sheet_entry (GtkSheet *sheet)
{
  GtkWidget *widget;
  GtkWidget *parent;
  GtkWidget *entry;
  gint found_entry = FALSE;

  widget = GTK_WIDGET (sheet);

  if (sheet->sheet_entry)
    {
      /* avoids warnings */
      gtk_widget_ref (sheet->sheet_entry);
      gtk_widget_unparent (sheet->sheet_entry);
      gtk_widget_destroy (sheet->sheet_entry);
    }

  if (sheet->entry_type)
    {
      if (!g_type_is_a (sheet->entry_type, GTK_TYPE_ENTRY))
	{
	  parent = g_object_new (sheet->entry_type, NULL);

	  sheet->sheet_entry = parent;

	  entry = gtk_sheet_get_entry (sheet);
	  if (GTK_IS_ENTRY (entry))
	    found_entry = TRUE;
	}
      else
	{
	  parent = g_object_new (sheet->entry_type, NULL);
	  entry = parent;
	  found_entry = TRUE;
	}

      if (!found_entry)
	{
	  g_warning ("Entry type must be GtkEntry subclass, using default");
	  entry = gtk_item_entry_new ();
	  sheet->sheet_entry = entry;
	}
      else
	sheet->sheet_entry = parent;
    }
  else
    {
      entry = gtk_item_entry_new ();
      sheet->sheet_entry = entry;
    }

  gtk_widget_size_request (sheet->sheet_entry, NULL);

  if (GTK_WIDGET_REALIZED (sheet))
    {
      gtk_widget_set_parent_window (sheet->sheet_entry, sheet->sheet_window);
      gtk_widget_set_parent (sheet->sheet_entry, GTK_WIDGET (sheet));
      gtk_widget_realize (sheet->sheet_entry);
    }

  g_signal_connect_swapped (G_OBJECT (entry), "key_press_event",
			    G_CALLBACK (gtk_sheet_entry_key_press),
			    sheet);

  gtk_widget_show (sheet->sheet_entry);
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

GtkWidget *
gtk_sheet_get_entry (GtkSheet *sheet)
{
  GtkWidget *parent;
  GtkWidget *entry = NULL;
  GtkTableChild *table_child;
  GtkBoxChild *box_child;
  GList *children = NULL;

  g_return_val_if_fail (sheet != NULL, NULL);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), NULL);
  g_return_val_if_fail (sheet->sheet_entry != NULL, NULL);

  if (GTK_IS_ENTRY (sheet->sheet_entry)) return (sheet->sheet_entry);

  parent = GTK_WIDGET (sheet->sheet_entry);

  if (GTK_IS_TABLE (parent)) children = GTK_TABLE (parent)->children;
  if (GTK_IS_BOX (parent)) children = GTK_BOX (parent)->children;

  if (GTK_IS_CONTAINER (parent))
    {
      gtk_container_forall (GTK_CONTAINER (parent), find_entry, &entry);

      if (GTK_IS_ENTRY (entry))
	return entry;
    }

  if (!children) return NULL;

  while (children)
    {
      if (GTK_IS_TABLE (parent))
	{
	  table_child = children->data;
	  entry = table_child->widget;
	}
      if (GTK_IS_BOX (parent))
	{
	  box_child = children->data;
	  entry = box_child->widget;
	}

      if (GTK_IS_ENTRY (entry))
	break;
      children = children->next;
    }


  if (!GTK_IS_ENTRY (entry)) return NULL;

  return (entry);

}

GtkWidget *
gtk_sheet_get_entry_widget (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, NULL);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), NULL);
  g_return_val_if_fail (sheet->sheet_entry != NULL, NULL);

  return (sheet->sheet_entry);
}


static void
gtk_sheet_button_draw (GtkSheet *sheet, GdkWindow *window,
		       GtkSheetButton *button, gboolean is_sensitive,
		       GdkRectangle allocation)
{
  GtkShadowType shadow_type;
  gint text_width = 0, text_height = 0;
  GtkSheetChild *child = NULL;
  PangoAlignment align = PANGO_ALIGN_LEFT;

  gboolean rtl ;

  gint state = 0;
  gint len = 0;
  gchar *line = 0;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (button != NULL);

  rtl = gtk_widget_get_direction (GTK_WIDGET (sheet)) == GTK_TEXT_DIR_RTL;

  gdk_window_clear_area (window,
			 allocation.x, allocation.y,
			 allocation.width, allocation.height);

  gtk_paint_box (sheet->button->style, window,
		 GTK_STATE_NORMAL, GTK_SHADOW_OUT,
		 &allocation, GTK_WIDGET (sheet->button),
		 "buttondefault",
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
		   "button",
		   allocation.x, allocation.y,
		   allocation.width, allocation.height);

  if (button->label_visible)
    {

      text_height = DEFAULT_ROW_HEIGHT (GTK_WIDGET (sheet))- 2 * CELLOFFSET;

      gdk_gc_set_clip_rectangle (GTK_WIDGET (sheet)->style->fg_gc[button->state],
				 &allocation);
      gdk_gc_set_clip_rectangle (GTK_WIDGET (sheet)->style->white_gc, &allocation);

      allocation.y += 2 * sheet->button->style->ythickness;


      if (button->label && strlen (button->label)>0)
	{
	  gchar *words = 0;
	  PangoLayout *layout = NULL;
	  gint real_x = allocation.x, real_y = allocation.y;

	  words = button->label;
	  line = g_new (gchar, 1);
	  line[0]='\0';

	  while (words && *words != '\0')
	    {
	      if (*words != '\n')
		{
		  len = strlen (line);
		  line = g_realloc (line, len + 2);
		  line[len]=*words;
		  line[len + 1]='\0';
		}
	      if (*words == '\n' || * (words + 1) == '\0')
		{
		  text_width = STRING_WIDTH (GTK_WIDGET (sheet), GTK_WIDGET (sheet)->style->font_desc, line);

		  layout = gtk_widget_create_pango_layout (GTK_WIDGET (sheet), line);
		  switch (button->justification)
		    {
		    case GTK_JUSTIFY_LEFT:
		      real_x = allocation.x + CELLOFFSET;
		      align = rtl ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT;
		      break;
		    case GTK_JUSTIFY_RIGHT:
		      real_x = allocation.x + allocation.width - text_width - CELLOFFSET;
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
		  g_object_unref (G_OBJECT (layout));

		  real_y += text_height + 2;

		  g_free (line);
		  line = g_new (gchar, 1);
		  line[0]='\0';
		}
	      words++;
	    }
	  g_free (line);
	}

      gdk_gc_set_clip_rectangle (GTK_WIDGET (sheet)->style->fg_gc[button->state],
				 NULL);
      gdk_gc_set_clip_rectangle (GTK_WIDGET (sheet)->style->white_gc, NULL);

    }

  if ((child = button->child) && (child->widget))
    {
      child->x = allocation.x;
      child->y = allocation.y;

      child->x += (allocation.width - child->widget->requisition.width) / 2;
      child->y += (allocation.height - child->widget->requisition.height) / 2;
      allocation.x = child->x;
      allocation.y = child->y;
      allocation.width = child->widget->requisition.width;
      allocation.height = child->widget->requisition.height;

      allocation.x = child->x;
      allocation.y = child->y;

      gtk_widget_set_state (child->widget, button->state);

      if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)) &&
	  GTK_WIDGET_MAPPED (child->widget))
	{
	  gtk_widget_size_allocate (child->widget,
				    &allocation);
	  gtk_widget_queue_draw (child->widget);
	}
    }

  gtk_sheet_button_free (button);
}


/* COLUMN value of - 1 indicates that the area to the right of the rightmost
   button should be redrawn */
static void
gtk_sheet_column_title_button_draw (GtkSheet *sheet, gint column)
{
  GdkWindow *window = NULL;
  GdkRectangle allocation;
  GtkSheetButton *button = NULL;
  gboolean is_sensitive = FALSE;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;

  if (column >= 0 && ! xxx_column_is_visible (sheet, column)) return;
  if (column >= 0 && !sheet->column_titles_visible) return;
  if (column >= 0 && column < MIN_VISIBLE_COLUMN (sheet)) return;
  if (column >= 0 && column > MAX_VISIBLE_COLUMN (sheet)) return;

  window = sheet->column_title_window;
  allocation.y = 0;
  allocation.height = sheet->column_title_area.height;

  if ( column == -1 )
    {
      const gint cols = xxx_column_count (sheet) ;
      allocation.x = COLUMN_LEFT_XPIXEL (sheet, cols - 1)
	;
      allocation.width = sheet->column_title_area.width
	+ sheet->column_title_area.x
	- allocation.x;

      gdk_window_clear_area (window,
			     allocation.x, allocation.y,
			     allocation.width, allocation.height);
    }
  else
    {
      button = xxx_column_button (sheet, column);
      allocation.x = COLUMN_LEFT_XPIXEL (sheet, column) + CELL_SPACING;
      if (sheet->row_titles_visible)
	allocation.x -= sheet->row_title_area.width;

      allocation.width = xxx_column_width (sheet, column);

      is_sensitive = xxx_column_is_sensitive (sheet, column);
      gtk_sheet_button_draw (sheet, window, button,
			     is_sensitive, allocation);
    }
}

static void
gtk_sheet_row_title_button_draw (GtkSheet *sheet, gint row)
{
  GdkWindow *window = NULL;
  GdkRectangle allocation;
  GtkSheetButton *button = NULL;
  gboolean is_sensitive = FALSE;


  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;

  if (row >= 0 && !yyy_row_is_visible (sheet, row)) return;
  if (row >= 0 && !sheet->row_titles_visible) return;
  if (row >= 0 && row < MIN_VISIBLE_ROW (sheet)) return;
  if (row >= 0 && row > MAX_VISIBLE_ROW (sheet)) return;


  window = sheet->row_title_window;
  button = yyy_row_button (sheet, row);
  allocation.x = 0;
  allocation.y = ROW_TOP_YPIXEL (sheet, row) + CELL_SPACING;
  if (sheet->column_titles_visible)
    allocation.y -= sheet->column_title_area.height;
  allocation.width = sheet->row_title_area.width;
  allocation.height = yyy_row_height (sheet, row);
  is_sensitive = yyy_row_is_sensitive (sheet, row);

  gtk_sheet_button_draw (sheet, window, button, is_sensitive, allocation);
}

/* SCROLLBARS
 *
 * functions:
 * adjust_scrollbars
 * vadjustment_value_changed
 * hadjustment_value_changed */

static void
adjust_scrollbars (GtkSheet * sheet)
{
  if (sheet->vadjustment)
    {
      sheet->vadjustment->page_size = sheet->sheet_window_height;
      sheet->vadjustment->page_increment = sheet->sheet_window_height / 2;
      sheet->vadjustment->step_increment = DEFAULT_ROW_HEIGHT (GTK_WIDGET (sheet));
      sheet->vadjustment->lower = 0;
      sheet->vadjustment->upper = SHEET_HEIGHT (sheet) + 80;
      g_signal_emit_by_name (G_OBJECT (sheet->vadjustment), "changed");

    }

  if (sheet->hadjustment)
    {
      sheet->hadjustment->page_size = sheet->sheet_window_width;
      sheet->hadjustment->page_increment = sheet->sheet_window_width / 2;
      sheet->hadjustment->step_increment = DEFAULT_COLUMN_WIDTH;
      sheet->hadjustment->lower = 0;
      sheet->hadjustment->upper = SHEET_WIDTH (sheet)+ 80;
      g_signal_emit_by_name (G_OBJECT (sheet->hadjustment), "changed");

    }
}

static void
vadjustment_value_changed (GtkAdjustment * adjustment,
			   gpointer data)
{
  GtkSheet *sheet;
  gint diff, value, old_value;
  gint row, new_row;
  gint y = 0;

  g_return_if_fail (adjustment != NULL);
  g_return_if_fail (data != NULL);
  g_return_if_fail (GTK_IS_SHEET (data));

  sheet = GTK_SHEET (data);

  if (GTK_SHEET_IS_FROZEN (sheet)) return;

  row = ROW_FROM_YPIXEL (sheet, sheet->column_title_area.height + CELL_SPACING);
  if (!sheet->column_titles_visible)
    row = ROW_FROM_YPIXEL (sheet, CELL_SPACING);

  old_value = - sheet->voffset;

  new_row = g_sheet_row_pixel_to_row (sheet->row_geometry,
				      adjustment->value, sheet);

  y = g_sheet_row_start_pixel (sheet->row_geometry, new_row, sheet);

  if (adjustment->value > sheet->old_vadjustment && sheet->old_vadjustment > 0. &&
      yyy_row_height (sheet, row) > sheet->vadjustment->step_increment)
    {
      /* This avoids embarrassing twitching */
      if (row == new_row && row != yyy_row_count (sheet) - 1 &&
	  adjustment->value - sheet->old_vadjustment >=
	  sheet->vadjustment->step_increment &&
	  new_row + 1 != MIN_VISIBLE_ROW (sheet))
	{
	  new_row +=1;
	  y = y+yyy_row_height (sheet, row);
	}
    }

  /* Negative old_adjustment enforces the redraw, otherwise avoid
     spureous redraw */
  if (sheet->old_vadjustment >= 0. && row == new_row)
    {
      sheet->old_vadjustment = sheet->vadjustment->value;
      return;
    }

  sheet->old_vadjustment = sheet->vadjustment->value;
  adjustment->value = y;


  if (new_row == 0)
    {
      sheet->vadjustment->step_increment = yyy_row_height (sheet, 0);
    }
  else
    {
      sheet->vadjustment->step_increment =
	MIN (yyy_row_height (sheet, new_row), yyy_row_height (sheet, new_row - 1));
    }

  sheet->vadjustment->value = adjustment->value;

  value = adjustment->value;

  if (value >= - sheet->voffset)
    {
      /* scroll down */
      diff = value + sheet->voffset;
    }
  else
    {
      /* scroll up */
      diff = - sheet->voffset - value;
    }

  sheet->voffset = - value;

  if (GTK_WIDGET_REALIZED (sheet->sheet_entry) &&
      sheet->state == GTK_SHEET_NORMAL &&
      sheet->active_cell.row >= 0 && sheet->active_cell.col >= 0 &&
      !gtk_sheet_cell_isvisible (sheet, sheet->active_cell.row,
				 sheet->active_cell.col))
    {
      const gchar *text;

      text = gtk_entry_get_text (GTK_ENTRY (gtk_sheet_get_entry (sheet)));

      if (!text || strlen (text) == 0)
	gtk_sheet_cell_clear (sheet,
			      sheet->active_cell.row,
			      sheet->active_cell.col);
      gtk_widget_unmap (sheet->sheet_entry);
    }

  gtk_sheet_position_children (sheet);

  gtk_sheet_range_draw (sheet, NULL);
  size_allocate_row_title_buttons (sheet);
  size_allocate_global_button (sheet);
}

static void
hadjustment_value_changed (GtkAdjustment * adjustment,
			   gpointer data)
{
  GtkSheet *sheet;
  gint i, diff, value, old_value;
  gint column, new_column;
  gint x = 0;

  g_return_if_fail (adjustment != NULL);
  g_return_if_fail (data != NULL);
  g_return_if_fail (GTK_IS_SHEET (data));

  sheet = GTK_SHEET (data);

  if (GTK_SHEET_IS_FROZEN (sheet)) return;

  column = COLUMN_FROM_XPIXEL (sheet, sheet->row_title_area.width + CELL_SPACING);
  if (!sheet->row_titles_visible)
    column = COLUMN_FROM_XPIXEL (sheet, CELL_SPACING);

  old_value = - sheet->hoffset;

  for (i = 0; i < xxx_column_count (sheet); i++)
    {
      if (xxx_column_is_visible (sheet, i)) x += xxx_column_width (sheet, i);
      if (x > adjustment->value) break;
    }
  x -= xxx_column_width (sheet, i);
  new_column = i;

  if (adjustment->value > sheet->old_hadjustment && sheet->old_hadjustment > 0 &&
      xxx_column_width (sheet, i) > sheet->hadjustment->step_increment)
    {
      /* This avoids embarrassing twitching */
      if (column == new_column && column != xxx_column_count (sheet) - 1 &&
	  adjustment->value - sheet->old_hadjustment >=
	  sheet->hadjustment->step_increment &&
	  new_column + 1 != MIN_VISIBLE_COLUMN (sheet))
	{
	  new_column += 1;
	  x += xxx_column_width (sheet, column);
	}
    }

  /* Negative old_adjustment enforces the redraw, otherwise avoid spureous redraw */
  if (sheet->old_hadjustment >= 0. && new_column == column)
    {
      sheet->old_hadjustment = sheet->hadjustment->value;
      return;
    }

  sheet->old_hadjustment = sheet->hadjustment->value;
  adjustment->value = x;

  if (new_column == 0)
    {
      sheet->hadjustment->step_increment = xxx_column_width (sheet, 0);
    }
  else
    {
      sheet->hadjustment->step_increment =
	MIN (xxx_column_width (sheet, new_column), xxx_column_width (sheet, new_column - 1));
    }


  sheet->hadjustment->value = adjustment->value;

  value = adjustment->value;

  if (value >= - sheet->hoffset)
    {
      /* scroll right */
      diff = value + sheet->hoffset;
    }
  else
    {
      /* scroll left */
      diff = - sheet->hoffset - value;
    }

  sheet->hoffset = - value;
  if (GTK_WIDGET_REALIZED (sheet->sheet_entry) &&
      sheet->state == GTK_SHEET_NORMAL &&
      sheet->active_cell.row >= 0 && sheet->active_cell.col >= 0 &&
      !gtk_sheet_cell_isvisible (sheet, sheet->active_cell.row,
				 sheet->active_cell.col))
    {
      const gchar *text;

      text = gtk_entry_get_text (GTK_ENTRY (gtk_sheet_get_entry (sheet)));
      if (!text || strlen (text) == 0)
	gtk_sheet_cell_clear (sheet,
			      sheet->active_cell.row,
			      sheet->active_cell.col);

      gtk_widget_unmap (sheet->sheet_entry);
    }

  gtk_sheet_position_children (sheet);

  gtk_sheet_range_draw (sheet, NULL);
  size_allocate_column_title_buttons (sheet);
}


/* COLUMN RESIZING */
static void
draw_xor_vline (GtkSheet * sheet)
{
  GtkWidget *widget;

  g_return_if_fail (sheet != NULL);

  widget = GTK_WIDGET (sheet);

  gdk_draw_line (widget->window, sheet->xor_gc,
		 sheet->x_drag,
		 sheet->column_title_area.height,
		 sheet->x_drag,
		 sheet->sheet_window_height + 1);
}

/* ROW RESIZING */
static void
draw_xor_hline (GtkSheet * sheet)
{
  GtkWidget *widget;

  g_return_if_fail (sheet != NULL);

  widget = GTK_WIDGET (sheet);

  gdk_draw_line (widget->window, sheet->xor_gc,
		 sheet->row_title_area.width,
		 sheet->y_drag,

		 sheet->sheet_window_width + 1,
		 sheet->y_drag);
}

/* SELECTED RANGE */
static void
draw_xor_rectangle (GtkSheet *sheet, GtkSheetRange range)
{
  gint i;
  GdkRectangle clip_area, area;
  GdkGCValues values;

  area.x = COLUMN_LEFT_XPIXEL (sheet, range.col0);
  area.y = ROW_TOP_YPIXEL (sheet, range.row0);
  area.width = COLUMN_LEFT_XPIXEL (sheet, range.coli)- area.x+
    xxx_column_width (sheet, range.coli);
  area.height = ROW_TOP_YPIXEL (sheet, range.rowi)- area.y+
    yyy_row_height (sheet, range.rowi);

  clip_area.x = sheet->row_title_area.width;
  clip_area.y = sheet->column_title_area.height;
  clip_area.width = sheet->sheet_window_width;
  clip_area.height = sheet->sheet_window_height;

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

  for (i =- 1; i <= 1; ++i)
    gdk_draw_rectangle (sheet->sheet_window,
			sheet->xor_gc,
			FALSE,
			area.x + i, area.y + i,
			area.width - 2 * i, area.height - 2 * i);


  gdk_gc_set_clip_rectangle (sheet->xor_gc, NULL);

  gdk_gc_set_foreground (sheet->xor_gc, &values.foreground);

}


/* this function returns the new width of the column being resized given
 * the column and x position of the cursor; the x cursor position is passed
 * in as a pointer and automaticaly corrected if it's beyond min / max limits */
static guint
new_column_width (GtkSheet * sheet,
		  gint column,
		  gint * x)
{
  gint cx, width;
  guint min_width;

  cx = *x;

  min_width = sheet->column_requisition;

  /* you can't shrink a column to less than its minimum width */
  if (cx < COLUMN_LEFT_XPIXEL (sheet, column) + min_width)
    {
      *x = cx = COLUMN_LEFT_XPIXEL (sheet, column) + min_width;
    }

  /* calculate new column width making sure it doesn't end up
   * less than the minimum width */
  width = cx - COLUMN_LEFT_XPIXEL (sheet, column);
  if (width < min_width)
    width = min_width;

  xxx_set_column_width (sheet, column, width);
  size_allocate_column_title_buttons (sheet);

  return width;
}

/* this function returns the new height of the row being resized given
 * the row and y position of the cursor; the y cursor position is passed
 * in as a pointer and automaticaly corrected if it's beyond min / max limits */
static guint
new_row_height (GtkSheet * sheet,
		gint row,
		gint * y)
{
  gint cy, height;
  guint min_height;

  cy = *y;
  min_height = sheet->row_requisition;

  /* you can't shrink a row to less than its minimum height */
  if (cy < ROW_TOP_YPIXEL (sheet, row) + min_height)

    {
      *y = cy = ROW_TOP_YPIXEL (sheet, row) + min_height;
    }

  /* calculate new row height making sure it doesn't end up
   * less than the minimum height */
  height = (cy - ROW_TOP_YPIXEL (sheet, row));
  if (height < min_height)
    height = min_height;

  yyy_set_row_height (sheet, row, height);
  size_allocate_row_title_buttons (sheet);

  return height;
}

static void
gtk_sheet_set_column_width (GtkSheet * sheet,
			    gint column,
			    guint width)
{
  guint min_width;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (column < 0 || column >= xxx_column_count (sheet))
    return;

  gtk_sheet_column_size_request (sheet, column, &min_width);
  if (width < min_width) return;

  xxx_set_column_width (sheet, column, width);

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)) && !GTK_SHEET_IS_FROZEN (sheet))
    {
      size_allocate_column_title_buttons (sheet);
      adjust_scrollbars (sheet);
      gtk_sheet_size_allocate_entry (sheet);
      gtk_sheet_range_draw (sheet, NULL);
    }

  g_signal_emit (G_OBJECT (sheet), sheet_signals[CHANGED], 0, -1, column);
  g_signal_emit (G_OBJECT (sheet), sheet_signals[NEW_COL_WIDTH], 0,
		 column, width);
}



void
gtk_sheet_set_row_height (GtkSheet * sheet,
			  gint row,
			  guint height)
{
  guint min_height;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (row < 0 || row >= yyy_row_count (sheet))
    return;

  gtk_sheet_row_size_request (sheet, row, &min_height);
  if (height < min_height) return;

  yyy_set_row_height (sheet, row, height);

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)) && !GTK_SHEET_IS_FROZEN (sheet))
    {
      size_allocate_row_title_buttons (sheet);
      adjust_scrollbars (sheet);
      gtk_sheet_size_allocate_entry (sheet);
      gtk_sheet_range_draw (sheet, NULL);
    }

  g_signal_emit (G_OBJECT (sheet), sheet_signals[CHANGED], 0, row, - 1);
  g_signal_emit (G_OBJECT (sheet), sheet_signals[NEW_ROW_HEIGHT], 0,
		 row, height);

}


gboolean
gtk_sheet_get_attributes (const GtkSheet *sheet, gint row, gint col,
			  GtkSheetCellAttr *attributes)
{
  const GdkColor *fg, *bg;
  const GtkJustification *j ;
  const PangoFontDescription *font_desc ;
  const GtkSheetCellBorder *border ;

  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);

  if (row < 0 || col < 0) return FALSE;

  init_attributes (sheet, col, attributes);

  if ( !sheet->model)
    return FALSE;

  attributes->is_editable = g_sheet_model_is_editable (sheet->model, row, col);
  attributes->is_visible = g_sheet_model_is_visible (sheet->model, row, col);

  fg = g_sheet_model_get_foreground (sheet->model, row, col);
  if ( fg )
    attributes->foreground = *fg;

  bg = g_sheet_model_get_background (sheet->model, row, col);
  if ( bg )
    attributes->background = *bg;

  j = g_sheet_model_get_justification (sheet->model, row, col);
  if (j) attributes->justification = *j;

  font_desc = g_sheet_model_get_font_desc (sheet->model, row, col);
  if ( font_desc ) attributes->font_desc = font_desc;

  border = g_sheet_model_get_cell_border (sheet->model, row, col);

  if ( border ) attributes->border = *border;

  return TRUE;
}

static void
init_attributes (const GtkSheet *sheet, gint col, GtkSheetCellAttr *attributes)
{
  /* DEFAULT VALUES */
  attributes->foreground = GTK_WIDGET (sheet)->style->black;
  attributes->background = sheet->bg_color;
  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      GdkColormap *colormap;
      colormap = gdk_colormap_get_system ();
      gdk_color_black (colormap, &attributes->foreground);
      attributes->background = sheet->bg_color;
    }
  attributes->justification = xxx_column_justification (sheet, col);
  attributes->border.width = 0;
  attributes->border.line_style = GDK_LINE_SOLID;
  attributes->border.cap_style = GDK_CAP_NOT_LAST;
  attributes->border.join_style = GDK_JOIN_MITER;
  attributes->border.mask = 0;
  attributes->border.color = GTK_WIDGET (sheet)->style->black;
  attributes->is_editable = TRUE;
  attributes->is_visible = TRUE;
  attributes->font_desc = GTK_WIDGET (sheet)->style->font_desc;
}


/********************************************************************
 * Container Functions:
 * gtk_sheet_add
 * gtk_sheet_put
 * gtk_sheet_attach
 * gtk_sheet_remove
 * gtk_sheet_move_child
 * gtk_sheet_position_child
 * gtk_sheet_position_children
 * gtk_sheet_realize_child
 * gtk_sheet_get_child_at
 ********************************************************************/

GtkSheetChild *
gtk_sheet_put (GtkSheet *sheet, GtkWidget *child, gint x, gint y)
{
  GtkRequisition child_requisition;
  GtkSheetChild *child_info;

  g_return_val_if_fail (sheet != NULL, NULL);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), NULL);
  g_return_val_if_fail (child != NULL, NULL);
  g_return_val_if_fail (child->parent == NULL, NULL);

  child_info = g_new (GtkSheetChild, 1);
  child_info->widget = child;
  child_info->x = x;
  child_info->y = y;
  child_info->attached_to_cell = FALSE;
  child_info->floating = TRUE;
  child_info->xpadding = child_info->ypadding = 0;
  child_info->xexpand = child_info->yexpand = FALSE;
  child_info->xshrink = child_info->yshrink = FALSE;
  child_info->xfill = child_info->yfill = FALSE;

  sheet->children = g_list_append (sheet->children, child_info);

  gtk_widget_set_parent (child, GTK_WIDGET (sheet));

  gtk_widget_size_request (child, &child_requisition);

  if (GTK_WIDGET_VISIBLE (GTK_WIDGET (sheet)))
    {
      if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)) &&
	  (!GTK_WIDGET_REALIZED (child) || GTK_WIDGET_NO_WINDOW (child)))
	gtk_sheet_realize_child (sheet, child_info);

      if (GTK_WIDGET_MAPPED (GTK_WIDGET (sheet)) &&
	  !GTK_WIDGET_MAPPED (child))
	gtk_widget_map (child);
    }

  gtk_sheet_position_child (sheet, child_info);

  /* This will avoid drawing on the titles */

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      if (sheet->row_titles_visible)
	gdk_window_show (sheet->row_title_window);
      if (sheet->column_titles_visible)
	gdk_window_show (sheet->column_title_window);
    }

  return (child_info);
}

void
gtk_sheet_attach_floating (GtkSheet *sheet,
			   GtkWidget *widget,
			   gint row, gint col)
{
  GdkRectangle area;
  GtkSheetChild *child;

  if (row < 0 || col < 0)
    {
      gtk_sheet_button_attach (sheet, widget, row, col);
      return;
    }

  gtk_sheet_get_cell_area (sheet, row, col, &area);
  child = gtk_sheet_put (sheet, widget, area.x, area.y);
  child->attached_to_cell = TRUE;
  child->row = row;
  child->col = col;
}

void
gtk_sheet_attach_default (GtkSheet *sheet,
			  GtkWidget *widget,
			  gint row, gint col)
{
  if (row < 0 || col < 0)
    {
      gtk_sheet_button_attach (sheet, widget, row, col);
      return;
    }

  gtk_sheet_attach (sheet, widget, row, col,
		    GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
}

void
gtk_sheet_attach (GtkSheet *sheet,
		  GtkWidget *widget,
		  gint row, gint col,
		  gint xoptions,
		  gint yoptions,
		  gint xpadding,
		  gint ypadding)
{
  GdkRectangle area;
  GtkSheetChild *child = NULL;

  if (row < 0 || col < 0)
    {
      gtk_sheet_button_attach (sheet, widget, row, col);
      return;
    }

  child = g_new0 (GtkSheetChild, 1);
  child->attached_to_cell = TRUE;
  child->floating = FALSE;
  child->widget = widget;
  child->row = row;
  child->col = col;
  child->xpadding = xpadding;
  child->ypadding = ypadding;
  child->xexpand = (xoptions & GTK_EXPAND) != 0;
  child->yexpand = (yoptions & GTK_EXPAND) != 0;
  child->xshrink = (xoptions & GTK_SHRINK) != 0;
  child->yshrink = (yoptions & GTK_SHRINK) != 0;
  child->xfill = (xoptions & GTK_FILL) != 0;
  child->yfill = (yoptions & GTK_FILL) != 0;

  sheet->children = g_list_append (sheet->children, child);

  gtk_sheet_get_cell_area (sheet, row, col, &area);

  child->x = area.x + child->xpadding;
  child->y = area.y + child->ypadding;

  if (GTK_WIDGET_VISIBLE (GTK_WIDGET (sheet)))
    {
      if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)) &&
	  (!GTK_WIDGET_REALIZED (widget) || GTK_WIDGET_NO_WINDOW (widget)))
	gtk_sheet_realize_child (sheet, child);

      if (GTK_WIDGET_MAPPED (GTK_WIDGET (sheet)) &&
	  !GTK_WIDGET_MAPPED (widget))
	gtk_widget_map (widget);
    }

  gtk_sheet_position_child (sheet, child);

  /* This will avoid drawing on the titles */

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      if (GTK_SHEET_ROW_TITLES_VISIBLE (sheet))
	gdk_window_show (sheet->row_title_window);
      if (GTK_SHEET_COL_TITLES_VISIBLE (sheet))
	gdk_window_show (sheet->column_title_window);
    }

}

void
gtk_sheet_button_attach		 (GtkSheet *sheet,
				  GtkWidget *widget,
				  gint row, gint col)
{
  GtkSheetButton *button = 0;
  GtkSheetChild *child;
  GtkRequisition button_requisition;

  if (row >= 0 && col >= 0) return;
  if (row < 0 && col < 0) return;

  child = g_new (GtkSheetChild, 1);
  child->widget = widget;
  child->x = 0;
  child->y = 0;
  child->attached_to_cell = TRUE;
  child->floating = FALSE;
  child->row = row;
  child->col = col;
  child->xpadding = child->ypadding = 0;
  child->xshrink = child->yshrink = FALSE;
  child->xfill = child->yfill = FALSE;


  sheet->children = g_list_append (sheet->children, child);

  gtk_sheet_button_size_request (sheet, button, &button_requisition);


  if (GTK_WIDGET_VISIBLE (GTK_WIDGET (sheet)))
    {
      if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)) &&
	  (!GTK_WIDGET_REALIZED (widget) || GTK_WIDGET_NO_WINDOW (widget)))
	gtk_sheet_realize_child (sheet, child);

      if (GTK_WIDGET_MAPPED (GTK_WIDGET (sheet)) &&
	  !GTK_WIDGET_MAPPED (widget))
	gtk_widget_map (widget);
    }

  if (row == -1) size_allocate_column_title_buttons (sheet);
  if (col == -1) size_allocate_row_title_buttons (sheet);

}

static void
label_size_request (GtkSheet *sheet, gchar *label, GtkRequisition *req)
{
  gchar *words;
  gchar word[1000];
  gint n = 0;
  gint row_height = DEFAULT_ROW_HEIGHT (GTK_WIDGET (sheet)) - 2 * CELLOFFSET + 2;

  req->height = 0;
  req->width = 0;
  words = label;

  while (words && *words != '\0')
    {
      if (*words == '\n' || * (words + 1) == '\0')
	{
	  req->height += row_height;

	  word[n] = '\0';
	  req->width = MAX (req->width, STRING_WIDTH (GTK_WIDGET (sheet), GTK_WIDGET (sheet)->style->font_desc, word));
	  n = 0;
	}
      else
	{
	  word[n++] = *words;
	}
      words++;
    }

  if (n > 0) req->height -= 2;
}

static void
gtk_sheet_button_size_request	 (GtkSheet *sheet,
				  const GtkSheetButton *button,
				  GtkRequisition *button_requisition)
{
  GtkRequisition requisition;
  GtkRequisition label_requisition;

  if (gtk_sheet_autoresize (sheet) && button->label && strlen (button->label) > 0)
    {
      label_size_request (sheet, button->label, &label_requisition);
      label_requisition.width += 2 * CELLOFFSET;
      label_requisition.height += 2 * CELLOFFSET;
    }
  else
    {
      label_requisition.height = DEFAULT_ROW_HEIGHT (GTK_WIDGET (sheet));
      label_requisition.width = COLUMN_MIN_WIDTH;
    }

  if (button->child)
    {
      gtk_widget_size_request (button->child->widget, &requisition);
      requisition.width += 2 * button->child->xpadding;
      requisition.height += 2 * button->child->ypadding;
      requisition.width += 2 * sheet->button->style->xthickness;
      requisition.height += 2 * sheet->button->style->ythickness;
    }
  else
    {
      requisition.height = DEFAULT_ROW_HEIGHT (GTK_WIDGET (sheet));
      requisition.width = COLUMN_MIN_WIDTH;
    }

  *button_requisition = requisition;
  button_requisition->width = MAX (requisition.width, label_requisition.width);
  button_requisition->height = MAX (requisition.height, label_requisition.height);

}

static void
gtk_sheet_row_size_request (GtkSheet *sheet,
			    gint row,
			    guint *requisition)
{
  GtkRequisition button_requisition;
  GList *children;

  gtk_sheet_button_size_request (sheet,
				 yyy_row_button (sheet, row),
				 &button_requisition);

  *requisition = button_requisition.height;

  children = sheet->children;
  while (children)
    {
      GtkSheetChild *child = (GtkSheetChild *)children->data;
      GtkRequisition child_requisition;

      if (child->attached_to_cell && child->row == row && child->col != -1 && !child->floating && !child->yshrink)
	{
	  gtk_widget_get_child_requisition (child->widget, &child_requisition);

	  if (child_requisition.height + 2 * child->ypadding > *requisition)
	    *requisition = child_requisition.height + 2 * child->ypadding;
	}
      children = children->next;
    }

  sheet->row_requisition = * requisition;
}

static void
gtk_sheet_column_size_request (GtkSheet *sheet,
			       gint col,
			       guint *requisition)
{
  GtkRequisition button_requisition;
  GList *children;

  gtk_sheet_button_size_request (sheet,
				 xxx_column_button (sheet, col),
				 &button_requisition);

  *requisition = button_requisition.width;

  children = sheet->children;
  while (children)
    {
      GtkSheetChild *child = (GtkSheetChild *)children->data;
      GtkRequisition child_requisition;

      if (child->attached_to_cell && child->col == col && child->row != -1 && !child->floating && !child->xshrink)
	{
	  gtk_widget_get_child_requisition (child->widget, &child_requisition);

	  if (child_requisition.width + 2 * child->xpadding > *requisition)
	    *requisition = child_requisition.width + 2 * child->xpadding;
	}
      children = children->next;
    }

  sheet->column_requisition = *requisition;
}

void
gtk_sheet_move_child (GtkSheet *sheet, GtkWidget *widget, gint x, gint y)
{
  GtkSheetChild *child;
  GList *children;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  children = sheet->children;
  while (children)
    {
      child = children->data;

      if (child->widget == widget)
	{
	  child->x = x;
	  child->y = y;
	  child->row = ROW_FROM_YPIXEL (sheet, y);
	  child->col = COLUMN_FROM_XPIXEL (sheet, x);
	  gtk_sheet_position_child (sheet, child);
	  return;
	}

      children = children->next;
    }

  g_warning ("Widget must be a GtkSheet child");

}

static void
gtk_sheet_position_child (GtkSheet *sheet, GtkSheetChild *child)
{
  GtkRequisition child_requisition;
  GtkAllocation child_allocation;
  gint xoffset = 0;
  gint yoffset = 0;
  gint x = 0, y = 0;
  GdkRectangle area;

  gtk_widget_get_child_requisition (child->widget, &child_requisition);

  if (sheet->column_titles_visible)
    yoffset = sheet->column_title_area.height;

  if (sheet->row_titles_visible)
    xoffset = sheet->row_title_area.width;

  if (child->attached_to_cell)
    {
      gtk_sheet_get_cell_area (sheet, child->row, child->col, &area);
      child->x = area.x + child->xpadding;
      child->y = area.y + child->ypadding;

      if (!child->floating)
	{
	  if (child_requisition.width + 2 * child->xpadding <= xxx_column_width (sheet, child->col))
	    {
	      if (child->xfill)
		{
		  child_requisition.width = child_allocation.width = xxx_column_width (sheet, child->col) - 2 * child->xpadding;
		}
	      else
		{
		  if (child->xexpand)
		    {
		      child->x = area.x + xxx_column_width (sheet, child->col) / 2 -
			child_requisition.width / 2;
		    }
		  child_allocation.width = child_requisition.width;
		}
	    }
	  else
	    {
	      if (!child->xshrink)
		{
		  gtk_sheet_set_column_width (sheet, child->col, child_requisition.width + 2 * child->xpadding);
		}
	      child_allocation.width = xxx_column_width (sheet, child->col) - 2 * child->xpadding;
	    }

	  if (child_requisition.height +
	      2 * child->ypadding <= yyy_row_height (sheet, child->row))
	    {
	      if (child->yfill)
		{
		  child_requisition.height = child_allocation.height =
		    yyy_row_height (sheet, child->row) - 2 * child->ypadding;
		}
	      else
		{
		  if (child->yexpand)
		    {
		      child->y = area.y + yyy_row_height (sheet, child->row) / 2
			- child_requisition.height / 2;
		    }
		  child_allocation.height = child_requisition.height;
		}
	    }
	  else
	    {
	      if (!child->yshrink)
		{
		  gtk_sheet_set_row_height (sheet, child->row, child_requisition.height + 2 * child->ypadding);
		}
	      child_allocation.height = yyy_row_height (sheet, child->row) -
		2 * child->ypadding;
	    }
	}
      else
	{
	  child_allocation.width = child_requisition.width;
	  child_allocation.height = child_requisition.height;
	}

      x = child_allocation.x = child->x + xoffset;
      y = child_allocation.y = child->y + yoffset;
    }
  else
    {
      x = child_allocation.x = child->x + sheet->hoffset + xoffset;
      x = child_allocation.x = child->x + xoffset;
      y = child_allocation.y = child->y + sheet->voffset + yoffset;
      y = child_allocation.y = child->y + yoffset;
      child_allocation.width = child_requisition.width;
      child_allocation.height = child_requisition.height;
    }

  gtk_widget_size_allocate (child->widget, &child_allocation);
  gtk_widget_queue_draw (child->widget);
}

static void
gtk_sheet_forall (GtkContainer *container,
		  gboolean include_internals,
		  GtkCallback callback,
		  gpointer callback_data)
{
  GtkSheet *sheet;
  GtkSheetChild *child;
  GList *children;

  g_return_if_fail (GTK_IS_SHEET (container));
  g_return_if_fail (callback != NULL);

  sheet = GTK_SHEET (container);
  children = sheet->children;
  while (children)
    {
      child = children->data;
      children = children->next;

      (* callback) (child->widget, callback_data);
    }
  if (sheet->button)
    (* callback) (sheet->button, callback_data);
  if (sheet->sheet_entry)
    (* callback) (sheet->sheet_entry, callback_data);
}


static void
gtk_sheet_position_children (GtkSheet *sheet)
{
  GList *children;
  GtkSheetChild *child;

  children = sheet->children;

  while (children)
    {
      child = (GtkSheetChild *)children->data;

      if (child->col != -1 && child->row != -1)
	gtk_sheet_position_child (sheet, child);

      if (child->row == -1)
	{
	  if (child->col < MIN_VISIBLE_COLUMN (sheet) ||
	      child->col > MAX_VISIBLE_COLUMN (sheet))
	    gtk_sheet_child_hide (child);
	  else
	    gtk_sheet_child_show (child);
	}
      if (child->col == -1)
	{
	  if (child->row < MIN_VISIBLE_ROW (sheet) ||
	      child->row > MAX_VISIBLE_ROW (sheet))
	    gtk_sheet_child_hide (child);
	  else
	    gtk_sheet_child_show (child);
	}

      children = children->next;
    }
}

static void
gtk_sheet_remove (GtkContainer *container, GtkWidget *widget)
{
  GtkSheet *sheet;
  GList *children;
  GtkSheetChild *child = 0;

  g_return_if_fail (container != NULL);
  g_return_if_fail (GTK_IS_SHEET (container));

  sheet = GTK_SHEET (container);

  children = sheet->children;

  while (children)
    {
      child = (GtkSheetChild *)children->data;

      if (child->widget == widget) break;

      children = children->next;
    }

  if (children)
    {
      gtk_widget_unparent (widget);
      child->widget = NULL;

      sheet->children = g_list_remove_link (sheet->children, children);
      g_list_free_1 (children);
      g_free (child);
    }

}

static void
gtk_sheet_realize_child (GtkSheet *sheet, GtkSheetChild *child)
{
  GtkWidget *widget;

  widget = GTK_WIDGET (sheet);

  if (GTK_WIDGET_REALIZED (widget))
    {
      if (child->row == -1)
	gtk_widget_set_parent_window (child->widget, sheet->column_title_window);
      else if (child->col == -1)
	gtk_widget_set_parent_window (child->widget, sheet->row_title_window);
      else
	gtk_widget_set_parent_window (child->widget, sheet->sheet_window);
    }

  gtk_widget_set_parent (child->widget, widget);
}



GtkSheetChild *
gtk_sheet_get_child_at (GtkSheet *sheet, gint row, gint col)
{
  GList *children;
  GtkSheetChild *child = 0;

  g_return_val_if_fail (sheet != NULL, NULL);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), NULL);

  children = sheet->children;

  while (children)
    {
      child = (GtkSheetChild *)children->data;

      if (child->attached_to_cell)
	if (child->row == row && child->col == col) break;

      children = children->next;
    }

  if (children) return child;

  return NULL;
}

static void
gtk_sheet_child_hide (GtkSheetChild *child)
{
  g_return_if_fail (child != NULL);
  gtk_widget_hide (child->widget);
}

static void
gtk_sheet_child_show (GtkSheetChild *child)
{
  g_return_if_fail (child != NULL);

  gtk_widget_show (child->widget);
}

GSheetModel *
gtk_sheet_get_model (const GtkSheet *sheet)
{
  g_return_val_if_fail (GTK_IS_SHEET (sheet), NULL);

  return sheet->model;
}


GtkSheetButton *
gtk_sheet_button_new (void)
{
  GtkSheetButton *button = g_malloc (sizeof (GtkSheetButton));

  button->state = GTK_STATE_NORMAL;
  button->label = NULL;
  button->label_visible = TRUE;
  button->child = NULL;
  button->justification = GTK_JUSTIFY_FILL;

  return button;
}


inline void
gtk_sheet_button_free (GtkSheetButton *button)
{
  if (!button) return ;

  g_free (button->label);
  g_free (button);
}


static GString *
range_to_text (const GtkSheet *sheet)
{
  gchar *celltext = NULL;
  gint r, c;
  GString *string;

  if ( !gtk_sheet_range_isvisible (sheet, sheet->range))
    return NULL;

  string = g_string_sized_new (80);

  for (r = sheet->range.row0; r <= sheet->range.rowi; ++r)
    {
      for (c = sheet->range.col0; c < sheet->range.coli; ++c)
	{
	  celltext = gtk_sheet_cell_get_text (sheet, r, c);
	  g_string_append (string, celltext);
	  g_string_append (string, "\t");
	  g_free (celltext);
	}
      celltext = gtk_sheet_cell_get_text (sheet, r, c);
      g_string_append (string, celltext);
      if ( r < sheet->range.rowi)
	g_string_append (string, "\n");
      g_free (celltext);
    }

  return string;
}

static GString *
range_to_html (const GtkSheet *sheet)
{
  gchar *celltext = NULL;
  gint r, c;
  GString *string;

  if ( !gtk_sheet_range_isvisible (sheet, sheet->range))
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
	  celltext = gtk_sheet_cell_get_text (sheet, r, c);
	  g_string_append (string, celltext);
	  g_string_append (string, "</td>\n");
	  g_free (celltext);
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
  GtkSheet *sheet = GTK_SHEET (data);
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
  GtkSheet *sheet = GTK_SHEET (data);
  gtk_sheet_real_unselect_range (sheet, NULL);
}

static void
gtk_sheet_update_primary_selection (GtkSheet *sheet)
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
  
  if (gtk_sheet_range_isvisible (sheet, sheet->range))
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

