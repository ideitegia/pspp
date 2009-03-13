/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009  Free Software Foundation

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

#include <gtk/gtksignal.h>
#include <gtk/gtkbox.h>
#include "helper.h"

#include "text-data-import-dialog.h"


#include <ui/syntax-gen.h>
#include <language/syntax-string-source.h>
#include <libpspp/message.h>
#include <stdlib.h>

#include <data/procedure.h>

#include "psppire.h"
#include "psppire-window.h"
#include "psppire-data-window.h"
#include "psppire-syntax-window.h"

#include "about.h"

#include "goto-case-dialog.h"
#include "weight-cases-dialog.h"
#include "split-file-dialog.h"
#include "transpose-dialog.h"
#include "sort-cases-dialog.h"
#include "select-cases-dialog.h"
#include "compute-dialog.h"
#include "find-dialog.h"
#include "rank-dialog.h"
#include "recode-dialog.h"
#include "comments-dialog.h"
#include "variable-info-dialog.h"
#include "descriptives-dialog.h"
#include "crosstabs-dialog.h"
#include "frequencies-dialog.h"
#include "examine-dialog.h"
#include "dict-display.h"
#include "regression-dialog.h"
#include "oneway-anova-dialog.h"
#include "t-test-independent-samples-dialog.h"
#include "t-test-one-sample.h"
#include "t-test-paired-samples.h"


#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid



static void psppire_data_window_base_finalize (PsppireDataWindowClass *, gpointer);
static void psppire_data_window_base_init     (PsppireDataWindowClass *class);
static void psppire_data_window_class_init    (PsppireDataWindowClass *class);
static void psppire_data_window_init          (PsppireDataWindow      *data_editor);


static void psppire_data_window_iface_init (PsppireWindowIface *iface);


GType
psppire_data_window_get_type (void)
{
  static GType psppire_data_window_type = 0;

  if (!psppire_data_window_type)
    {
      static const GTypeInfo psppire_data_window_info =
	{
	  sizeof (PsppireDataWindowClass),
	  (GBaseInitFunc) psppire_data_window_base_init,
	  (GBaseFinalizeFunc) psppire_data_window_base_finalize,
	  (GClassInitFunc)psppire_data_window_class_init,
	  (GClassFinalizeFunc) NULL,
	  NULL,
	  sizeof (PsppireDataWindow),
	  0,
	  (GInstanceInitFunc) psppire_data_window_init,
	};

      static const GInterfaceInfo window_interface_info =
	{
	  (GInterfaceInitFunc) psppire_data_window_iface_init,
	  NULL,
	  NULL
	};

      psppire_data_window_type =
	g_type_register_static (PSPPIRE_TYPE_WINDOW, "PsppireDataWindow",
				&psppire_data_window_info, 0);

      
      g_type_add_interface_static (psppire_data_window_type,
				   PSPPIRE_TYPE_WINDOW_MODEL,
				   &window_interface_info);
    }

  return psppire_data_window_type;
}

static GObjectClass *parent_class ;

static void
psppire_data_window_finalize (GObject *object)
{
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (object);

  g_object_unref (de->builder);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    (*G_OBJECT_CLASS (parent_class)->finalize) (object);
}


static void
psppire_data_window_class_init (PsppireDataWindowClass *class)
{
  parent_class = g_type_class_peek_parent (class);
}


static void
psppire_data_window_base_init (PsppireDataWindowClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = psppire_data_window_finalize;
}



static void
psppire_data_window_base_finalize (PsppireDataWindowClass *class,
				   gpointer class_data)
{
}





extern PsppireVarStore *the_var_store;
extern struct dataset *the_dataset;
extern PsppireDataStore *the_data_store ;

extern GtkRecentManager *the_recent_mgr;

static void
set_paste_menuitem_sensitivity (PsppireDataWindow *de, gboolean x)
{
  GtkAction *edit_paste = get_action_assert (de->builder, "edit_paste");

  gtk_action_set_sensitive (edit_paste, x);
}

static void
set_cut_copy_menuitem_sensitivity (PsppireDataWindow *de, gboolean x)
{
  GtkAction *edit_copy = get_action_assert (de->builder, "edit_copy");
  GtkAction *edit_cut = get_action_assert (de->builder, "edit_cut");

  gtk_action_set_sensitive (edit_copy, x);
  gtk_action_set_sensitive (edit_cut, x);
}

/* Run the EXECUTE command. */
static void
execute (GtkMenuItem *mi, gpointer data)
{
  struct getl_interface *sss = create_syntax_string_source ("EXECUTE.");

  execute_syntax (sss);
}

static void
transformation_change_callback (bool transformations_pending,
				gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  GtkUIManager *uim = GTK_UI_MANAGER (get_object_assert (de->builder, "uimanager1", GTK_TYPE_UI_MANAGER));

  GtkWidget *menuitem =
    gtk_ui_manager_get_widget (uim,"/ui/menubar/transform/transform_run-pending");

  GtkWidget *status_label  =
    get_widget_assert (de->builder, "case-counter-area");

  gtk_widget_set_sensitive (menuitem, transformations_pending);


  if ( transformations_pending)
    gtk_label_set_text (GTK_LABEL (status_label),
			_("Transformations Pending"));
  else
    gtk_label_set_text (GTK_LABEL (status_label), "");
}

