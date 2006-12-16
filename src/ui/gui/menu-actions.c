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

#include <config.h>
#include <stdlib.h>
#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include <math/sort.h>

#include <data/casefile.h>
#include <data/file-handle-def.h>
#include <data/sys-file-reader.h>
#include <data/case.h>
#include <data/variable.h>

#include <glade/glade.h>
#include <gtk/gtk.h>

#include <libpspp/str.h>

#include <gtksheet/gtksheet.h>
#include "helper.h"
#include "menu-actions.h"

#include "psppire-dict.h"

#include "var-sheet.h"
#include "data-sheet.h"

#include "psppire-var-store.h"
#include "psppire-data-store.h"

#include "sort-cases-dialog.h"


extern GladeXML *xml;


extern PsppireDict *the_dictionary ;

static struct file_handle *psppire_handle = 0;

static const gchar handle_name[] = "psppire_handle";

static const gchar untitled[] = N_("Untitled");

static const gchar window_title[] = N_("PSPP Data Editor");


/* Sets the title bar to TEXT */
static void
psppire_set_window_title(const gchar *text)
{
  GtkWidget *data_editor = get_widget_assert(xml, "data_editor");

  gchar *title = g_strdup_printf("%s --- %s", text, gettext(window_title));

  gtk_window_set_title(GTK_WINDOW(data_editor), title);

  g_free(title);
}

/* Clear the active file and set the data and var sheets to
   reflect this.
 */
gboolean
clear_file(void)
{
  PsppireDataStore *data_store ;
  GtkSheet *data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));
  GtkSheet *var_sheet = GTK_SHEET(get_widget_assert(xml, "variable_sheet"));

  gtk_sheet_set_active_cell(data_sheet, -1, -1);
  gtk_sheet_set_active_cell(var_sheet, 0, 0);

  if ( GTK_WIDGET_REALIZED(GTK_WIDGET(data_sheet)))
    gtk_sheet_unselect_range(data_sheet);

  if ( GTK_WIDGET_REALIZED(GTK_WIDGET(var_sheet)))
    gtk_sheet_unselect_range(var_sheet);

  gtk_sheet_moveto(data_sheet, 0, 0, 0.0, 0.0);
  gtk_sheet_moveto(var_sheet,  0, 0, 0.0, 0.0);

  data_store = PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));

  psppire_data_store_clear(data_store);

  psppire_set_window_title(gettext(untitled));

  if (psppire_handle)
    fh_free(psppire_handle);
  psppire_handle = 0 ;

  return TRUE;
}

void
on_new1_activate                       (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  clear_file();
}



/* Load a system file.
   Return TRUE if successfull
*/
gboolean
load_system_file(const gchar *file_name)
{
  int var_cnt ;

  PsppireVarStore *var_store ;
  PsppireDataStore *data_store ;
  struct dictionary *new_dict;
  struct sfm_read_info ri;
  struct sfm_reader *reader ;

  GtkWidget *data_sheet = get_widget_assert(xml, "data_sheet");
  GtkWidget *var_sheet = get_widget_assert(xml, "variable_sheet");

  g_assert(data_sheet);
  g_assert(var_sheet);

  clear_file();

  psppire_handle =
    fh_create_file (handle_name, file_name, fh_default_properties());

  if ( !psppire_handle )
    {
      g_warning("Cannot read handle for reading system file \"%s\"\n",
		file_name);
      return FALSE;
    }

  reader = sfm_open_reader (psppire_handle, &new_dict, &ri);

  if ( ! reader )
    return FALSE;

  /* FIXME: We need a better way of updating a dictionary than this */
  the_dictionary = psppire_dict_new_from_dict(new_dict);

  var_store =
    PSPPIRE_VAR_STORE(gtk_sheet_get_model(GTK_SHEET(var_sheet)));

  psppire_var_store_set_dictionary(var_store, the_dictionary);


  data_store =
    PSPPIRE_DATA_STORE(gtk_sheet_get_model(GTK_SHEET(data_sheet)));

  psppire_data_store_set_dictionary(data_store,
				    the_dictionary);

  psppire_set_window_title(basename(file_name));

  var_cnt = dict_get_next_value_idx(the_dictionary->dict);
  if ( var_cnt == 0 )
    return FALSE;


  for(;;)
    {
      struct ccase c;
      case_create(&c, var_cnt);
      if ( 0 == sfm_read_case (reader, &c) )
	{
	  case_destroy(&c);
	  break;
	}

      if ( !psppire_case_file_append_case(data_store->case_file, &c) )
	{
	  g_warning("Cannot write case to casefile\n");
	  break;
	}
      case_destroy(&c);
    }

  sfm_close_reader(reader);

  psppire_case_file_get_case_count(data_store->case_file);

  return TRUE;
}

