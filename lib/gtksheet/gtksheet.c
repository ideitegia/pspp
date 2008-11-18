#define GDK_MULTIHEAD_SAFE 1
#define GLIB_DISABLE_DEPRECATED 1
#define GDK_DISABLE_DEPRECATED 1
#define GTK_DISABLE_DEPRECATED 1
/*
 * Copyright (C) 2006, 2008 Free Software Foundation
 *
 * This version of GtkSheet has been *heavily* modified, for the specific
 * requirements of PSPPIRE.  The changes are copyright by the
 * Free Software Foundation.  The copyright notice for the original work is
 * below.
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
 * SECTION:gtksheet
 * @short_description: spreadsheet widget for gtk2
 *
 * GtkSheet is a matrix widget for GTK+. It consists of an scrollable grid of
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
#include "gtkitementry.h"
#include "gtksheet.h"
#include "gtkextra-marshal.h"
#include "gsheetmodel.h"

/* sheet flags */
enum
  {
    GTK_SHEET_REDRAW_PENDING = 1 << 0,
    GTK_SHEET_IN_XDRAG = 1 << 1,
    GTK_SHEET_IN_YDRAG = 1 << 2,
    GTK_SHEET_IN_DRAG = 1 << 3,
    GTK_SHEET_IN_SELECTION = 1 << 4,
    GTK_SHEET_IN_RESIZE = 1 << 5
  };

#define GTK_SHEET_FLAGS(sheet) (GTK_SHEET (sheet)->flags)
#define GTK_SHEET_SET_FLAGS(sheet,flag) (GTK_SHEET_FLAGS (sheet) |= (flag))
#define GTK_SHEET_UNSET_FLAGS(sheet,flag) (GTK_SHEET_FLAGS (sheet) &= ~ (flag))

#define GTK_SHEET_IN_XDRAG(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_IN_XDRAG)
#define GTK_SHEET_IN_YDRAG(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_IN_YDRAG)
#define GTK_SHEET_IN_DRAG(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_IN_DRAG)
#define GTK_SHEET_IN_SELECTION(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_IN_SELECTION)
#define GTK_SHEET_IN_RESIZE(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_IN_RESIZE)
#define GTK_SHEET_REDRAW_PENDING(sheet) (GTK_SHEET_FLAGS (sheet) & GTK_SHEET_REDRAW_PENDING)

#define CELL_SPACING 1

#define TIMEOUT_HOVER 300
#define COLUMN_MIN_WIDTH 10
#define COLUMN_TITLES_HEIGHT 4
#define DEFAULT_COLUMN_WIDTH 80

static void gtk_sheet_update_primary_selection (GtkSheet *sheet);
static void gtk_sheet_column_title_button_draw (GtkSheet *sheet, gint column);

static void gtk_sheet_row_title_button_draw (GtkSheet *sheet, gint row);

static void gtk_sheet_set_row_height (GtkSheet *sheet,
				      gint row,
				      guint height);

static gboolean gtk_sheet_cell_empty (const GtkSheet *, gint, gint);

static void destroy_hover_window (GtkSheetHoverTitle *);
static GtkSheetHoverTitle *create_hover_window (void);

static GtkStateType gtk_sheet_cell_get_state (GtkSheet *sheet, gint row, gint col);


static inline  void
dispose_string (const GtkSheet *sheet, gchar *text)
{
  GSheetModel *model = gtk_sheet_get_model (sheet);

  if ( ! model )
    return;

  if (g_sheet_model_free_strings (model))
    g_free (text);
}

static guint
default_row_height (const GtkSheet *sheet)
{
  GtkWidget *widget = GTK_WIDGET (sheet);

  if (!widget->style->font_desc) return 25;
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

      return PANGO_PIXELS (val) + 2 * COLUMN_TITLES_HEIGHT;
    }
}

static
guint STRING_WIDTH (GtkWidget *widget,
		    const PangoFontDescription *font, const gchar *text)
{
  PangoRectangle rect;
  PangoLayout *layout;

  layout = gtk_widget_create_pango_layout (widget, text);
  pango_layout_set_font_description (layout, font);

  pango_layout_get_extents (layout, NULL, &rect);

  g_object_unref (layout);
  return PANGO_PIXELS (rect.width);
}

/* Return the row containing pixel Y */
static gint
yyy_row_ypixel_to_row (const GtkSheet *sheet, gint y)
{
  GSheetRow *geo = sheet->row_geometry;

  if (y < 0)
    {
      g_error ("This shouldnt happen");
      return -1;
    }

  return g_sheet_row_pixel_to_row (geo, y);
}


static inline glong
min_visible_row (const GtkSheet *sheet)
{
  return yyy_row_ypixel_to_row (sheet, sheet->vadjustment->value);
}


static inline glong
max_visible_row (const GtkSheet *sheet)
{
  return yyy_row_ypixel_to_row (sheet,
			   sheet->vadjustment->value +
			   sheet->vadjustment->page_size);
}


/* returns the column index from a x pixel location */
static inline gint
column_from_xpixel (const GtkSheet *sheet, gint x)
{
  gint i;
  gint cx = 0;

  if (x < 0) return -1;
  for (i = 0;
       i < g_sheet_column_get_column_count (sheet->column_geometry); i++)
    {
      if (x >= cx &&
	  x <= (cx + g_sheet_column_get_width (sheet->column_geometry, i)))
	return i;

      cx += g_sheet_column_get_width (sheet->column_geometry, i);
    }

  /* no match */
  return g_sheet_column_get_column_count (sheet->column_geometry) - 1;
}


static inline glong
min_visible_column (const GtkSheet *sheet)
{
  return column_from_xpixel (sheet, sheet->hadjustment->value);
}


static inline glong
max_visible_column (const GtkSheet *sheet)
{
  return column_from_xpixel (sheet,
			     sheet->hadjustment->value +
			     sheet->hadjustment->page_size);
}


/* The size of the region (in pixels) around the row/column boundaries
   where the height/width may be grabbed to change size */
#define DRAG_WIDTH 6

