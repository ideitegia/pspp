/* This version of GtkSheet has been heavily modified, for the specific
 *  requirements of PSPPIRE.
 *
 * GtkSheet widget for Gtk+.
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


#ifndef __GTK_EXTRA_SHEET_H__
#define __GTK_EXTRA_SHEET_H__


struct _GtkSheet ;

typedef struct _GtkSheet GtkSheet;


struct _GtkSheetChild
{
  GtkWidget *widget;
  gint x,y ;
  gboolean attached_to_cell;
  gboolean floating;
  gint row, col;
  guint16 xpadding;
  guint16 ypadding;
  gboolean xexpand;
  gboolean yexpand;
  gboolean xshrink;
  gboolean yshrink;
  gboolean xfill;
  gboolean yfill;
};

typedef struct _GtkSheetChild GtkSheetChild;



struct _GtkSheetButton
{
  GtkStateType state;
  gchar *label;

  gboolean label_visible;
  GtkSheetChild *child;

  GtkJustification justification;
};

typedef struct _GtkSheetButton GtkSheetButton;



GtkSheetButton * gtk_sheet_button_new(void);

inline void gtk_sheet_button_free(GtkSheetButton *button);


#endif /* __GTK_EXTRA_SHEET_H__ */