/* Callback for when the dictionary changes its filter variable */
static void
on_filter_change (GObject *o, gint filter_index, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  GtkWidget *filter_status_area =
    get_widget_assert (de->builder, "filter-use-status-area");

  if ( filter_index == -1 )
    {
      gtk_label_set_text (GTK_LABEL (filter_status_area), _("Filter off"));
    }
  else
    {
      PsppireVarStore *vs = NULL;
      struct variable *var ;
      gchar *text ;

      g_object_get (de->data_editor, "var-store", &vs, NULL);

      var = psppire_dict_get_variable (vs->dict, filter_index);

      text = g_strdup_printf (_("Filter by %s"), var_get_name (var));

      gtk_label_set_text (GTK_LABEL (filter_status_area), text);

      g_free (text);
    }
}

/* Callback for when the dictionary changes its split variables */
static void
on_split_change (PsppireDict *dict, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  size_t n_split_vars = dict_get_split_cnt (dict->dict);

  GtkWidget *split_status_area =
    get_widget_assert (de->builder, "split-file-status-area");

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




/* Callback for when the dictionary changes its weights */
static void
on_weight_change (GObject *o, gint weight_index, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  GtkWidget *weight_status_area =
    get_widget_assert (de->builder, "weight-status-area");

  if ( weight_index == -1 )
    {
      gtk_label_set_text (GTK_LABEL (weight_status_area), _("Weights off"));
    }
  else
    {
      struct variable *var ;
      PsppireVarStore *vs = NULL;
      gchar *text;

      g_object_get (de->data_editor, "var-store", &vs, NULL);

      var = psppire_dict_get_variable (vs->dict, weight_index);

      text = g_strdup_printf (_("Weight by %s"), var_get_name (var));

      gtk_label_set_text (GTK_LABEL (weight_status_area), text);

      g_free (text);
    }
}

#if 0
static void
dump_rm (GtkRecentManager *rm)
{
  GList *items = gtk_recent_manager_get_items (rm);

  GList *i;

  g_print ("Recent Items:\n");
  for (i = items; i; i = i->next)
    {
      GtkRecentInfo *ri = i->data;

      g_print ("Item: %s (Mime: %s) (Desc: %s) (URI: %s)\n",
	       gtk_recent_info_get_short_name (ri),
	       gtk_recent_info_get_mime_type (ri),
	       gtk_recent_info_get_description (ri),
	       gtk_recent_info_get_uri (ri)
	       );


      gtk_recent_info_unref (ri);
    }

  g_list_free (items);
}
#endif


static gboolean
load_file (PsppireWindow *de, const gchar *file_name)
{
  struct getl_interface *sss;
  struct string filename;

  ds_init_empty (&filename);
  syntax_gen_string (&filename, ss_cstr (file_name));

  sss = create_syntax_string_source ("GET FILE=%s.",
				     ds_cstr (&filename));

  ds_destroy (&filename);

  if (execute_syntax (sss) )
    {
      psppire_window_set_filename (de, file_name);
      return TRUE;
    }

  return FALSE;
}


/* Callback for the data_open action.
   Prompts for a filename and opens it */
static void
open_data_dialog (GtkAction *action, PsppireWindow *de)
{
  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Open"),
				 GTK_WINDOW (de),
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


  {
    gchar *dir_name;
    gchar *filename = NULL;
    g_object_get (de, "filename", &filename, NULL);

    dir_name = g_path_get_dirname (filename);
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog),
					 dir_name);
    free (dir_name);
  }

  switch (gtk_dialog_run (GTK_DIALOG (dialog)))
    {
    case GTK_RESPONSE_ACCEPT:
      {
	gchar *name =
	  gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

	psppire_window_load (de, name);

	g_free (name);
      }
      break;
    default:
      break;
    }

  gtk_widget_destroy (dialog);
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


/* Save DE to file */
static void
save_file (PsppireWindow *w)
{
  gchar *fn = NULL;
  GString *fnx;
  struct getl_interface *sss;
  struct string file_name ;
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (w);

  g_object_get (w, "filename", &fn, NULL);

  fnx = g_string_new (fn);

  if ( ! name_has_suffix (fnx->str))
    {
      if ( de->save_as_portable)
	g_string_append (fnx, ".por");
      else
	g_string_append (fnx, ".sav");
    }

  ds_init_empty (&file_name);
  syntax_gen_string (&file_name, ss_cstr (fnx->str));
  g_string_free (fnx, FALSE);

  if ( de->save_as_portable )
    {
      sss = create_syntax_string_source ("EXPORT OUTFILE=%s.",
					 ds_cstr (&file_name));
    }
  else
    {
      sss = create_syntax_string_source ("SAVE OUTFILE=%s.",
					 ds_cstr (&file_name));
    }

  ds_destroy (&file_name);

  execute_syntax (sss);
}


static void
insert_case (GtkAction *action, gpointer data)
{
  PsppireDataWindow *dw = PSPPIRE_DATA_WINDOW (data);

  psppire_data_editor_insert_case (dw->data_editor);
}

static void
on_insert_variable (GtkAction *action, gpointer data)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (data);
  psppire_data_editor_insert_variable (de);
}


/* Callback for data_save_as action. Prompt for a filename and save */
static void
data_save_as_dialog (PsppireDataWindow *de)
{
  GtkWidget *button_sys;
  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Save"),
				 GTK_WINDOW (de),
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
	GString *filename =
	  g_string_new
	  (
	   gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog))
	   );

	de->save_as_portable =
	  ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button_sys));

	if ( ! name_has_suffix (filename->str))
	  {
	    if ( de->save_as_portable)
	      g_string_append (filename, ".por");
	    else
	      g_string_append (filename, ".sav");
	  }

	psppire_window_set_filename (PSPPIRE_WINDOW (de), filename->str);

	save_file (PSPPIRE_WINDOW (de));

	g_string_free (filename, TRUE);
      }
      break;
    default:
      break;
    }

  gtk_widget_destroy (dialog);
}