static gboolean
on_column_boundary (const GtkSheet *sheet, gint x, gint *column)
{
  gint col;

  x += sheet->hadjustment->value;

  col = column_from_xpixel (sheet, x);

  if ( column_from_xpixel (sheet, x - DRAG_WIDTH / 2) < col )
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

static inline gboolean
POSSIBLE_YDRAG (const GtkSheet *sheet, gint y, gint *drag_row)
{
  gint row, ydrag;

  y += sheet->vadjustment->value;
  row = yyy_row_ypixel_to_row (sheet, y);
  *drag_row = row;

  ydrag = g_sheet_row_start_pixel (sheet->row_geometry, row) + CELL_SPACING;
  if (y <= ydrag + DRAG_WIDTH / 2 && row != 0)
    {
      *drag_row = row - 1;
      return g_sheet_row_get_sensitivity (sheet->row_geometry, row - 1);
    }

  ydrag += g_sheet_row_get_height (sheet->row_geometry, row);

  if (y >= ydrag - DRAG_WIDTH / 2 && y <= ydrag + DRAG_WIDTH / 2)
    return g_sheet_row_get_sensitivity (sheet->row_geometry, row);

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

  *drag_column = column_from_xpixel (sheet, x);
  *drag_row = yyy_row_ypixel_to_row (sheet, y);

  if (x >= g_sheet_column_start_pixel (sheet->column_geometry, sheet->range.col0) - DRAG_WIDTH / 2 &&
      x <= g_sheet_column_start_pixel (sheet->column_geometry, sheet->range.coli) +
      g_sheet_column_get_width (sheet->column_geometry, sheet->range.coli) + DRAG_WIDTH / 2)
    {
      ydrag = g_sheet_row_start_pixel (sheet->row_geometry, sheet->range.row0);
      if (y >= ydrag - DRAG_WIDTH / 2 && y <= ydrag + DRAG_WIDTH / 2)
	{
	  *drag_row = sheet->range.row0;
	  return TRUE;
	}
      ydrag = g_sheet_row_start_pixel (sheet->row_geometry, sheet->range.rowi) +
	g_sheet_row_get_height (sheet->row_geometry, sheet->range.rowi);
      if (y >= ydrag - DRAG_WIDTH / 2 && y <= ydrag + DRAG_WIDTH / 2)
	{
	  *drag_row = sheet->range.rowi;
	  return TRUE;
	}
    }

  if (y >= g_sheet_row_start_pixel (sheet->row_geometry, sheet->range.row0) - DRAG_WIDTH / 2 &&
      y <= g_sheet_row_start_pixel (sheet->row_geometry, sheet->range.rowi) +
      g_sheet_row_get_height (sheet->row_geometry, sheet->range.rowi) + DRAG_WIDTH / 2)
    {
      xdrag = g_sheet_column_start_pixel (sheet->column_geometry, sheet->range.col0);
      if (x >= xdrag - DRAG_WIDTH / 2 && x <= xdrag + DRAG_WIDTH / 2)
	{
	  *drag_column = sheet->range.col0;
	  return TRUE;
	}
      xdrag = g_sheet_column_start_pixel (sheet->column_geometry, sheet->range.coli) +
	g_sheet_column_get_width (sheet->column_geometry, sheet->range.coli);
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

  xdrag = g_sheet_column_start_pixel (sheet->column_geometry, sheet->range.coli)+
    g_sheet_column_get_width (sheet->column_geometry, sheet->range.coli);

  ydrag = g_sheet_row_start_pixel (sheet->row_geometry, sheet->range.rowi) +
    g_sheet_row_get_height (sheet->row_geometry, sheet->range.rowi);

  if (sheet->state == GTK_SHEET_COLUMN_SELECTED)
    ydrag = g_sheet_row_start_pixel (sheet->row_geometry, min_visible_row (sheet));

  if (sheet->state == GTK_SHEET_ROW_SELECTED)
    xdrag = g_sheet_column_start_pixel (sheet->column_geometry, min_visible_column (sheet));

  *drag_column = column_from_xpixel (sheet, x);
  *drag_row = yyy_row_ypixel_to_row (sheet, y);

  if (x >= xdrag - DRAG_WIDTH / 2 && x <= xdrag + DRAG_WIDTH / 2 &&
      y >= ydrag - DRAG_WIDTH / 2 && y <= ydrag + DRAG_WIDTH / 2) return TRUE;

  return FALSE;
}


static gboolean
rectangle_from_range (GtkSheet *sheet, const GtkSheetRange *range,
		      GdkRectangle *r)
{
  g_return_val_if_fail (range, FALSE);

  r->x = g_sheet_column_start_pixel (sheet->column_geometry, range->col0);
  r->x -= sheet->hadjustment->value;

  if ( sheet->row_titles_visible)
    r->x += sheet->row_title_area.width;


  r->y = g_sheet_row_start_pixel (sheet->row_geometry, range->row0);
  r->y -= sheet->vadjustment->value;

  if ( sheet->column_titles_visible)
    r->y += sheet->column_title_area.height;

  r->width = g_sheet_column_start_pixel (sheet->column_geometry, range->coli) -
    g_sheet_column_start_pixel (sheet->column_geometry, range->col0) +
    g_sheet_column_get_width (sheet->column_geometry, range->coli);

  r->height = g_sheet_row_start_pixel (sheet->row_geometry, range->rowi) -
    g_sheet_row_start_pixel (sheet->row_geometry, range->row0) +
    g_sheet_row_get_height (sheet->row_geometry, range->rowi);

  return TRUE;
}

static gboolean
rectangle_from_cell (GtkSheet *sheet, gint row, gint col,
		     GdkRectangle *r)
{
  GtkSheetRange range;
  g_return_val_if_fail (row >= 0, FALSE);
  g_return_val_if_fail (col >= 0, FALSE);

  range.row0 = range.rowi = row;
  range.col0 = range.coli = col;

  return rectangle_from_range (sheet, &range, r);
}


static void gtk_sheet_class_init 		 (GtkSheetClass *klass);
static void gtk_sheet_init 			 (GtkSheet *sheet);
static void gtk_sheet_dispose 			 (GObject *object);
static void gtk_sheet_finalize 			 (GObject *object);
static void gtk_sheet_style_set 		 (GtkWidget *widget,
						  GtkStyle *previous_style);
static void gtk_sheet_realize 			 (GtkWidget *widget);
static void gtk_sheet_unrealize 		 (GtkWidget *widget);
static void gtk_sheet_map 			 (GtkWidget *widget);
static void gtk_sheet_unmap 			 (GtkWidget *widget);
static gint gtk_sheet_expose 			 (GtkWidget *widget,
						  GdkEventExpose *event);
static void gtk_sheet_forall 			 (GtkContainer *container,
						  gboolean include_internals,
						  GtkCallback callback,
						  gpointer callback_data);

static void gtk_sheet_set_scroll_adjustments	 (GtkSheet *sheet,
						  GtkAdjustment *hadjustment,
						  GtkAdjustment *vadjustment);

static gint gtk_sheet_button_press 		 (GtkWidget *widget,
						  GdkEventButton *event);
static gint gtk_sheet_button_release 		 (GtkWidget *widget,
						  GdkEventButton *event);
static gint gtk_sheet_motion 			 (GtkWidget *widget,
						  GdkEventMotion *event);
static gboolean gtk_sheet_crossing_notify           (GtkWidget *widget,
						     GdkEventCrossing *event);
static gint gtk_sheet_entry_key_press		 (GtkWidget *widget,
						  GdkEventKey *key);
static gboolean gtk_sheet_key_press		 (GtkWidget *widget,
						  GdkEventKey *key);
static void gtk_sheet_size_request 		 (GtkWidget *widget,
						  GtkRequisition *requisition);
static void gtk_sheet_size_allocate 		 (GtkWidget *widget,
						  GtkAllocation *allocation);

/* Sheet queries */

static gboolean gtk_sheet_range_isvisible (const GtkSheet *sheet,
					   GtkSheetRange range);
static gboolean gtk_sheet_cell_isvisible  (GtkSheet *sheet,
					   gint row, gint column);
/* Drawing Routines */

/* draw cell background and frame */
static void gtk_sheet_cell_draw_bg 	 (GtkSheet *sheet,
						  gint row, gint column);

/* draw cell contents */
static void gtk_sheet_cell_draw_label 		 (GtkSheet *sheet,
						  gint row, gint column);

/* draw visible part of range. If range == NULL then draw the whole screen */
static void gtk_sheet_range_draw (GtkSheet *sheet,
				  const GtkSheetRange *range);

/* highlight the visible part of the selected range */
static void gtk_sheet_range_draw_selection	 (GtkSheet *sheet,
						  GtkSheetRange range);

/* Selection */

static gboolean gtk_sheet_move_query   		 (GtkSheet *sheet,
						  gint row, gint column);
static void gtk_sheet_real_select_range 	 (GtkSheet *sheet,
						  const GtkSheetRange *range);
static void gtk_sheet_real_unselect_range 	 (GtkSheet *sheet,
						  const GtkSheetRange *range);
static void gtk_sheet_extend_selection		 (GtkSheet *sheet,
						  gint row, gint column);
static void gtk_sheet_new_selection		 (GtkSheet *sheet,
						  GtkSheetRange *range);
static void gtk_sheet_draw_border 		 (GtkSheet *sheet,
						  GtkSheetRange range);

/* Active Cell handling */

static void gtk_sheet_entry_changed		 (GtkWidget *widget,
						  gpointer data);
static void gtk_sheet_deactivate_cell	 (GtkSheet *sheet);
static void gtk_sheet_hide_active_cell		 (GtkSheet *sheet);
static gboolean gtk_sheet_activate_cell		 (GtkSheet *sheet,
						  gint row, gint col);
static void gtk_sheet_draw_active_cell		 (GtkSheet *sheet);
static void gtk_sheet_show_active_cell		 (GtkSheet *sheet);
static gboolean gtk_sheet_click_cell		 (GtkSheet *sheet,
						  gint row,
						  gint column);


/* Scrollbars */

static void adjust_scrollbars 			 (GtkSheet *sheet);
static void vadjustment_value_changed 		 (GtkAdjustment *adjustment,
						  gpointer data);
static void hadjustment_value_changed 		 (GtkAdjustment *adjustment,
						  gpointer data);


static void draw_xor_vline 			 (GtkSheet *sheet);
static void draw_xor_hline 			 (GtkSheet *sheet);
static void draw_xor_rectangle			 (GtkSheet *sheet,
						  GtkSheetRange range);

static guint new_column_width 			 (GtkSheet *sheet,
						  gint column,
						  gint *x);
static guint new_row_height 			 (GtkSheet *sheet,
						  gint row,
						  gint *y);
/* Sheet Button */

static void create_global_button		 (GtkSheet *sheet);
static void global_button_clicked		 (GtkWidget *widget,
						  gpointer data);
/* Sheet Entry */

static void create_sheet_entry			 (GtkSheet *sheet);
static void gtk_sheet_size_allocate_entry	 (GtkSheet *sheet);
static void gtk_sheet_entry_set_max_size	 (GtkSheet *sheet);

/* Sheet button gadgets */

static void size_allocate_column_title_buttons 	 (GtkSheet *sheet);
static void size_allocate_row_title_buttons 	 (GtkSheet *sheet);


static void size_allocate_global_button 	 (GtkSheet *sheet);
static void gtk_sheet_button_size_request	 (GtkSheet *sheet,
						  const GtkSheetButton *button,
						  GtkRequisition *requisition);

/* Attributes routines */
static void init_attributes			 (const GtkSheet *sheet,
						  gint col,
						  GtkSheetCellAttr *attributes);


/* Memory allocation routines */
static void gtk_sheet_real_cell_clear 		 (GtkSheet *sheet,
						  gint row,
						  gint column);


static void gtk_sheet_column_size_request (GtkSheet *sheet,
					   gint col,
					   guint *requisition);
static void gtk_sheet_row_size_request (GtkSheet *sheet,
					gint row,
					guint *requisition);


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
    DEACTIVATE,
    ACTIVATE,
    CHANGED,
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
	g_type_register_static (GTK_TYPE_BIN, "GtkSheet",
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


static void column_titles_changed (GtkWidget *w, gint first, gint n_columns,
				   gpointer data);

/* Properties */
enum
  {
    PROP_0,
    PROP_ROW_GEO,
    PROP_COL_GEO,
    PROP_MODEL
  };

static void
gtk_sheet_set_row_geometry (GtkSheet *sheet, GSheetRow *geo)
{
  if ( sheet->row_geometry ) g_object_unref (sheet->row_geometry);

  sheet->row_geometry = geo;

  if ( sheet->row_geometry ) g_object_ref (sheet->row_geometry);
}

static void
gtk_sheet_set_column_geometry (GtkSheet *sheet, GSheetColumn *geo)
{
  if ( sheet->column_geometry ) g_object_unref (sheet->column_geometry);

  sheet->column_geometry = geo;

  if ( sheet->column_geometry ) g_object_ref (sheet->column_geometry);
}


static void
gtk_sheet_set_property (GObject         *object,
			guint            prop_id,
			const GValue    *value,
			GParamSpec      *pspec)

{
  GtkSheet *sheet = GTK_SHEET (object);

  switch (prop_id)
    {
    case PROP_ROW_GEO:
      gtk_sheet_set_row_geometry (sheet, g_value_get_pointer (value));
      break;
    case PROP_COL_GEO:
      gtk_sheet_set_column_geometry (sheet, g_value_get_pointer (value));
      if ( sheet->column_geometry)
	g_signal_connect (sheet->column_geometry, "columns_changed",
			  G_CALLBACK (column_titles_changed), sheet);
      break;
    case PROP_MODEL:
      gtk_sheet_set_model (sheet, g_value_get_pointer (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void
gtk_sheet_get_property (GObject         *object,
			guint            prop_id,
			GValue          *value,
			GParamSpec      *pspec)
{
  GtkSheet *sheet = GTK_SHEET (object);

  switch (prop_id)
    {
    case PROP_ROW_GEO:
      g_value_set_pointer (value, sheet->row_geometry);
      break;
    case PROP_COL_GEO:
      g_value_set_pointer (value, sheet->column_geometry);
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
gtk_sheet_class_init (GtkSheetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  GParamSpec *row_geo_spec ;
  GParamSpec *col_geo_spec ;
  GParamSpec *model_spec ;

  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

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
		  g_cclosure_marshal_VOID__INT,
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
		  g_cclosure_marshal_VOID__INT,
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
		  g_cclosure_marshal_VOID__INT,
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
		  g_cclosure_marshal_VOID__INT,
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
		  g_cclosure_marshal_VOID__BOXED,
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
		  gtkextra_VOID__INT_INT,
		  G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);

  sheet_signals[ACTIVATE] =
    g_signal_new ("activate",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, activate),
		  NULL, NULL,
		  gtkextra_BOOLEAN__INT_INT,
		  G_TYPE_BOOLEAN, 2, G_TYPE_INT, G_TYPE_INT);

  sheet_signals[CHANGED] =
    g_signal_new ("changed",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST,
		  offsetof (GtkSheetClass, changed),
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
  container_class->remove = NULL;
  container_class->forall = gtk_sheet_forall;

  object_class->dispose = gtk_sheet_dispose;
  object_class->finalize = gtk_sheet_finalize;


  row_geo_spec =
    g_param_spec_pointer ("row-geometry",
			  "Row Geometry",
			  "A pointer to the model of the row geometry",
			  G_PARAM_READABLE | G_PARAM_WRITABLE );

  col_geo_spec =
    g_param_spec_pointer ("column-geometry",
			  "Column Geometry",
			  "A pointer to the model of the column geometry",
			  G_PARAM_READABLE | G_PARAM_WRITABLE );

  model_spec =
    g_param_spec_pointer ("model",
			  "Model",
			  "A pointer to the data model",
			  G_PARAM_READABLE | G_PARAM_WRITABLE );


  object_class->set_property = gtk_sheet_set_property;
  object_class->get_property = gtk_sheet_get_property;

  g_object_class_install_property (object_class,
                                   PROP_ROW_GEO,
                                   row_geo_spec);

  g_object_class_install_property (object_class,
                                   PROP_COL_GEO,
                                   col_geo_spec);

  g_object_class_install_property (object_class,
                                   PROP_MODEL,
                                   model_spec);


  widget_class->realize = gtk_sheet_realize;
  widget_class->unrealize = gtk_sheet_unrealize;
  widget_class->map = gtk_sheet_map;
  widget_class->unmap = gtk_sheet_unmap;
  widget_class->style_set = gtk_sheet_style_set;
  widget_class->button_press_event = gtk_sheet_button_press;
  widget_class->button_release_event = gtk_sheet_button_release;
  widget_class->motion_notify_event = gtk_sheet_motion;
  widget_class->enter_notify_event = gtk_sheet_crossing_notify;
  widget_class->leave_notify_event = gtk_sheet_crossing_notify;
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
  klass->changed = NULL;
}

static void
gtk_sheet_init (GtkSheet *sheet)
{
  sheet->model = NULL;
  sheet->column_geometry = NULL;
  sheet->row_geometry = NULL;

  sheet->flags = 0;
  sheet->selection_mode = GTK_SELECTION_NONE;
  sheet->state = GTK_SHEET_NORMAL;

  GTK_WIDGET_UNSET_FLAGS (sheet, GTK_NO_WINDOW);
  GTK_WIDGET_SET_FLAGS (sheet, GTK_CAN_FOCUS);

  sheet->column_title_window = NULL;
  sheet->column_title_area.x = 0;
  sheet->column_title_area.y = 0;
  sheet->column_title_area.width = 0;
  sheet->column_title_area.height = default_row_height (sheet);

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

  sheet->state = GTK_SHEET_NORMAL;

  sheet->sheet_window = NULL;
  sheet->entry_widget = NULL;
  sheet->entry_container = NULL;
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

  sheet->columns_resizable = TRUE;
  sheet->rows_resizable = TRUE;

  sheet->row_titles_visible = TRUE;
  sheet->row_title_area.width = DEFAULT_COLUMN_WIDTH;

  sheet->column_titles_visible = TRUE;


  /* create sheet entry */
  sheet->entry_type = 0;
  create_sheet_entry (sheet);

  /* create global selection button */
  create_global_button (sheet);
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
  range.coli = g_sheet_column_get_column_count (sheet->column_geometry) - 1;
  range.rowi = g_sheet_row_get_row_count (sheet->row_geometry) - 1;

  adjust_scrollbars (sheet);

  if (sheet->active_cell.col >= model_columns)
    gtk_sheet_activate_cell (sheet, sheet->active_cell.row, model_columns - 1);

  for (i = first_column; i <= max_visible_column (sheet); i++)
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
  range.rowi = g_sheet_row_get_row_count (sheet->row_geometry) - 1;
  range.coli = g_sheet_column_get_column_count (sheet->column_geometry) - 1;

  adjust_scrollbars (sheet);

  if (sheet->active_cell.row >= model_rows)
    gtk_sheet_activate_cell (sheet, model_rows - 1, sheet->active_cell.col);

  for (i = first_row; i <= max_visible_row (sheet); i++)
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

  if ( max_visible_row (sheet) >
       g_sheet_model_get_row_count (sheet->model)
       ||
       max_visible_column (sheet) >
       g_sheet_model_get_column_count (sheet->model))
    {
      gtk_sheet_move_query (sheet, 0, 0);
    }

  if ( ( row0 < 0 && col0 < 0 ) || ( rowi < 0 && coli < 0 ) )
    {
      gint i;
      gtk_sheet_range_draw (sheet, NULL);
      adjust_scrollbars (sheet);

      for (i = min_visible_row (sheet); i <= max_visible_row (sheet); i++)
	gtk_sheet_row_title_button_draw (sheet, i);

      for (i = min_visible_column (sheet);
	   i <= max_visible_column (sheet); i++)
	gtk_sheet_column_title_button_draw (sheet, i);

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

  gtk_sheet_range_draw (sheet, &range);
}


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
gtk_sheet_new (GSheetRow *vgeo, GSheetColumn *hgeo, GSheetModel *model)
{
  GtkWidget *widget = g_object_new (GTK_TYPE_SHEET,
				    "row-geometry", vgeo,
				    "column-geometry", hgeo,
				    "model", model,
				    NULL);
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

  if (sheet->model ) g_object_unref (sheet->model);

  sheet->model = model;

  if ( model)
    {
      g_object_ref (model);

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
}


/* Call back for when the column titles have changed.
   FIRST is the first column changed.
   N_COLUMNS is the number of columns which have changed, or -1, which
   indicates that the column has changed to its right-most extremity
*/
static void
column_titles_changed (GtkWidget *w, gint first, gint n_columns, gpointer data)
{
  GtkSheet *sheet = GTK_SHEET (data);
  gboolean extremity = FALSE;

  if ( n_columns == -1 )
    {
      extremity = TRUE;
      n_columns = g_sheet_column_get_column_count (sheet->column_geometry) - 1 ;
    }

  {
    gint i;
    for ( i = first ; i <= first + n_columns ; ++i )
      {
	gtk_sheet_column_title_button_draw (sheet, i);
	g_signal_emit (sheet, sheet_signals[CHANGED], 0, -1, i);
      }
  }

  if ( extremity)
    gtk_sheet_column_title_button_draw (sheet, -1);

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
      g_signal_connect (gtk_sheet_get_entry (sheet),
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

  gtk_sheet_range_draw (sheet, NULL);
}

gboolean
gtk_sheet_grid_visible (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), 0);

  return sheet->show_grid;
}

guint
gtk_sheet_get_columns_count (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), 0);

  return g_sheet_column_get_column_count (sheet->column_geometry);
}

static void
gtk_sheet_set_column_width (GtkSheet *sheet,
			    gint column,
			    guint width);


static void
gtk_sheet_autoresize_column (GtkSheet *sheet, gint column)
{
  gint text_width = 0;
  gint row;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));
  if (column >= g_sheet_column_get_column_count (sheet->column_geometry) || column < 0) return;

  for (row = 0; row < g_sheet_row_get_row_count (sheet->row_geometry); row++)
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
		+ 2 * COLUMN_TITLES_HEIGHT + attributes.border.width;
	      text_width = MAX (text_width, width);
	    }
	}
      dispose_string (sheet, text);
    }

  if (text_width > g_sheet_column_get_width (sheet->column_geometry, column) )
    {
      gtk_sheet_set_column_width (sheet, column, text_width);
      GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_REDRAW_PENDING);
    }
}

void
gtk_sheet_show_column_titles (GtkSheet *sheet)
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
}


