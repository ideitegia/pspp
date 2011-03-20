/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010  Free Software Foundation

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
#include <stdlib.h>

#include "data/any-reader.h"
#include "data/dataset.h"
#include "language/lexer/lexer.h"
#include "libpspp/message.h"
#include "ui/gui/aggregate-dialog.h"
#include "ui/gui/binomial-dialog.h"
#include "ui/gui/chi-square-dialog.h"
#include "ui/gui/comments-dialog.h"
#include "ui/gui/compute-dialog.h"
#include "ui/gui/correlation-dialog.h"
#include "ui/gui/crosstabs-dialog.h"
#include "ui/gui/descriptives-dialog.h"
#include "ui/gui/examine-dialog.h"
#include "ui/gui/executor.h"
#include "ui/gui/factor-dialog.h"
#include "ui/gui/find-dialog.h"
#include "ui/gui/frequencies-dialog.h"
#include "ui/gui/goto-case-dialog.h"
#include "ui/gui/help-menu.h"
#include "ui/gui/helper.h"
#include "ui/gui/k-related-dialog.h"
#include "ui/gui/oneway-anova-dialog.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-syntax-window.h"
#include "ui/gui/psppire-window.h"
#include "ui/gui/psppire.h"
#include "ui/gui/rank-dialog.h"
#include "ui/gui/recode-dialog.h"
#include "ui/gui/regression-dialog.h"
#include "ui/gui/reliability-dialog.h"
#include "ui/gui/roc-dialog.h"
#include "ui/gui/select-cases-dialog.h"
#include "ui/gui/sort-cases-dialog.h"
#include "ui/gui/split-file-dialog.h"
#include "ui/gui/t-test-independent-samples-dialog.h"
#include "ui/gui/t-test-one-sample.h"
#include "ui/gui/t-test-paired-samples.h"
#include "ui/gui/text-data-import-dialog.h"
#include "ui/gui/transpose-dialog.h"
#include "ui/gui/variable-info-dialog.h"
#include "ui/gui/weight-cases-dialog.h"
#include "ui/syntax-gen.h"

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
execute (void)
{
  execute_const_syntax_string ("EXECUTE.");
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
      PsppireDict *dict = NULL;
      struct variable *var ;
      gchar *text ;

      g_object_get (de->data_editor, "var-store", &vs, NULL);
      g_object_get (vs, "dictionary", &dict, NULL);

      var = psppire_dict_get_variable (dict, filter_index);

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
      PsppireDict *dict = NULL;
      gchar *text;

      g_object_get (de->data_editor, "var-store", &vs, NULL);
      g_object_get (vs, "dictionary", &dict, NULL);

      var = psppire_dict_get_variable (dict, weight_index);

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
  gchar *native_file_name;
  struct string filename;
  gchar *syntax;
  bool ok;

  ds_init_empty (&filename);

  native_file_name =
    convert_glib_filename_to_system_filename (file_name, NULL);

  syntax_gen_string (&filename, ss_cstr (native_file_name));

  g_free (native_file_name);

  syntax = g_strdup_printf ("GET FILE=%s.", ds_cstr (&filename));
  ds_destroy (&filename);

  ok = execute_syntax (lex_reader_for_string (syntax));
  g_free (syntax);
  return ok;
}

