/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2007  Free Software Foundation

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
#include <stdlib.h>
#include <gettext.h>

#include <glade/glade.h>
#include <gtk/gtk.h>

#include "window-manager.h"
#include <gtksheet/gtksheet.h>

#include "helper.h"
#include "about.h"
#include "psppire-dialog.h"
#include "psppire-selector.h"
#include "weight-cases-dialog.h"
#include "split-file-dialog.h"
#include "transpose-dialog.h"
#include "sort-cases-dialog.h"
#include "compute-dialog.h"
#include "goto-case-dialog.h"
#include "comments-dialog.h"
#include "variable-info-dialog.h"
#include "dict-display.h"

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include "data-editor.h"
#include "syntax-editor.h"
#include <language/syntax-string-source.h>
#include <libpspp/syntax-gen.h>
#include "window-manager.h"

#include "psppire-data-store.h"
#include "psppire-var-store.h"


static void create_data_sheet_variable_popup_menu (struct data_editor *);
static void create_data_sheet_cases_popup_menu (struct data_editor *);

static void popup_variable_menu (GtkSheet *, gint,
				 GdkEventButton *, gpointer data);

static void popup_cases_menu (GtkSheet *, gint,
				 GdkEventButton *, gpointer data);

/* Update the data_ref_entry with the reference of the active cell */
static gint update_data_ref_entry (const GtkSheet *sheet,
				   gint row, gint col, gpointer data);

static void register_data_editor_actions (struct data_editor *de);

static void insert_variable (GtkAction *, gpointer data);
static void insert_case (GtkAction *a, gpointer data);
static void delete_cases (GtkAction *a, gpointer data);
static void delete_variables (GtkAction *a, gpointer data);

static void toggle_value_labels (GtkToggleAction *a, gpointer data);

/* Switch between the VAR SHEET and the DATA SHEET */

static gboolean click2column (GtkWidget *w, gint col, gpointer data);

static gboolean click2row (GtkWidget *w, gint row, gpointer data);


static void select_sheet (struct data_editor *de, guint page_num);


/* Callback for when the dictionary changes properties*/
static void on_weight_change (GObject *, gint, gpointer);
static void on_filter_change (GObject *, gint, gpointer);
static void on_split_change (PsppireDict *, gpointer);

static void data_var_select (GtkNotebook *notebook,
			    GtkNotebookPage *page,
			    guint page_num,
			    gpointer user_data);

static void status_bar_activate (GtkCheckMenuItem *, gpointer);

static void grid_lines_activate (GtkCheckMenuItem *, gpointer);

static void data_sheet_activate (GtkCheckMenuItem *, gpointer);

static void variable_sheet_activate (GtkCheckMenuItem *, gpointer );

static void fonts_activate (GtkMenuItem *, gpointer);

static void file_quit (GtkCheckMenuItem *, gpointer );

static void
enable_delete_cases (GtkWidget *w, gint var, gpointer data)
{
  struct data_editor *de = data;

  gtk_action_set_visible (de->delete_cases, var != -1);
}


static void
enable_delete_variables (GtkWidget *w, gint var, gpointer data)
{
  struct data_editor *de = data;

  gtk_action_set_visible (de->delete_variables, var != -1);
}


static void open_data_file (const gchar *, struct data_editor *);



#if RECENT_LISTS_AVAILABLE

static void
on_recent_data_select (GtkMenuShell *menushell,   gpointer user_data)
{
  gchar *file;
  struct data_editor *de = user_data;

  gchar *uri =
    gtk_recent_chooser_get_current_uri (GTK_RECENT_CHOOSER (menushell));

  file = g_filename_from_uri (uri, NULL, NULL);

  g_free (uri);

  open_data_file (file, de);

  g_free (file);
}

static void
on_recent_files_select (GtkMenuShell *menushell,   gpointer user_data)
{
  gchar *file;

  struct syntax_editor *se ;

  gchar *uri =
    gtk_recent_chooser_get_current_uri (GTK_RECENT_CHOOSER (menushell));

  file = g_filename_from_uri (uri, NULL, NULL);

  g_free (uri);

  se = (struct syntax_editor *)
    window_create (WINDOW_SYNTAX, file);

  load_editor_from_file (se, file, NULL);

  g_free (file);
}

#endif

static void
datum_entry_activate (GtkEntry *entry, gpointer data)
{
  gint row, column;
  GtkSheet *data_sheet = GTK_SHEET (data);
  PsppireDataStore *store = PSPPIRE_DATA_STORE (gtk_sheet_get_model (data_sheet));

  const char *text = gtk_entry_get_text (entry);

  gtk_sheet_get_active_cell (data_sheet, &row, &column);

  if ( row == -1 || column == -1)
    return;

  psppire_data_store_set_string (store, text, row, column);
}