void
gtk_sheet_show_row_titles (GtkSheet *sheet)
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
}

void
gtk_sheet_hide_column_titles (GtkSheet *sheet)
{
  if (!sheet->column_titles_visible) return;

  sheet->column_titles_visible = FALSE;

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      if (sheet->column_title_window)
	gdk_window_hide (sheet->column_title_window);
      if (GTK_WIDGET_VISIBLE (sheet->button))
	gtk_widget_hide (sheet->button);

      adjust_scrollbars (sheet);
    }

  if (sheet->vadjustment)
    g_signal_emit_by_name (sheet->vadjustment,
			   "value_changed");
}

void
gtk_sheet_hide_row_titles (GtkSheet *sheet)
{
  if (!sheet->row_titles_visible) return;

  sheet->row_titles_visible = FALSE;

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      if (sheet->row_title_window)
	gdk_window_hide (sheet->row_title_window);

      if (GTK_WIDGET_VISIBLE (sheet->button))
	gtk_widget_hide (sheet->button);

      adjust_scrollbars (sheet);
    }

  if (sheet->hadjustment)
    g_signal_emit_by_name (sheet->hadjustment,
			   "value_changed");
}


void
gtk_sheet_moveto (GtkSheet *sheet,
		  gint row,
		  gint column,
		  gfloat row_align,
		  gfloat col_align)
{
  gint x, y;
  gint width, height;
  gint adjust;
  gint min_row, min_col;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));
  g_return_if_fail (sheet->hadjustment != NULL);
  g_return_if_fail (sheet->vadjustment != NULL);

  if (row < 0 || row >= g_sheet_row_get_row_count (sheet->row_geometry))
    return;
  if (column < 0 || column >= g_sheet_column_get_column_count (sheet->column_geometry))
    return;

  gdk_drawable_get_size (sheet->sheet_window, &width, &height);

  /* adjust vertical scrollbar */
  if (row >= 0 && row_align >= 0.0)
    {
      y = g_sheet_row_start_pixel (sheet->row_geometry, row)
	- (gint) ( row_align * height + (1.0 - row_align)
		   * g_sheet_row_get_height (sheet->row_geometry, row));

      /* This forces the sheet to scroll when you don't see the entire cell */
      min_row = row;
      adjust = 0;
      if (row_align >= 1.0)
	{
	  while (min_row >= 0 && min_row > min_visible_row (sheet))
	    {
	      adjust += g_sheet_row_get_height (sheet->row_geometry, min_row);

	      if (adjust >= height)
		{
		  break;
		}
	      min_row--;
	    }
	  min_row = MAX (min_row, 0);

	  min_row ++;

	  y = g_sheet_row_start_pixel (sheet->row_geometry, min_row) +
	    g_sheet_row_get_height (sheet->row_geometry, min_row) - 1;
	}

      if (y < 0)
	sheet->vadjustment->value = 0.0;
      else
	sheet->vadjustment->value = y;

      g_signal_emit_by_name (sheet->vadjustment,
			     "value_changed");

    }

  /* adjust horizontal scrollbar */
  if (column >= 0 && col_align >= 0.0)
    {
      x = g_sheet_column_start_pixel (sheet->column_geometry, column)
	- (gint) ( col_align*width + (1.0 - col_align)*
		   g_sheet_column_get_width (sheet->column_geometry, column));

      /* This forces the sheet to scroll when you don't see the entire cell */
      min_col = column;
      adjust = 0;
      if (col_align == 1.0)
	{
	  while (min_col >= 0 && min_col > min_visible_column (sheet))
	    {
	      adjust += g_sheet_column_get_width (sheet->column_geometry, min_col);

	      if (adjust >= width)
		{
		  break;
		}
	      min_col--;
	    }
	  min_col = MAX (min_col, 0);
	  x = g_sheet_column_start_pixel (sheet->column_geometry, min_col) +
	    g_sheet_column_get_width (sheet->column_geometry, min_col) - 1;
	}

      if (x < 0)
	sheet->hadjustment->value = 0.0;
      else
	sheet->hadjustment->value = x;

      g_signal_emit_by_name (sheet->hadjustment,
			     "value_changed");
    }
}


static gboolean
gtk_sheet_columns_resizable (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);

  return sheet->columns_resizable;
}


static gboolean
gtk_sheet_rows_resizable (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);

  return sheet->rows_resizable;
}


void
gtk_sheet_select_row (GtkSheet *sheet, gint row)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (row < 0 || row >= g_sheet_row_get_row_count (sheet->row_geometry))
    return;

  if (sheet->state != GTK_SHEET_NORMAL)
    gtk_sheet_real_unselect_range (sheet, NULL);
  else
    gtk_sheet_deactivate_cell (sheet);

  sheet->state = GTK_SHEET_ROW_SELECTED;
  sheet->range.row0 = row;
  sheet->range.col0 = 0;
  sheet->range.rowi = row;
  sheet->range.coli = g_sheet_column_get_column_count (sheet->column_geometry) - 1;
  sheet->active_cell.row = row;
  sheet->active_cell.col = 0;

  g_signal_emit (sheet, sheet_signals[SELECT_ROW], 0, row);
  gtk_sheet_real_select_range (sheet, NULL);
}


void
gtk_sheet_select_column (GtkSheet *sheet, gint column)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (column < 0 || column >= g_sheet_column_get_column_count (sheet->column_geometry))
    return;

  if (sheet->state != GTK_SHEET_NORMAL)
    gtk_sheet_real_unselect_range (sheet, NULL);
  else
    gtk_sheet_deactivate_cell (sheet);


  sheet->state = GTK_SHEET_COLUMN_SELECTED;
  sheet->range.row0 = 0;
  sheet->range.col0 = column;
  sheet->range.rowi = g_sheet_row_get_row_count (sheet->row_geometry) - 1;
  sheet->range.coli = column;
  sheet->active_cell.row = 0;
  sheet->active_cell.col = column;

  g_signal_emit (sheet, sheet_signals[SELECT_COLUMN], 0, column);
  gtk_sheet_real_select_range (sheet, NULL);
}




static gboolean
gtk_sheet_range_isvisible (const GtkSheet *sheet,
			   GtkSheetRange range)
{
  g_return_val_if_fail (sheet != NULL, FALSE);

  if (range.row0 < 0 || range.row0 >= g_sheet_row_get_row_count (sheet->row_geometry))
    return FALSE;

  if (range.rowi < 0 || range.rowi >= g_sheet_row_get_row_count (sheet->row_geometry))
    return FALSE;

  if (range.col0 < 0 || range.col0 >= g_sheet_column_get_column_count (sheet->column_geometry))
    return FALSE;

  if (range.coli < 0 || range.coli >= g_sheet_column_get_column_count (sheet->column_geometry))
    return FALSE;

  if (range.rowi < min_visible_row (sheet))
    return FALSE;

  if (range.row0 > max_visible_row (sheet))
    return FALSE;

  if (range.coli < min_visible_column (sheet))
    return FALSE;

  if (range.col0 > max_visible_column (sheet))
    return FALSE;

  return TRUE;
}

static gboolean
gtk_sheet_cell_isvisible (GtkSheet *sheet,
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

  range->row0 = min_visible_row (sheet);
  range->col0 = min_visible_column (sheet);
  range->rowi = max_visible_row (sheet);
  range->coli = max_visible_column (sheet);
}


static void
gtk_sheet_set_scroll_adjustments (GtkSheet *sheet,
				  GtkAdjustment *hadjustment,
				  GtkAdjustment *vadjustment)
{
  if ( sheet->vadjustment != vadjustment )
    {
      if (sheet->vadjustment)
	g_object_unref (sheet->vadjustment);
      sheet->vadjustment = vadjustment;
      g_object_ref (vadjustment);

      g_signal_connect (sheet->vadjustment, "value_changed",
			G_CALLBACK (vadjustment_value_changed),
			sheet);
    }

  if ( sheet->hadjustment != hadjustment )
    {
      if (sheet->hadjustment)
	g_object_unref (sheet->hadjustment);
      sheet->hadjustment = hadjustment;
      g_object_ref (hadjustment);

      g_signal_connect (sheet->hadjustment, "value_changed",
			G_CALLBACK (hadjustment_value_changed),
			sheet);
    }
}

