/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2006  Free Software Foundation

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

#include <glade/glade.h>
#include <gtk/gtk.h>


#include <gtksheet/gtksheet.h>

#include "helper.h"
#include "about.h"

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include "data-editor.h"
#include "syntax-editor.h"
#include "window-manager.h"

#include "psppire-data-store.h"
#include "psppire-var-store.h"


/* Switch between the VAR SHEET and the DATA SHEET */
enum {PAGE_DATA_SHEET = 0, PAGE_VAR_SHEET};

static gboolean click2column (GtkWidget *w, gint col, gpointer data);

static gboolean click2row (GtkWidget *w, gint row, gpointer data);


static void select_sheet (struct data_editor *de, guint page_num);


static void data_var_select (GtkNotebook *notebook,
			    GtkNotebookPage *page,
			    guint page_num,
			    gpointer user_data);

static void status_bar_activate (GtkCheckMenuItem *, gpointer);

static void grid_lines_activate (GtkCheckMenuItem *, gpointer);

static void data_sheet_activate (GtkCheckMenuItem *, gpointer);

static void variable_sheet_activate (GtkCheckMenuItem *, gpointer );

static void fonts_activate (GtkMenuItem *, gpointer);

static void value_labels_activate (GtkCheckMenuItem *, gpointer);
static void value_labels_toggled (GtkToggleToolButton *, gpointer);


static void file_quit (GtkCheckMenuItem *, gpointer );

static void on_clear_activate (GtkMenuItem *, gpointer);

static void
enable_edit_clear (GtkWidget *w, gint row, gpointer data)
{
  struct data_editor *de = data;

  GtkWidget *menuitem = get_widget_assert (de->xml, "edit_clear");

  gtk_widget_set_sensitive (menuitem, TRUE);
}

static gboolean
disable_edit_clear (GtkWidget *w, gint x, gint y, gpointer data)
{
  struct data_editor *de = data;

  GtkWidget *menuitem = get_widget_assert (de->xml, "edit_clear");

  gtk_widget_set_sensitive (menuitem, FALSE);

  return FALSE;
}



/*
  Create a new data editor.
*/
struct data_editor *
new_data_editor (void)
{
  struct data_editor *de ;
  struct editor_window *e;

  de = g_malloc (sizeof (*de));

  e = (struct editor_window *) de;

  de->xml = glade_xml_new (PKGDATADIR "/data-editor.glade", NULL, NULL);

  connect_help (de->xml);

  e->window = get_widget_assert (de->xml, "data_editor");

  g_signal_connect (get_widget_assert (de->xml,"file_new_data"),
		    "activate",
		    G_CALLBACK (new_data_window),
		    e->window);

  g_signal_connect (get_widget_assert (de->xml,"file_open_data"),
		    "activate",
		    G_CALLBACK (open_data_window),
		    e->window);

  g_signal_connect (get_widget_assert (de->xml,"file_new_syntax"),
		    "activate",
		    G_CALLBACK (new_syntax_window),
		    e->window);

  g_signal_connect (get_widget_assert (de->xml,"file_open_syntax"),
		    "activate",
		    G_CALLBACK (open_syntax_window),
		    e->window);


  g_signal_connect (get_widget_assert (de->xml,"edit_clear"),
		    "activate",
		    G_CALLBACK (on_clear_activate),
		    de);



  g_signal_connect (get_widget_assert (de->xml,"help_about"),
		    "activate",
		    G_CALLBACK (about_new),
		    e->window);


  g_signal_connect (get_widget_assert (de->xml,"help_reference"),
		    "activate",
		    G_CALLBACK (reference_manual),
		    e->window);



  g_signal_connect (get_widget_assert (de->xml,"data_sheet"),
		    "double-click-column",
		    G_CALLBACK (click2column),
		    de);


  g_signal_connect (get_widget_assert (de->xml, "variable_sheet"),
		    "double-click-row",
		    GTK_SIGNAL_FUNC (click2row),
		    de);


  g_signal_connect (get_widget_assert (de->xml, "variable_sheet"),
		    "select-row",
		    GTK_SIGNAL_FUNC (enable_edit_clear),
		    de);

  g_signal_connect (get_widget_assert (de->xml, "variable_sheet"),
		    "activate",
		    GTK_SIGNAL_FUNC (disable_edit_clear),
		    de);


  g_signal_connect (get_widget_assert (de->xml, "notebook"),
		    "switch-page",
		    G_CALLBACK (data_var_select), de);



  g_signal_connect (get_widget_assert (de->xml, "view_statusbar"),
		    "activate",
		    G_CALLBACK (status_bar_activate), de);


  g_signal_connect (get_widget_assert (de->xml, "view_gridlines"),
		    "activate",
		    G_CALLBACK (grid_lines_activate), de);



  g_signal_connect (get_widget_assert (de->xml, "view_data"),
		    "activate",
		    G_CALLBACK (data_sheet_activate), de);

  g_signal_connect (get_widget_assert (de->xml, "view_variables"),
		    "activate",
		    G_CALLBACK (variable_sheet_activate), de);



  g_signal_connect (get_widget_assert (de->xml, "view_fonts"),
		    "activate",
		    G_CALLBACK (fonts_activate), de);



  g_signal_connect (get_widget_assert (de->xml, "view_valuelabels"),
		    "activate",
		    G_CALLBACK (value_labels_activate), de);


  g_signal_connect (get_widget_assert (de->xml, "togglebutton-value-labels"),
		    "toggled",
		    G_CALLBACK (value_labels_toggled), de);


  g_signal_connect (get_widget_assert (de->xml, "file_quit"),
		    "activate",
		    G_CALLBACK (file_quit), de);


  select_sheet (de, PAGE_DATA_SHEET);

  return de;
}


