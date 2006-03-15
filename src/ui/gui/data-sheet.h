/* 
   PSPPIRE --- A Graphical User Interface for PSPP
   Copyright (C) 2004  Free Software Foundation
   Written by John Darrington

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */


#ifndef DATA_SHEET_H
#define DATA_SHEET_H

#include <gtksheet/gtksheet.h>
#include "psppire-case-array.h"
#include "psppire-dict.h"

void psppire_data_sheet_clear(GtkSheet *sheet);

#if 0
void psppire_data_sheet_set_dictionary(GtkSheet *sheet, PsppireDict *d);
#endif

GtkWidget* psppire_data_sheet_create (gchar *widget_name, 
				      gchar *string1, 
				      gchar *string2, 
				      gint int1, gint int2);


void data_sheet_set_cell_value(GtkSheet *sheet, gint row, gint col, 
			       const GValue *value);


void psppire_data_sheet_set_show_labels(GtkSheet *sheet, gboolean show_labels);

/* Repair any damage that may have been done to the data sheet */
void psppire_data_sheet_redisplay(GtkSheet *sheet);

guint columnWidthToPixels(GtkSheet *sheet, gint column, guint width);


#endif
