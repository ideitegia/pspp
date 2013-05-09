/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2011, 2012  Free Software Foundation

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
#include "builder-wrapper.h"
#include "psppire-dialog.h"
#include "psppire-data-window.h"
#include "psppire-data-store.h"


static void
refresh (PsppireDataSheet *ds, GtkBuilder *xml)
{
  PsppireDataStore *data_store = psppire_data_sheet_get_data_store (ds);
  casenumber case_count ;

  GtkWidget *case_num_entry = get_widget_assert (xml, "goto-case-case-num-entry");

  case_count =  psppire_data_store_get_case_count (data_store);

  gtk_spin_button_set_range (GTK_SPIN_BUTTON (case_num_entry), 1, case_count);
}

void
goto_case_dialog (PsppireDataSheet *ds)
{
  GtkWindow *top_level;
  gint response;
  GtkBuilder *xml = builder_new ("goto-case.ui");
  GtkWidget *dialog = get_widget_assert   (xml, "goto-case-dialog");

  top_level = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (ds)));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), top_level);

  refresh (ds, xml);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  if ( response == PSPPIRE_RESPONSE_GOTO )
    {
      PsppireDataStore *data_store = psppire_data_sheet_get_data_store (ds);
      glong case_num;
      GtkWidget *case_num_entry =
	get_widget_assert (xml, "goto-case-case-num-entry");

      case_num = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (case_num_entry))
	- FIRST_CASE_NUMBER ;

      if (case_num >= 0
          && case_num < psppire_data_store_get_case_count (data_store))
        psppire_data_sheet_goto_case (ds, case_num);
    }
}