/* Callback for data_save action.
 */
static void
data_save (PsppireWindow *de)
{
  const gchar *fn = psppire_window_get_filename (de);

  if ( NULL != fn)
    psppire_window_save (de);
  else
    data_save_as_dialog (PSPPIRE_DATA_WINDOW (de));
}


/* Callback for data_new action.
   Performs the NEW FILE command */
static void
new_file (GtkAction *action, PsppireDataWindow *de)
{
  struct getl_interface *sss =
    create_syntax_string_source ("NEW FILE.");

  execute_syntax (sss);

  psppire_window_set_filename (PSPPIRE_WINDOW (de), NULL);
}



static void
on_edit_paste (GtkAction *a, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  psppire_data_editor_clip_paste (de->data_editor);
}

static void
on_edit_copy (GtkMenuItem *m, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  psppire_data_editor_clip_copy (de->data_editor);
}



static void
on_edit_cut (GtkMenuItem *m, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  psppire_data_editor_clip_cut (de->data_editor);
}


static void
status_bar_activate (GtkToggleAction *action, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);
  GtkWidget *statusbar = get_widget_assert (de->builder, "status-bar");

  if ( gtk_toggle_action_get_active (action) )
    gtk_widget_show (statusbar);
  else
    gtk_widget_hide (statusbar);
}


static void
grid_lines_activate (GtkToggleAction *action, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);
  const gboolean grid_visible = gtk_toggle_action_get_active (action);

  psppire_data_editor_show_grid (de->data_editor, grid_visible);
}

static void
data_view_activate (GtkCheckMenuItem *menuitem, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_DATA_VIEW);
}


static void
variable_view_activate (GtkCheckMenuItem *menuitem, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_VARIABLE_VIEW);
}


static void
fonts_activate (GtkMenuItem *menuitem, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);
  PangoFontDescription *current_font;
  gchar *font_name;
  GtkWidget *dialog =
    gtk_font_selection_dialog_new (_("Font Selection"));


  current_font = GTK_WIDGET(de->data_editor)->style->font_desc;
  font_name = pango_font_description_to_string (current_font);

  gtk_font_selection_dialog_set_font_name (GTK_FONT_SELECTION_DIALOG (dialog), font_name);

  g_free (font_name);

  gtk_window_set_transient_for (GTK_WINDOW (dialog),
				GTK_WINDOW (get_widget_assert (de->builder,
							       "data_editor")));
  if ( GTK_RESPONSE_OK == gtk_dialog_run (GTK_DIALOG (dialog)) )
    {
      const gchar *font = gtk_font_selection_dialog_get_font_name
	(GTK_FONT_SELECTION_DIALOG (dialog));

      PangoFontDescription* font_desc =
	pango_font_description_from_string (font);

      psppire_data_editor_set_font (de->data_editor, font_desc);
    }

  gtk_widget_hide (dialog);
}



/* Callback for the value labels action */
static void
toggle_value_labels (GtkToggleAction *ta, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  g_object_set (de->data_editor, "value-labels", gtk_toggle_action_get_active (ta), NULL);
}

static void
toggle_split_window (GtkToggleAction *ta, gpointer data)
{
  PsppireDataWindow  *de = PSPPIRE_DATA_WINDOW (data);

  psppire_data_editor_split_window (de->data_editor,
				    gtk_toggle_action_get_active (ta));
}


static void
file_quit (GtkCheckMenuItem *menuitem, gpointer data)
{
  /* FIXME: Need to be more intelligent here.
     Give the user the opportunity to save any unsaved data.
  */
  g_object_unref (the_data_store);

  psppire_quit ();
}



static GtkWidget *
create_data_sheet_variable_popup_menu (PsppireDataWindow *de)
{
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *sort_ascending =
    gtk_action_create_menu_item (gtk_action_new ("sort-up",
						 _("Sort Ascending"),
						 NULL,
						 "gtk-sort-ascending"));

  GtkWidget *sort_descending =
    gtk_action_create_menu_item (gtk_action_new ("sort-down",
						 _("Sort Descending"),
						 NULL,
						 "gtk-sort-descending"));

  GtkWidget *insert_variable =
    gtk_menu_item_new_with_label (_("Insert Variable"));

  GtkWidget *clear_variable =
    gtk_menu_item_new_with_label (_("Clear"));


  gtk_action_connect_proxy (de->delete_variables,
			    clear_variable );


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), insert_variable);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu),
			 gtk_separator_menu_item_new ());


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), clear_variable);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu),
			 gtk_separator_menu_item_new ());


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sort_ascending);


  g_signal_connect_swapped (sort_ascending, "activate",
			    G_CALLBACK (psppire_data_editor_sort_ascending),
			    de->data_editor);

  g_signal_connect_swapped (sort_descending, "activate",
			    G_CALLBACK (psppire_data_editor_sort_descending),
			    de->data_editor);

  g_signal_connect_swapped (insert_variable, "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->insert_variable);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sort_descending);

  gtk_widget_show_all (menu);

  return menu;
}