static void
gtk_sheet_finalize (GObject *object)
{
  GtkSheet *sheet;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GTK_IS_SHEET (object));

  sheet = GTK_SHEET (object);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    (*G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
gtk_sheet_dispose  (GObject *object)
{
  GtkSheet *sheet = GTK_SHEET (object);

  g_return_if_fail (object != NULL);
  g_return_if_fail (GTK_IS_SHEET (object));

  if ( sheet->dispose_has_run )
    return ;

  sheet->dispose_has_run = TRUE;

  if (sheet->model) g_object_unref (sheet->model);
  if (sheet->row_geometry) g_object_unref (sheet->row_geometry);
  if (sheet->column_geometry) g_object_unref (sheet->column_geometry);

  g_object_unref (sheet->entry_container);
  sheet->entry_container = NULL;

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
gtk_sheet_realize (GtkWidget *widget)
{
  GtkSheet *sheet;
  GdkWindowAttr attributes;
  const gint attributes_mask =
    GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP | GDK_WA_CURSOR;

  GdkGCValues values, auxvalues;
  GdkColormap *colormap;
  GdkDisplay *display;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SHEET (widget));

  sheet = GTK_SHEET (widget);

  GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

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

  gdk_gc_get_values (sheet->fg_gc, &auxvalues);

  values.foreground = widget->style->white;
  values.function = GDK_INVERT;
  values.subwindow_mode = GDK_INCLUDE_INFERIORS;
  values.line_width = 3;

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


  sheet->cursor_drag = gdk_cursor_new_for_display (display, GDK_PLUS);

  if (sheet->column_titles_visible)
    gdk_window_show (sheet->column_title_window);
  if (sheet->row_titles_visible)
    gdk_window_show (sheet->row_title_window);

  sheet->hover_window = create_hover_window ();

  size_allocate_row_title_buttons (sheet);
  size_allocate_column_title_buttons (sheet);

  gtk_sheet_update_primary_selection (sheet);
}

static void
create_global_button (GtkSheet *sheet)
{
  sheet->button = gtk_button_new_with_label (" ");

  g_object_ref_sink (sheet->button);

  g_signal_connect (sheet->button,
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
  gtk_sheet_click_cell (GTK_SHEET (data), -1, -1);
  gtk_widget_grab_focus (GTK_WIDGET (data));
}


static void
gtk_sheet_unrealize (GtkWidget *widget)
{
  GtkSheet *sheet;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SHEET (widget));

  sheet = GTK_SHEET (widget);

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
gtk_sheet_map (GtkWidget *widget)
{
  GtkSheet *sheet = GTK_SHEET (widget);

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SHEET (widget));

  if (!GTK_WIDGET_MAPPED (widget))
    {
      GTK_WIDGET_SET_FLAGS (widget, GTK_MAPPED);

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

      if (!GTK_WIDGET_MAPPED (sheet->entry_widget)
	  && sheet->active_cell.row >= 0
	  && sheet->active_cell.col >= 0 )
	{
	  gtk_widget_show (sheet->entry_widget);
	  gtk_widget_map (sheet->entry_widget);
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
    }
}

static void
gtk_sheet_unmap (GtkWidget *widget)
{
  GtkSheet *sheet = GTK_SHEET (widget);

  if (!GTK_WIDGET_MAPPED (widget))
    return;

  GTK_WIDGET_UNSET_FLAGS (widget, GTK_MAPPED);

  gdk_window_hide (sheet->sheet_window);
  if (sheet->column_titles_visible)
    gdk_window_hide (sheet->column_title_window);
  if (sheet->row_titles_visible)
    gdk_window_hide (sheet->row_title_window);
  gdk_window_hide (widget->window);

  if (GTK_WIDGET_MAPPED (sheet->entry_widget))
    gtk_widget_unmap (sheet->entry_widget);

  if (GTK_WIDGET_MAPPED (sheet->button))
    gtk_widget_unmap (sheet->button);
}


static void
gtk_sheet_cell_draw_bg (GtkSheet *sheet, gint row, gint col)
{
  GtkSheetCellAttr attributes;
  GdkRectangle area;

  g_return_if_fail (sheet != NULL);

  /* bail now if we aren't yet drawable */
  if (!GTK_WIDGET_DRAWABLE (sheet)) return;

  if (row < 0 ||
      row >= g_sheet_row_get_row_count (sheet->row_geometry))
    return;

  if (col < 0 ||
      col >= g_sheet_column_get_column_count (sheet->column_geometry))
    return;

  gtk_sheet_get_attributes (sheet, row, col, &attributes);

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
}


static void
gtk_sheet_cell_draw_label (GtkSheet *sheet, gint row, gint col)
{
  GtkWidget *widget;
  GdkRectangle area;
  GtkSheetCellAttr attributes;
  PangoLayout *layout;
  PangoRectangle text;
  gint font_height;

  gchar *label;

  g_return_if_fail (sheet != NULL);

  if (!GTK_WIDGET_DRAWABLE (sheet))
    return;

  label = gtk_sheet_cell_get_text (sheet, row, col);
  if (!label)
    return;

  if (row < 0 || row >= g_sheet_row_get_row_count (sheet->row_geometry))
    return;

  if (col < 0 ||
      col >= g_sheet_column_get_column_count (sheet->column_geometry))
    return;

  widget = GTK_WIDGET (sheet);

  gtk_sheet_get_attributes (sheet, row, col, &attributes);

  /* select GC for background rectangle */
  gdk_gc_set_foreground (sheet->fg_gc, &attributes.foreground);
  gdk_gc_set_foreground (sheet->bg_gc, &attributes.background);

  rectangle_from_cell (sheet, row, col, &area);

  layout = gtk_widget_create_pango_layout (GTK_WIDGET (sheet), label);
  dispose_string (sheet, label);
  pango_layout_set_font_description (layout, attributes.font_desc);


  pango_layout_get_pixel_extents (layout, NULL, &text);

  gdk_gc_set_clip_rectangle (sheet->fg_gc, &area);

  font_height = pango_font_description_get_size (attributes.font_desc);
  if ( !pango_font_description_get_size_is_absolute (attributes.font_desc))
    font_height /= PANGO_SCALE;

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
gtk_sheet_range_draw (GtkSheet *sheet, const GtkSheetRange *range)
{
  gint i, j;

  GdkRectangle area;
  GtkSheetRange drawing_range;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_SHEET (sheet));

  if (!GTK_WIDGET_DRAWABLE (GTK_WIDGET (sheet))) return;
  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;
  if (!GTK_WIDGET_MAPPED (GTK_WIDGET (sheet))) return;

  if (range == NULL)
    {
      drawing_range.row0 = min_visible_row (sheet);
      drawing_range.col0 = min_visible_column (sheet);
      drawing_range.rowi = MIN (max_visible_row (sheet),
				g_sheet_row_get_row_count (sheet->row_geometry) - 1);
      drawing_range.coli = max_visible_column (sheet);
      gdk_drawable_get_size (sheet->sheet_window, &area.width, &area.height);
      area.x = area.y = 0;
    }
  else
    {
      drawing_range.row0 = MAX (range->row0, min_visible_row (sheet));
      drawing_range.col0 = MAX (range->col0, min_visible_column (sheet));
      drawing_range.rowi = MIN (range->rowi, max_visible_row (sheet));
      drawing_range.coli = MIN (range->coli, max_visible_column (sheet));


      rectangle_from_range (sheet, &drawing_range, &area);
    }

  gdk_draw_rectangle (sheet->sheet_window,
		      GTK_WIDGET (sheet)->style->white_gc,
		      TRUE,
		      area.x, area.y,
		      area.width, area.height);

  for (i = drawing_range.row0; i <= drawing_range.rowi; i++)
    for (j = drawing_range.col0; j <= drawing_range.coli; j++)
      {
	gtk_sheet_cell_draw_bg (sheet, i, j);
	gtk_sheet_cell_draw_label (sheet, i, j);
      }

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

  range.col0 = MAX (range.col0, min_visible_column (sheet));
  range.coli = MIN (range.coli, max_visible_column (sheet));
  range.row0 = MAX (range.row0, min_visible_row (sheet));
  range.rowi = MIN (range.rowi, max_visible_row (sheet));

  for (i = range.row0; i <= range.rowi; i++)
    {
      for (j = range.col0; j <= range.coli; j++)
	{
	  if (gtk_sheet_cell_get_state (sheet, i, j) == GTK_STATE_SELECTED)
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

  gtk_sheet_draw_border (sheet, sheet->range);
}

static void gtk_sheet_set_cell (GtkSheet *sheet, gint row, gint col,
				GtkJustification justification,
				const gchar *text);


static inline gint
safe_strcmp (const gchar *s1, const gchar *s2)
{
  if ( !s1 && !s2) return 0;
  if ( !s1) return - 1;
  if ( !s2) return +1;
  return strcmp (s1, s2);
}

static void
gtk_sheet_set_cell (GtkSheet *sheet, gint row, gint col,
		    GtkJustification justification,
		    const gchar *text)
{
  GSheetModel *model ;
  gboolean changed = FALSE;
  gchar *old_text ;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (col >= g_sheet_column_get_column_count (sheet->column_geometry)
      || row >= g_sheet_row_get_row_count (sheet->row_geometry))
    return;

  if (col < 0 || row < 0) return;

  model = gtk_sheet_get_model (sheet);

  old_text = g_sheet_model_get_string (model, row, col);

  if (0 != safe_strcmp (old_text, text))
    changed = g_sheet_model_set_string (model, text, row, col);

  if ( g_sheet_model_free_strings (model))
    g_free (old_text);

  if ( changed )
    g_signal_emit (sheet, sheet_signals[CHANGED], 0, row, col);
}


void
gtk_sheet_cell_clear (GtkSheet *sheet, gint row, gint column)
{
  GtkSheetRange range;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));
  if (column >= g_sheet_column_get_column_count (sheet->column_geometry) ||
      row >= g_sheet_row_get_row_count (sheet->row_geometry)) return;

  if (column < 0 || row < 0) return;

  range.row0 = row;
  range.rowi = row;
  range.col0 = min_visible_column (sheet);
  range.coli = max_visible_column (sheet);

  gtk_sheet_real_cell_clear (sheet, row, column);

  gtk_sheet_range_draw (sheet, &range);
}

static void
gtk_sheet_real_cell_clear (GtkSheet *sheet, gint row, gint column)
{
  GSheetModel *model = gtk_sheet_get_model (sheet);

  gchar *old_text = gtk_sheet_cell_get_text (sheet, row, column);

  if (old_text && strlen (old_text) > 0 )
    {
      g_sheet_model_datum_clear (model, row, column);
    }

  dispose_string (sheet, old_text);
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

  if (col >= g_sheet_column_get_column_count (sheet->column_geometry) || row >= g_sheet_row_get_row_count (sheet->row_geometry))
    return NULL;
  if (col < 0 || row < 0) return NULL;

  model = gtk_sheet_get_model (sheet);

  if ( !model )
    return NULL;

  return g_sheet_model_get_string (model, row, col);
}


static GtkStateType
gtk_sheet_cell_get_state (GtkSheet *sheet, gint row, gint col)
{
  gint state;
  GtkSheetRange *range;

  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), 0);
  if (col >= g_sheet_column_get_column_count (sheet->column_geometry) || row >= g_sheet_row_get_row_count (sheet->row_geometry)) return 0;
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

/* Convert X, Y (in pixels) to *ROW, *COLUMN
   If the function returns FALSE, then the results will be unreliable.
*/
static gboolean
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

  if ( sheet->column_titles_visible)
    y -= sheet->column_title_area.height;

  y += sheet->vadjustment->value;

  if ( y < 0 && sheet->column_titles_visible)
    {
      trow = -1;
    }
  else
    {
      trow = yyy_row_ypixel_to_row (sheet, y);
      if (trow > g_sheet_row_get_row_count (sheet->row_geometry))
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
      if (tcol > g_sheet_column_get_column_count (sheet->column_geometry))
	return FALSE;
    }

  *column = tcol;

  return TRUE;
}

gboolean
gtk_sheet_get_cell_area (GtkSheet *sheet,
			 gint row,
			 gint column,
			 GdkRectangle *area)
{
  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), 0);

  if (row >= g_sheet_row_get_row_count (sheet->row_geometry) || column >= g_sheet_column_get_column_count (sheet->column_geometry))
    return FALSE;

  area->x = (column == -1) ? 0 : g_sheet_column_start_pixel (sheet->column_geometry, column);
  area->y = (row == -1)    ? 0 : g_sheet_row_start_pixel (sheet->row_geometry, row);

  area->width= (column == -1) ? sheet->row_title_area.width
    : g_sheet_column_get_width (sheet->column_geometry, column);

  area->height= (row == -1) ? sheet->column_title_area.height
    : g_sheet_row_get_height (sheet->row_geometry, row);

  return TRUE;
}

gboolean
gtk_sheet_set_active_cell (GtkSheet *sheet, gint row, gint col)
{
  g_return_val_if_fail (sheet != NULL, 0);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), 0);

  if (row < -1 || col < -1) return FALSE;
  if (row >= g_sheet_row_get_row_count (sheet->row_geometry)
      ||
      col >= g_sheet_column_get_column_count (sheet->column_geometry))
    return FALSE;

  sheet->active_cell.row = row;
  sheet->active_cell.col = col;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    return TRUE;

  gtk_sheet_deactivate_cell (sheet);

  if ( row == -1 || col == -1)
    {
      gtk_sheet_hide_active_cell (sheet);
      return TRUE;
    }

  return gtk_sheet_activate_cell (sheet, row, col);
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

  sheet->active_cell.row = -1;
  sheet->active_cell.col = -1;

  text = gtk_entry_get_text (GTK_ENTRY (gtk_sheet_get_entry (sheet)));

  if (text && strlen (text) > 0)
    {
      gtk_sheet_get_attributes (sheet, row, col, &attributes);
      justification = attributes.justification;
      gtk_sheet_set_cell (sheet, row, col, justification, text);
    }

  sheet->active_cell.row = row;;
  sheet->active_cell.col = col;
}


static void
gtk_sheet_deactivate_cell (GtkSheet *sheet)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return ;
  if (sheet->state != GTK_SHEET_NORMAL) return ;


  if ( sheet->active_cell.row == -1 || sheet->active_cell.col == -1 )
    return ;

  /*
  g_print ("%s\n", __FUNCTION__);


  GtkSheetRange r;
  r.col0 = r.coli = sheet->active_cell.col;
  r.row0 = r.rowi = sheet->active_cell.row;
  gtk_sheet_range_draw (sheet, &r);
  */


  g_signal_emit (sheet, sheet_signals[DEACTIVATE], 0,
		 sheet->active_cell.row,
		 sheet->active_cell.col);


  g_signal_handlers_disconnect_by_func (gtk_sheet_get_entry (sheet),
					G_CALLBACK (gtk_sheet_entry_changed),
					sheet);

  gtk_sheet_hide_active_cell (sheet);
  sheet->active_cell.row = -1;
  sheet->active_cell.col = -1;

#if 0
  if (GTK_SHEET_REDRAW_PENDING (sheet))
    {
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_REDRAW_PENDING);
      gtk_sheet_range_draw (sheet, NULL);
    }
#endif
}



static void
gtk_sheet_hide_active_cell (GtkSheet *sheet)
{
  GdkRectangle area;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    return;

  if (sheet->active_cell.row < 0 ||
      sheet->active_cell.col < 0) return;

  gtk_widget_hide (sheet->entry_widget);
  gtk_widget_unmap (sheet->entry_widget);

  rectangle_from_cell (sheet,
		       sheet->active_cell.row, sheet->active_cell.col,
		       &area);

  gdk_draw_rectangle (sheet->sheet_window,
		      GTK_WIDGET (sheet)->style->white_gc,
		      TRUE,
		      area.x, area.y,
		      area.width, area.height);

  gtk_sheet_cell_draw_bg (sheet, sheet->active_cell.row,
			       sheet->active_cell.col);

  gtk_sheet_cell_draw_label (sheet, sheet->active_cell.row,
			     sheet->active_cell.col);

  GTK_WIDGET_UNSET_FLAGS (GTK_WIDGET (sheet->entry_widget), GTK_VISIBLE);
}

