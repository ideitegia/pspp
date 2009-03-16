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


#ifndef PSPPIRE_EXTRA_SHEET_H__
#define PSPPIRE_EXTRA_SHEET_H__


struct _PsppireSheet ;

typedef struct _PsppireSheet PsppireSheet;


struct _PsppireSheetButton
{
  GtkStateType state;
  gchar *label;

  gboolean label_visible;

  GtkJustification justification;
  gboolean overstruck;
};

struct _PsppireSheetCell
{
  gint row;
  gint col;
};

typedef struct _PsppireSheetButton PsppireSheetButton;
typedef struct _PsppireSheetCell PsppireSheetCell;

PsppireSheetButton * psppire_sheet_button_new (void);

void psppire_sheet_button_free (PsppireSheetButton *button);


#endif /* PSPPIRE_EXTRA_SHEET_H__ */