static GtkWidget *
create_data_sheet_cases_popup_menu (PsppireDataWindow *de)
{
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *insert_case =
    gtk_menu_item_new_with_label (_("Insert Case"));

  GtkWidget *delete_case =
    gtk_menu_item_new_with_label (_("Clear"));


  gtk_action_connect_proxy (de->delete_cases,
			    delete_case);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), insert_case);

  g_signal_connect_swapped (insert_case, "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->insert_case);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu),
			 gtk_separator_menu_item_new ());


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), delete_case);


  gtk_widget_show_all (menu);

  return menu;
}


static GtkWidget *
create_var_sheet_variable_popup_menu (PsppireDataWindow *de)
{
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *insert_variable =
    gtk_menu_item_new_with_label (_("Insert Variable"));

  GtkWidget *delete_variable =
    gtk_menu_item_new_with_label (_("Clear"));


  gtk_action_connect_proxy (de->delete_variables,
			    delete_variable);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), insert_variable);

  g_signal_connect_swapped (insert_variable, "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->insert_variable);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu),
			 gtk_separator_menu_item_new ());


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), delete_variable);


  gtk_widget_show_all (menu);

  return menu;
}


static void
on_recent_data_select (GtkMenuShell *menushell,
		       PsppireWindow *window)
{
  gchar *file;

  gchar *uri =
    gtk_recent_chooser_get_current_uri (GTK_RECENT_CHOOSER (menushell));

  file = g_filename_from_uri (uri, NULL, NULL);

  g_free (uri);

  psppire_window_load (window, file);

  g_free (file);
}

static void
on_recent_files_select (GtkMenuShell *menushell,   gpointer user_data)
{
  gchar *file;

  GtkWidget *se ;

  gchar *uri =
    gtk_recent_chooser_get_current_uri (GTK_RECENT_CHOOSER (menushell));

  file = g_filename_from_uri (uri, NULL, NULL);

  g_free (uri);

  se = psppire_syntax_window_new ();

  if ( psppire_window_load (PSPPIRE_WINDOW (se), file) ) 
    gtk_widget_show (se);
  else
    gtk_widget_destroy (se);

  g_free (file);
}


static void
enable_delete_cases (GtkWidget *w, gint case_num, gpointer data)
{
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);

  gtk_action_set_visible (de->delete_cases, case_num != -1);
}


static void
enable_delete_variables (GtkWidget *w, gint var, gpointer data)
{
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);

  gtk_action_set_visible (de->delete_variables, var != -1);
}

/* Callback for when the datasheet/varsheet is selected */
static void
on_switch_sheet (GtkNotebook *notebook,
		 GtkNotebookPage *page,
		 guint page_num,
		 gpointer user_data)
{
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (user_data);

  GtkUIManager *uim = GTK_UI_MANAGER (get_object_assert (de->builder, "uimanager1", GTK_TYPE_UI_MANAGER));

  GtkWidget *view_data =
    gtk_ui_manager_get_widget (uim,"/ui/menubar/view/view_data");

  GtkWidget *view_variables =
    gtk_ui_manager_get_widget (uim,"/ui/menubar/view/view_variables");

  switch (page_num)
    {
    case PSPPIRE_DATA_EDITOR_VARIABLE_VIEW:
      gtk_widget_hide (view_variables);
      gtk_widget_show (view_data);
      gtk_action_set_sensitive (de->insert_variable, TRUE);
      gtk_action_set_sensitive (de->insert_case, FALSE);
      gtk_action_set_sensitive (de->invoke_goto_dialog, FALSE);
      break;
    case PSPPIRE_DATA_EDITOR_DATA_VIEW:
      gtk_widget_show (view_variables);
      gtk_widget_hide (view_data);
      gtk_action_set_sensitive (de->invoke_goto_dialog, TRUE);
      gtk_action_set_sensitive (de->insert_case, TRUE);
      break;
    default:
      g_assert_not_reached ();
      break;
    }

#if 0
  update_paste_menuitem (de, page_num);
#endif
}


static GtkAction *
resolve_action (GtkBuilder *builder, const gchar *action, const gchar *proxy)
{
  GtkWidget *pr = NULL;
  GtkAction *act = get_action_assert (builder, action);
  g_assert (GTK_IS_ACTION (act));

  if ( proxy )
    pr = get_widget_assert (builder, proxy);

  if ( pr )
    gtk_action_connect_proxy (act, pr);

  return act;
}


static void
set_unsaved (gpointer w)
{
  psppire_window_set_unsaved (PSPPIRE_WINDOW (w));
}