static gboolean
gtk_sheet_activate_cell (GtkSheet *sheet, gint row, gint col)
{
  gboolean veto = TRUE;

  g_return_val_if_fail (sheet != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), FALSE);

  if (row < 0 || col < 0) return FALSE;

  if ( row > g_sheet_row_get_row_count (sheet->row_geometry)
       || col > g_sheet_column_get_column_count (sheet->column_geometry))
    return FALSE;

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

  g_signal_connect (gtk_sheet_get_entry (sheet),
		    "changed",
		    G_CALLBACK (gtk_sheet_entry_changed),
		    sheet);

   g_signal_emit (sheet, sheet_signals [ACTIVATE], 0, row, col, &veto);

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

  GTK_WIDGET_SET_FLAGS (GTK_WIDGET (sheet->entry_widget), GTK_VISIBLE);

  sheet_entry = GTK_ENTRY (gtk_sheet_get_entry (sheet));

  gtk_sheet_get_attributes (sheet, row, col, &attributes);

  justification = GTK_JUSTIFY_LEFT;


  text = gtk_sheet_cell_get_text (sheet, row, col);
  if ( ! text )
    text = g_strdup ("");

  gtk_entry_set_visibility (GTK_ENTRY (sheet_entry), attributes.is_visible);


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

  gtk_widget_map (sheet->entry_widget);

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

  if (!gtk_sheet_cell_isvisible (sheet, row, col))
    return;

  range.col0 = range.coli = col;
  range.row0 = range.rowi = row;

  gtk_sheet_draw_border (sheet, range);
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

	  state = gtk_sheet_cell_get_state (sheet, i, j);
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
		  x = g_sheet_column_start_pixel (sheet->column_geometry, j);
		  y = g_sheet_row_start_pixel (sheet->row_geometry, i);
		  width = g_sheet_column_start_pixel (sheet->column_geometry, j)- x+
		    g_sheet_column_get_width (sheet->column_geometry, j);
		  height = g_sheet_row_start_pixel (sheet->row_geometry, i) - y + g_sheet_row_get_height (sheet->row_geometry, i);

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
		      x = g_sheet_column_start_pixel (sheet->column_geometry, j);
		      y = g_sheet_row_start_pixel (sheet->row_geometry, i);
		      width = g_sheet_column_start_pixel (sheet->column_geometry, j)- x+
			g_sheet_column_get_width (sheet->column_geometry, j);

		      height = g_sheet_row_start_pixel (sheet->row_geometry, i) - y + g_sheet_row_get_height (sheet->row_geometry, i);

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

	  if (state == GTK_STATE_SELECTED && !selected)
	    {

	      x = g_sheet_column_start_pixel (sheet->column_geometry, j);
	      y = g_sheet_row_start_pixel (sheet->row_geometry, i);
	      width = g_sheet_column_start_pixel (sheet->column_geometry, j) - x + g_sheet_column_get_width (sheet->column_geometry, j);
	      height = g_sheet_row_start_pixel (sheet->row_geometry, i) - y + g_sheet_row_get_height (sheet->row_geometry, i);

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

	  state = gtk_sheet_cell_get_state (sheet, i, j);
	  selected= (i <= new_range.rowi && i >= new_range.row0 &&
		     j <= new_range.coli && j >= new_range.col0) ? TRUE : FALSE;

	  if (state != GTK_STATE_SELECTED && selected &&
	      (i != sheet->active_cell.row || j != sheet->active_cell.col))
	    {

	      x = g_sheet_column_start_pixel (sheet->column_geometry, j);
	      y = g_sheet_row_start_pixel (sheet->row_geometry, i);
	      width = g_sheet_column_start_pixel (sheet->column_geometry, j) - x + g_sheet_column_get_width (sheet->column_geometry, j);
	      height = g_sheet_row_start_pixel (sheet->row_geometry, i) - y + g_sheet_row_get_height (sheet->row_geometry, i);

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
	      x = g_sheet_column_start_pixel (sheet->column_geometry, j);
	      y = g_sheet_row_start_pixel (sheet->row_geometry, i);
	      width = g_sheet_column_get_width (sheet->column_geometry, j);
	      height = g_sheet_row_get_height (sheet->row_geometry, i);
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
gtk_sheet_draw_border (GtkSheet *sheet, GtkSheetRange new_range)
{
  GdkRectangle area;

  rectangle_from_range (sheet, &new_range, &area);

  gdk_draw_rectangle (sheet->sheet_window,
		      sheet->xor_gc,
		      FALSE,
		      area.x + 1,
		      area.y + 1,
		      area.width - 2,
		      area.height - 2);
}


static void
gtk_sheet_real_select_range (GtkSheet *sheet,
			     const GtkSheetRange *range)
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
      gtk_sheet_new_selection (sheet, &sheet->range);
    }
  else
    {
      gtk_sheet_range_draw_selection (sheet, sheet->range);
    }
#endif

  gtk_sheet_update_primary_selection (sheet);

  g_signal_emit (sheet, sheet_signals[SELECT_RANGE], 0, &sheet->range);
}


void
gtk_sheet_get_selected_range (GtkSheet *sheet, GtkSheetRange *range)
{
  g_return_if_fail (sheet != NULL);
  *range = sheet->range;
}


void
gtk_sheet_select_range (GtkSheet *sheet, const GtkSheetRange *range)
{
  g_return_if_fail (sheet != NULL);

  if (range == NULL) range=&sheet->range;

  if (range->row0 < 0 || range->rowi < 0) return;
  if (range->col0 < 0 || range->coli < 0) return;


  if (sheet->state != GTK_SHEET_NORMAL)
    gtk_sheet_real_unselect_range (sheet, NULL);
  else
    gtk_sheet_deactivate_cell (sheet);

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
gtk_sheet_unselect_range (GtkSheet *sheet)
{
  if (! GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    return;

  gtk_sheet_real_unselect_range (sheet, NULL);
  sheet->state = GTK_STATE_NORMAL;

  gtk_sheet_activate_cell (sheet,
			   sheet->active_cell.row, sheet->active_cell.col);
}


static void
gtk_sheet_real_unselect_range (GtkSheet *sheet,
			       const GtkSheetRange *range)
{
  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)));

  if ( range == NULL)
    range = &sheet->range;

  if (range->row0 < 0 || range->rowi < 0) return;
  if (range->col0 < 0 || range->coli < 0) return;

  g_signal_emit (sheet, sheet_signals[SELECT_COLUMN], 0, -1);
  g_signal_emit (sheet, sheet_signals[SELECT_ROW], 0, -1);

#if 0
  if (gtk_sheet_range_isvisible (sheet, *range))
    gtk_sheet_draw_backing_pixmap (sheet, *range);
#endif

  sheet->range.row0 = -1;
  sheet->range.rowi = -1;
  sheet->range.col0 = -1;
  sheet->range.coli = -1;
}


static gint
gtk_sheet_expose (GtkWidget *widget,
		  GdkEventExpose *event)
{
  GtkSheet *sheet;
  GtkSheetRange range;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  sheet = GTK_SHEET (widget);

  if (!GTK_WIDGET_DRAWABLE (widget))
    return FALSE;

  /* exposure events on the sheet */
  if (event->window == sheet->row_title_window &&
      sheet->row_titles_visible)
    {
      gint i;
      for (i = min_visible_row (sheet); i <= max_visible_row (sheet); i++)
	gtk_sheet_row_title_button_draw (sheet, i);
    }

  if (event->window == sheet->column_title_window &&
      sheet->column_titles_visible)
    {
      gint i;
      for (i = min_visible_column (sheet);
	   i <= max_visible_column (sheet);
	   ++i)
	gtk_sheet_column_title_button_draw (sheet, i);
    }


  range.row0 =
    yyy_row_ypixel_to_row (sheet,
			   event->area.y + sheet->vadjustment->value);
  range.row0--;

  range.rowi =
    yyy_row_ypixel_to_row (sheet,
			   event->area.y +
			   event->area.height + sheet->vadjustment->value);
  range.rowi++;

  range.col0 =
    column_from_xpixel (sheet,
			event->area.x + sheet->hadjustment->value);
  range.col0--;

  range.coli =
    column_from_xpixel (sheet,
			event->area.x + event->area.width +
			sheet->hadjustment->value);
  range.coli++;

  if (event->window == sheet->sheet_window)
    {
      gtk_sheet_range_draw (sheet, &range);

      if (sheet->state != GTK_SHEET_NORMAL)
	{
	  if (gtk_sheet_range_isvisible (sheet, sheet->range))
	    gtk_sheet_range_draw (sheet, &sheet->range);

	  if (GTK_SHEET_IN_RESIZE (sheet) || GTK_SHEET_IN_DRAG (sheet))
	    gtk_sheet_range_draw (sheet, &sheet->drag_range);

	  if (gtk_sheet_range_isvisible (sheet, sheet->range))
	    gtk_sheet_range_draw_selection (sheet, sheet->range);
	  if (GTK_SHEET_IN_RESIZE (sheet) || GTK_SHEET_IN_DRAG (sheet))
	    draw_xor_rectangle (sheet, sheet->drag_range);
	}

      if ((!GTK_SHEET_IN_XDRAG (sheet)) && (!GTK_SHEET_IN_YDRAG (sheet)))
	{
	  if (sheet->state == GTK_SHEET_NORMAL)
	    gtk_sheet_draw_active_cell (sheet);
	}
    }

  if (sheet->state != GTK_SHEET_NORMAL && GTK_SHEET_IN_SELECTION (sheet))
    gtk_widget_grab_focus (GTK_WIDGET (sheet));

  (* GTK_WIDGET_CLASS (parent_class)->expose_event) (widget, event);

  return FALSE;
}


static gboolean
gtk_sheet_button_press (GtkWidget *widget,
			GdkEventButton *event)
{
  GtkSheet *sheet;
  GdkModifierType mods;
  gint x, y;
  gint  row, column;
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
      g_signal_emit (sheet,
		     sheet_signals[BUTTON_EVENT_COLUMN], 0,
		     column, event);

      if ( event->type == GDK_2BUTTON_PRESS && event->button == 1)
	g_signal_emit (sheet,
		       sheet_signals[DOUBLE_CLICK_COLUMN], 0, column);

    }
  else if (event->window == sheet->row_title_window)
    {
      g_signal_emit (sheet,
		     sheet_signals[BUTTON_EVENT_ROW], 0,
		     row, event);

      if ( event->type == GDK_2BUTTON_PRESS && event->button == 1)
	g_signal_emit (sheet,
		       sheet_signals[DOUBLE_CLICK_ROW], 0, row);
    }


  gdk_window_get_pointer (widget->window, NULL, NULL, &mods);

  if (! (mods & GDK_BUTTON1_MASK)) return TRUE;


  /* press on resize windows */
  if (event->window == sheet->column_title_window &&
      gtk_sheet_columns_resizable (sheet))
    {
#if 0
      gtk_widget_get_pointer (widget, &sheet->x_drag, NULL);
      if ( sheet->row_titles_visible)
	sheet->x_drag -= sheet->row_title_area.width;
#endif

      sheet->x_drag = event->x;

      if (on_column_boundary (sheet, sheet->x_drag, &sheet->drag_cell.col))
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
	      gtk_sheet_deactivate_cell (sheet);
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
	       && sheet->active_cell.row >= 0
	       && sheet->active_cell.col >= 0
	       )
	{
	  if (sheet->state == GTK_STATE_NORMAL)
	    {
	      row = sheet->active_cell.row;
	      column = sheet->active_cell.col;
	      gtk_sheet_deactivate_cell (sheet);
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
	  veto = gtk_sheet_click_cell (sheet, row, column);
	  if (veto) GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);
	}
    }

  if (event->window == sheet->column_title_window)
    {
      gtk_widget_get_pointer (widget, &x, &y);
      if ( sheet->row_titles_visible)
	x -= sheet->row_title_area.width;

      x += sheet->hadjustment->value;

      column = column_from_xpixel (sheet, x);

      if (g_sheet_column_get_sensitivity (sheet->column_geometry, column))
	{
	  veto = gtk_sheet_click_cell (sheet, -1, column);
	  gtk_grab_add (GTK_WIDGET (sheet));
	  gtk_widget_grab_focus (GTK_WIDGET (sheet));
	  GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);
	}
    }

  if (event->window == sheet->row_title_window)
    {
      gtk_widget_get_pointer (widget, &x, &y);
      if ( sheet->column_titles_visible)
	y -= sheet->column_title_area.height;

      y += sheet->vadjustment->value;

      row = yyy_row_ypixel_to_row (sheet, y);
      if (g_sheet_row_get_sensitivity (sheet->row_geometry, row))
	{
	  veto = gtk_sheet_click_cell (sheet, row, -1);
	  gtk_grab_add (GTK_WIDGET (sheet));
	  gtk_widget_grab_focus (GTK_WIDGET (sheet));
	  GTK_SHEET_SET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);
	}
    }

  return TRUE;
}

static gboolean
gtk_sheet_click_cell (GtkSheet *sheet, gint row, gint column)
{
  gboolean forbid_move;

  if (row >= g_sheet_row_get_row_count (sheet->row_geometry)
      || column >= g_sheet_column_get_column_count (sheet->column_geometry))
    {
      return FALSE;
    }

  g_signal_emit (sheet, sheet_signals[TRAVERSE], 0,
		 sheet->active_cell.row, sheet->active_cell.col,
		 &row, &column, &forbid_move);

  if (forbid_move)
    {
      if (sheet->state == GTK_STATE_NORMAL)
	return FALSE;

      row = sheet->active_cell.row;
      column = sheet->active_cell.col;

      gtk_sheet_activate_cell (sheet, row, column);
      return FALSE;
    }

  if (row == -1 && column >= 0)
    {
      gtk_sheet_select_column (sheet, column);
      return TRUE;
    }

  if (column == -1 && row >= 0)
    {
      gtk_sheet_select_row (sheet, row);
      return TRUE;
    }

  if (row == -1 && column == -1)
    {
      sheet->range.row0 = 0;
      sheet->range.col0 = 0;
      sheet->range.rowi = g_sheet_row_get_row_count (sheet->row_geometry) - 1;
      sheet->range.coli =
	g_sheet_column_get_column_count (sheet->column_geometry) - 1;
      sheet->active_cell.row = 0;
      sheet->active_cell.col = 0;
      gtk_sheet_select_range (sheet, NULL);
      return TRUE;
    }

  if (sheet->state != GTK_SHEET_NORMAL)
    {
      sheet->state = GTK_SHEET_NORMAL;
      gtk_sheet_real_unselect_range (sheet, NULL);
    }
  else
    {
      gtk_sheet_deactivate_cell (sheet);
      gtk_sheet_activate_cell (sheet, row, column);
    }

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
  return TRUE;
}