/*
  Create a new data editor.
*/
struct data_editor *
new_data_editor (void)
{
  struct data_editor *de ;
  struct editor_window *e;
  GtkSheet *var_sheet ;
  GtkSheet *data_sheet ;
  PsppireVarStore *vs;
  GtkWidget *datum_entry;

  de = g_malloc0 (sizeof (*de));

  e = (struct editor_window *) de;

  de->xml = XML_NEW ("data-editor.glade");

  var_sheet = GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));
  data_sheet = GTK_SHEET (get_widget_assert (de->xml, "data_sheet"));

  vs = PSPPIRE_VAR_STORE (gtk_sheet_get_model (var_sheet));

  g_assert(vs); /* Traps a possible bug in win32 build */

  g_signal_connect (G_OBJECT (data_sheet), "activate",
		    G_CALLBACK (update_data_ref_entry),
		    de->xml);

  datum_entry = get_widget_assert (de->xml, "datum_entry");

  g_signal_connect (G_OBJECT (datum_entry), "activate",
		    G_CALLBACK (datum_entry_activate),
		    data_sheet);

  g_signal_connect (vs->dict, "weight-changed",
		    G_CALLBACK (on_weight_change),
		    de);

  g_signal_connect (vs->dict, "filter-changed",
		    G_CALLBACK (on_filter_change),
		    de);

  g_signal_connect (vs->dict, "split-changed",
		    G_CALLBACK (on_split_change),
		    de);

  connect_help (de->xml);


  register_data_editor_actions (de);

  de->toggle_value_labels =
    gtk_toggle_action_new ("toggle-value-labels",
			   _("Labels"),
			   _("Show (hide) value labels"),
			   "pspp-value-labels");

  g_signal_connect (de->toggle_value_labels, "activate",
		    G_CALLBACK (toggle_value_labels), de);


  gtk_action_connect_proxy (GTK_ACTION (de->toggle_value_labels),
			    get_widget_assert (de->xml,
					       "togglebutton-value-labels"));


  gtk_action_connect_proxy (GTK_ACTION (de->toggle_value_labels),
			    get_widget_assert (de->xml,
					       "view_value-labels"));

  de->delete_cases =
    gtk_action_new ("clear-cases",
		    _("Clear"),
		    _("Delete the cases at the selected position(s)"),
		    "pspp-clear-cases");

  g_signal_connect (de->delete_cases, "activate",
		    G_CALLBACK (delete_cases), de);

  gtk_action_connect_proxy (de->delete_cases,
			    get_widget_assert (de->xml, "edit_clear-cases"));


  gtk_action_set_visible (de->delete_cases, FALSE);

  de->delete_variables =
    gtk_action_new ("clear-variables",
		    _("Clear"),
		    _("Delete the variables at the selected position(s)"),
		    "pspp-clear-variables");

  g_signal_connect (de->delete_variables, "activate",
		    G_CALLBACK (delete_variables), de);

  gtk_action_connect_proxy (de->delete_variables,
			    get_widget_assert (de->xml, "edit_clear-variables")
			    );

  gtk_action_set_visible (de->delete_variables, FALSE);

  de->insert_variable =
    gtk_action_new ("insert-variable",
		    _("Insert Variable"),
		    _("Create a new variable at the current position"),
		    "pspp-insert-variable");

  g_signal_connect (de->insert_variable, "activate",
		    G_CALLBACK (insert_variable), de);


  gtk_action_connect_proxy (de->insert_variable,
			    get_widget_assert (de->xml, "button-insert-variable")
			    );

  gtk_action_connect_proxy (de->insert_variable,
			    get_widget_assert (de->xml, "data_insert-variable")
			    );


  de->insert_case =
    gtk_action_new ("insert-case",
		    _("Insert Case"),
		    _("Create a new case at the current position"),
		    "pspp-insert-case");

  g_signal_connect (de->insert_case, "activate",
		    G_CALLBACK (insert_case), de);


  gtk_action_connect_proxy (de->insert_case,
			    get_widget_assert (de->xml, "button-insert-case")
			    );


  gtk_action_connect_proxy (de->insert_case,
			    get_widget_assert (de->xml, "data_insert-case")
			    );



  de->invoke_goto_dialog =
    gtk_action_new ("goto-case-dialog",
		    _("Goto Case"),
		    _("Jump to a Case in the Data Sheet"),
		    "gtk-jump-to");


  gtk_action_connect_proxy (de->invoke_goto_dialog,
			    get_widget_assert (de->xml, "button-goto-case")
			    );

  gtk_action_connect_proxy (de->invoke_goto_dialog,
			    get_widget_assert (de->xml, "data_goto-case")
			    );


  g_signal_connect (de->invoke_goto_dialog, "activate",
		    G_CALLBACK (goto_case_dialog), de);


  de->invoke_weight_cases_dialog =
    gtk_action_new ("weight-cases-dialog",
		    _("Weights"),
		    _("Weight cases by variable"),
		    "pspp-weight-cases");

  g_signal_connect (de->invoke_weight_cases_dialog, "activate",
		    G_CALLBACK (weight_cases_dialog), de);


  de->invoke_transpose_dialog =
    gtk_action_new ("transpose-dialog",
		    _("Transpose"),
		    _("Transpose the cases with the variables"),
		    NULL);


  g_signal_connect (de->invoke_transpose_dialog, "activate",
		    G_CALLBACK (transpose_dialog), de);



  de->invoke_split_file_dialog =
    gtk_action_new ("split-file-dialog",
		    _("Split"),
		    _("Split the active file"),
		    "pspp-split-file");

  g_signal_connect (de->invoke_split_file_dialog, "activate",
		    G_CALLBACK (split_file_dialog), de);



  de->invoke_sort_cases_dialog =
    gtk_action_new ("sort-cases-dialog",
		    _("Sort"),
		    _("Sort cases in the active file"),
		    "pspp-sort-cases");

  g_signal_connect (de->invoke_sort_cases_dialog, "activate",
		    G_CALLBACK (sort_cases_dialog), de);


  de->invoke_compute_dialog =
    gtk_action_new ("compute-dialog",
		    _("Compute"),
		    _("Compute new values for a variable"),
		    "pspp-compute");

  g_signal_connect (de->invoke_compute_dialog, "activate",
		    G_CALLBACK (compute_dialog), de);

  de->invoke_comments_dialog =
    gtk_action_new ("commments-dialog",
		    _("Data File Comments"),
		    _("Commentary text for the data file"),
		    NULL);

  g_signal_connect (de->invoke_comments_dialog, "activate",
		    G_CALLBACK (comments_dialog), de);

  de->invoke_variable_info_dialog  =
    gtk_action_new ("variable-info-dialog",
		    _("Variables"),
		    _("Jump to Variable"),
		    "pspp-goto-variable");

  g_signal_connect (de->invoke_variable_info_dialog, "activate",
		    G_CALLBACK (variable_info_dialog), de);

  e->window = GTK_WINDOW (get_widget_assert (de->xml, "data_editor"));

  g_signal_connect_swapped (get_widget_assert (de->xml,"file_new_data"),
			    "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->action_data_new);

  g_signal_connect_swapped (get_widget_assert (de->xml,"file_open_data"),
			    "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->action_data_open);


#if RECENT_LISTS_AVAILABLE
  {
    GtkRecentManager *rm = gtk_recent_manager_get_default ();
    GtkWidget *recent_data = get_widget_assert (de->xml, "file_recent-data");
    GtkWidget *recent_files = get_widget_assert (de->xml, "file_recent-files");
    GtkWidget *recent_separator = get_widget_assert (de->xml, "file_separator1");

    GtkWidget *menu = gtk_recent_chooser_menu_new_for_manager (rm);

    GtkRecentFilter *filter = gtk_recent_filter_new ();

    gtk_widget_show (recent_data);
    gtk_widget_show (recent_files);
    gtk_widget_show (recent_separator);

    gtk_recent_filter_add_pattern (filter, "*.sav");
    gtk_recent_filter_add_pattern (filter, "*.SAV");

    gtk_recent_chooser_add_filter (GTK_RECENT_CHOOSER (menu), filter);

    gtk_widget_set_sensitive (recent_data, TRUE);
    g_signal_connect (menu, "selection-done",
		      G_CALLBACK (on_recent_data_select), de);

    gtk_menu_item_set_submenu (GTK_MENU_ITEM (recent_data), menu);


    filter = gtk_recent_filter_new ();
    menu = gtk_recent_chooser_menu_new_for_manager (rm);

    gtk_recent_filter_add_pattern (filter, "*.sps");
    gtk_recent_filter_add_pattern (filter, "*.SPS");

    gtk_recent_chooser_add_filter (GTK_RECENT_CHOOSER (menu), filter);

    gtk_widget_set_sensitive (recent_files, TRUE);
    g_signal_connect (menu, "selection-done",
		      G_CALLBACK (on_recent_files_select), de);

    gtk_menu_item_set_submenu (GTK_MENU_ITEM (recent_files), menu);
  }
#endif

  g_signal_connect (get_widget_assert (de->xml,"file_new_syntax"),
		    "activate",
		    G_CALLBACK (new_syntax_window),
		    e->window);

  g_signal_connect (get_widget_assert (de->xml,"file_open_syntax"),
		    "activate",
		    G_CALLBACK (open_syntax_window),
		    e->window);

  g_signal_connect_swapped (get_widget_assert (de->xml,"file_save"),
			    "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->action_data_save);

  g_signal_connect_swapped (get_widget_assert (de->xml,"file_save_as"),
			    "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->action_data_save_as);

  gtk_action_connect_proxy (de->invoke_weight_cases_dialog,
			    get_widget_assert (de->xml, "data_weight-cases")
			    );

  gtk_action_connect_proxy (de->invoke_transpose_dialog,
			    get_widget_assert (de->xml, "data_transpose")
			    );

  gtk_action_connect_proxy (de->invoke_split_file_dialog,
			    get_widget_assert (de->xml, "data_split-file")
			    );

  gtk_action_connect_proxy (de->invoke_sort_cases_dialog,
			    get_widget_assert (de->xml, "data_sort-cases")
			    );

  gtk_action_connect_proxy (de->invoke_compute_dialog,
			    get_widget_assert (de->xml, "transform_compute")
			    );

  gtk_action_connect_proxy (de->invoke_comments_dialog,
			    get_widget_assert (de->xml, "utilities_comments")
			    );

  gtk_action_connect_proxy (de->invoke_variable_info_dialog,
			    get_widget_assert (de->xml, "utilities_variables")
			    );

  g_signal_connect (get_widget_assert (de->xml,"help_about"),
		    "activate",
		    G_CALLBACK (about_new),
		    e->window);


  g_signal_connect (get_widget_assert (de->xml,"help_reference"),
		    "activate",
		    G_CALLBACK (reference_manual),
		    e->window);

  g_signal_connect (data_sheet,
		    "double-click-column",
		    G_CALLBACK (click2column),
		    de);

  g_signal_connect (data_sheet,
		    "select-column",
		    G_CALLBACK (enable_delete_variables),
		    de);

  g_signal_connect (data_sheet,
		    "select-row",
		    G_CALLBACK (enable_delete_cases),
		    de);


  g_signal_connect (var_sheet,
		    "double-click-row",
		    GTK_SIGNAL_FUNC (click2row),
		    de);

  g_signal_connect_after (var_sheet,
		    "select-row",
		    G_CALLBACK (enable_delete_variables),
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




  gtk_action_connect_proxy (de->action_data_open,
			    get_widget_assert (de->xml, "button-open")
			    );

  gtk_action_connect_proxy (de->action_data_save,
			    get_widget_assert (de->xml, "button-save")
			    );

  gtk_action_connect_proxy (de->invoke_variable_info_dialog,
			    get_widget_assert (de->xml, "button-goto-variable")
			    );

  gtk_action_connect_proxy (de->invoke_weight_cases_dialog,
			    get_widget_assert (de->xml, "button-weight-cases")
			    );

  gtk_action_connect_proxy (de->invoke_split_file_dialog,
			    get_widget_assert (de->xml, "button-split-file")
			    );

  g_signal_connect (get_widget_assert (de->xml, "file_quit"),
		    "activate",
		    G_CALLBACK (file_quit), de);


  g_signal_connect (get_widget_assert (de->xml, "windows_minimise_all"),
		    "activate",
		    G_CALLBACK (minimise_all_windows), NULL);


  create_data_sheet_variable_popup_menu (de);
  create_data_sheet_cases_popup_menu (de);

  g_signal_connect (G_OBJECT (data_sheet), "button-event-column",
		    G_CALLBACK (popup_variable_menu), de);

  g_signal_connect (G_OBJECT (data_sheet), "button-event-row",
		    G_CALLBACK (popup_cases_menu), de);


  select_sheet (de, PAGE_DATA_SHEET);

  return de;
}


/* Callback which occurs when the var sheet's row title
   button is double clicked */
static gboolean
click2row (GtkWidget *w, gint row, gpointer data)
{
  struct data_editor *de = data;
  GtkSheetRange visible_range;

  gint current_row, current_column;

  GtkWidget *data_sheet  = get_widget_assert (de->xml, "data_sheet");

  data_editor_select_sheet (de, PAGE_DATA_SHEET);

  gtk_sheet_get_active_cell (GTK_SHEET (data_sheet),
			     &current_row, &current_column);

  gtk_sheet_set_active_cell (GTK_SHEET (data_sheet), current_row, row);

  gtk_sheet_get_visible_range (GTK_SHEET (data_sheet), &visible_range);

  if ( row < visible_range.col0 || row > visible_range.coli)
    {
      gtk_sheet_moveto (GTK_SHEET (data_sheet),
			current_row, row, 0, 0);
    }

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
  GtkWidget *view_data = get_widget_assert (de->xml, "view_data");
  GtkWidget *view_variables = get_widget_assert (de->xml, "view_variables");

  switch (page_num)
    {
    case PAGE_VAR_SHEET:
      gtk_widget_hide (view_variables);
      gtk_widget_show (view_data);
      gtk_action_set_sensitive (de->insert_variable, TRUE);
      gtk_action_set_sensitive (de->insert_case, FALSE);
      gtk_action_set_sensitive (de->invoke_goto_dialog, FALSE);
      break;
    case PAGE_DATA_SHEET:
      gtk_widget_show (view_variables);
      gtk_widget_hide (view_data);
      gtk_action_set_sensitive (de->invoke_goto_dialog, TRUE);
      gtk_action_set_sensitive (de->insert_case, TRUE);
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


static void
status_bar_activate (GtkCheckMenuItem *menuitem, gpointer data)
{
  struct data_editor *de = data;
  GtkWidget *statusbar = get_widget_assert (de->xml, "status-bar");

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



/* Callback for the value labels action */
static void
toggle_value_labels (GtkToggleAction *ta, gpointer data)
{
  struct data_editor *de = data;

  GtkSheet *data_sheet = GTK_SHEET (get_widget_assert (de->xml, "data_sheet"));

  PsppireDataStore *ds = PSPPIRE_DATA_STORE (gtk_sheet_get_model (data_sheet));


  psppire_data_store_show_labels (ds,
				  gtk_toggle_action_get_active (ta));
}


static void
file_quit (GtkCheckMenuItem *menuitem, gpointer data)
{
  /* FIXME: Need to be more intelligent here.
     Give the user the opportunity to save any unsaved data.
  */
  gtk_main_quit ();
}

static void
delete_cases (GtkAction *action, gpointer data)
{
  struct data_editor *de = data;
  GtkSheet *data_sheet =
    GTK_SHEET (get_widget_assert (de->xml, "data_sheet"));

  GtkSheetRange range;

  PsppireDataStore *data_store = PSPPIRE_DATA_STORE
    (gtk_sheet_get_model (data_sheet) );


  /* This shouldn't be able to happen, because the action
     should be disabled */
  g_return_if_fail (gtk_sheet_get_state (data_sheet)
		    ==  GTK_SHEET_ROW_SELECTED );

  gtk_sheet_get_selected_range (data_sheet, &range);

  gtk_sheet_unselect_range (data_sheet);

  psppire_data_store_delete_cases (data_store, range.row0,
				   1 + range.rowi - range.row0);

}

static void
delete_variables (GtkAction *a, gpointer data)
{
  struct data_editor *de = data;
  GtkSheetRange range;

  GtkNotebook *notebook = GTK_NOTEBOOK (get_widget_assert (de->xml,
							   "notebook"));

  const gint page = gtk_notebook_get_current_page (notebook);

  GtkSheet *sheet = GTK_SHEET (get_widget_assert (de->xml,
						  (page == PAGE_VAR_SHEET) ?
						  "variable_sheet" :
						  "data_sheet"));


  gtk_sheet_get_selected_range (sheet, &range);

  switch ( page )
    {
    case PAGE_VAR_SHEET:
      {
	PsppireVarStore *vs =
	  PSPPIRE_VAR_STORE (gtk_sheet_get_model (sheet));

	psppire_dict_delete_variables (vs->dict,
				       range.row0,
				       1 +
				       range.rowi -
				       range.row0 );
      }
      break;
    case PAGE_DATA_SHEET:
      {
	PsppireDataStore *ds =
	  PSPPIRE_DATA_STORE (gtk_sheet_get_model (sheet));

	psppire_dict_delete_variables (ds->dict,
				       range.col0,
				       1 +
				       range.coli -
				       range.col0 );
      }
      break;
    };

  gtk_sheet_unselect_range (sheet);
}

static void
insert_case (GtkAction *action, gpointer data)
{
  gint current_row ;
  struct data_editor *de = data;

  GtkSheet *data_sheet =
    GTK_SHEET (get_widget_assert (de->xml, "data_sheet"));

  PsppireDataStore *ds = PSPPIRE_DATA_STORE
    (gtk_sheet_get_model (data_sheet) );


  gtk_sheet_get_active_cell (data_sheet, &current_row, NULL);

  if (current_row < 0) current_row = 0;

  psppire_data_store_insert_new_case (ds, current_row);
}

/* Insert a new variable before the current row in the variable sheet,
   or before the current column in the data sheet, whichever is selected */
static void
insert_variable (GtkAction *action, gpointer data)
{
  struct data_editor *de = data;
  gint posn = -1;

  GtkWidget *notebook = get_widget_assert (de->xml, "notebook");

  GtkSheet *var_sheet =
    GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

  PsppireVarStore *vs = PSPPIRE_VAR_STORE
    (gtk_sheet_get_model (var_sheet) );

  switch ( gtk_notebook_get_current_page ( GTK_NOTEBOOK (notebook)) )
    {
    case PAGE_VAR_SHEET:
      posn = var_sheet->active_cell.row;
      break;
    case PAGE_DATA_SHEET:
      {
	GtkSheet *data_sheet =
	  GTK_SHEET (get_widget_assert (de->xml, "data_sheet"));

	if ( data_sheet->state == GTK_SHEET_COLUMN_SELECTED )
	  posn = data_sheet->range.col0;
	else
	  posn = data_sheet->active_cell.col;
      }
      break;
    default:
      g_assert_not_reached ();
    }

  if ( posn == -1 ) posn = 0;

  psppire_dict_insert_variable (vs->dict, posn, NULL);
}

/* Callback for when the dictionary changes its split variables */
static void
on_split_change (PsppireDict *dict, gpointer data)
{
  struct data_editor *de = data;

  size_t n_split_vars = dict_get_split_cnt (dict->dict);

  GtkWidget *split_status_area =
    get_widget_assert (de->xml, "split-file-status-area");

  if ( n_split_vars == 0 )
    {
      gtk_label_set_text (GTK_LABEL (split_status_area), _("No Split"));
    }
  else
    {
      gint i;
      GString *text;
      const struct variable *const * split_vars =
	dict_get_split_vars (dict->dict);

      text = g_string_new (_("Split by "));

      for (i = 0 ; i < n_split_vars - 1; ++i )
	{
	  g_string_append_printf (text, "%s, ", var_get_name (split_vars[i]));
	}
      g_string_append (text, var_get_name (split_vars[i]));

      gtk_label_set_text (GTK_LABEL (split_status_area), text->str);

      g_string_free (text, TRUE);
    }
}


/* Callback for when the dictionary changes its filter variable */
static void
on_filter_change (GObject *o, gint filter_index, gpointer data)
{
  struct data_editor *de = data;
  GtkWidget *filter_status_area =
    get_widget_assert (de->xml, "filter-use-status-area");

  if ( filter_index == -1 )
    {
      gtk_label_set_text (GTK_LABEL (filter_status_area), _("Filter off"));
    }
  else
    {
      GtkSheet *var_sheet =
	GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

      PsppireVarStore *vs = PSPPIRE_VAR_STORE
	(gtk_sheet_get_model (var_sheet) );

      struct variable *var = psppire_dict_get_variable (vs->dict,
							filter_index);

      gchar *text = g_strdup_printf (_("Filter by %s"), var_get_name (var));

      gtk_label_set_text (GTK_LABEL (filter_status_area), text);

      g_free (text);
    }
}

/* Callback for when the dictionary changes its weights */
static void
on_weight_change (GObject *o, gint weight_index, gpointer data)
{
  struct data_editor *de = data;
  GtkWidget *weight_status_area =
    get_widget_assert (de->xml, "weight-status-area");

  if ( weight_index == -1 )
    {
      gtk_label_set_text (GTK_LABEL (weight_status_area), _("Weights off"));
    }
  else
    {
      GtkSheet *var_sheet =
	GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

      PsppireVarStore *vs = PSPPIRE_VAR_STORE
	(gtk_sheet_get_model (var_sheet) );

      struct variable *var = psppire_dict_get_variable (vs->dict,
							weight_index);

      gchar *text = g_strdup_printf (_("Weight by %s"), var_get_name (var));

      gtk_label_set_text (GTK_LABEL (weight_status_area), text);

      g_free (text);
    }
}




static void data_save_as_dialog (GtkAction *, struct data_editor *de);
static void new_file (GtkAction *, struct editor_window *de);
static void open_data_dialog (GtkAction *, struct data_editor *de);
static void data_save (GtkAction *action, struct data_editor *e);


/* Create the GtkActions and connect to their signals */
static void
register_data_editor_actions (struct data_editor *de)
{
  de->action_data_open =
    gtk_action_new ("data-open-dialog",
		    _("Open"),
		    _("Open a data file"),
		    "gtk-open");

  g_signal_connect (de->action_data_open, "activate",
		    G_CALLBACK (open_data_dialog), de);


  de->action_data_save = gtk_action_new ("data-save",
					    _("Save"),
					    _("Save data to file"),
					    "gtk-save");

  g_signal_connect (de->action_data_save, "activate",
		    G_CALLBACK (data_save), de);



  de->action_data_save_as = gtk_action_new ("data-save-as-dialog",
					    _("Save As"),
					    _("Save data to file"),
					    "gtk-save");

  g_signal_connect (de->action_data_save_as, "activate",
		    G_CALLBACK (data_save_as_dialog), de);

  de->action_data_new =
    gtk_action_new ("data-new",
		    _("New"),
		    _("New data file"),
		    NULL);

  g_signal_connect (de->action_data_new, "activate",
		    G_CALLBACK (new_file), de);
}

/* Returns true if NAME has a suffix which might denote a PSPP file */
static gboolean
name_has_suffix (const gchar *name)
{
  if ( g_str_has_suffix (name, ".sav"))
    return TRUE;
  if ( g_str_has_suffix (name, ".SAV"))
    return TRUE;
  if ( g_str_has_suffix (name, ".por"))
    return TRUE;
  if ( g_str_has_suffix (name, ".POR"))
    return TRUE;

  return FALSE;
}

/* Append SUFFIX to the filename of DE */
static void
append_filename_suffix (struct data_editor *de, const gchar *suffix)
{
  if ( ! name_has_suffix (de->file_name))
    {
      gchar *s = de->file_name;
      de->file_name = g_strconcat (de->file_name, suffix, NULL);
      g_free (s);
    }
}

/* Save DE to file */
static void
save_file (struct data_editor *de)
{
  struct getl_interface *sss;
  struct string file_name ;

  g_assert (de->file_name);

  ds_init_cstr (&file_name, de->file_name);
  gen_quoted_string (&file_name);

  if ( de->save_as_portable )
    {
      append_filename_suffix (de, ".por");
      sss = create_syntax_string_source ("EXPORT OUTFILE=%s.",
					 ds_cstr (&file_name));
    }
  else
    {
      append_filename_suffix (de, ".sav");
      sss = create_syntax_string_source ("SAVE OUTFILE=%s.",
					 ds_cstr (&file_name));
    }

  ds_destroy (&file_name);

  execute_syntax (sss);
}


/* Callback for data_save action.
   If there's an existing file name, then just save,
   otherwise prompt for a file name, then save */
static void
data_save (GtkAction *action, struct data_editor *de)
{
  if ( de->file_name)
    save_file (de);
  else
    data_save_as_dialog (action, de);
}


/* Callback for data_save_as action. Prompt for a filename and save */
static void
data_save_as_dialog (GtkAction *action, struct data_editor *de)
{
  struct editor_window *e = (struct editor_window *) de;

  GtkWidget *button_sys;
  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Save"),
				 GTK_WINDOW (e->window),
				 GTK_FILE_CHOOSER_ACTION_SAVE,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				 GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
				 NULL);

  GtkFileFilter *filter = gtk_file_filter_new ();
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

  {
    GtkWidget *button_por;
    GtkWidget *vbox = gtk_vbox_new (TRUE, 5);
    button_sys =
      gtk_radio_button_new_with_label (NULL, _("System File"));

    button_por =
      gtk_radio_button_new_with_label
      (gtk_radio_button_get_group (GTK_RADIO_BUTTON(button_sys)),
       _("Portable File"));

    gtk_box_pack_start_defaults (GTK_BOX (vbox), button_sys);
    gtk_box_pack_start_defaults (GTK_BOX (vbox), button_por);

    gtk_widget_show_all (vbox);

    gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER(dialog), vbox);
  }

  switch (gtk_dialog_run (GTK_DIALOG (dialog)))
    {
    case GTK_RESPONSE_ACCEPT:
      {
	g_free (de->file_name);

	de->file_name =
	  gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

	de->save_as_portable =
	  ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button_sys));

	save_file (de);

	window_set_name_from_filename (e, de->file_name);
      }
      break;
    default:
      break;
    }

  gtk_widget_destroy (dialog);
}


/* Callback for data_new action.
   Performs the NEW FILE command */
static void
new_file (GtkAction *action, struct editor_window *e)
{
  struct data_editor *de = (struct data_editor *) e;

  struct getl_interface *sss =
    create_syntax_string_source ("NEW FILE.");

  execute_syntax (sss);

  g_free (de->file_name);
  de->file_name = NULL;

  default_window_name (e);
}


static void
open_data_file (const gchar *file_name, struct data_editor *de)
{
  struct getl_interface *sss;
  struct string filename;

  ds_init_cstr (&filename, file_name);

  gen_quoted_string (&filename);

  sss = create_syntax_string_source ("GET FILE=%s.",
				     ds_cstr (&filename));

  execute_syntax (sss);
  ds_destroy (&filename);

  window_set_name_from_filename ((struct editor_window *) de, file_name);
}


/* Callback for the data_open action.
   Prompts for a filename and opens it */
static void
open_data_dialog (GtkAction *action, struct data_editor *de)
{
  struct editor_window *e = (struct editor_window *) de;

  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Open"),
				 GTK_WINDOW (e->window),
				 GTK_FILE_CHOOSER_ACTION_OPEN,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				 GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				 NULL);

  GtkFileFilter *filter = gtk_file_filter_new ();
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


  if ( de->file_name)
    {
      gchar *dir_name = g_path_get_dirname (de->file_name);
      gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog),
					   dir_name);
      free (dir_name);
    }

  switch (gtk_dialog_run (GTK_DIALOG (dialog)))
    {
    case GTK_RESPONSE_ACCEPT:
      {
	g_free (de->file_name);
	de->file_name =
	  gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

	open_data_file (de->file_name, de);

#if RECENT_LISTS_AVAILABLE
	{
	  GtkRecentManager *manager = gtk_recent_manager_get_default();
	  gchar *uri = g_filename_to_uri (de->file_name, NULL, NULL);

	  if ( ! gtk_recent_manager_add_item (manager, uri))
	    g_warning ("Could not add item %s to recent list\n",uri);

	  g_free (uri);
	}
#endif

      }
      break;
    default:
      break;
    }

  gtk_widget_destroy (dialog);
}



