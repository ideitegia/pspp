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
#include "goto-case-dialog.h"
#include "helper.h"
#include "psppire-dialog.h"
#include "data-editor.h"
#include "psppire-data-store.h"
#include <gtksheet/gtksheet.h>


static void
refresh (const struct data_editor *de, GladeXML *xml)
{
  GtkSheet *data_sheet =
    GTK_SHEET (get_widget_assert (de->xml, "data_sheet"));

  PsppireDataStore *ds =
    PSPPIRE_DATA_STORE (gtk_sheet_get_model (data_sheet));

  GtkWidget *case_num_entry =
    get_widget_assert (xml, "goto-case-case-num-entry");

  casenumber case_count =
    psppire_data_store_get_case_count (ds);

  gtk_spin_button_set_range (GTK_SPIN_BUTTON (case_num_entry),
			     1, case_count);
}

void
goto_case_dialog (GObject *o, gpointer data)
{
  gint response;
  GladeXML *xml = XML_NEW ("psppire.glade");
  struct data_editor *de = data;

  GtkWidget *dialog = get_widget_assert   (xml, "goto-case-dialog");


  gtk_window_set_transient_for (GTK_WINDOW (dialog), de->parent.window);

  refresh (de, xml);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  if ( response == PSPPIRE_RESPONSE_GOTO )
    {
      gint row, column;
      GtkSheet *data_sheet =
	GTK_SHEET (get_widget_assert (de->xml, "data_sheet"));


      GtkWidget *case_num_entry =
	get_widget_assert (xml, "goto-case-case-num-entry");

      gtk_sheet_get_active_cell (data_sheet, &row, &column);

      row =
	gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (case_num_entry))
	- FIRST_CASE_NUMBER ;

      gtk_sheet_moveto (data_sheet,
			row, column,
			0.5, 0.5);

      gtk_sheet_set_active_cell (data_sheet, row, column);
    }
}