/* Callback which occurs when the var sheet's row title
   button is double clicked */
static gboolean
click2row (GtkWidget *w, gint row, gpointer data)
{
  struct data_editor *de = data;

  gint current_row, current_column;

  GtkWidget *data_sheet  = get_widget_assert (de->xml, "data_sheet");

  data_editor_select_sheet (de, PAGE_DATA_SHEET);

  gtk_sheet_get_active_cell (GTK_SHEET (data_sheet),
			     &current_row, &current_column);

  gtk_sheet_set_active_cell (GTK_SHEET (data_sheet), current_row, row);

  return FALSE;
}


/* Callback which occurs when the data sheet's column title
   is double clicked */
static gboolean
click2column (GtkWidget *w, gint col, gpointer data)
{
  struct data_editor *de = data;

  gint current_row, current_column;

  GtkWidget *var_sheet  = get_widget_assert (de->xml, "variable_sheet");

  data_editor_select_sheet (de, PAGE_VAR_SHEET);

  gtk_sheet_get_active_cell (GTK_SHEET (var_sheet),
			     &current_row, &current_column);

  gtk_sheet_set_active_cell (GTK_SHEET (var_sheet), col, current_column);

  return FALSE;
}




void
new_data_window (GtkMenuItem *menuitem, gpointer parent)
{
  window_create (WINDOW_DATA, NULL);
}


static void
select_sheet (struct data_editor *de, guint page_num)
{
  GtkWidget *insert_variable = get_widget_assert (de->xml, "insert-variable");
  GtkWidget *insert_cases = get_widget_assert (de->xml, "insert-cases");

  GtkWidget *view_data = get_widget_assert (de->xml, "view_data");
  GtkWidget *view_variables = get_widget_assert (de->xml, "view_variables");

  switch (page_num)
    {
    case PAGE_VAR_SHEET:
      gtk_widget_hide (view_variables);
      gtk_widget_show (view_data);
      gtk_widget_set_sensitive (insert_variable, TRUE);
      gtk_widget_set_sensitive (insert_cases, FALSE);
      break;
    case PAGE_DATA_SHEET:
      gtk_widget_show (view_variables);
      gtk_widget_hide (view_data);
      gtk_widget_set_sensitive (insert_variable, FALSE);
      gtk_widget_set_sensitive (insert_cases, TRUE);
      break;
    default:
      g_assert_not_reached ();
      break;
    }
}


static void
data_var_select (GtkNotebook *notebook,
		GtkNotebookPage *page,
		guint page_num,
		gpointer user_data)
{
  struct data_editor *de = user_data;

  select_sheet (de, page_num);
}




void
data_editor_select_sheet (struct data_editor *de, gint page)
{
  gtk_notebook_set_current_page
   (
    GTK_NOTEBOOK (get_widget_assert (de->xml,"notebook")), page
    );
}


void
open_data_window (GtkMenuItem *menuitem, gpointer parent)
{
  bool finished = FALSE;

  GtkWidget *dialog;

  GtkFileFilter *filter ;

  dialog = gtk_file_chooser_dialog_new (_("Open"),
					GTK_WINDOW (parent),
					GTK_FILE_CHOOSER_ACTION_OPEN,
					GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					NULL);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("System Files (*.sav)"));
  gtk_file_filter_add_pattern (filter, "*.sav");
  gtk_file_filter_add_pattern (filter, "*.SAV");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Portable Files (*.por) "));
  gtk_file_filter_add_pattern (filter, "*.por");
  gtk_file_filter_add_pattern (filter, "*.POR");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  do {

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
      {
	gchar *file_name =
	  gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

	g_free (file_name);
      }
    else
      finished = TRUE;

  } while ( ! finished ) ;

  gtk_widget_destroy (dialog);
}




static void
status_bar_activate (GtkCheckMenuItem *menuitem, gpointer data)
{
  struct data_editor *de = data;
  GtkWidget *statusbar = get_widget_assert (de->xml, "statusbar");

  if ( gtk_check_menu_item_get_active (menuitem) )
    gtk_widget_show (statusbar);
  else
    gtk_widget_hide (statusbar);
}




