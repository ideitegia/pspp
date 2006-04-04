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
   02110-1301, USA. */


/* This module creates the Variable Sheet used for inputing the
   variables in the  dictonary */

#include <data/value-labels.h>

#include <glade/glade.h>
#include <gtk/gtk.h>

#include <stdlib.h>
#include <string.h>

#define min(A,B) ((A < B)?A:B)

#include <gtksheet/gtksheet.h>
#include <gtksheet/gsheet-hetero-column.h>
#include <gtksheet/gsheet-uniform-row.h>

#include "psppire-var-store.h"
#include "helper.h"
#include "menu-actions.h"
#include "psppire-dict.h"
#include "psppire-variable.h"
#include "var-type-dialog.h"
#include "var-sheet.h"
#include "customentry.h"

#include "val-labs-dialog.h"
#include "missing-val-dialog.h"

#define _(A) A
#define N_(A) A


static const gint n_initial_rows = 40;


extern GladeXML *xml;

struct column_parameters
{
  gchar label[20];  
  gint width ;
};

static const struct column_parameters column_def[] = {
  {N_("Name"),    80},
  {N_("Type"),    100},
  {N_("Width"),   57}, 
  {N_("Decimals"),91}, 
  {N_("Label"),   95}, 
  {N_("Values"),  103},
  {N_("Missing"), 95}, 
  {N_("Columns"), 80}, 
  {N_("Align"),   69}, 
  {N_("Measure"), 99}, 
};


static gboolean
click2row(GtkWidget *w, gint row, gpointer data)
{
  gint current_row, current_column;
  GtkWidget *data_sheet  = get_widget_assert(xml, "data_sheet");

  select_sheet(PAGE_DATA_SHEET);

  gtk_sheet_get_active_cell(GTK_SHEET(data_sheet), 
			    &current_row, &current_column);

  gtk_sheet_set_active_cell(GTK_SHEET(data_sheet), current_row, row);

  return FALSE;
}



const gchar *alignments[]={
  _("Left"),
  _("Right"),
  _("Centre"),
  0
};

const gchar *measures[]={
  _("Nominal"),
  _("Ordinal"),
  _("Scale"),
  0
};

static GtkListStore *
create_label_list(const gchar **labels)
{
  const gchar *s;
  gint i = 0;
  GtkTreeIter iter;

  GtkListStore *list_store;
  list_store = gtk_list_store_new (1, G_TYPE_STRING);


  while ( (s = labels[i++]))
    {
      gtk_list_store_append (list_store, &iter);
      gtk_list_store_set (list_store, &iter,
			  0, s,
			  -1);
    }
	
  return list_store;
}

/* Callback for when the alignment combo box 
   item is selected */
static void        
change_alignment(GtkComboBox *cb,
    gpointer user_data)
{
  struct PsppireVariable *pv = user_data;
  gint active_item = gtk_combo_box_get_active(cb);

  if ( active_item < 0 ) return ;

  psppire_variable_set_alignment(pv, active_item);
}



/* Callback for when the measure combo box 
   item is selected */
static void        
change_measure(GtkComboBox *cb,
    gpointer user_data)
{
  struct PsppireVariable *pv = user_data;
  gint active_item = gtk_combo_box_get_active(cb);

  if ( active_item < 0 ) return ;

  psppire_variable_set_measure(pv, active_item);
}



static gboolean 
traverse_cell_callback (GtkSheet * sheet, 
			gint row, gint column, 
			gint *new_row, gint *new_column
			)
{
  PsppireVarStore *var_store = PSPPIRE_VAR_STORE(gtk_sheet_get_model(sheet));

  gint n_vars = psppire_var_store_get_var_cnt(var_store);

  if ( row == n_vars && *new_row >= n_vars)
    {
      GtkEntry *entry = GTK_ENTRY(gtk_sheet_get_entry(sheet));

      const gchar *name = gtk_entry_get_text(entry);

      if (! psppire_dict_check_name(var_store->dict, name, TRUE))
	return FALSE;
      
      psppire_dict_insert_variable(var_store->dict, row, name);

      return TRUE;
    }

  /* If the destination cell is outside the current  variables, then
     accept the destination only as the name column of the first blank row
  */
  if ( *new_row > n_vars) 
    return FALSE;
  
  if ( *new_row >= n_vars && *new_column != COL_NAME) 
    return FALSE;

  return TRUE;
}