static gint
gtk_sheet_button_release (GtkWidget *widget,
			  GdkEventButton *event)
{
  gint y;
  GdkDisplay *display = gtk_widget_get_display (widget);

  GtkSheet *sheet = GTK_SHEET (widget);

  /* release on resize windows */
  if (GTK_SHEET_IN_XDRAG (sheet))
    {
      gint xpos = event->x;
      gint width;
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_XDRAG);
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);

      gdk_display_pointer_ungrab (display, event->time);
      draw_xor_vline (sheet);

      width = new_column_width (sheet, sheet->drag_cell.col, &xpos);

      gtk_sheet_set_column_width (sheet, sheet->drag_cell.col, width);
      return TRUE;
    }

  if (GTK_SHEET_IN_YDRAG (sheet))
    {
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_YDRAG);
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);
      gtk_widget_get_pointer (widget, NULL, &y);
      gdk_display_pointer_ungrab (display, event->time);
      draw_xor_hline (sheet);

      gtk_sheet_set_row_height (sheet, sheet->drag_cell.row,
				new_row_height (sheet, sheet->drag_cell.row, &y));
      g_signal_emit_by_name (sheet->vadjustment, "value_changed");
      return TRUE;
    }


  if (GTK_SHEET_IN_DRAG (sheet))
    {
      GtkSheetRange old_range;
      draw_xor_rectangle (sheet, sheet->drag_range);
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_DRAG);
      gdk_display_pointer_ungrab (display, event->time);

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
      g_signal_emit (sheet, sheet_signals[MOVE_RANGE], 0,
		     &sheet->drag_range, &sheet->range);
      gtk_sheet_select_range (sheet, &sheet->range);
    }

  if (GTK_SHEET_IN_RESIZE (sheet))
    {
      GtkSheetRange old_range;
      draw_xor_rectangle (sheet, sheet->drag_range);
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_RESIZE);
      gdk_display_pointer_ungrab (display, event->time);

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
      g_signal_emit (sheet, sheet_signals[RESIZE_RANGE], 0,
		     &sheet->drag_range, &sheet->range);
      gtk_sheet_select_range (sheet, &sheet->range);
    }

  if (sheet->state == GTK_SHEET_NORMAL && GTK_SHEET_IN_SELECTION (sheet))
    {
      GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);
      gdk_display_pointer_ungrab (display, event->time);
      gtk_sheet_activate_cell (sheet, sheet->active_cell.row,
			       sheet->active_cell.col);
    }

  if (GTK_SHEET_IN_SELECTION)
    gdk_display_pointer_ungrab (display, event->time);
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

static void
destroy_hover_window (GtkSheetHoverTitle *h)
{
  gtk_widget_destroy (h->window);
  g_free (h);
}

static GtkSheetHoverTitle *
create_hover_window (void)
{
  GtkSheetHoverTitle *hw = g_malloc (sizeof (*hw));

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
      if (sheet->row_title_under)
	{
	  GSheetRow *row_geo = sheet->row_geometry;
	  gchar *text;

	  text = g_sheet_row_get_subtitle (row_geo, row);

	  show_subtitle (sheet, row, -1, text);
	  g_free (text);
	}

      if (sheet->column_title_under)
	{
	  GSheetColumn *col_geo = sheet->column_geometry;
	  gchar *text;

	  text = g_sheet_column_get_subtitle (col_geo, column);

	  show_subtitle (sheet, -1, column, text);

	  g_free (text);
	}
    }

  return FALSE;
}