void
open_data                      (GtkMenuItem     *menuitem,
				gpointer         user_data)
{
  bool finished = FALSE;

  GtkWidget *dialog;
  GtkWidget *data_editor  = get_widget_assert(xml, "data_editor");
  GtkFileFilter *filter ;

  dialog = gtk_file_chooser_dialog_new (_("Open"),
					GTK_WINDOW(data_editor),
					GTK_FILE_CHOOSER_ACTION_OPEN,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					NULL);

  filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, _("System Files (*.sav)"));
  gtk_file_filter_add_pattern(filter, "*.sav");
  gtk_file_filter_add_pattern(filter, "*.SAV");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

  filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, _("Portable Files (*.por) "));
  gtk_file_filter_add_pattern(filter, "*.por");
  gtk_file_filter_add_pattern(filter, "*.POR");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

  filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, _("All Files"));
  gtk_file_filter_add_pattern(filter, "*");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

  do {

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
      {
	gchar *file_name =
	  gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));

	finished =  load_system_file(file_name) ;

	g_free(file_name);
      }
    else
      finished = TRUE;

  } while ( ! finished ) ;

  gtk_widget_destroy (dialog);
}


void
on_data3_activate                      (GtkMenuItem     *menuitem,
					gpointer         user_data)
{
  open_data(menuitem, user_data);
}

void
on_data5_activate                      (GtkMenuItem     *menuitem,
					gpointer         user_data)
{
  open_data(menuitem, user_data);
}


/* Re initialise HANDLE, by interrogating the user for a new file name */
static gboolean
recreate_save_handle(struct file_handle **handle)
{
  gint response;
  GtkWidget *dialog;

  GtkWidget *data_editor  = get_widget_assert(xml, "data_editor");

  dialog = gtk_file_chooser_dialog_new (_("Save Data As"),
					GTK_WINDOW(data_editor),
					GTK_FILE_CHOOSER_ACTION_SAVE,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
					NULL);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response == GTK_RESPONSE_ACCEPT)
    {
      char *file_name = gtk_file_chooser_get_filename
	(GTK_FILE_CHOOSER (dialog));


      if ( *handle )
	fh_free(*handle);

      *handle = fh_create_file (handle_name, file_name,
		      	fh_default_properties());

      psppire_set_window_title(basename(file_name));

      g_free (file_name);
    }

  gtk_widget_destroy (dialog);

  return ( response == GTK_RESPONSE_ACCEPT ) ;
}

void
on_save1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  GtkSheet *data_sheet ;
  PsppireDataStore *data_store ;

  if ( ! psppire_handle )
    {
      if ( ! recreate_save_handle(&psppire_handle) )
	return;
    }

  data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));
  data_store = PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));

  if ( psppire_handle )
    psppire_data_store_create_system_file(data_store,
				       psppire_handle);
}


void
on_save_as1_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  GtkSheet *data_sheet ;
  PsppireDataStore *data_store ;

  if ( ! recreate_save_handle(&psppire_handle) )
    return ;

  if ( ! psppire_handle )
    return ;

  data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));
  data_store = PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));

  if ( psppire_handle )
    psppire_data_store_create_system_file(data_store,
				       psppire_handle);
}


void
on_quit1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  gtk_main_quit();
}


void
on_clear_activate                    (GtkMenuItem     *menuitem,
				      gpointer         user_data)
{
  GtkNotebook *notebook = GTK_NOTEBOOK(get_widget_assert(xml, "notebook1"));
  gint page = -1;

  page = gtk_notebook_get_current_page(notebook);

  switch (page)
    {
    case PAGE_VAR_SHEET:
	    break;
    case PAGE_DATA_SHEET:
      {
	GtkSheet *data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));
	PsppireDataStore *data_store =
	  PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));


	switch ( data_sheet->state )
	  {
	  case GTK_SHEET_ROW_SELECTED:
	    psppire_case_file_delete_cases(data_store->case_file,
					   data_sheet->range.rowi
					   - data_sheet->range.row0 + 1,
					   data_sheet->range.row0);
	    break;
	  case GTK_SHEET_COLUMN_SELECTED:
	    {
	      gint fv;
	      struct variable *pv =
		psppire_dict_get_variable (the_dictionary,
					   data_sheet->range.col0);

	      fv = var_get_case_index (pv);

	      psppire_dict_delete_variables (the_dictionary,
					     data_sheet->range.col0,
					     1);

	      psppire_case_file_insert_values (data_store->case_file,
					       -1, fv);
	    }
	    break;
	  default:
	    gtk_sheet_cell_clear (data_sheet,
				  data_sheet->active_cell.row,
				  data_sheet->active_cell.col);
	    break;
	  }

      }
      break;
    }

}

