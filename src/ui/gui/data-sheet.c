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



extern GladeXML *xml;

static gboolean 
traverse_callback (GtkSheet * sheet, 
		   gint row, gint col, 
		   gint *new_row, gint *new_column
		   )
{
  gint case_count;
  PsppireDataStore *data_store = PSPPIRE_DATA_STORE(gtk_sheet_get_model(sheet));
  const gint n_vars = psppire_dict_get_var_cnt(data_store->dict);

  if ( *new_column >= n_vars ) 
    return FALSE;

  case_count = psppire_case_file_get_case_count(data_store->case_file);

  if ( *new_row >= case_count )
    {
      gint i;

      for ( i = case_count ; i <= *new_row; ++i ) 
	psppire_data_store_insert_new_case (data_store, i);

      return TRUE;
    }

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
gint 
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


/* Return the width that an  'M' character would occupy when typeset in WIDGET */
static guint 
calc_m_width(GtkWidget *widget, const PangoFontDescription *font_desc)
{
  PangoRectangle rect;
  PangoLayout *layout ;
  PangoContext * context;

  context = gtk_widget_create_pango_context (widget);
  g_assert (context);
  layout = pango_layout_new (context);
  g_assert (layout);

  pango_layout_set_text (layout, "M", 1);
  
  pango_layout_set_font_description (layout, font_desc);

  pango_layout_get_extents (layout, NULL, &rect);

  g_object_unref(G_OBJECT(layout));
  g_object_unref(G_OBJECT(context));

  return PANGO_PIXELS(rect.width);
}



void
font_change_callback(GObject *obj, gpointer data)
{
  GtkWidget *sheet  = data;
  PsppireDataStore *ds = PSPPIRE_DATA_STORE(obj);

  ds->width_of_m = calc_m_width(sheet, ds->font_desc);
}

GtkWidget*
psppire_data_sheet_create (gchar *widget_name, gchar *string1, gchar *string2,
			   gint int1, gint int2)
{
  GtkWidget *sheet;

  sheet = gtk_sheet_new(G_SHEET_ROW(data_store), 
			G_SHEET_COLUMN(data_store), "data sheet", 0); 

  data_store->width_of_m = calc_m_width(sheet, data_store->font_desc);

  g_signal_connect (G_OBJECT (sheet), "activate",
		    G_CALLBACK (update_data_ref_entry),
		    0);

  g_signal_connect (G_OBJECT (sheet), "traverse",
		    G_CALLBACK (traverse_callback), 0);


  g_signal_connect (G_OBJECT (sheet), "double-click-column",
		    G_CALLBACK (click2column),
		    0);

  g_signal_connect (G_OBJECT (data_store), "font-changed",
		    G_CALLBACK (font_change_callback), sheet);

  gtk_sheet_set_active_cell(GTK_SHEET(sheet), -1, -1);
  gtk_widget_show(sheet);

  return sheet;
}
