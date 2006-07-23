/* GSheetColumn --- an abstract model of the column geometry of a 
   GSheet widget.

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

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <gobject/gvaluecollector.h>
#include "gsheet-column-iface.h"
#include "gtkextra-marshal.h"
#include "gtkextra-sheet.h"

enum {
  COLUMNS_CHANGED,
  LAST_SIGNAL
};

static guint sheet_column_signals[LAST_SIGNAL];



static void      g_sheet_column_base_init   (gpointer g_class);


inline GType
g_sheet_column_get_type (void)
{
  static GType sheet_column_type = 0;

  if (! sheet_column_type)
    {
      static const GTypeInfo sheet_column_info =

      {
        sizeof (GSheetColumnIface), /* class_size */
	g_sheet_column_base_init,   /* base_init */
	NULL,		/* base_finalize */
	NULL,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	0,
	0,              /* n_preallocs */
	NULL
      };

      sheet_column_type =
	g_type_register_static (G_TYPE_INTERFACE, "GSheetColumn",
				&sheet_column_info, 0);

      g_assert(sheet_column_type);

      g_type_interface_add_prerequisite (sheet_column_type, G_TYPE_OBJECT);
    }

  return sheet_column_type;
}


static void
g_sheet_column_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;

  if (! initialized)
    {

      sheet_column_signals[COLUMNS_CHANGED] =
	g_signal_new ("columns_changed",
		      G_TYPE_SHEET_COLUMN,
		      G_SIGNAL_RUN_LAST,
		      G_STRUCT_OFFSET (GSheetColumnIface, columns_changed),
		      NULL, NULL,
		      gtkextra_VOID__INT_INT,
		      G_TYPE_NONE, 2,
		      G_TYPE_INT,
		      G_TYPE_INT);


      initialized = TRUE;
    }
}


inline void  
g_sheet_column_set_width (GSheetColumn *column, gint col, gint size)
{
  g_return_if_fail (G_IS_SHEET_COLUMN (column));

  if ((G_SHEET_COLUMN_GET_IFACE (column)->set_width) ) 
    (G_SHEET_COLUMN_GET_IFACE (column)->set_width) (column, col, size);
}


inline gint 
g_sheet_column_get_width (const GSheetColumn *column, gint col)
{
  g_return_val_if_fail (G_IS_SHEET_COLUMN (column), -1);

  g_assert (G_SHEET_COLUMN_GET_IFACE (column)->get_width);
  
  return (G_SHEET_COLUMN_GET_IFACE (column)->get_width) (column, col);
}



inline gboolean  
g_sheet_column_get_visibility(const GSheetColumn *column,
					    gint col)
{
  g_return_val_if_fail (G_IS_SHEET_COLUMN (column), FALSE);

  g_assert (G_SHEET_COLUMN_GET_IFACE (column)->get_visibility);
  
  return (G_SHEET_COLUMN_GET_IFACE (column)->get_visibility) (column, 
								  col);

}

inline gboolean  
g_sheet_column_get_sensitivity(const GSheetColumn *column,
					     gint col)
{
  g_return_val_if_fail (G_IS_SHEET_COLUMN (column), FALSE);

  g_assert (G_SHEET_COLUMN_GET_IFACE (column)->get_sensitivity);
  
  return (G_SHEET_COLUMN_GET_IFACE (column)->get_sensitivity) (column, 
								   col);

}


inline GtkSheetButton *
g_sheet_column_get_button(const GSheetColumn *column,
			      gint col)
{
  GtkSheetButton *button = gtk_sheet_button_new();

  GSheetColumnIface *iface = G_SHEET_COLUMN_GET_IFACE (column);

  g_return_val_if_fail (G_IS_SHEET_COLUMN (column), FALSE);

  if ( iface->get_button_label)
    button->label = iface->get_button_label(column, col);

  return button;
}

inline GtkJustification 
g_sheet_column_get_justification(const GSheetColumn *column, 
				     gint col)
{
  g_return_val_if_fail (G_IS_SHEET_COLUMN (column), FALSE);

  g_assert (G_SHEET_COLUMN_GET_IFACE (column)->get_justification);
  
  return (G_SHEET_COLUMN_GET_IFACE (column)->get_justification) (column, col);
}




inline gint  
g_sheet_column_get_left_text_column (const GSheetColumn *column,
					 gint col)
{
  g_return_val_if_fail (G_IS_SHEET_COLUMN (column), -1);

  if  ( ! G_SHEET_COLUMN_GET_IFACE (column)->get_left_text_column)
    return col;
  
  return (G_SHEET_COLUMN_GET_IFACE (column)->get_left_text_column) (column, col);

}

inline gint  
g_sheet_column_get_right_text_column (const GSheetColumn *column,
					  gint col)
{
  g_return_val_if_fail (G_IS_SHEET_COLUMN (column), -1);

  if  ( ! G_SHEET_COLUMN_GET_IFACE (column)->get_right_text_column)
    return col;
  
  return (G_SHEET_COLUMN_GET_IFACE (column)->get_right_text_column) (column, col);

}

inline void 
g_sheet_column_set_left_text_column (const GSheetColumn *column,
					 gint col, gint i)
{
  g_return_if_fail (G_IS_SHEET_COLUMN (column));

  if  ( G_SHEET_COLUMN_GET_IFACE (column)->set_left_text_column)
    (G_SHEET_COLUMN_GET_IFACE (column)->set_left_text_column) (column, col, i);

}


inline void 
g_sheet_column_set_right_text_column (const GSheetColumn *column,
					  gint col, gint i)
{
  g_return_if_fail (G_IS_SHEET_COLUMN (column));

  if  ( G_SHEET_COLUMN_GET_IFACE (column)->set_right_text_column)
    (G_SHEET_COLUMN_GET_IFACE (column)->set_right_text_column) (column, col, i);
}

inline gint  
g_sheet_column_get_column_count(const GSheetColumn *geo)
{
  g_return_val_if_fail (G_IS_SHEET_COLUMN (geo), -1);

  g_assert  ( G_SHEET_COLUMN_GET_IFACE (geo)->get_column_count);

  return (G_SHEET_COLUMN_GET_IFACE (geo)->get_column_count) (geo);
}

inline gint  
g_sheet_column_start_pixel(const GSheetColumn *geo, gint col)
{
  gint i;
  gint start_pixel = 0;

  g_return_val_if_fail (G_IS_SHEET_COLUMN (geo), -1);
  g_return_val_if_fail (col < 
			g_sheet_column_get_column_count(geo),-1);

  for ( i = 0 ; i < col ; ++i ) 
    {
      if ( g_sheet_column_get_visibility(geo, i))
	start_pixel += g_sheet_column_get_width(geo, i);
    }
  
  return start_pixel;

}



inline void
g_sheet_column_columns_changed(GSheetColumn *geo, 
				 gint first, gint n_columns)
{
  g_return_if_fail (G_IS_SHEET_COLUMN (geo));

  g_signal_emit (geo, sheet_column_signals[COLUMNS_CHANGED], 0, 
		 first, n_columns);
}