void
on_about1_activate(GtkMenuItem     *menuitem,
		   gpointer         user_data)
{
  GtkWidget *about =  get_widget_assert(xml, "aboutdialog1");


  GdkPixbuf *pb  = gdk_pixbuf_new_from_file_at_size( "pspplogo.png", 64, 64, 0);

  gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(about), pb);

  gtk_widget_show(about);

  gtk_window_set_transient_for(GTK_WINDOW(about),
                               GTK_WINDOW(get_widget_assert(xml, "data_editor")));
}


/* Set the value labels state from the toolbar's toggle button */
void
on_togglebutton_value_labels_toggled(GtkToggleToolButton *toggle_tool_button,
				     gpointer             user_data)
{
  GtkSheet *data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));
  GtkCheckMenuItem *item =
    GTK_CHECK_MENU_ITEM(get_widget_assert(xml, "menuitem-value-labels"));

  PsppireDataStore *ds = PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));

  gboolean show_value_labels = gtk_toggle_tool_button_get_active(toggle_tool_button);

  gtk_check_menu_item_set_active(item, show_value_labels);

  psppire_data_store_show_labels(ds, show_value_labels);
}

/* Set the value labels state from the view menu */
void
on_value_labels_activate(GtkCheckMenuItem     *menuitem,
			  gpointer         user_data)
{
  GtkSheet *data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));
  GtkToggleToolButton *tb =
   GTK_TOGGLE_TOOL_BUTTON(get_widget_assert(xml, "togglebutton-value-labels"));

  PsppireDataStore *ds = PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));

  gboolean show_value_labels = gtk_check_menu_item_get_active(menuitem);

  gtk_toggle_tool_button_set_active(tb, show_value_labels);

  psppire_data_store_show_labels(ds, show_value_labels);
}

void
on_status_bar1_activate(GtkCheckMenuItem     *menuitem,
 gpointer         user_data)
{

  if ( gtk_check_menu_item_get_active(menuitem) )
    gtk_widget_show(get_widget_assert(xml, "statusbar1"));
  else
    gtk_widget_hide(get_widget_assert(xml, "statusbar1"));
}

void
on_grid_lines1_activate(GtkCheckMenuItem     *menuitem,
 gpointer         user_data)
{

  const bool grid_visible = gtk_check_menu_item_get_active(menuitem);

  gtk_sheet_show_grid(GTK_SHEET(get_widget_assert(xml, "variable_sheet")),
		      grid_visible);

  gtk_sheet_show_grid(GTK_SHEET(get_widget_assert(xml, "data_sheet")),
		      grid_visible);
}


void
on_fonts1_activate(GtkMenuItem     *menuitem,
 gpointer         user_data)
{
  static GtkWidget *dialog = 0 ;
  if ( !dialog )
    dialog   = gtk_font_selection_dialog_new(_("Font Selection"));

  gtk_window_set_transient_for(GTK_WINDOW(dialog),
                               GTK_WINDOW(get_widget_assert(xml, "data_editor")));


  if ( GTK_RESPONSE_OK == gtk_dialog_run(GTK_DIALOG(dialog)) )
    {
      GtkSheet *data_sheet =
	GTK_SHEET(get_widget_assert(xml, "data_sheet"));

      GtkSheet *var_sheet =
	GTK_SHEET(get_widget_assert(xml, "variable_sheet"));

      PsppireDataStore *ds = PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));
      PsppireVarStore *vs = PSPPIRE_VAR_STORE(gtk_sheet_get_model(var_sheet));

      const gchar *font = gtk_font_selection_dialog_get_font_name
	(GTK_FONT_SELECTION_DIALOG(dialog));

      PangoFontDescription* font_desc =
	pango_font_description_from_string(font);

      psppire_var_store_set_font(vs, font_desc);
      psppire_data_store_set_font(ds, font_desc);
    }

  gtk_widget_hide(dialog);

}


static GtkWidget *menuitems[2];
static GtkNotebook *notebook = 0;