/* Update the data_ref_entry with the reference of the active cell */
static gint
update_data_ref_entry (const GtkSheet *sheet, gint row, gint col, gpointer data)
{
  GladeXML *data_editor_xml = data;

  PsppireDataStore *data_store =
    PSPPIRE_DATA_STORE (gtk_sheet_get_model (sheet));

  g_return_val_if_fail (data_editor_xml, FALSE);

  if (data_store)
    {
      const struct variable *var =
	psppire_dict_get_variable (data_store->dict, col);

      /* The entry where the reference to the current cell is displayed */
      GtkEntry *cell_ref_entry =
	GTK_ENTRY (get_widget_assert (data_editor_xml,
				      "cell_ref_entry"));
      GtkEntry *datum_entry =
	GTK_ENTRY (get_widget_assert (data_editor_xml,
				      "datum_entry"));

      if ( var )
	{
	  gchar *text = g_strdup_printf ("%d: %s", row + FIRST_CASE_NUMBER,
					 var_get_name (var));

	  gchar *s = pspp_locale_to_utf8 (text, -1, 0);

	  g_free (text);

	  gtk_entry_set_text (cell_ref_entry, s);

	  g_free (s);
	}
      else
	gtk_entry_set_text (cell_ref_entry, "");


      if ( var )
	{
	  gchar *text =
	    psppire_data_store_get_string (data_store, row,
					   var_get_dict_index(var));
	  g_strchug (text);

	  gtk_entry_set_text (datum_entry, text);

	  free (text);
	}
      else
	gtk_entry_set_text (datum_entry, "");
    }

  return FALSE;
}





