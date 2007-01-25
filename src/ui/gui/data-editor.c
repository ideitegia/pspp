/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2006, 2007  Free Software Foundation

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

#include "window-manager.h"
#include <gtksheet/gtksheet.h>

#include "helper.h"
#include "about.h"
#include "psppire-dialog.h"
#include "psppire-var-select.h"

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include "data-editor.h"
#include "syntax-editor.h"
#include <language/syntax-string-source.h>
#include "window-manager.h"

#include "psppire-data-store.h"
#include "psppire-var-store.h"

#include "weight-cases-dialog.h"

static void register_data_editor_actions (struct data_editor *de);

static void insert_variable (GtkCheckMenuItem *m, gpointer data);


/* Switch between the VAR SHEET and the DATA SHEET */
enum {PAGE_DATA_SHEET = 0, PAGE_VAR_SHEET};

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

static void weight_cases_dialog (GObject *o, gpointer data);


/*
  Create a new data editor.
*/
struct data_editor *
new_data_editor (void)
{
  struct data_editor *de ;
  struct editor_window *e;
  GtkSheet *var_sheet ;
  PsppireVarStore *vs;

  de = g_malloc0 (sizeof (*de));

  e = (struct editor_window *) de;

  de->xml = glade_xml_new (PKGDATADIR "/data-editor.glade", NULL, NULL);


  var_sheet = GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

  vs = PSPPIRE_VAR_STORE (gtk_sheet_get_model (var_sheet));

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

  de->invoke_weight_cases_dialog =
    gtk_action_new ("weight-cases-dialog",
		    _("Weights"),
		    _("Weight cases by variable"),
		    "pspp-weight-cases");


  g_signal_connect (de->invoke_weight_cases_dialog, "activate",
		    G_CALLBACK (weight_cases_dialog), de);

  e->window = GTK_WINDOW (get_widget_assert (de->xml, "data_editor"));

  g_signal_connect_swapped (get_widget_assert (de->xml,"file_new_data"),
			    "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->action_data_new);

  g_signal_connect_swapped (get_widget_assert (de->xml,"file_open_data"),
			    "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->action_data_open);

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


  g_signal_connect (get_widget_assert (de->xml,"edit_clear"),
		    "activate",
		    G_CALLBACK (on_clear_activate),
		    de);


  g_signal_connect (get_widget_assert (de->xml,"data_insert-variable"),
		    "activate",
		    G_CALLBACK (insert_variable),
		    de);

  gtk_action_connect_proxy (de->invoke_weight_cases_dialog,
			    get_widget_assert (de->xml, "data_weight-cases")
			    );


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

  gtk_action_connect_proxy (de->action_data_open,
			    get_widget_assert (de->xml, "button-open")
			    );

  gtk_action_connect_proxy (de->action_data_save,
			    get_widget_assert (de->xml, "button-save")
			    );

  gtk_action_connect_proxy (de->invoke_weight_cases_dialog,
			    get_widget_assert (de->xml, "button-weight-cases")
			    );

  g_signal_connect (get_widget_assert (de->xml, "file_quit"),
		    "activate",
		    G_CALLBACK (file_quit), de);


  g_signal_connect (get_widget_assert (de->xml, "windows_minimise_all"),
		    "activate",
		    G_CALLBACK (minimise_all_windows), NULL);


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
  GtkWidget *insert_variable = get_widget_assert (de->xml, "data_insert-variable");
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
#if 0
      gtk_widget_set_sensitive (insert_cases, TRUE);
#endif
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


/* Insert a new variable before the current row in the variable sheet,
   or before the current column in the data sheet, whichever is selected */
static void
insert_variable (GtkCheckMenuItem *m, gpointer data)
{
  struct data_editor *de = data;
  gint posn;

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
      struct variable *const * split_vars = dict_get_split_vars (dict->dict);

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

static void
weight_cases_dialog (GObject *o, gpointer data)
{
  gint response;
  struct data_editor *de = data;
  GtkSheet *var_sheet =
    GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));


  GladeXML *xml = glade_xml_new (PKGDATADIR "/psppire.glade",
				 "weight-cases-dialog", NULL);


  GtkWidget *treeview =  get_widget_assert (xml, "treeview");
  GtkWidget *entry =  get_widget_assert (xml, "entry1");


  PsppireVarStore *vs = PSPPIRE_VAR_STORE (gtk_sheet_get_model (var_sheet));

  PsppireVarSelect *select = psppire_var_select_new (treeview,
						     entry, vs->dict);


  PsppireDialog *dialog = create_weight_dialog (select, xml);

  response = psppire_dialog_run (dialog);

  g_object_unref (xml);

  switch (response)
    {
    case GTK_RESPONSE_OK:
    {
      struct getl_interface *sss ;
      const GList *list = psppire_var_select_get_variables (select);

      g_assert ( g_list_length ((GList *)list) <= 1 );

      if ( list == NULL)
	  {
	    sss = create_syntax_string_source ("WEIGHT OFF.");
	  }
      else
	{
	  struct variable *var = list->data;

	    sss = create_syntax_string_source ("WEIGHT BY %s.\n",
					       var_get_name (var));
	  }

	execute_syntax (sss);
	}
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	struct syntax_editor *se =  (struct syntax_editor *) window_create (WINDOW_SYNTAX, NULL);

	const GList *list = psppire_var_select_get_variables (select);

	g_assert ( g_list_length ((GList *)list) <= 1 );

	if ( list == NULL)
	  {
	    gtk_text_buffer_insert_at_cursor (se->buffer, "WEIGHT OFF.", -1);
	  }
	else
	  {
	    struct variable *var = list->data;

	    gchar *text = g_strdup_printf ("WEIGHT BY %s.",
					   var_get_name (var));

	    gtk_text_buffer_insert_at_cursor (se->buffer,
					      text, -1);

	    g_free (text);
	  }
      }
      break;
    default:
      break;
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

  g_assert (de->file_name);

  if ( de->save_as_portable )
    {
      append_filename_suffix (de, ".por");
      sss = create_syntax_string_source ("EXPORT OUTFILE='%s'.",
					 de->file_name);
    }
  else
    {
      append_filename_suffix (de, ".sav");
      sss = create_syntax_string_source ("SAVE OUTFILE='%s'.",
					 de->file_name);
    }

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
new_file (GtkAction *action, struct editor_window *de)
{
  struct getl_interface *sss =
    create_syntax_string_source ("NEW FILE.");

  execute_syntax (sss);

  default_window_name (de);
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

  switch (gtk_dialog_run (GTK_DIALOG (dialog)))
    {
    case GTK_RESPONSE_ACCEPT:
      {
	struct getl_interface *sss;
	g_free (de->file_name);
	de->file_name =
	  gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

	sss = create_syntax_string_source ("GET FILE='%s'.", de->file_name);

	execute_syntax (sss);

	window_set_name_from_filename (e, de->file_name);
      }
      break;
    default:
      break;
    }

  gtk_widget_destroy (dialog);
}