static void
psppire_data_window_init (PsppireDataWindow *de)
{
  PsppireVarStore *vs;

  GtkWidget *menubar;
  GtkWidget *hb ;
  GtkWidget *sb ;

  GtkWidget *box = gtk_vbox_new (FALSE, 0);
  de->builder = builder_new ("data-editor.ui");

  menubar = get_widget_assert (de->builder, "menubar");
  hb = get_widget_assert (de->builder, "handlebox1");
  sb = get_widget_assert (de->builder, "status-bar");

  de->data_editor =
    PSPPIRE_DATA_EDITOR (psppire_data_editor_new (the_var_store, the_data_store));

  g_signal_connect_swapped (the_data_store, "case-changed",
			    G_CALLBACK (set_unsaved), de);

  g_signal_connect_swapped (the_data_store, "case-inserted",
			    G_CALLBACK (set_unsaved), de);

  g_signal_connect_swapped (the_data_store, "cases-deleted",
			    G_CALLBACK (set_unsaved), de);

  dataset_set_callback (the_dataset, set_unsaved, de);

  connect_help (de->builder);

  gtk_box_pack_start (GTK_BOX (box), menubar, FALSE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), hb, FALSE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (de->data_editor), TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), sb, FALSE, TRUE, 0);

  gtk_container_add (GTK_CONTAINER (de), box);

  set_cut_copy_menuitem_sensitivity (de, FALSE);

  g_signal_connect_swapped (de->data_editor, "data-selection-changed",
		    G_CALLBACK (set_cut_copy_menuitem_sensitivity), de);


  set_paste_menuitem_sensitivity (de, FALSE);

  g_signal_connect_swapped (de->data_editor, "data-available-changed",
		    G_CALLBACK (set_paste_menuitem_sensitivity), de);

  dataset_add_transform_change_callback (the_dataset,
					 transformation_change_callback,
					 de);


  vs = the_var_store;

  g_assert(vs); /* Traps a possible bug in w32 build */

  g_signal_connect (vs->dict, "weight-changed",
		    G_CALLBACK (on_weight_change),
		    de);

  g_signal_connect (vs->dict, "filter-changed",
		    G_CALLBACK (on_filter_change),
		    de);

  g_signal_connect (vs->dict, "split-changed",
		    G_CALLBACK (on_split_change),
		    de);


  g_signal_connect (get_action_assert (de->builder, "edit_copy"),
		    "activate",
		    G_CALLBACK (on_edit_copy), de);

  g_signal_connect (get_action_assert (de->builder, "edit_cut"),
		    "activate",
		    G_CALLBACK (on_edit_cut), de);



  {
    GtkWidget *toolbarbutton = get_widget_assert (de->builder, "button-open");

    GtkAction *action_data_open =
      resolve_action (de->builder, "file_open_data", NULL);

    g_object_set (action_data_open,
		  "tooltip",  _("Open a data file"),
		  "stock-id", "gtk-open",
		  NULL);

    g_signal_connect (action_data_open, "activate",
		      G_CALLBACK (open_data_dialog), de);

    g_signal_connect_swapped (toolbarbutton, "clicked",
		      G_CALLBACK (gtk_action_activate), action_data_open);
  }



  {
    GtkAction *action_data_new =
      resolve_action (de->builder, "file_new_data", NULL);

    g_object_set (action_data_new,
		  "tooltip", _("New data file"),
		  "stock-id", "gtk-new",
		  NULL);

    g_signal_connect (action_data_new, "activate",
		      G_CALLBACK (new_file), de);
  }



  {
    GtkAction *invoke_text_import_assistant =
      resolve_action (de->builder, "file_import-text", NULL);

    g_object_set (invoke_text_import_assistant,
		  "tooltip",  _("Import text data file"),
		  "stock-id", "gtk-convert",
		  NULL);

    g_signal_connect (invoke_text_import_assistant, "activate",
		      G_CALLBACK (text_data_import_assistant), de);
  }



  {
    GtkAction *action_data_save =
      resolve_action (de->builder, "file_save", "button-save");


    g_object_set (action_data_save,
		  "tooltip", _("Save data to file"),
		  "stock-id", "gtk-save",
		  NULL);

    g_signal_connect_swapped (action_data_save, "activate",
			      G_CALLBACK (data_save), de);
  }




  {
    GtkAction *action_data_save_as =
      resolve_action (de->builder, "file_save_as", NULL);

    g_object_set (action_data_save_as,
		  "label", _("Save As"),
		  "tooltip", _("Save data to file"),
		  "stock-id", "gtk-save-as",
		  NULL);

    g_signal_connect_swapped (action_data_save_as, "activate",
		      G_CALLBACK (data_save_as_dialog), de);
  }



  {
    GtkAction *value_labels_action =
      resolve_action (de->builder,
		      "view_value-labels", "togglebutton-value-labels");

    g_object_set (value_labels_action,
		  "tooltip",  _("Show/hide value labels"),
		  "stock-id", "pspp-value-labels",
		  NULL);

    g_signal_connect (value_labels_action, "toggled",
		      G_CALLBACK (toggle_value_labels), de);
  }


  g_signal_connect (get_action_assert (de->builder, "edit_paste"), "activate",
		    G_CALLBACK (on_edit_paste),
		    de);

  {
    de->delete_cases =
      resolve_action (de->builder, "edit_clear-cases", NULL);


    g_object_set (de->delete_cases,
		  "label", _("Clear"),
		  "tooltip", _("Delete the cases at the selected position(s)"),
		  "stock-id", "gtk-clear",
		  NULL);

    g_signal_connect_swapped (de->delete_cases, "activate",
			      G_CALLBACK (psppire_data_editor_delete_cases),
			      de->data_editor);

    gtk_action_set_visible (de->delete_cases, FALSE);
  }


  {
    de->delete_variables =
      resolve_action (de->builder, "edit_clear-variables", NULL);

    g_object_set (de->delete_variables,
		  "label", _("Clear"),
		  "tooltip", _("Delete the variables at the selected position(s)"),
		  "stock-id", "gtk-clear",
		  NULL);


    g_signal_connect_swapped (de->delete_variables, "activate",
			      G_CALLBACK (psppire_data_editor_delete_variables),
			      de->data_editor);

    gtk_action_set_visible (de->delete_variables, FALSE);
  }


  de->insert_variable =
    resolve_action (de->builder, "edit_insert-variable",
		    "button-insert-variable");

  g_object_set (de->insert_variable,
		"tooltip", _("Create a new variable at the current position"),
		"stock-id", "pspp-insert-variable",
		NULL);

  g_signal_connect (de->insert_variable, "activate",
		    G_CALLBACK (on_insert_variable), de->data_editor);





  de->insert_case =
    resolve_action (de->builder, "edit_insert-case", "button-insert-case");

  g_object_set (de->insert_case,
		"tooltip", _("Create a new case at the current position"),
		"stock-id", "pspp-insert-case",
		NULL);

  g_signal_connect (de->insert_case, "activate",
		    G_CALLBACK (insert_case), de);





  de->invoke_goto_dialog =
    resolve_action (de->builder, "edit_goto-case", "button-goto-case");


  g_object_set (de->invoke_goto_dialog,
		"tooltip", _("Jump to a Case in the Data Sheet"),
		"stock-id", "gtk-jump-to",
		NULL);

  g_signal_connect (de->invoke_goto_dialog, "activate",
		    G_CALLBACK (goto_case_dialog), de);



  {
    GtkAction *invoke_weight_cases_dialog =
      resolve_action (de->builder, "data_weight-cases", "button-weight-cases");


    g_object_set (invoke_weight_cases_dialog,
		  "stock-id", "pspp-weight-cases",
		  "tooltip", _("Weight cases by variable"),
		  NULL);

    g_signal_connect (invoke_weight_cases_dialog, "activate",
		      G_CALLBACK (weight_cases_dialog), de);
  }


  {
    GtkAction *invoke_transpose_dialog =
      resolve_action (de->builder, "data_transpose", NULL);


    g_object_set (invoke_transpose_dialog,
		  "tooltip", _("Transpose the cases with the variables"),
		  "stock-id", "pspp-transpose",
		  NULL);

    g_signal_connect (invoke_transpose_dialog, "activate",
		      G_CALLBACK (transpose_dialog), de);
  }


  {
    GtkAction *invoke_split_file_dialog =
      resolve_action (de->builder, "data_split-file", "button-split-file");

    g_object_set (invoke_split_file_dialog,
		  "tooltip", _("Split the active file"),
		  "stock-id", "pspp-split-file",
		  NULL);

    g_signal_connect (invoke_split_file_dialog, "activate",
		      G_CALLBACK (split_file_dialog), de);
  }


  {
    GtkAction *invoke_sort_cases_dialog =
      resolve_action (de->builder, "data_sort-cases", NULL);


    g_object_set (invoke_sort_cases_dialog,
		  "tooltip", _("Sort cases in the active file"),
		  "stock-id", "gtk-sort-ascending",
		  NULL);

    g_signal_connect (invoke_sort_cases_dialog, "activate",
		      G_CALLBACK (sort_cases_dialog), de);
  }


  {
    GtkAction *invoke_select_cases_dialog =
      resolve_action (de->builder, "data_select-cases", "button-select-cases");

    g_object_set (invoke_select_cases_dialog,
		  "tooltip", _("Select cases from the active file"),
		  "stock-id", "pspp-select-cases",
		  NULL);

    g_signal_connect (invoke_select_cases_dialog, "activate",
		      G_CALLBACK (select_cases_dialog), de);
  }


  {
    GtkAction *invoke_compute_dialog =
      resolve_action (de->builder, "transform_compute", NULL);

    g_object_set (invoke_compute_dialog,
		  "tooltip", _("Compute new values for a variable"),
		  "stock-id", "pspp-compute",
		  NULL);

    g_signal_connect (invoke_compute_dialog, "activate",
		      G_CALLBACK (compute_dialog), de);
  }


  {
    GtkAction *invoke_oneway_anova_dialog =
      resolve_action (de->builder, "oneway-anova", NULL);

    g_object_set (invoke_oneway_anova_dialog,
		  "tooltip", _("Perform one way analysis of variance"),
		  NULL);

    g_signal_connect (invoke_oneway_anova_dialog, "activate",
		      G_CALLBACK (oneway_anova_dialog), de);
  }


  {
    GtkAction *invoke_t_test_independent_samples_dialog =
      resolve_action (de->builder, "indep-t-test", NULL);


    g_object_set (invoke_t_test_independent_samples_dialog,
		  "tooltip",
		  _("Calculate T Test for samples from independent groups"),
		  NULL);

    g_signal_connect (invoke_t_test_independent_samples_dialog, "activate",
		      G_CALLBACK (t_test_independent_samples_dialog), de);
  }


  {
    GtkAction *invoke_t_test_paired_samples_dialog =
      resolve_action (de->builder, "paired-t-test", NULL);

    g_object_set (invoke_t_test_paired_samples_dialog,
		  "tooltip",
		  _("Calculate T Test for paired samples"),
		  NULL);

    g_signal_connect (invoke_t_test_paired_samples_dialog, "activate",
		      G_CALLBACK (t_test_paired_samples_dialog), de);
  }


  {
    GtkAction *invoke_t_test_one_sample_dialog =
      resolve_action (de->builder, "one-sample-t-test", NULL);

    g_object_set (invoke_t_test_one_sample_dialog,
		  "tooltip",
		  _("Calculate T Test for sample from a single distribution"),
		  NULL);

    g_signal_connect (invoke_t_test_one_sample_dialog, "activate",
		      G_CALLBACK (t_test_one_sample_dialog), de);
  }


  {
    GtkAction *invoke_comments_dialog =
      resolve_action (de->builder, "utilities_comments", NULL);


    g_object_set (invoke_comments_dialog,
		  "tooltip",
		  _("Commentary text for the data file"),
		  NULL);

    g_signal_connect (invoke_comments_dialog, "activate",
		      G_CALLBACK (comments_dialog), de);
  }



  {
    GtkAction *invoke_find_dialog =
      resolve_action (de->builder, "edit_find", "button-find");

    g_object_set (invoke_find_dialog, "stock-id", "gtk-find", NULL);

    g_signal_connect (invoke_find_dialog, "activate",
		      G_CALLBACK (find_dialog), de);
  }


  {
    GtkAction *invoke_rank_dialog =
      resolve_action (de->builder, "transform_rank", NULL);

    g_object_set (invoke_rank_dialog,
		  "stock-id", "pspp-rank-cases",
		  "tooltip", _("Rank Cases"),
		  NULL);

    g_signal_connect (invoke_rank_dialog, "activate",
		      G_CALLBACK (rank_dialog), de);
  }


  {
    GtkAction *invoke_recode_same_dialog =
      resolve_action (de->builder, "transform_recode-same", NULL);

    g_object_set (invoke_recode_same_dialog,
		  "stock-id", "pspp-recode-same",
		  "tooltip", _("Recode values into the same variables"),
		  NULL);

    g_signal_connect (invoke_recode_same_dialog, "activate",
		      G_CALLBACK (recode_same_dialog), de);
  }


  {
    GtkAction *invoke_recode_different_dialog  =
      resolve_action (de->builder, "transform_recode-different", NULL);

    g_object_set (invoke_recode_different_dialog,
		  "stock-id", "pspp-recode-different",
		  "tooltip", _("Recode values into different variables"),
		  NULL);

    g_signal_connect (invoke_recode_different_dialog, "activate",
		      G_CALLBACK (recode_different_dialog), de);
  }


  {
    GtkAction *invoke_variable_info_dialog  =
      resolve_action (de->builder, "utilities_variables", "button-goto-variable");

    g_object_set (invoke_variable_info_dialog,
		  "stock-id", "pspp-goto-variable",
		  "tooltip", _("Jump to variable"),
		  NULL);

    g_signal_connect (invoke_variable_info_dialog, "activate",
		      G_CALLBACK (variable_info_dialog), de);
  }


  {
    GtkAction *invoke_descriptives_dialog =
      resolve_action (de->builder,  "analyze_descriptives", NULL);

    g_object_set (invoke_descriptives_dialog,
		  "tooltip", _("Calculate descriptive statistics (mean, variance, ...)"),
		  "stock-id", "pspp-descriptives",
		  NULL);

    g_signal_connect (invoke_descriptives_dialog, "activate",
		      G_CALLBACK (descriptives_dialog), de);
  }


  {
    GtkAction *invoke_frequencies_dialog =
      resolve_action (de->builder,  "analyze_frequencies", NULL);

    g_object_set (invoke_frequencies_dialog,
		  "tooltip", _("Generate frequency statistics"),
		  "stock-id", "pspp-frequencies",
		  NULL);

    g_signal_connect (invoke_frequencies_dialog, "activate",
		      G_CALLBACK (frequencies_dialog), de);
  }


  {
    GtkAction *invoke_crosstabs_dialog =
      resolve_action (de->builder, "crosstabs", NULL);

    g_object_set (invoke_crosstabs_dialog,
		  "tooltip", _("Generate crosstabulations"),
		  "stock-id", "pspp-crosstabs",
		  NULL);

    g_signal_connect (invoke_crosstabs_dialog, "activate",
		      G_CALLBACK (crosstabs_dialog), de);
  }



  {
    GtkAction *invoke_examine_dialog =
      resolve_action (de->builder, "analyze_explore", NULL);

    g_object_set (invoke_examine_dialog,
		  "tooltip", _("Examine Data by Factors"),
		  "stock-id", "pspp-examine",
		  NULL);

    g_signal_connect (invoke_examine_dialog, "activate",
		      G_CALLBACK (examine_dialog), de);
  }


  {
    GtkAction *invoke_regression_dialog =
      resolve_action (de->builder, "linear-regression", NULL);

    g_object_set (invoke_regression_dialog,
		  "tooltip", _("Estimate parameters of the linear model"),
		  "stock-id", "pspp-regression",
		  NULL
		  );

    g_signal_connect (invoke_regression_dialog, "activate",
		      G_CALLBACK (regression_dialog), de);
  }

  { 
    GtkUIManager *uim = GTK_UI_MANAGER (get_object_assert (de->builder, "uimanager1", GTK_TYPE_UI_MANAGER));

    GtkWidget *recent_data =
      gtk_ui_manager_get_widget (uim,"/ui/menubar/file/file_recent-data");

    GtkWidget *recent_files =
      gtk_ui_manager_get_widget (uim,"/ui/menubar/file/file_recent-files");


    GtkWidget *menu_data =
      gtk_recent_chooser_menu_new_for_manager (the_recent_mgr);

    GtkWidget *menu_files =
      gtk_recent_chooser_menu_new_for_manager (the_recent_mgr);

    {
      GtkRecentFilter *filter = gtk_recent_filter_new ();

      gtk_recent_filter_add_pattern (filter, "*.sav");
      gtk_recent_filter_add_pattern (filter, "*.SAV");
      gtk_recent_filter_add_pattern (filter, "*.por");
      gtk_recent_filter_add_pattern (filter, "*.POR");

      gtk_recent_chooser_set_sort_type (GTK_RECENT_CHOOSER (menu_data), GTK_RECENT_SORT_MRU);

      gtk_recent_chooser_add_filter (GTK_RECENT_CHOOSER (menu_data), filter);
    }

    gtk_menu_item_set_submenu (GTK_MENU_ITEM (recent_data), menu_data);


    g_signal_connect (menu_data, "selection-done",
		      G_CALLBACK (on_recent_data_select),
		      de);

    {
      GtkRecentFilter *filter = gtk_recent_filter_new ();

      gtk_recent_filter_add_pattern (filter, "*.sps");
      gtk_recent_filter_add_pattern (filter, "*.SPS");

      gtk_recent_chooser_set_sort_type (GTK_RECENT_CHOOSER (menu_files), GTK_RECENT_SORT_MRU);

      gtk_recent_chooser_add_filter (GTK_RECENT_CHOOSER (menu_files), filter);
    }

    gtk_menu_item_set_submenu (GTK_MENU_ITEM (recent_files), menu_files);

    g_signal_connect (menu_files, "selection-done",
		      G_CALLBACK (on_recent_files_select),
		      de);

  }

  g_signal_connect (get_action_assert (de->builder,"file_new_syntax"),
		    "activate",
		    G_CALLBACK (create_syntax_window),
		    NULL);

  g_signal_connect (get_action_assert (de->builder,"file_open_syntax"),
		    "activate",
		    G_CALLBACK (open_syntax_window),
		    de);

  {
    GtkAction *abt = get_action_assert (de->builder, "help_about");
    g_object_set (abt, "stock-id", "gtk-about", NULL);
    g_signal_connect (abt,
		      "activate",
		      G_CALLBACK (about_new),
		      de);
  }


  g_signal_connect (get_action_assert (de->builder,"help_reference"),
		    "activate",
		    G_CALLBACK (reference_manual),
		    de);


  g_signal_connect (de->data_editor,
		    "cases-selected",
		    G_CALLBACK (enable_delete_cases),
		    de);

  g_signal_connect (de->data_editor,
		    "variables-selected",
		    G_CALLBACK (enable_delete_variables),
		    de);


  g_signal_connect (de->data_editor,
		    "switch-page",
		    G_CALLBACK (on_switch_sheet), de);

  gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_VARIABLE_VIEW);
  gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_DATA_VIEW);

  g_signal_connect (get_action_assert (de->builder, "view_statusbar"),
		    "activate",
		    G_CALLBACK (status_bar_activate), de);


  g_signal_connect (get_action_assert (de->builder, "view_gridlines"),
		    "activate",
		    G_CALLBACK (grid_lines_activate), de);



  g_signal_connect (get_action_assert (de->builder, "view_data"),
		    "activate",
		    G_CALLBACK (data_view_activate), de);

  g_signal_connect (get_action_assert (de->builder, "view_variables"),
		    "activate",
		    G_CALLBACK (variable_view_activate), de);


  {
    GtkAction *font_action =
      resolve_action (de->builder, "view_fonts", NULL);

    g_object_set (font_action,
		  "stock-id", "gtk-select-font",
		  NULL);

    g_signal_connect (font_action,
		      "activate",
		      G_CALLBACK (fonts_activate), de);
  }



  g_signal_connect (get_action_assert (de->builder, "file_quit"),
		    "activate",
		    G_CALLBACK (file_quit), de);

  g_signal_connect (get_action_assert (de->builder, "transform_run-pending"),
		    "activate",
		    G_CALLBACK (execute), de);


  g_signal_connect (get_action_assert (de->builder, "windows_minimise_all"),
		    "activate",
		    G_CALLBACK (psppire_window_minimise_all), NULL);


  {
    GtkAction *split_window_action =
      resolve_action (de->builder, "windows_split", NULL);

    g_object_set (split_window_action,
		  "tooltip", _("Split the window vertically and horizontally"),
		  "stock-id", "pspp-split-window",
		  NULL);

    g_signal_connect (split_window_action, "toggled",
		      G_CALLBACK (toggle_split_window),
		      de);
  }

  de->data_sheet_variable_popup_menu =
    GTK_MENU (create_data_sheet_variable_popup_menu (de));

  de->var_sheet_variable_popup_menu =
    GTK_MENU (create_var_sheet_variable_popup_menu (de));

  de->data_sheet_cases_popup_menu =
    GTK_MENU (create_data_sheet_cases_popup_menu (de));

  {
  GtkUIManager *uim = GTK_UI_MANAGER (get_object_assert (de->builder, "uimanager1", GTK_TYPE_UI_MANAGER));

  PSPPIRE_WINDOW (de)->menu =
    GTK_MENU_SHELL (gtk_ui_manager_get_widget (uim,"/ui/menubar/windows/windows_minimise_all")->parent);
  }


  g_object_set (de->data_editor,
		"datasheet-column-menu", de->data_sheet_variable_popup_menu,
		"datasheet-row-menu", de->data_sheet_cases_popup_menu,
		"varsheet-row-menu", de->var_sheet_variable_popup_menu,
		NULL);

  gtk_widget_show (GTK_WIDGET (de->data_editor));
  gtk_widget_show (box);
}


GtkWidget*
psppire_data_window_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_data_window_get_type (),
				   "description", _("Data Editor"),
				   NULL));
}






static void
psppire_data_window_iface_init (PsppireWindowIface *iface)
{
  iface->save = save_file;
  iface->load = load_file;
}