static void
do_sort (PsppireDataStore *ds, int var, gboolean descend)
{
  GString *string = g_string_new ("SORT CASES BY ");

  const struct variable *v =
    psppire_dict_get_variable (ds->dict, var);

  g_string_append_printf (string, "%s", var_get_name (v));

  if ( descend )
    g_string_append (string, " (D)");

  g_string_append (string, ".");

  execute_syntax (create_syntax_string_source (string->str));

  g_string_free (string, TRUE);
}


static void
sort_up (GtkMenuItem *item, gpointer data)
{
  GtkSheet *sheet  = data;
  GtkSheetRange range;
  gtk_sheet_get_selected_range (sheet, &range);

  do_sort (PSPPIRE_DATA_STORE (gtk_sheet_get_model(sheet)),
	   range.col0, FALSE);

}

static void
sort_down (GtkMenuItem *item, gpointer data)
{
  GtkSheet *sheet  = data;
  GtkSheetRange range;
  gtk_sheet_get_selected_range (sheet, &range);

  do_sort (PSPPIRE_DATA_STORE (gtk_sheet_get_model(sheet)),
	   range.col0, TRUE);
}




static void
create_data_sheet_variable_popup_menu (struct data_editor *de)
{
  GtkSheet *sheet  = GTK_SHEET (get_widget_assert (de->xml, "data_sheet"));
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *sort_ascending =
    gtk_menu_item_new_with_label (_("Sort Ascending"));

  GtkWidget *sort_descending =
    gtk_menu_item_new_with_label (_("Sort Descending"));


  GtkWidget *insert_variable =
    gtk_menu_item_new_with_label (_("Insert Variable"));

  GtkWidget *clear_variable =
    gtk_menu_item_new_with_label (_("Clear"));


  gtk_action_connect_proxy (de->insert_variable,
			    insert_variable );


  gtk_action_connect_proxy (de->delete_variables,
			    clear_variable );


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), insert_variable);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu),
			 gtk_separator_menu_item_new ());


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), clear_variable);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu),
			 gtk_separator_menu_item_new ());


  g_signal_connect (G_OBJECT (sort_ascending), "activate",
		    G_CALLBACK (sort_up), sheet);

  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sort_ascending);


  g_signal_connect (G_OBJECT (sort_descending), "activate",
		    G_CALLBACK (sort_down), sheet);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sort_descending);

  gtk_widget_show_all (menu);


  de->data_sheet_variable_popup_menu = GTK_MENU(menu);
}


