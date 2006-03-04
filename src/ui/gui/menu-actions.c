/* 
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2004, 2005  Free Software Foundation
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
#include <file-handle-def.h>
#include <sys-file-reader.h>
#include <case.h>

#include <glade/glade.h>
#include <gtk/gtk.h>

#include <gtksheet.h>
#include "helper.h"
#include "menu-actions.h"
#include "psppire-variable.h"
#include "psppire-dict.h"

#include "var-sheet.h"
#include "data-sheet.h"

#include "psppire-var-store.h"
#include "psppire-data-store.h"

#define _(A) A
#define N_(A) A


extern GladeXML *xml;


extern PsppireDict *the_dictionary ;
extern PsppireCaseArray *the_cases ;


static struct file_handle *psppire_handle = 0;

static const gchar handle_name[] = "psppire_handle";

static const gchar untitled[] = _("Untitled");

static const gchar window_title[]=_("PSPP Data Editor");


static void
psppire_set_window_title(const gchar *text)
{
  GtkWidget *data_editor = get_widget_assert(xml, "data_editor");
  
  gchar *title = g_strdup_printf("%s --- %s", text, window_title);

  gtk_window_set_title(GTK_WINDOW(data_editor), title);

  g_free(title);
}

void
on_new1_activate                       (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  psppire_dict_clear(the_dictionary);
  psppire_case_array_clear(the_cases);

  psppire_set_window_title(untitled);

  if (psppire_handle)
    fh_free(psppire_handle);
  psppire_handle = 0 ;
}

static gboolean
populate_case_from_reader(struct ccase *c, gpointer aux)
{
  struct sfm_reader *reader = aux;

  return sfm_read_case(reader, c);
}


void
on_open1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  GtkWidget *dialog;
  GtkWidget *data_editor  = get_widget_assert(xml, "data_editor");
 
  dialog = gtk_file_chooser_dialog_new (_("Open"),
					GTK_WINDOW(data_editor),
					GTK_FILE_CHOOSER_ACTION_OPEN,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					NULL);
  GtkFileFilter *filter ;

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


  bool finished = FALSE;
  do {

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
      {
	GtkWidget *data_sheet = get_widget_assert(xml, "data_sheet");
	g_assert(data_sheet);

	GtkWidget *var_sheet = get_widget_assert(xml, "variable_sheet");
	g_assert(var_sheet);

	char *filename = gtk_file_chooser_get_filename
	  (GTK_FILE_CHOOSER (dialog));

	if ( psppire_handle ) 
	  fh_free(psppire_handle);

	psppire_handle = 
	  fh_create_file (handle_name, filename, fh_default_properties());

	if ( !psppire_handle ) 
	  {
	    g_warning("Cannot read handle for reading system file \"%s\"\n", 
		      filename);
	    continue;
	  }

	struct dictionary *new_dict;
	struct sfm_read_info ri;
	struct sfm_reader *reader ; 

	reader = sfm_open_reader (psppire_handle, &new_dict, &ri);
      
	if ( ! reader ) 
	  continue;

	the_dictionary = psppire_dict_new_from_dict(new_dict);

	PsppireVarStore *var_store = 
	  PSPPIRE_VAR_STORE(gtk_sheet_get_model(GTK_SHEET(var_sheet)));
	
	psppire_var_store_set_dictionary(var_store, the_dictionary);


	PsppireDataStore *data_store = 
	  PSPPIRE_DATA_STORE(gtk_sheet_get_model(GTK_SHEET(data_sheet)));
	

	psppire_data_store_set_dictionary(data_store,
					  the_dictionary);

	psppire_case_array_clear(data_store->cases);


	psppire_set_window_title(basename(filename));

	g_free (filename);

	const int ni = dict_get_next_value_idx(the_dictionary->dict);
	if ( ni == 0 ) 
	  goto done;
      
	gint case_num;
	for(case_num=0;;case_num++)
	  {
	    if (!psppire_case_array_add_case(the_cases, 
				     populate_case_from_reader, reader))
	      break;
	  }

	sfm_close_reader(reader);
	finished = TRUE;
      }
    else
      {
	finished = TRUE;
      }
  } while ( ! finished ) ;

 done:
  gtk_widget_destroy (dialog);
}


/* Re initialise HANDLE, by interrogating the user for a new file name */
static void
recreate_save_handle(struct file_handle **handle)
{
  GtkWidget *dialog;

  GtkWidget *data_editor  = get_widget_assert(xml, "data_editor");

  dialog = gtk_file_chooser_dialog_new (_("Save Data As"),
					GTK_WINDOW(data_editor),
					GTK_FILE_CHOOSER_ACTION_SAVE,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
					NULL);

  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
      char *filename = gtk_file_chooser_get_filename
	(GTK_FILE_CHOOSER (dialog));

#if 0
      if ( *handle ) 
	destroy_file_handle(*handle, 0);
#endif
      *handle = fh_create_file (handle_name, filename, fh_default_properties());

      psppire_set_window_title(basename(filename));

      g_free (filename);
    }

  gtk_widget_destroy (dialog);
}

