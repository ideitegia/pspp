/* 
   PSPPIRE --- A Graphical User Interface for PSPP
   Copyright (C) 2004, 2005, 2006  Free Software Foundation
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
   02110-1301, USA. 
*/

#include <gtk/gtk.h>
#include <glade/glade.h>

#include <ctype.h>

#include <gtksheet/gtksheet.h>

#include <gtksheet/gsheet-uniform-row.h>

#include "psppire-dict.h"
#include "psppire-variable.h"
#include "psppire-data-store.h"
#include "helper.h"

#include <data/value-labels.h>
#include <data/case.h>
#include <data/data-in.h>

#include "menu-actions.h"
#include "data-sheet.h"

#define _(A) A
#define N_(A) A


extern GladeXML *xml;


static gboolean 
traverse_callback (GtkSheet * sheet, 
			gint row, gint col, 
			gint *new_row, gint *new_column
			)
{
  PsppireDataStore *data_store = PSPPIRE_DATA_STORE(gtk_sheet_get_model(sheet));

  const gint n_vars = psppire_dict_get_var_cnt(data_store->dict);

  if ( *new_column >= n_vars ) 
    return FALSE;

  return TRUE;
}



/* Callback which occurs when the column title is double clicked */
static gboolean
click2column(GtkWidget *w, gint col, gpointer data)
{
  gint current_row, current_column;
  GtkWidget *var_sheet  = get_widget_assert(xml, "variable_sheet");

  select_sheet(PAGE_VAR_SHEET);

  gtk_sheet_get_active_cell(GTK_SHEET(var_sheet), 
			    &current_row, &current_column);

  gtk_sheet_set_active_cell(GTK_SHEET(var_sheet), col, current_column);

  return FALSE;
}


/* Update the data_ref_entry with the reference of the active cell */
static gint 
update_data_ref_entry(const GtkSheet *sheet, gint row, gint col)
{

  /* The entry where the reference to the current cell is displayed */
  GtkEntry *cell_ref_entry;

  PsppireDataStore *data_store = PSPPIRE_DATA_STORE(gtk_sheet_get_model(sheet));
  if (data_store)
    {
      const struct PsppireVariable *pv = 
	psppire_dict_get_variable(data_store->dict, col);

      gchar *text ;
      gchar *s ;

      if ( !xml) 
	return FALSE;

      text = g_strdup_printf("%d: %s", row, 
			     pv ? psppire_variable_get_name(pv) : "");
  
      cell_ref_entry = GTK_ENTRY(get_widget_assert(xml, "cell_ref_entry"));

      s = pspp_locale_to_utf8(text, -1, 0);

      g_free(text);

      gtk_entry_set_text(cell_ref_entry, s);

      g_free(s);
    }

  return FALSE;
}


extern PsppireDataStore *data_store ;


GtkWidget*
psppire_data_sheet_create (gchar *widget_name, gchar *string1, gchar *string2,
			   gint int1, gint int2)
{
  GtkWidget *sheet;

  const gint rows = 10046;

  GObject *row_geometry = g_sheet_uniform_row_new(25, rows); 

  sheet = gtk_sheet_new(G_SHEET_ROW(row_geometry), 
			G_SHEET_COLUMN(data_store), "data sheet", 0); 


  g_signal_connect (GTK_OBJECT (sheet), "activate",
		    GTK_SIGNAL_FUNC (update_data_ref_entry),
		    0);

  g_signal_connect (GTK_OBJECT (sheet), "traverse",
		    GTK_SIGNAL_FUNC (traverse_callback), 0);


  g_signal_connect (GTK_OBJECT (sheet), "double-click-column",
		    GTK_SIGNAL_FUNC (click2column),
		    0);

  gtk_widget_show(sheet);

  return sheet;
}