static void
create_data_sheet_cases_popup_menu (struct data_editor *de)
{
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *insert_case =
    gtk_menu_item_new_with_label (_("Insert Case"));

  GtkWidget *delete_case =
    gtk_menu_item_new_with_label (_("Clear"));


  gtk_action_connect_proxy (de->insert_case,
			    insert_case);


  gtk_action_connect_proxy (de->delete_cases,
			    delete_case);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), insert_case);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu),
			 gtk_separator_menu_item_new ());


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), delete_case);


  gtk_widget_show_all (menu);


  de->data_sheet_cases_popup_menu = GTK_MENU (menu);
}


static void
popup_variable_menu (GtkSheet *sheet, gint column,
		     GdkEventButton *event, gpointer data)
{
  struct data_editor *de = data;

  PsppireDataStore *data_store =
    PSPPIRE_DATA_STORE (gtk_sheet_get_model (sheet));

  const struct variable *v =
    psppire_dict_get_variable (data_store->dict, column);

  if ( v && event->button == 3)
    {

      gtk_sheet_select_column (sheet, column);

      gtk_menu_popup (GTK_MENU (de->data_sheet_variable_popup_menu),
		      NULL, NULL, NULL, NULL,
		      event->button, event->time);
    }
}


static void
popup_cases_menu (GtkSheet *sheet, gint row,
		  GdkEventButton *event, gpointer data)
{
  struct data_editor *de = data;

  PsppireDataStore *data_store =
    PSPPIRE_DATA_STORE (gtk_sheet_get_model (sheet));

  if ( row <= psppire_data_store_get_case_count (data_store) &&
       event->button == 3)
    {
      gtk_sheet_select_row (sheet, row);

      gtk_menu_popup (GTK_MENU (de->data_sheet_cases_popup_menu),
		      NULL, NULL, NULL, NULL,
		      event->button, event->time);
    }
}


