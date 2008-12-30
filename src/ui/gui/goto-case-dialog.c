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
#include "psppire-data-window.h"
#include "psppire-data-store.h"


static void
refresh (const PsppireDataWindow *de, GladeXML *xml)
{
  PsppireDataStore *ds = NULL;
  casenumber case_count ;

  GtkWidget *case_num_entry = get_widget_assert (xml, "goto-case-case-num-entry");

  g_object_get (de->data_editor, "data-store", &ds, NULL);

  case_count =  psppire_data_store_get_case_count (ds);

  gtk_spin_button_set_range (GTK_SPIN_BUTTON (case_num_entry),
			     1, case_count);
}

void
goto_case_dialog (GObject *o, gpointer data)
{
  gint response;
  GladeXML *xml = XML_NEW ("psppire.glade");
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);

  GtkWidget *dialog = get_widget_assert   (xml, "goto-case-dialog");


  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  refresh (de, xml);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  if ( response == PSPPIRE_RESPONSE_GOTO )
    {
      glong case_num;
      GtkWidget *case_num_entry =
	get_widget_assert (xml, "goto-case-case-num-entry");

      case_num = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (case_num_entry))
	- FIRST_CASE_NUMBER ;

      g_object_set (de->data_editor, "current-case", case_num, NULL);
    }
}