/* Callback whenever the cell on the var sheet is entered or left.
   It sets the entry box type appropriately.
*/
static gboolean 
var_sheet_cell_change_entry (GtkSheet * sheet, gint row, gint column, 
			     gpointer leaving)
{
  GtkSheetCellAttr attributes;
  PsppireVarStore *var_store ;
  struct PsppireVariable *pv ;

  g_return_val_if_fail(sheet != NULL, FALSE);

  var_store = PSPPIRE_VAR_STORE(gtk_sheet_get_model(sheet));

  if ( row >= psppire_var_store_get_var_cnt(var_store))
    return TRUE;

  if ( leaving ) 
    {
      gtk_sheet_change_entry(sheet, GTK_TYPE_ENTRY);
      return TRUE;
    }


  gtk_sheet_get_attributes(sheet, row, column, &attributes);

  pv = psppire_var_store_get_variable(var_store, row);

  switch (column)
    {
    case COL_ALIGN:
      {
	static GtkListStore *list_store = 0;
	GtkComboBoxEntry *cbe;
	gtk_sheet_change_entry(sheet, GTK_TYPE_COMBO_BOX_ENTRY);
	cbe = 
	  GTK_COMBO_BOX_ENTRY(gtk_sheet_get_entry(sheet)->parent);


	if ( ! list_store) list_store = create_label_list(alignments);

	gtk_combo_box_set_model(GTK_COMBO_BOX(cbe), 
				GTK_TREE_MODEL(list_store));

	gtk_combo_box_entry_set_text_column (cbe, 0);


	g_signal_connect(G_OBJECT(cbe),"changed", 
			 G_CALLBACK(change_alignment), pv);
      }
      break;
    case COL_MEASURE:
      {
	static GtkListStore *list_store = 0;
	GtkComboBoxEntry *cbe;
	gtk_sheet_change_entry(sheet, GTK_TYPE_COMBO_BOX_ENTRY);
	cbe = 
	  GTK_COMBO_BOX_ENTRY(gtk_sheet_get_entry(sheet)->parent);


	if ( ! list_store) list_store = create_label_list(measures);

	gtk_combo_box_set_model(GTK_COMBO_BOX(cbe), 
				GTK_TREE_MODEL(list_store));

	gtk_combo_box_entry_set_text_column (cbe, 0);

	g_signal_connect(G_OBJECT(cbe),"changed", 
			 G_CALLBACK(change_measure), pv);
      }
      break;

    case COL_VALUES:
      {
	static struct val_labs_dialog *val_labs_dialog = 0;

	PsppireCustomEntry *customEntry;

	gtk_sheet_change_entry(sheet, PSPPIRE_CUSTOM_ENTRY_TYPE);

	customEntry = 
	  PSPPIRE_CUSTOM_ENTRY(gtk_sheet_get_entry(sheet));


	if (!val_labs_dialog ) 
	    val_labs_dialog = val_labs_dialog_create(xml);

	val_labs_dialog->pv = pv;

	g_signal_connect_swapped(GTK_OBJECT(customEntry),
				 "clicked",
				 GTK_SIGNAL_FUNC(val_labs_dialog_show),
				 val_labs_dialog);
      }
      break;
    case COL_MISSING:
      {
	static struct missing_val_dialog *missing_val_dialog = 0;
	PsppireCustomEntry *customEntry;
	
	gtk_sheet_change_entry(sheet, PSPPIRE_CUSTOM_ENTRY_TYPE);

	customEntry = 
	  PSPPIRE_CUSTOM_ENTRY(gtk_sheet_get_entry(sheet));

	if (!missing_val_dialog ) 
	    missing_val_dialog = missing_val_dialog_create(xml);

	missing_val_dialog->pv = psppire_var_store_get_variable(var_store, row);

	g_signal_connect_swapped(GTK_OBJECT(customEntry),
				 "clicked",
				 GTK_SIGNAL_FUNC(missing_val_dialog_show),
				 missing_val_dialog);
      }
      break;

    case COL_TYPE:
      {
	static struct var_type_dialog *var_type_dialog = 0;

	PsppireCustomEntry *customEntry;

	gtk_sheet_change_entry(sheet, PSPPIRE_CUSTOM_ENTRY_TYPE);

	customEntry = 
	  PSPPIRE_CUSTOM_ENTRY(gtk_sheet_get_entry(sheet));


	/* Popup the Variable Type dialog box */
	if (!var_type_dialog ) 
	    var_type_dialog = var_type_dialog_create(xml);


	var_type_dialog->pv = pv;

	g_signal_connect_swapped(GTK_OBJECT(customEntry),
				 "clicked",
				 GTK_SIGNAL_FUNC(var_type_dialog_show),
				 var_type_dialog);
      }
      break;
    case COL_WIDTH:
    case COL_DECIMALS:
    case COL_COLUMNS:
      {
	if ( attributes.is_editable) 
	  {
	    gint r_min, r_max;

	    const gchar *s = gtk_sheet_cell_get_text(sheet, row, column);

	    if (!s) 
	      return FALSE;

	    {
	      GtkSpinButton *spinButton ;
	      const gint current_value  = atoi(s);
	      GtkObject *adj ;

	      const struct fmt_spec *fmt = psppire_variable_get_write_spec(pv);
	      switch (column) 
		{
		case COL_WIDTH:
		  r_min = fmt->d + 1;
		  r_max = (psppire_variable_get_type(pv) == ALPHA) ? 255 : 40;
		  break;
		case COL_DECIMALS:
		  r_min = 0 ; 
		  r_max = min(fmt->w - 1, 16);
		  break;
		case COL_COLUMNS:
		  r_min = 1;
		  r_max = 255 ; /* Is this a sensible value ? */
		  break;
		default:
		  g_assert_not_reached();
		}

	      adj = gtk_adjustment_new(current_value,
				       r_min, r_max,
				       1.0, 1.0, 1.0 /* steps */
				       );

	      gtk_sheet_change_entry(sheet, GTK_TYPE_SPIN_BUTTON);

	      spinButton = 
		GTK_SPIN_BUTTON(gtk_sheet_get_entry(sheet));

	      gtk_spin_button_set_adjustment(spinButton, GTK_ADJUSTMENT(adj));
	      gtk_spin_button_set_digits(spinButton, 0);
	    }
	  }
      }
      break; 

    default:
      gtk_sheet_change_entry(sheet, GTK_TYPE_ENTRY);
      break;
    }

  return TRUE;
}