static GtkWidget *
sysfile_chooser_dialog (PsppireWindow *toplevel)
{
  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Open"),
				 GTK_WINDOW (toplevel),
				 GTK_FILE_CHOOSER_ACTION_OPEN,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				 GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				 NULL);

  GtkFileFilter *filter;

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Data and Syntax Files"));
  gtk_file_filter_add_pattern (filter, "*.sav");
  gtk_file_filter_add_pattern (filter, "*.SAV");
  gtk_file_filter_add_pattern (filter, "*.por");
  gtk_file_filter_add_pattern (filter, "*.POR");
  gtk_file_filter_add_pattern (filter, "*.sps");
  gtk_file_filter_add_pattern (filter, "*.SPS");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

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
  gtk_file_filter_set_name (filter, _("Syntax Files (*.sps) "));
  gtk_file_filter_add_pattern (filter, "*.sps");
  gtk_file_filter_add_pattern (filter, "*.SPS");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  {
    gchar *dir_name;
    gchar *filename = NULL;
    g_object_get (toplevel, "filename", &filename, NULL);

    if ( ! g_path_is_absolute (filename))
      {
	gchar *path =
	  g_build_filename (g_get_current_dir (), filename, NULL);
	dir_name = g_path_get_dirname (path);
	g_free (path);
      }
    else
      {
	dir_name = g_path_get_dirname (filename);
      }
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog),
					 dir_name);
    free (dir_name);
  }

  return dialog;
}

/* Callback for the data_open action.
   Prompts for a filename and opens it */
static void
open_window (PsppireWindow *de)
{
  GtkWidget *dialog = sysfile_chooser_dialog (de);

  switch (gtk_dialog_run (GTK_DIALOG (dialog)))
    {
    case GTK_RESPONSE_ACCEPT:
      {
	gchar *name =
	  gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

	gchar *sysname = convert_glib_filename_to_system_filename (name, NULL);

	if (any_reader_may_open (sysname))
	  psppire_window_load (de, name);
	else
	  open_syntax_window (name);

	g_free (sysname);
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
  gchar *native_file_name = NULL;
  gchar *file_name = NULL;
  GString *fnx;
  struct string filename ;
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (w);
  gchar *syntax;

  g_object_get (w, "filename", &file_name, NULL);

  fnx = g_string_new (file_name);

  if ( ! name_has_suffix (fnx->str))
    {
      if ( de->save_as_portable)
	g_string_append (fnx, ".por");
      else
	g_string_append (fnx, ".sav");
    }

  ds_init_empty (&filename);

  native_file_name =
    convert_glib_filename_to_system_filename (fnx->str, NULL);

  g_string_free (fnx, TRUE);

  syntax_gen_string (&filename, ss_cstr (native_file_name));
  g_free (native_file_name);

  syntax = g_strdup_printf ("%s OUTFILE=%s.",
                            de->save_as_portable ? "EXPORT" : "SAVE",
                            ds_cstr (&filename));

  ds_destroy (&filename);

  g_free (execute_syntax_string (syntax));
}


static void
insert_case (PsppireDataWindow *dw)
{
  psppire_data_editor_insert_case (dw->data_editor);
}

static void
on_insert_variable (PsppireDataWindow *dw)
{
  psppire_data_editor_insert_variable (dw->data_editor);
}


static void
display_dict (PsppireDataWindow *de)
{
  execute_const_syntax_string ("DISPLAY DICTIONARY.");
}

static void
sysfile_info (PsppireDataWindow *de)
{
  GtkWidget *dialog = sysfile_chooser_dialog (PSPPIRE_WINDOW (de));

  if  ( GTK_RESPONSE_ACCEPT == gtk_dialog_run (GTK_DIALOG (dialog)))
    {
      struct string filename;
      gchar *file_name =
	gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

      gchar *native_file_name =
	convert_glib_filename_to_system_filename (file_name, NULL);

      gchar *syntax;

      ds_init_empty (&filename);

      syntax_gen_string (&filename, ss_cstr (native_file_name));

      g_free (native_file_name);

      syntax = g_strdup_printf ("SYSFILE INFO %s.", ds_cstr (&filename));
      g_free (execute_syntax_string (syntax));
    }

  gtk_widget_destroy (dialog);
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

    psppire_box_pack_start_defaults (GTK_BOX (vbox), button_sys);
    psppire_box_pack_start_defaults (GTK_BOX (vbox), button_por);

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
new_file (PsppireDataWindow *de)
{
  execute_const_syntax_string ("NEW FILE.");
  psppire_window_set_filename (PSPPIRE_WINDOW (de), NULL);
}



static void
on_edit_paste (PsppireDataWindow  *de)
{
  psppire_data_editor_clip_paste (de->data_editor);
}

static void
on_edit_copy (PsppireDataWindow  *de)
{
  psppire_data_editor_clip_copy (de->data_editor);
}



static void
on_edit_cut (PsppireDataWindow  *de)
{
  psppire_data_editor_clip_cut (de->data_editor);
}


static void
status_bar_activate (PsppireDataWindow  *de, GtkToggleAction *action)
{
  GtkWidget *statusbar = get_widget_assert (de->builder, "status-bar");

  if ( gtk_toggle_action_get_active (action))
    gtk_widget_show (statusbar);
  else
    gtk_widget_hide (statusbar);
}


static void
grid_lines_activate (PsppireDataWindow  *de, GtkToggleAction *action)
{
  const gboolean grid_visible = gtk_toggle_action_get_active (action);

  psppire_data_editor_show_grid (de->data_editor, grid_visible);
}

static void
data_view_activate (PsppireDataWindow  *de)
{
  gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_DATA_VIEW);
}


static void
variable_view_activate (PsppireDataWindow  *de)
{
  gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_VARIABLE_VIEW);
}


static void
fonts_activate (PsppireDataWindow  *de)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (de));
  PangoFontDescription *current_font;
  gchar *font_name;
  GtkWidget *dialog =
    gtk_font_selection_dialog_new (_("Font Selection"));


  current_font = GTK_WIDGET(de->data_editor)->style->font_desc;
  font_name = pango_font_description_to_string (current_font);

  gtk_font_selection_dialog_set_font_name (GTK_FONT_SELECTION_DIALOG (dialog), font_name);

  g_free (font_name);

  gtk_window_set_transient_for (GTK_WINDOW (dialog),
				GTK_WINDOW (toplevel));

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
toggle_value_labels (PsppireDataWindow  *de, GtkToggleAction *ta)
{
  g_object_set (de->data_editor, "value-labels", gtk_toggle_action_get_active (ta), NULL);
}

