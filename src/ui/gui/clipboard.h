/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007  Free Software Foundation

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>
#include <gtksheet/gtksheet.h>

#ifndef CLIPBOARD_H
#define CLIPBOARD_H


void data_sheet_set_clip (GtkSheet *data_sheet);

void data_sheet_contents_received_callback (GtkClipboard *clipboard,
					    GtkSelectionData *sd,
					    gpointer data);


#endif /* CLIPBOARD_H */