static void
grid_lines_activate (GtkCheckMenuItem *menuitem, gpointer data)
{
  struct data_editor *de = data;
  const bool grid_visible = gtk_check_menu_item_get_active (menuitem);

  gtk_sheet_show_grid (GTK_SHEET (get_widget_assert (de->xml,
						     "variable_sheet")),
		       grid_visible);

  gtk_sheet_show_grid (GTK_SHEET (get_widget_assert (de->xml, "data_sheet")),
		       grid_visible);
}



static void
data_sheet_activate (GtkCheckMenuItem *menuitem, gpointer data)
{
  struct data_editor *de = data;

  data_editor_select_sheet (de, PAGE_DATA_SHEET);
}


static void
variable_sheet_activate (GtkCheckMenuItem *menuitem, gpointer data)
{
  struct data_editor *de = data;

  data_editor_select_sheet (de, PAGE_VAR_SHEET);
}


static void
fonts_activate (GtkMenuItem *menuitem, gpointer data)
{
  struct data_editor *de = data;
  GtkWidget *dialog =
    gtk_font_selection_dialog_new (_("Font Selection"));

  gtk_window_set_transient_for (GTK_WINDOW (dialog),
				GTK_WINDOW (get_widget_assert (de->xml,
							       "data_editor")));
  if ( GTK_RESPONSE_OK == gtk_dialog_run (GTK_DIALOG (dialog)) )
    {
      GtkSheet *data_sheet =
	GTK_SHEET (get_widget_assert (de->xml, "data_sheet"));

      GtkSheet *var_sheet =
	GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

      PsppireDataStore *ds = PSPPIRE_DATA_STORE (gtk_sheet_get_model (data_sheet));
      PsppireVarStore *vs = PSPPIRE_VAR_STORE (gtk_sheet_get_model (var_sheet));

      const gchar *font = gtk_font_selection_dialog_get_font_name
	(GTK_FONT_SELECTION_DIALOG (dialog));

      PangoFontDescription* font_desc =
	pango_font_description_from_string (font);

      psppire_var_store_set_font (vs, font_desc);
      psppire_data_store_set_font (ds, font_desc);
    }

  gtk_widget_hide (dialog);
}


/* The next two callbacks are mutually co-operative */

/* Callback for the value labels menu item */
static void
value_labels_activate (GtkCheckMenuItem *menuitem, gpointer data)
{
  struct data_editor *de = data;

  GtkSheet *data_sheet = GTK_SHEET (get_widget_assert (de->xml, "data_sheet"));

  GtkToggleToolButton *tb =
    GTK_TOGGLE_TOOL_BUTTON (get_widget_assert (de->xml,
					       "togglebutton-value-labels"));

  PsppireDataStore *ds = PSPPIRE_DATA_STORE (gtk_sheet_get_model (data_sheet));

  gboolean show_value_labels = gtk_check_menu_item_get_active (menuitem);

  gtk_toggle_tool_button_set_active (tb, show_value_labels);

  psppire_data_store_show_labels (ds, show_value_labels);
}


/* Callback for the value labels tooglebutton */
static void
value_labels_toggled (GtkToggleToolButton *toggle_tool_button,
		      gpointer data)
{
  struct data_editor *de = data;

  GtkSheet *data_sheet = GTK_SHEET (get_widget_assert (de->xml, "data_sheet"));

  GtkCheckMenuItem *item =
    GTK_CHECK_MENU_ITEM (get_widget_assert (de->xml, "view_valuelabels"));

  PsppireDataStore *ds = PSPPIRE_DATA_STORE (gtk_sheet_get_model (data_sheet));

  gboolean show_value_labels =
    gtk_toggle_tool_button_get_active (toggle_tool_button);

  gtk_check_menu_item_set_active (item, show_value_labels);

  psppire_data_store_show_labels (ds, show_value_labels);
}


static void
file_quit (GtkCheckMenuItem *menuitem, gpointer data)
{
  /* FIXME: Need to be more intelligent here.
     Give the user the opportunity to save any unsaved data.
  */
  gtk_main_quit ();
}



/* Callback for when the Clear item in the edit menu is activated */
static void
on_clear_activate (GtkMenuItem *menuitem, gpointer data)
{
  struct data_editor *de = data;

  GtkNotebook *notebook = GTK_NOTEBOOK (get_widget_assert (de->xml,
							   "notebook"));

  switch ( gtk_notebook_get_current_page (notebook) )
    {
    case PAGE_VAR_SHEET:
      {
	GtkSheet *var_sheet =
	  GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

	PsppireVarStore *vs = PSPPIRE_VAR_STORE
	  (gtk_sheet_get_model (var_sheet) );

	/* This shouldn't be able to happen, because the menuitem
	   should be disabled */
	g_return_if_fail (var_sheet->state  ==  GTK_SHEET_ROW_SELECTED );

	psppire_dict_delete_variables (vs->dict,
				       var_sheet->range.row0,
				       1 +
				       var_sheet->range.rowi -
				       var_sheet->range.row0 );
      }
      break;
      case PAGE_DATA_SHEET:
	break;
      default:
	g_assert_not_reached ();
    }
}