static gboolean
gtk_sheet_motion (GtkWidget *widget,  GdkEventMotion *event)
{
  GtkSheet *sheet;
  GdkModifierType mods;
  GdkCursorType new_cursor;
  gint x, y;
  gint row, column;
  GdkDisplay *display;

  g_return_val_if_fail (widget != NULL, FALSE);
  g_return_val_if_fail (GTK_IS_SHEET (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  sheet = GTK_SHEET (widget);

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

      if ( gtk_sheet_get_pixel_info (sheet, wx, wy, &row, &column) )
	{
	  if ( row != sheet->hover_window->row ||
	       column != sheet->hover_window->column)
	    {
	      gtk_widget_hide (sheet->hover_window->window);
	    }
	}
    }

  if (event->window == sheet->column_title_window &&
      gtk_sheet_columns_resizable (sheet))
    {
      if (!GTK_SHEET_IN_SELECTION (sheet) &&
	  on_column_boundary (sheet, x, &column))
	{
	  new_cursor = GDK_SB_H_DOUBLE_ARROW;
	  if (new_cursor != sheet->cursor_drag->type)
	    {
	      gdk_cursor_unref (sheet->cursor_drag);
	      sheet->cursor_drag = gdk_cursor_new_for_display (display, GDK_SB_H_DOUBLE_ARROW);
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
	      gdk_cursor_unref (sheet->cursor_drag);
	      sheet->cursor_drag = gdk_cursor_new_for_display (display, GDK_TOP_LEFT_ARROW);
	      gdk_window_set_cursor (sheet->column_title_window,
				     sheet->cursor_drag);
	    }
	}
    }

  if (event->window == sheet->row_title_window &&
      gtk_sheet_rows_resizable (sheet))
    {
      if (!GTK_SHEET_IN_SELECTION (sheet) && POSSIBLE_YDRAG (sheet, y, &column))
	{
	  new_cursor = GDK_SB_V_DOUBLE_ARROW;
	  if (new_cursor != sheet->cursor_drag->type)
	    {
	      gdk_cursor_unref (sheet->cursor_drag);
	      sheet->cursor_drag = gdk_cursor_new_for_display (display, GDK_SB_V_DOUBLE_ARROW);
	      gdk_window_set_cursor (sheet->row_title_window, sheet->cursor_drag);
	    }
	}
      else
	{
	  new_cursor = GDK_TOP_LEFT_ARROW;
	  if (!GTK_SHEET_IN_YDRAG (sheet) &&
	      new_cursor != sheet->cursor_drag->type)
	    {
	      gdk_cursor_unref (sheet->cursor_drag);
	      sheet->cursor_drag = gdk_cursor_new_for_display (display, GDK_TOP_LEFT_ARROW);
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
      gdk_cursor_unref (sheet->cursor_drag);
      sheet->cursor_drag = gdk_cursor_new_for_display (display, GDK_PLUS);
      gdk_window_set_cursor (sheet->sheet_window, sheet->cursor_drag);
    }

  new_cursor = GDK_TOP_LEFT_ARROW;
  if ( event->window == sheet->sheet_window &&
       ! (POSSIBLE_RESIZE (sheet, x, y, &row, &column) ||
	  GTK_SHEET_IN_RESIZE (sheet)) &&
       (POSSIBLE_DRAG (sheet, x, y, &row, &column) ||
	GTK_SHEET_IN_DRAG (sheet)) &&
       new_cursor != sheet->cursor_drag->type)
    {
      gdk_cursor_unref (sheet->cursor_drag);
      sheet->cursor_drag = gdk_cursor_new_for_display (display, GDK_TOP_LEFT_ARROW);
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
      gdk_cursor_unref (sheet->cursor_drag);
      sheet->cursor_drag = gdk_cursor_new_for_display (display, GDK_SIZING);
      gdk_window_set_cursor (sheet->sheet_window, sheet->cursor_drag);
    }


  gdk_window_get_pointer (widget->window, &x, &y, &mods);
  if (! (mods & GDK_BUTTON1_MASK)) return FALSE;

  if (GTK_SHEET_IN_XDRAG (sheet))
    {
      x = event->x;

      new_column_width (sheet, sheet->drag_cell.col, &x);
#if 0
      if (x != sheet->x_drag)
	{
	  draw_xor_vline (sheet);
	  sheet->x_drag = x;
	  draw_xor_vline (sheet);
	}
#endif
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
      column = column_from_xpixel (sheet, x)- sheet->drag_cell.col;
      row = yyy_row_ypixel_to_row (sheet, y) - sheet->drag_cell.row;
      if (sheet->state == GTK_SHEET_COLUMN_SELECTED) row = 0;
      if (sheet->state == GTK_SHEET_ROW_SELECTED) column = 0;
      sheet->x_drag = x;
      sheet->y_drag = y;
      aux = sheet->range;
      if (aux.row0 + row >= 0 && aux.rowi + row < g_sheet_row_get_row_count (sheet->row_geometry) &&
	  aux.col0 + column >= 0 && aux.coli + column < g_sheet_column_get_column_count (sheet->column_geometry))
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
      if (abs (x - g_sheet_column_start_pixel (sheet->column_geometry, sheet->drag_cell.col)) >
	  abs (y - g_sheet_row_start_pixel (sheet->row_geometry, sheet->drag_cell.row))) v_h = 2;

      current_col = column_from_xpixel (sheet, x);
      current_row = yyy_row_ypixel_to_row (sheet, y);
      column = current_col - sheet->drag_cell.col;
      row = current_row - sheet->drag_cell.row;

      /*use half of column width resp. row height as threshold to
	expand selection*/
      col_threshold = g_sheet_column_start_pixel (sheet->column_geometry, current_col) +
	g_sheet_column_get_width (sheet->column_geometry, current_col) / 2;
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
      row_threshold = g_sheet_row_start_pixel (sheet->row_geometry, current_row) +
	g_sheet_row_get_height (sheet->row_geometry, current_row)/2;
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

      if (aux.row0 + row >= 0 && aux.rowi + row < g_sheet_row_get_row_count (sheet->row_geometry) &&
	  aux.col0 + column >= 0 && aux.coli + column < g_sheet_column_get_column_count (sheet->column_geometry))
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
gtk_sheet_crossing_notify (GtkWidget *widget,
			   GdkEventCrossing *event)
{
  GtkSheet *sheet = GTK_SHEET (widget);

  if (event->window == sheet->column_title_window)
    sheet->column_title_under = event->type == GDK_ENTER_NOTIFY;
  else if (event->window == sheet->row_title_window)
    sheet->row_title_under = event->type == GDK_ENTER_NOTIFY;

  return TRUE;
}


static gboolean
gtk_sheet_move_query (GtkSheet *sheet, gint row, gint column)
{
  gint height, width;
  gint new_row = row;
  gint new_col = column;

  gint row_move = FALSE;
  gint column_move = FALSE;
  gfloat row_align = -1.0;
  gfloat col_align = -1.0;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    return FALSE;

  gdk_drawable_get_size (sheet->sheet_window, &width, &height);

  if (row >= max_visible_row (sheet) &&
      sheet->state != GTK_SHEET_COLUMN_SELECTED)
    {
      row_align = 1.;
      new_row = MIN (g_sheet_row_get_row_count (sheet->row_geometry) - 1, row + 1);
      row_move = TRUE;
      if (max_visible_row (sheet) == g_sheet_row_get_row_count (sheet->row_geometry) - 1 &&
	  g_sheet_row_start_pixel (sheet->row_geometry, g_sheet_row_get_row_count (sheet->row_geometry) - 1) +
	  g_sheet_row_get_height (sheet->row_geometry, g_sheet_row_get_row_count (sheet->row_geometry) - 1) < height)
	{
	  row_move = FALSE;
	  row_align = -1.;
	}
    }

  if (row < min_visible_row (sheet) && sheet->state != GTK_SHEET_COLUMN_SELECTED)
    {
      row_align= 0.;
      row_move = TRUE;
    }
  if (column >= max_visible_column (sheet) && sheet->state != GTK_SHEET_ROW_SELECTED)
    {
      col_align = 1.;
      new_col = MIN (g_sheet_column_get_column_count (sheet->column_geometry) - 1, column + 1);
      column_move = TRUE;
      if (max_visible_column (sheet) == (g_sheet_column_get_column_count (sheet->column_geometry) - 1) &&
	  g_sheet_column_start_pixel (sheet->column_geometry, g_sheet_column_get_column_count (sheet->column_geometry) - 1) +
	  g_sheet_column_get_width (sheet->column_geometry, g_sheet_column_get_column_count (sheet->column_geometry) - 1) < width)
	{
	  column_move = FALSE;
	  col_align = -1.;
	}
    }
  if (column < min_visible_column (sheet) && sheet->state != GTK_SHEET_ROW_SELECTED)
    {
      col_align = 0.0;
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
      column = g_sheet_column_get_column_count (sheet->column_geometry) - 1;
      break;
    case GTK_SHEET_COLUMN_SELECTED:
      row = g_sheet_row_get_row_count (sheet->row_geometry) - 1;
      break;
    case GTK_SHEET_NORMAL:
      sheet->state = GTK_SHEET_RANGE_SELECTED;
      r = sheet->active_cell.row;
      c = sheet->active_cell.col;
      sheet->range.col0 = c;
      sheet->range.row0 = r;
      sheet->range.coli = c;
      sheet->range.rowi = r;
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
  g_signal_emit_by_name (widget, "key_press_event", key, &focus);
  return focus;
}


/* Number of rows in a step-increment */
#define ROWS_PER_STEP 1


static void
page_vertical (GtkSheet *sheet, GtkScrollType dir)
{
  gint old_row = sheet->active_cell.row ;
  glong vpixel = g_sheet_row_start_pixel (sheet->row_geometry, old_row);

  gint new_row;

  vpixel -= g_sheet_row_start_pixel (sheet->row_geometry,
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


  vpixel += g_sheet_row_start_pixel (sheet->row_geometry,
				     min_visible_row (sheet));

  new_row =  yyy_row_ypixel_to_row (sheet, vpixel);

  gtk_sheet_activate_cell (sheet, new_row,
			   sheet->active_cell.col);
}


static void
step_horizontal (GtkSheet *sheet, GtkScrollType dir)
{
  switch ( dir)
    {
    case GTK_SCROLL_STEP_RIGHT:

      gtk_sheet_activate_cell (sheet,
			       sheet->active_cell.row,
			       sheet->active_cell.col + 1);
      break;
    case GTK_SCROLL_STEP_LEFT:

      gtk_sheet_activate_cell (sheet,
			       sheet->active_cell.row,
			       sheet->active_cell.col - 1);
      break;

    default:
      g_assert_not_reached ();
      break;
    }

  if ( sheet->active_cell.col >= max_visible_column (sheet))
    {
      glong hpos  =
	g_sheet_column_start_pixel (sheet->column_geometry,
				    sheet->active_cell.col + 1);
      hpos -= sheet->hadjustment->page_size;

      gtk_adjustment_set_value (sheet->hadjustment,
				hpos);
    }
  else if ( sheet->active_cell.col <= min_visible_column (sheet))
    {
      glong hpos  =
	g_sheet_column_start_pixel (sheet->column_geometry,
				    sheet->active_cell.col);

      gtk_adjustment_set_value (sheet->hadjustment,
				hpos);
    }
}

static gboolean
gtk_sheet_key_press (GtkWidget *widget,
		     GdkEventKey *key)
{
  GtkSheet *sheet = GTK_SHEET (widget);

  GTK_SHEET_UNSET_FLAGS (sheet, GTK_SHEET_IN_SELECTION);

  switch (key->keyval)
    {
    case GDK_Tab:
    case GDK_Right:
      step_horizontal (sheet, GTK_SCROLL_STEP_RIGHT);
      break;
    case GDK_ISO_Left_Tab:
    case GDK_Left:
      step_horizontal (sheet, GTK_SCROLL_STEP_LEFT);
      break;

    case GDK_Return:
    case GDK_Down:
      gtk_sheet_activate_cell (sheet,
			       sheet->active_cell.row + ROWS_PER_STEP,
			       sheet->active_cell.col);

      if ( sheet->active_cell.row >= max_visible_row (sheet))
	gtk_adjustment_set_value (sheet->vadjustment,
				  sheet->vadjustment->value +
				  sheet->vadjustment->step_increment);
      break;
    case GDK_Up:
      gtk_sheet_activate_cell (sheet,
			       sheet->active_cell.row - ROWS_PER_STEP,
			       sheet->active_cell.col);

      if ( sheet->active_cell.row < min_visible_row (sheet))
	gtk_adjustment_set_value (sheet->vadjustment,
				  sheet->vadjustment->value -
				  sheet->vadjustment->step_increment);
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

      gtk_sheet_activate_cell (sheet,  0,
			       sheet->active_cell.col);

      break;

    case GDK_End:
      gtk_adjustment_set_value (sheet->vadjustment,
				sheet->vadjustment->upper -
				sheet->vadjustment->page_size -
				sheet->vadjustment->page_increment);

      /*
	gtk_sheet_activate_cell (sheet,
	g_sheet_row_get_row_count (sheet->row_geometry) - 1,
	sheet->active_cell.col);
      */

      break;
    default:
      return FALSE;
      break;
    }

  return TRUE;
}

static void
gtk_sheet_size_request (GtkWidget *widget,
			GtkRequisition *requisition)
{
  GtkSheet *sheet;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (GTK_IS_SHEET (widget));
  g_return_if_fail (requisition != NULL);

  sheet = GTK_SHEET (widget);

  requisition->width = 3 * DEFAULT_COLUMN_WIDTH;
  requisition->height = 3 * default_row_height (sheet);

  /* compute the size of the column title area */
  if (sheet->column_titles_visible)
    requisition->height += sheet->column_title_area.height;

  /* compute the size of the row title area */
  if (sheet->row_titles_visible)
    requisition->width += sheet->row_title_area.width;
}


static void
gtk_sheet_size_allocate (GtkWidget *widget,
			 GtkAllocation *allocation)
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
    {
      sheet->column_title_area.x = sheet->row_title_area.width;
    }

  sheet->column_title_area.width = sheet_allocation.width ;


  if (GTK_WIDGET_REALIZED (widget) && sheet->column_titles_visible)
    gdk_window_move_resize (sheet->column_title_window,
			    sheet->column_title_area.x,
			    sheet->column_title_area.y,
			    sheet->column_title_area.width,
			    sheet->column_title_area.height);


  /* column button allocation */
  size_allocate_column_title_buttons (sheet);

  /* position the window which holds the row title buttons */
  sheet->row_title_area.x = 0;
  sheet->row_title_area.y = 0;
  if (sheet->column_titles_visible)
    {
      sheet->row_title_area.y = sheet->column_title_area.height;
    }

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

  /* set the scrollbars adjustments */
  adjust_scrollbars (sheet);
}

static void
size_allocate_column_title_buttons (GtkSheet *sheet)
{
  gint i;
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
      g_sheet_column_get_column_count (sheet->column_geometry) - 1)
    gdk_window_clear_area (sheet->column_title_window,
			   0, 0,
			   sheet->column_title_area.width,
			   sheet->column_title_area.height);

  if (!GTK_WIDGET_DRAWABLE (sheet)) return;

  size_allocate_global_button (sheet);

  for (i = min_visible_column (sheet); i <= max_visible_column (sheet); i++)
    gtk_sheet_column_title_button_draw (sheet, i);
}

static void
size_allocate_row_title_buttons (GtkSheet *sheet)
{
  gint i;
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

  if (max_visible_row (sheet) == g_sheet_row_get_row_count (sheet->row_geometry) - 1)
    gdk_window_clear_area (sheet->row_title_window,
			   0, 0,
			   sheet->row_title_area.width,
			   sheet->row_title_area.height);

  if (!GTK_WIDGET_DRAWABLE (sheet)) return;

  size_allocate_global_button (sheet);

  for (i = min_visible_row (sheet); i <= max_visible_row (sheet); i++)
    {
      if ( i >= g_sheet_row_get_row_count (sheet->row_geometry))
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
  gint size, max_size, text_size;
  const gchar *text;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;
  if (!GTK_WIDGET_MAPPED (GTK_WIDGET (sheet))) return;

  sheet_entry = GTK_ENTRY (gtk_sheet_get_entry (sheet));

  if ( ! gtk_sheet_get_attributes (sheet, sheet->active_cell.row,
				   sheet->active_cell.col,
				   &attributes) )
    return ;

  if ( GTK_WIDGET_REALIZED (sheet->entry_widget) )
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
      gtk_widget_size_request (sheet->entry_widget, NULL);
      GTK_WIDGET (sheet_entry)->style = previous_style;

      if (style != previous_style)
	{
	  if (!GTK_IS_ITEM_ENTRY (sheet->entry_widget))
	    {
	      style->bg[GTK_STATE_NORMAL] = previous_style->bg[GTK_STATE_NORMAL];
	      style->fg[GTK_STATE_NORMAL] = previous_style->fg[GTK_STATE_NORMAL];
	      style->bg[GTK_STATE_ACTIVE] = previous_style->bg[GTK_STATE_ACTIVE];
	      style->fg[GTK_STATE_ACTIVE] = previous_style->fg[GTK_STATE_ACTIVE];
	    }
	  gtk_widget_set_style (GTK_WIDGET (sheet_entry), style);
          g_object_unref (style);
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

  row = sheet->active_cell.row;
  col = sheet->active_cell.col;


  rectangle_from_cell (sheet, row, col, &shentry_allocation);

  size = MIN (text_size, max_size);
  size = MAX (size, shentry_allocation.width - 2 * COLUMN_TITLES_HEIGHT);

  if (GTK_IS_ITEM_ENTRY (sheet->entry_widget))
    {
      shentry_allocation.height -= 2 * COLUMN_TITLES_HEIGHT;
      shentry_allocation.y += COLUMN_TITLES_HEIGHT;
      shentry_allocation.width = size;

      switch (GTK_ITEM_ENTRY (sheet_entry)->justification)
	{
	case GTK_JUSTIFY_CENTER:
	  shentry_allocation.x += shentry_allocation.width / 2 - size / 2;
	  break;
	case GTK_JUSTIFY_RIGHT:
	  shentry_allocation.x += shentry_allocation.width - size - COLUMN_TITLES_HEIGHT;
	  break;
	case GTK_JUSTIFY_LEFT:
	case GTK_JUSTIFY_FILL:
	  shentry_allocation.x += COLUMN_TITLES_HEIGHT;
	  break;
	}
    }

  if (!GTK_IS_ITEM_ENTRY (sheet->entry_widget))
    {
      shentry_allocation.x += 2;
      shentry_allocation.y += 2;
      shentry_allocation.width -= MIN (shentry_allocation.width, 3);
      shentry_allocation.height -= MIN (shentry_allocation.height, 3);
    }

  gtk_widget_size_allocate (sheet->entry_widget, &shentry_allocation);

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
  gint width;

  row = sheet->active_cell.row;
  col = sheet->active_cell.col;

  if ( ! GTK_IS_ITEM_ENTRY (sheet->entry_widget) )
    return;

  justification = GTK_ITEM_ENTRY (sheet->entry_widget)->justification;

  gdk_drawable_get_size (sheet->sheet_window, &width, NULL);

  switch (justification)
    {
    case GTK_JUSTIFY_FILL:
    case GTK_JUSTIFY_LEFT:
      for (i = col + 1; i <= max_visible_column (sheet); i++)
	{
	  if ((s = gtk_sheet_cell_get_text (sheet, row, i)))
	    {
	      g_free (s);
	      break;
	    }
	  size += g_sheet_column_get_width (sheet->column_geometry, i);
	}

      size = MIN (size, width -
		  g_sheet_column_start_pixel (sheet->column_geometry, col));
      break;
    case GTK_JUSTIFY_RIGHT:
      for (i = col - 1; i >= min_visible_column (sheet); i--)
	{
	  if ((s = gtk_sheet_cell_get_text (sheet, row, i)))
	    {
	      g_free (s);
	      break;
	    }
	  size += g_sheet_column_get_width (sheet->column_geometry, i);
	}
      break;
    case GTK_JUSTIFY_CENTER:
      for (i = col + 1; i <= max_visible_column (sheet); i++)
	{
	  sizer += g_sheet_column_get_width (sheet->column_geometry, i);
	}
      for (i = col - 1; i >= min_visible_column (sheet); i--)
	{
	  if ((s = gtk_sheet_cell_get_text (sheet, row, i)))
	    {
	      g_free (s);
	      break;
	    }
	  sizel += g_sheet_column_get_width (sheet->column_geometry, i);
	}
      size = 2 * MIN (sizel, sizer);
      break;
    }

  if (size != 0)
    size += g_sheet_column_get_width (sheet->column_geometry, col);
  GTK_ITEM_ENTRY (sheet->entry_widget)->text_max_size = size;
}


static void
create_sheet_entry (GtkSheet *sheet)
{
  if (sheet->entry_widget)
    {
      gtk_widget_unparent (sheet->entry_widget);
    }

  if (sheet->entry_type)
    {
      sheet->entry_container = g_object_new (sheet->entry_type, NULL);
      g_object_ref_sink (sheet->entry_container);
      sheet->entry_widget = gtk_sheet_get_entry (sheet);

      if  ( NULL == sheet->entry_widget)
	{
	  g_warning ("Entry type is %s. It must be GtkEntry subclass, or a widget containing one. "
		     "Using default", g_type_name (sheet->entry_type));
	  g_object_unref (sheet->entry_container);
	  sheet->entry_widget = sheet->entry_container = gtk_item_entry_new ();
	}
      else
	{
	  sheet->entry_widget = sheet->entry_container ;
	}
    }
  else
    {
      sheet->entry_widget = sheet->entry_container = gtk_item_entry_new ();
      g_object_ref_sink (sheet->entry_container);
    }

  gtk_widget_size_request (sheet->entry_widget, NULL);

  if (GTK_WIDGET_REALIZED (sheet))
    {
      gtk_widget_set_parent_window (sheet->entry_widget, sheet->sheet_window);
      gtk_widget_set_parent (sheet->entry_widget, GTK_WIDGET (sheet));
      gtk_widget_realize (sheet->entry_widget);
    }

  g_signal_connect_swapped (sheet->entry_widget, "key_press_event",
			    G_CALLBACK (gtk_sheet_entry_key_press),
			    sheet);

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

GtkWidget *
gtk_sheet_get_entry (GtkSheet *sheet)
{
  GtkWidget *parent;
  GtkWidget *entry = NULL;

  g_return_val_if_fail (sheet != NULL, NULL);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), NULL);
  g_return_val_if_fail (sheet->entry_widget != NULL, NULL);

  if (GTK_IS_ENTRY (sheet->entry_container))
    return (sheet->entry_container);

  parent = sheet->entry_container;

  if (GTK_IS_CONTAINER (parent))
    {
      gtk_container_forall (GTK_CONTAINER (parent), find_entry, &entry);

      if (GTK_IS_ENTRY (entry))
	return entry;
    }

  if (!GTK_IS_ENTRY (entry)) return NULL;

  return (entry);

}

GtkWidget *
gtk_sheet_get_entry_widget (GtkSheet *sheet)
{
  g_return_val_if_fail (sheet != NULL, NULL);
  g_return_val_if_fail (GTK_IS_SHEET (sheet), NULL);
  g_return_val_if_fail (sheet->entry_widget != NULL, NULL);

  return (sheet->entry_widget);
}


static void
gtk_sheet_button_draw (GtkSheet *sheet, GdkWindow *window,
		       GtkSheetButton *button, gboolean is_sensitive,
		       GdkRectangle allocation)
{
  GtkShadowType shadow_type;
  gint text_width = 0, text_height = 0;
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

      text_height = default_row_height (sheet) -
	2 * COLUMN_TITLES_HEIGHT;

      gdk_gc_set_clip_rectangle (GTK_WIDGET (sheet)->style->fg_gc[button->state],
				 &allocation);
      gdk_gc_set_clip_rectangle (GTK_WIDGET (sheet)->style->white_gc,
				 &allocation);

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

  gtk_sheet_button_free (button);
}

static void
gtk_sheet_column_title_button_draw (GtkSheet *sheet, gint column)
{
  GdkRectangle allocation;
  GtkSheetButton *button = NULL;
  gboolean is_sensitive = FALSE;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;

  if (!sheet->column_titles_visible) return;

  if (column < min_visible_column (sheet)) return;
  if (column > max_visible_column (sheet)) return;

  button = g_sheet_column_get_button (sheet->column_geometry, column);
  allocation.y = 0;
  allocation.x = g_sheet_column_start_pixel (sheet->column_geometry, column) + CELL_SPACING;
  allocation.x -= sheet->hadjustment->value;

  allocation.height = sheet->column_title_area.height;
  allocation.width = g_sheet_column_get_width (sheet->column_geometry, column);
  is_sensitive = g_sheet_column_get_sensitivity (sheet->column_geometry, column);

  gtk_sheet_button_draw (sheet, sheet->column_title_window,
			 button, is_sensitive, allocation);
}


static void
gtk_sheet_row_title_button_draw (GtkSheet *sheet, gint row)
{
  GdkRectangle allocation;
  GtkSheetButton *button = NULL;
  gboolean is_sensitive = FALSE;


  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet))) return;

  if (!sheet->row_titles_visible) return;

  if (row < min_visible_row (sheet)) return;
  if (row > max_visible_row (sheet)) return;

  button = g_sheet_row_get_button (sheet->row_geometry, row);
  allocation.x = 0;
  allocation.y = g_sheet_row_start_pixel (sheet->row_geometry, row) + CELL_SPACING;
  allocation.y -= sheet->vadjustment->value;

  allocation.width = sheet->row_title_area.width;
  allocation.height = g_sheet_row_get_height (sheet->row_geometry, row);
  is_sensitive = g_sheet_row_get_sensitivity (sheet->row_geometry, row);

  gtk_sheet_button_draw (sheet, sheet->row_title_window,
			 button, is_sensitive, allocation);
}

/* SCROLLBARS
 *
 * functions:
 * adjust_scrollbars
 * vadjustment_value_changed
 * hadjustment_value_changed */

static void
adjust_scrollbars (GtkSheet *sheet)
{
  gint width, height;

  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    return;

  gdk_drawable_get_size (sheet->sheet_window, &width, &height);

  if (sheet->vadjustment)
    {
      glong last_row = g_sheet_row_get_row_count (sheet->row_geometry) - 1;

      sheet->vadjustment->step_increment =
	ROWS_PER_STEP *
	g_sheet_row_get_height (sheet->row_geometry, last_row);

      sheet->vadjustment->page_increment =
	height -
	sheet->column_title_area.height -
	g_sheet_row_get_height (sheet->row_geometry, last_row);



      sheet->vadjustment->upper =
	g_sheet_row_start_pixel (sheet->row_geometry, last_row)
	+
	g_sheet_row_get_height (sheet->row_geometry, last_row)
	;

      if (sheet->column_titles_visible)
	sheet->vadjustment->upper += sheet->column_title_area.height;

      sheet->vadjustment->lower = 0;
      sheet->vadjustment->page_size = height;

      g_signal_emit_by_name (sheet->vadjustment, "changed");
    }

  if (sheet->hadjustment)
    {
      gint last_col;
      sheet->hadjustment->step_increment = 1 ; //DEFAULT_COLUMN_WIDTH;

      sheet->hadjustment->page_increment = width;

      last_col = g_sheet_column_get_column_count (sheet->column_geometry) - 1;

      sheet->hadjustment->upper =
	g_sheet_column_start_pixel (sheet->column_geometry, last_col)
	+
	g_sheet_column_get_width (sheet->column_geometry, last_col)
	;

      if (sheet->row_titles_visible)
	sheet->hadjustment->upper += sheet->row_title_area.width;

      sheet->hadjustment->lower = 0;
      sheet->hadjustment->page_size = width;

      g_signal_emit_by_name (sheet->hadjustment, "changed");
    }
}

static void
vadjustment_value_changed (GtkAdjustment *adjustment,
			   gpointer data)
{
  GtkSheet *sheet = GTK_SHEET (data);

  g_return_if_fail (adjustment != NULL);

  if ( ! GTK_WIDGET_REALIZED (sheet)) return;

  gtk_widget_hide (sheet->entry_widget);
  gtk_sheet_range_draw (sheet, NULL);
  size_allocate_row_title_buttons (sheet);
  //  size_allocate_global_button (sheet);
}


static void
hadjustment_value_changed (GtkAdjustment *adjustment,
			   gpointer data)
{
  GtkSheet *sheet = GTK_SHEET (data);

  g_return_if_fail (adjustment != NULL);

  if ( ! GTK_WIDGET_REALIZED (sheet)) return;

  gtk_widget_hide (sheet->entry_widget);
  gtk_sheet_range_draw (sheet, NULL);
  size_allocate_column_title_buttons (sheet);
  //  size_allocate_global_button (sheet);
}


/* COLUMN RESIZING */
static void
draw_xor_vline (GtkSheet *sheet)
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
draw_xor_hline (GtkSheet *sheet)

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
draw_xor_rectangle (GtkSheet *sheet, GtkSheetRange range)
{
  gint i = 0;
  GdkRectangle clip_area, area;
  GdkGCValues values;

  area.x = g_sheet_column_start_pixel (sheet->column_geometry, range.col0);
  area.y = g_sheet_row_start_pixel (sheet->row_geometry, range.row0);
  area.width = g_sheet_column_start_pixel (sheet->column_geometry, range.coli)- area.x+
    g_sheet_column_get_width (sheet->column_geometry, range.coli);
  area.height = g_sheet_row_start_pixel (sheet->row_geometry, range.rowi)- area.y +
    g_sheet_row_get_height (sheet->row_geometry, range.rowi);

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


/* this function returns the new width of the column being resized given
 * the COLUMN and X position of the cursor; the x cursor position is passed
 * in as a pointer and automaticaly corrected if it's outside the acceptable
 * range */
static guint
new_column_width (GtkSheet *sheet, gint column, gint *x)
{
  gint left_pos = g_sheet_column_start_pixel (sheet->column_geometry, column)
    - sheet->hadjustment->value;

  gint width = *x - left_pos;

  if ( width < sheet->column_requisition)
    {
      width = sheet->column_requisition;
      *x = left_pos + width;
    }

  g_sheet_column_set_width (sheet->column_geometry, column, width);

  size_allocate_column_title_buttons (sheet);

  return width;
}

/* this function returns the new height of the row being resized given
 * the row and y position of the cursor; the y cursor position is passed
 * in as a pointer and automaticaly corrected if it's beyond min / max limits */
static guint
new_row_height (GtkSheet *sheet, gint row, gint *y)
{
  gint height;
  guint min_height;

  gint cy = *y;
  min_height = sheet->row_requisition;

  /* you can't shrink a row to less than its minimum height */
  if (cy < g_sheet_row_start_pixel (sheet->row_geometry, row) + min_height)

    {
      *y = cy = g_sheet_row_start_pixel (sheet->row_geometry, row) + min_height;
    }

  /* calculate new row height making sure it doesn't end up
   * less than the minimum height */
  height = (cy - g_sheet_row_start_pixel (sheet->row_geometry, row));
  if (height < min_height)
    height = min_height;

  g_sheet_row_set_height (sheet->row_geometry, row, height);
  size_allocate_row_title_buttons (sheet);

  return height;
}

static void
gtk_sheet_set_column_width (GtkSheet *sheet,
			    gint column,
			    guint width)
{
  guint min_width;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (column < 0 || column >= g_sheet_column_get_column_count (sheet->column_geometry))
    return;

  gtk_sheet_column_size_request (sheet, column, &min_width);
  if (width < min_width) return;

  g_sheet_column_set_width (sheet->column_geometry, column, width);

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      size_allocate_column_title_buttons (sheet);
      adjust_scrollbars (sheet);
      gtk_sheet_size_allocate_entry (sheet);
      gtk_sheet_range_draw (sheet, NULL);
    }

  g_signal_emit (sheet, sheet_signals[CHANGED], 0, -1, column);
}



static void
gtk_sheet_set_row_height (GtkSheet *sheet,
			  gint row,
			  guint height)
{
  guint min_height;

  g_return_if_fail (sheet != NULL);
  g_return_if_fail (GTK_IS_SHEET (sheet));

  if (row < 0 || row >= g_sheet_row_get_row_count (sheet->row_geometry))
    return;

  gtk_sheet_row_size_request (sheet, row, &min_height);
  if (height < min_height) return;

  g_sheet_row_set_height (sheet->row_geometry, row, height);

  if (GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)) )
    {
      size_allocate_row_title_buttons (sheet);
      adjust_scrollbars (sheet);
      gtk_sheet_size_allocate_entry (sheet);
      gtk_sheet_range_draw (sheet, NULL);
    }

  g_signal_emit (sheet, sheet_signals[CHANGED], 0, row, - 1);
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
  attributes->justification = j ? *j : GTK_JUSTIFY_LEFT;

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
  attributes->background = sheet->color[BG_COLOR];
  if (!GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    {
      attributes->background = sheet->color[BG_COLOR];
    }
  attributes->justification = g_sheet_column_get_justification (sheet->column_geometry, col);
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


static void
gtk_sheet_button_size_request	 (GtkSheet *sheet,
				  const GtkSheetButton *button,
				  GtkRequisition *button_requisition)
{
  GtkRequisition requisition;
  GtkRequisition label_requisition;

  label_requisition.height = default_row_height (sheet);
  label_requisition.width = COLUMN_MIN_WIDTH;

  requisition.height = default_row_height (sheet);
  requisition.width = COLUMN_MIN_WIDTH;


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

  gtk_sheet_button_size_request (sheet,
				 g_sheet_row_get_button (sheet->row_geometry, row),
				 &button_requisition);

  *requisition = button_requisition.height;

  sheet->row_requisition = *requisition;
}

static void
gtk_sheet_column_size_request (GtkSheet *sheet,
			       gint col,
			       guint *requisition)
{
  GtkRequisition button_requisition;

  GtkSheetButton *button = g_sheet_column_get_button (sheet->column_geometry, col);

  gtk_sheet_button_size_request (sheet,
				 button,
				 &button_requisition);

  gtk_sheet_button_free (button);

  *requisition = button_requisition.width;

  sheet->column_requisition = *requisition;
}


static void
gtk_sheet_forall (GtkContainer *container,
		  gboolean include_internals,
		  GtkCallback callback,
		  gpointer callback_data)
{
  GtkSheet *sheet = GTK_SHEET (container);

  g_return_if_fail (callback != NULL);

  if (sheet->button && sheet->button->parent)
    (* callback) (sheet->button, callback_data);

  if (sheet->entry_container && GTK_IS_CONTAINER (sheet->entry_container))
    (* callback) (sheet->entry_container, callback_data);
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
  button->justification = GTK_JUSTIFY_FILL;

  return button;
}


void
gtk_sheet_button_free (GtkSheetButton *button)
{
  if (!button) return ;

  g_free (button->label);
  g_free (button);
}


static void
append_cell_text (GString *string, const GtkSheet *sheet, gint r, gint c)
{
  gchar *celltext = gtk_sheet_cell_get_text (sheet, r, c);

  if ( NULL == celltext)
    return;

  g_string_append (string, celltext);
  g_free (celltext);
}


static GString *
range_to_text (const GtkSheet *sheet)
{
  gint r, c;
  GString *string;

  if ( !gtk_sheet_range_isvisible (sheet, sheet->range))
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
range_to_html (const GtkSheet *sheet)
{
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
  if ( ! GTK_WIDGET_REALIZED (GTK_WIDGET (sheet)))
    return;

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