static void
switch_menus(gint page)
{
  GtkWidget *insert_variable = get_widget_assert(xml, "insert-variable");
  GtkWidget *insert_cases = get_widget_assert(xml, "insert-cases");

  switch (page)
    {
    case PAGE_VAR_SHEET:
      gtk_widget_hide(menuitems[PAGE_VAR_SHEET]);
      gtk_widget_show(menuitems[PAGE_DATA_SHEET]);
      gtk_widget_set_sensitive(insert_variable, TRUE);
      gtk_widget_set_sensitive(insert_cases, FALSE);
      break;
    case PAGE_DATA_SHEET:
      gtk_widget_show(menuitems[PAGE_VAR_SHEET]);
      gtk_widget_hide(menuitems[PAGE_DATA_SHEET]);
      gtk_widget_set_sensitive(insert_variable, FALSE);
      gtk_widget_set_sensitive(insert_cases, TRUE);
      break;
    default:
      g_assert_not_reached();
      break;
    }
}


void
select_sheet(gint page)
{
  gtk_notebook_set_current_page(notebook, page);
  switch_menus(page);
}



static void
data_var_select(GtkNotebook *notebook,
		GtkNotebookPage *page,
		guint page_num,
		gpointer user_data)
{
  switch_menus(page_num);
}


/* Initialised things on the variable sheet */
void
var_data_selection_init(void)
{
  notebook = GTK_NOTEBOOK(get_widget_assert(xml, "notebook1"));
  menuitems[PAGE_DATA_SHEET] = get_widget_assert(xml, "data1");
  menuitems[PAGE_VAR_SHEET] = get_widget_assert(xml, "variables1");

  gtk_notebook_set_current_page(notebook, PAGE_DATA_SHEET);
  gtk_widget_hide(menuitems[PAGE_DATA_SHEET]);
  gtk_widget_show(menuitems[PAGE_VAR_SHEET]);


  g_signal_connect(G_OBJECT(notebook), "switch-page",
		   G_CALLBACK(data_var_select), 0);

}


void
on_data1_activate(GtkMenuItem     *menuitem,
		  gpointer         user_data)
{
  select_sheet(PAGE_DATA_SHEET);
}


void
on_variables1_activate(GtkMenuItem     *menuitem,
		  gpointer         user_data)
{
  select_sheet(PAGE_VAR_SHEET);
}



void
on_go_to_case_activate(GtkMenuItem     *menuitem,
		       gpointer         user_data)
{
  GtkWidget *dialog = get_widget_assert(xml, "go_to_case_dialog");
  GtkEntry *entry = GTK_ENTRY(get_widget_assert(xml, "entry_go_to_case"));
  GtkSheet *data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));

  gint result = gtk_dialog_run(GTK_DIALOG(dialog));



  switch (result)
    {
    case GTK_RESPONSE_OK:
      {
	gint row, column;
	const gchar *text = gtk_entry_get_text(entry);
	gint casenum = g_strtod(text, NULL);

	gtk_sheet_get_active_cell(data_sheet, &row, &column);
	if ( column < 0 ) column = 0;
	if ( row < 0 ) row = 0;

	gtk_sheet_set_active_cell(data_sheet, casenum, column);
      }
      break;
    default:
      break;
    }

  gtk_widget_hide(dialog);
  gtk_entry_set_text(entry, "");
}



void
on_sort_cases_activate (GtkMenuItem     *menuitem,
			gpointer         user_data)
{
  gint response;
  PsppireDataStore *data_store ;

  struct sort_criteria criteria;
  static struct sort_cases_dialog *dialog ;

  GtkSheet *data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));

  data_store = PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));

  if ( NULL == dialog)
    dialog = sort_cases_dialog_create(xml);

  response = sort_cases_dialog_run(dialog, the_dictionary, &criteria);

  switch ( response)
    {
    case GTK_RESPONSE_OK:
      psppire_case_file_sort(data_store->case_file, &criteria);
      break;
    }
}


static void
insert_case(void)
{
  gint row, col;
  PsppireDataStore *data_store ;
  GtkSheet *data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));

  data_store = PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));

  gtk_sheet_get_active_cell(data_sheet, &row, &col);

  psppire_data_store_insert_new_case(data_store, row);
}

void
on_insert_case_clicked (GtkButton *button, gpointer user_data)
{
  insert_case();
}

void
on_insert_cases (GtkMenuItem *menuitem, gpointer user_data)
{
  insert_case();
}


void
on_insert_variable (GtkMenuItem *menuitem, gpointer user_data)
{
  gint row, col;
  GtkSheet *var_sheet = GTK_SHEET(get_widget_assert(xml, "variable_sheet"));

  gtk_sheet_get_active_cell(var_sheet, &row, &col);

  psppire_dict_insert_variable(the_dictionary, row, NULL);
}