void
on_save1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  if ( ! psppire_handle ) 
    recreate_save_handle(&psppire_handle);
  
  GtkSheet *data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));
  PsppireDataStore *data_store = PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));
  
  if ( psppire_handle ) 
    psppire_data_store_create_system_file(data_store,
				       psppire_handle);
}


void
on_save_as1_activate                   (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  recreate_save_handle(&psppire_handle);
  if ( ! psppire_handle ) 
    return ;

  GtkSheet *data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));
  PsppireDataStore *data_store = PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));

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
on_cut1_activate                       (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_copy1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_paste1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_insert1_activate                    (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  GtkNotebook *notebook = GTK_NOTEBOOK(get_widget_assert(xml, "notebook1"));
  gint page = -1;

  page = gtk_notebook_get_current_page(notebook);

  switch (page) 
    {
    case PAGE_DATA_SHEET:
      {
	GtkSheet *data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));
	PsppireDataStore *data_store = 
	  PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));

	psppire_case_array_insert_case(data_store->cases, data_sheet->range.row0);
      }
      break;
    case PAGE_VAR_SHEET:
      {
	GtkSheet *var_sheet = 
	  GTK_SHEET(get_widget_assert(xml, "variable_sheet"));

	PsppireVarStore *var_store = 
	  PSPPIRE_VAR_STORE(gtk_sheet_get_model(var_sheet));

	psppire_dict_insert_variable(var_store->dict, var_sheet->range.row0, 0);
      }
      break;
    }
}

void
on_delete1_activate                    (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  gint page = -1;
  GtkWidget *notebook = get_widget_assert(xml, "notebook1");

  page = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
  switch ( page) 
    {
    case PAGE_DATA_SHEET:
      {
	GtkSheet *data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));
	PsppireDataStore *data_store = 
	  PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));

	psppire_case_array_delete_cases(data_store->cases, 
				    data_sheet->range.row0, 
				    1 + data_sheet->range.rowi 
				    - data_sheet->range.row0  );
      }
      break;
    case PAGE_VAR_SHEET:
      {
	GtkSheet *var_sheet = 
	  GTK_SHEET(get_widget_assert(xml, "variable_sheet"));

	PsppireVarStore *var_store = 
	  PSPPIRE_VAR_STORE(gtk_sheet_get_model(var_sheet));

	psppire_dict_delete_variables(var_store->dict, 
				   var_sheet->range.row0,
				   1 + var_sheet->range.rowi 
				   - var_sheet->range.row0  );
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



void
on_toolbars1_activate
                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{


}

void
on_value_labels1_activate(GtkCheckMenuItem     *menuitem,
			  gpointer         user_data)
{
  GtkSheet *data_sheet = GTK_SHEET(get_widget_assert(xml, "data_sheet"));
  PsppireDataStore *ds = PSPPIRE_DATA_STORE(gtk_sheet_get_model(data_sheet));
		
  psppire_data_store_show_labels(ds, 
			      gtk_check_menu_item_get_active(menuitem));
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
  switch (page) 
    {
    case PAGE_VAR_SHEET:
      gtk_widget_hide(menuitems[PAGE_VAR_SHEET]);
      gtk_widget_show(menuitems[PAGE_DATA_SHEET]);
      break;
    case PAGE_DATA_SHEET:
      gtk_widget_show(menuitems[PAGE_VAR_SHEET]);
      gtk_widget_hide(menuitems[PAGE_DATA_SHEET]);
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

static void
var_data_selection_init()
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


/* Callback which occurs when gtk_main is entered */
gboolean
callbacks_on_init(gpointer data)
{
  psppire_set_window_title(untitled);

  var_data_selection_init();

  return FALSE;
}
