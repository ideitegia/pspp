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
#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <gobject/gvaluecollector.h>
#include "gsheet-row-iface.h"
#include "gtkextra-marshal.h"


enum {
  ROWS_CHANGED,
  LAST_SIGNAL
};

static guint sheet_row_signals[LAST_SIGNAL];



static void      g_sheet_row_base_init   (gpointer g_class);


GType
g_sheet_row_get_type (void)
{
  static GType sheet_row_type = 0;

  if (! sheet_row_type)
    {
      static const GTypeInfo sheet_row_info =

      {
        sizeof (GSheetRowIface), /* class_size */
	g_sheet_row_base_init,   /* base_init */
	NULL,		/* base_finalize */
	NULL,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	0,
	0,              /* n_preallocs */
	NULL
      };

      sheet_row_type =
	g_type_register_static (G_TYPE_INTERFACE, "GSheetRow",
				&sheet_row_info, 0);

      g_type_interface_add_prerequisite (sheet_row_type, G_TYPE_OBJECT);
    }

  return sheet_row_type;
}


static GtkSheetButton default_button;

static void
g_sheet_row_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;

  if (! initialized)
    {

      sheet_row_signals[ROWS_CHANGED] =
	g_signal_new ("rows_changed",
		      G_TYPE_SHEET_ROW,
		      G_SIGNAL_RUN_LAST,
		      G_STRUCT_OFFSET (GSheetRowIface, rows_changed),
		      NULL, NULL,
		      gtkextra_VOID__INT_INT,
		      G_TYPE_NONE, 2,
		      G_TYPE_INT,
		      G_TYPE_INT);


      default_button.state = GTK_STATE_NORMAL;
      default_button.label = NULL;
      default_button.label_visible = TRUE;
      default_button.child = NULL;
      default_button.justification = GTK_JUSTIFY_FILL;

      initialized = TRUE;
    }
}

void  
g_sheet_row_set_height (GSheetRow *row_geo,
				gint row, gint size, gpointer data)
{
  g_return_if_fail (G_IS_SHEET_ROW (row_geo));

  if ((G_SHEET_ROW_GET_IFACE (row_geo)->set_height) ) 
    (G_SHEET_ROW_GET_IFACE (row_geo)->set_height) (row_geo, row, 
							size, data);
}


gint 
g_sheet_row_get_height     (const GSheetRow *row_geo, 
				    gint row, gpointer data)
{
  g_return_val_if_fail (G_IS_SHEET_ROW (row_geo), -1);

  g_assert (G_SHEET_ROW_GET_IFACE (row_geo)->get_height);
  
  return (G_SHEET_ROW_GET_IFACE (row_geo)->get_height) (row_geo, row, 
							     data);
}



gboolean  
g_sheet_row_get_visibility(const GSheetRow *row_geo,
					    gint row, gpointer data)
{
  g_return_val_if_fail (G_IS_SHEET_ROW (row_geo), FALSE);

  g_assert (G_SHEET_ROW_GET_IFACE (row_geo)->get_visibility);
  
  return (G_SHEET_ROW_GET_IFACE (row_geo)->get_visibility) (row_geo, 
								  row, data);

}

gboolean  
g_sheet_row_get_sensitivity(const GSheetRow *row_geo,
					     gint row, gpointer data)
{
  g_return_val_if_fail (G_IS_SHEET_ROW (row_geo), FALSE);

  g_assert (G_SHEET_ROW_GET_IFACE (row_geo)->get_sensitivity);
  
  return (G_SHEET_ROW_GET_IFACE (row_geo)->get_sensitivity) (row_geo, 
								   row, data);

}


GtkSheetButton *
g_sheet_row_get_button(const GSheetRow *row_geo,
			      gint row, gpointer data)
{
  GtkSheetButton *button  = gtk_sheet_button_new();

  GSheetRowIface *iface = G_SHEET_ROW_GET_IFACE (row_geo);

  g_return_val_if_fail (G_IS_SHEET_ROW (row_geo), FALSE);

  if ( iface->get_button_label)
    button->label = iface->get_button_label(row_geo, row, data);

  return button;
}


gint  
g_sheet_row_get_row_count(const GSheetRow *geo, gpointer data)
{
  g_return_val_if_fail (G_IS_SHEET_ROW (geo), -1);

  g_assert  ( G_SHEET_ROW_GET_IFACE (geo)->get_row_count);

  return (G_SHEET_ROW_GET_IFACE (geo)->get_row_count) (geo, data);
}

/**
 * g_sheet_row_start_pixel:
 * @geo: the row model
 * @row: the row number
 * @sheet: pointer to the sheet 
 *
 * Returns the top y pixel for ROW.
 * Instances may override this method in order to achieve time and/or memory
 * optmisation.
 *
 * Returns: the y coordinate of the top of the row.
 */

gint  
g_sheet_row_start_pixel(const GSheetRow *geo, gint row, gpointer data)
{
  gint i;
  gint start_pixel = 0;

  g_return_val_if_fail (G_IS_SHEET_ROW (geo), -1);
  g_return_val_if_fail (row >= 0, -1);
  g_return_val_if_fail (row < 
			g_sheet_row_get_row_count(geo, data),-1);

  if ( G_SHEET_ROW_GET_IFACE(geo)->top_ypixel) 
    return (G_SHEET_ROW_GET_IFACE(geo)->top_ypixel)(geo, row, data);

  for ( i = 0 ; i < row ; ++i ) 
    {
      if ( g_sheet_row_get_visibility(geo, i, data))
	start_pixel += g_sheet_row_get_height(geo, i, data);
    }
  
  return start_pixel;
}


gint  
g_sheet_row_pixel_to_row(const GSheetRow *geo, gint pixel, 
			 gpointer data)
{
  gint i, cy;
  g_return_val_if_fail (G_IS_SHEET_ROW (geo), -1);
  g_return_val_if_fail (pixel >= 0, -1) ;

  if ( G_SHEET_ROW_GET_IFACE(geo)->pixel_to_row) 
    return (G_SHEET_ROW_GET_IFACE(geo)->pixel_to_row)(geo, pixel, data);

  cy = 0;
  for (i = 0; i < g_sheet_row_get_row_count(geo, data); ++i ) 
    {
      if (pixel >= cy  && 
	  pixel <= (cy + g_sheet_row_get_height(geo, i, data)) && 
	  g_sheet_row_get_visibility(geo, i, data))
	return i;

      if(g_sheet_row_get_visibility(geo, i, data))
	cy += g_sheet_row_get_height(geo, i, data);
    }

  /* no match */
  return g_sheet_row_get_row_count(geo, data) - 1;
}



void
g_sheet_row_rows_deleted(GSheetRow *geo, 
				 gint first, gint n_rows)
{
  g_return_if_fail (G_IS_SHEET_ROW (geo));

  g_signal_emit (geo, sheet_row_signals[ROWS_CHANGED], 0, 
		 first, n_rows);
}