/* Create the var sheet */
GtkWidget*
psppire_variable_sheet_create (gchar *widget_name, 
			       gchar *string1, 
			       gchar *string2,
			       gint int1, gint int2)
{
  gint i;
  GtkWidget *sheet;

  GObject *geo = g_sheet_hetero_column_new(75, n_COLS);
  GObject *row_geometry = g_sheet_uniform_row_new(25, n_initial_rows); 



  sheet = gtk_sheet_new(G_SHEET_ROW(row_geometry),
			G_SHEET_COLUMN(geo), 
			"variable sheet", 0); 

  g_signal_connect (GTK_OBJECT (sheet), "activate",
		    GTK_SIGNAL_FUNC (var_sheet_cell_change_entry),
		    0);

  g_signal_connect (GTK_OBJECT (sheet), "deactivate",
		    GTK_SIGNAL_FUNC (var_sheet_cell_change_entry),
		    (void *) 1);

  g_signal_connect (GTK_OBJECT (sheet), "traverse",
		    GTK_SIGNAL_FUNC (traverse_cell_callback), 0);


  g_signal_connect (GTK_OBJECT (sheet), "double-click-row",
		    GTK_SIGNAL_FUNC (click2row),
		    sheet);

  for (i = 0 ; i < n_COLS ; ++i ) 
    {
      g_sheet_hetero_column_set_button_label(G_SHEET_HETERO_COLUMN(geo), i, 
					       column_def[i].label);

      g_sheet_hetero_column_set_width(G_SHEET_HETERO_COLUMN(geo), i, 
					       column_def[i].width);
    }

  gtk_widget_show(sheet);

  return sheet;
}