static void
toggle_split_window (PsppireDataWindow  *de, GtkToggleAction *ta)
{
  psppire_data_editor_split_window (de->data_editor,
				    gtk_toggle_action_get_active (ta));
}


static void
file_quit (void)
{
  /* FIXME: Need to be more intelligent here.
     Give the user the opportunity to save any unsaved data.
  */
  g_object_unref (the_data_store);

  psppire_quit ();
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



static void
set_unsaved (gpointer w)
{
  psppire_window_set_unsaved (PSPPIRE_WINDOW (w));
}


/* Connects the action called ACTION_NAME to HANDLER passing DW as the auxilliary data.
   Returns a pointer to the action
*/
static GtkAction *
connect_action (PsppireDataWindow *dw, const char *action_name, 
				    GCallback handler)
{
  GtkAction *action = get_action_assert (dw->builder, action_name);
 
  g_signal_connect_swapped (action, "activate", handler, dw);

  return action;
}

static void
psppire_data_window_init (PsppireDataWindow *de)
{
  PsppireVarStore *vs;
  PsppireDict *dict = NULL;

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

  g_object_get (vs, "dictionary", &dict, NULL);

  g_signal_connect (dict, "weight-changed",
		    G_CALLBACK (on_weight_change),
		    de);

  g_signal_connect (dict, "filter-changed",
		    G_CALLBACK (on_filter_change),
		    de);

  g_signal_connect (dict, "split-changed",
		    G_CALLBACK (on_split_change),
		    de);


  connect_action (de, "edit_copy", G_CALLBACK (on_edit_copy));

  connect_action (de, "edit_cut", G_CALLBACK (on_edit_cut));

  connect_action (de, "file_new_data", G_CALLBACK (new_file));

  connect_action (de, "file_import-text", G_CALLBACK (text_data_import_assistant));

  connect_action (de, "file_save", G_CALLBACK (data_save));
 
  connect_action (de, "file_open", G_CALLBACK (open_window));

  connect_action (de, "file_save_as", G_CALLBACK (data_save_as_dialog));

  connect_action (de, "file_information_working-file", G_CALLBACK (display_dict));

  connect_action (de, "file_information_external-file", G_CALLBACK (sysfile_info));

  connect_action (de, "edit_paste", G_CALLBACK (on_edit_paste));

  de->insert_case = connect_action (de, "edit_insert-case", G_CALLBACK (insert_case));

  de->insert_variable = connect_action (de, "action_insert-variable", G_CALLBACK (on_insert_variable));

  de->invoke_goto_dialog = connect_action (de, "edit_goto-case", G_CALLBACK (goto_case_dialog));

  g_signal_connect_swapped (get_action_assert (de->builder, "view_value-labels"), "toggled", G_CALLBACK (toggle_value_labels), de);

  {
    de->delete_cases = get_action_assert (de->builder, "edit_clear-cases");

    g_signal_connect_swapped (de->delete_cases, "activate", G_CALLBACK (psppire_data_editor_delete_cases), de->data_editor);

    gtk_action_set_visible (de->delete_cases, FALSE);
  }


  {
    de->delete_variables = get_action_assert (de->builder, "edit_clear-variables");

    g_signal_connect_swapped (de->delete_variables, "activate", G_CALLBACK (psppire_data_editor_delete_variables), de->data_editor);

    gtk_action_set_visible (de->delete_variables, FALSE);
  }


  connect_action (de, "data_transpose", G_CALLBACK (transpose_dialog));

  connect_action (de, "data_select-cases", G_CALLBACK (select_cases_dialog));
 
  connect_action (de, "data_sort-cases", G_CALLBACK (sort_cases_dialog));

  connect_action (de, "data_aggregate", G_CALLBACK (aggregate_dialog));

  connect_action (de, "transform_compute", G_CALLBACK (compute_dialog));

  connect_action (de, "edit_find", G_CALLBACK (find_dialog));

  connect_action (de, "data_split-file", G_CALLBACK (split_file_dialog));

  connect_action (de, "data_weight-cases", G_CALLBACK (weight_cases_dialog));


  connect_action (de, "utilities_variables", G_CALLBACK (variable_info_dialog));
 
  connect_action (de, "oneway-anova", G_CALLBACK (oneway_anova_dialog));

  connect_action (de, "indep-t-test", G_CALLBACK (t_test_independent_samples_dialog));

  connect_action (de, "paired-t-test", G_CALLBACK (t_test_paired_samples_dialog));

  connect_action (de, "one-sample-t-test", G_CALLBACK (t_test_one_sample_dialog));

  connect_action (de, "utilities_comments", G_CALLBACK (comments_dialog));
 
  connect_action (de, "transform_rank", G_CALLBACK (rank_dialog));
 
  connect_action (de, "transform_recode-same", G_CALLBACK (recode_same_dialog));
 
  connect_action (de, "transform_recode-different", G_CALLBACK (recode_different_dialog));

  connect_action (de, "analyze_descriptives", G_CALLBACK (descriptives_dialog));
 
  connect_action (de, "analyze_frequencies", G_CALLBACK (frequencies_dialog));
 
  connect_action (de, "crosstabs", G_CALLBACK (crosstabs_dialog));
 
  connect_action (de, "analyze_explore", G_CALLBACK (examine_dialog));
 
  connect_action (de, "linear-regression", G_CALLBACK (regression_dialog));
 
  connect_action (de, "reliability", G_CALLBACK (reliability_dialog));
 
  connect_action (de, "roc-curve", G_CALLBACK (roc_dialog));

  connect_action (de, "correlation", G_CALLBACK (correlation_dialog));
 
  connect_action (de, "factor-analysis", G_CALLBACK (factor_dialog));

  connect_action (de, "chi-square", G_CALLBACK (chisquare_dialog));

  connect_action (de, "binomial", G_CALLBACK (binomial_dialog));

  connect_action (de, "k-related-samples", G_CALLBACK (k_related_dialog));
 

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


    g_signal_connect (menu_data, "selection-done", G_CALLBACK (on_recent_data_select), de);

    {
      GtkRecentFilter *filter = gtk_recent_filter_new ();

      gtk_recent_filter_add_pattern (filter, "*.sps");
      gtk_recent_filter_add_pattern (filter, "*.SPS");

      gtk_recent_chooser_set_sort_type (GTK_RECENT_CHOOSER (menu_files), GTK_RECENT_SORT_MRU);

      gtk_recent_chooser_add_filter (GTK_RECENT_CHOOSER (menu_files), filter);
    }

    gtk_menu_item_set_submenu (GTK_MENU_ITEM (recent_files), menu_files);

    g_signal_connect (menu_files, "selection-done", G_CALLBACK (on_recent_files_select), de);

  }

  connect_action (de, "file_new_syntax", G_CALLBACK (create_syntax_window));


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

  connect_action (de, "view_statusbar", G_CALLBACK (status_bar_activate));

  connect_action (de, "view_gridlines", G_CALLBACK (grid_lines_activate));

  connect_action (de, "view_data", G_CALLBACK (data_view_activate));

  connect_action (de, "view_variables", G_CALLBACK (variable_view_activate));

  connect_action (de, "view_fonts", G_CALLBACK (fonts_activate));

  connect_action (de, "file_quit", G_CALLBACK (file_quit));

  connect_action (de, "transform_run-pending", G_CALLBACK (execute));

  connect_action (de, "windows_minimise_all", G_CALLBACK (psppire_window_minimise_all));

  g_signal_connect_swapped (get_action_assert (de->builder, "windows_split"), "toggled", G_CALLBACK (toggle_split_window), de);

  {
    GtkUIManager *uim = GTK_UI_MANAGER (get_object_assert (de->builder, "uimanager1", GTK_TYPE_UI_MANAGER));

    merge_help_menu (uim);
    
    PSPPIRE_WINDOW (de)->menu =
      GTK_MENU_SHELL (gtk_ui_manager_get_widget (uim,"/ui/menubar/windows/windows_minimise_all")->parent);
  }

  {
    GtkWidget *data_sheet_cases_popup_menu = get_widget_assert (de->builder,
								"datasheet-cases-popup");

    GtkWidget *var_sheet_variable_popup_menu = get_widget_assert (de->builder,
								  "varsheet-variable-popup");

    GtkWidget *data_sheet_variable_popup_menu = get_widget_assert (de->builder,
								   "datasheet-variable-popup");

    g_signal_connect_swapped (get_action_assert (de->builder, "sort-up"), "activate",
			      G_CALLBACK (psppire_data_editor_sort_ascending),
			      de->data_editor);

    g_signal_connect_swapped (get_action_assert (de->builder, "sort-down"), "activate",
			      G_CALLBACK (psppire_data_editor_sort_descending),
			      de->data_editor);

    g_object_set (de->data_editor,
		  "datasheet-column-menu", data_sheet_variable_popup_menu,
		  "datasheet-row-menu", data_sheet_cases_popup_menu,
		  "varsheet-row-menu", var_sheet_variable_popup_menu,
		  NULL);
  }

  gtk_widget_show (GTK_WIDGET (de->data_editor));
  gtk_widget_show (box);
}


GtkWidget*
psppire_data_window_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_data_window_get_type (),
				   /* TRANSLATORS: This will form a filename.  Please avoid whitespace. */
				   "filename", _("PSPP-data"),
				   "description", _("Data Editor"),
				   NULL));
}


static void
psppire_data_window_iface_init (PsppireWindowIface *iface)
{
  iface->save = save_file;
  iface->load = load_file;
}

