/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2007, 2008  Free Software Foundation

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

#include "psppire-data-editor.h"

#include "helper.h"
#include "about.h"
#include <data/procedure.h>
#include "psppire-dialog.h"
#include "psppire-selector.h"
#include "weight-cases-dialog.h"
#include "split-file-dialog.h"
#include "transpose-dialog.h"
#include "sort-cases-dialog.h"
#include "select-cases-dialog.h"
#include "compute-dialog.h"
#include "goto-case-dialog.h"
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
#include "text-data-import-dialog.h"

#include "oneway-anova-dialog.h"
#include "t-test-independent-samples-dialog.h"
#include "t-test-one-sample.h"
#include "t-test-paired-samples.h"

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include "data-editor.h"
#include "syntax-editor.h"
#include <language/syntax-string-source.h>
#include <language/command.h>
#include <ui/syntax-gen.h>
#include "window-manager.h"

#include "psppire-data-store.h"
#include "psppire-var-store.h"

static void on_edit_copy (GtkMenuItem *, gpointer);
static void on_edit_cut (GtkMenuItem *, gpointer);
static void on_edit_paste (GtkAction *a, gpointer data);


static GtkWidget * create_data_sheet_variable_popup_menu (struct data_editor *);
static GtkWidget * create_data_sheet_cases_popup_menu (struct data_editor *);

static void register_data_editor_actions (struct data_editor *de);
static void on_insert_variable (GtkAction *, gpointer data);
static void insert_case (GtkAction *a, gpointer data);

static void toggle_value_labels (GtkToggleAction *a, gpointer data);

/* Callback for when the dictionary changes properties*/
static void on_weight_change (GObject *, gint, gpointer);
static void on_filter_change (GObject *, gint, gpointer);
static void on_split_change (PsppireDict *, gpointer);

static void on_switch_sheet (GtkNotebook *notebook,
			    GtkNotebookPage *page,
			    guint page_num,
			    gpointer user_data);

static void status_bar_activate (GtkCheckMenuItem *, gpointer);

static void grid_lines_activate (GtkCheckMenuItem *, gpointer);

static void data_view_activate (GtkCheckMenuItem *, gpointer);

static void variable_view_activate (GtkCheckMenuItem *, gpointer );

static void fonts_activate (GtkMenuItem *, gpointer);

static void file_quit (GtkCheckMenuItem *, gpointer );

static void
enable_delete_cases (GtkWidget *w, gint case_num, gpointer data)
{
  struct data_editor *de = data;

  gtk_action_set_visible (de->delete_cases, case_num != -1);
}


static void
enable_delete_variables (GtkWidget *w, gint var, gpointer data)
{
  struct data_editor *de = data;

  gtk_action_set_visible (de->delete_variables, var != -1);
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
  struct data_editor *de = data;
  GtkWidget *menuitem =
    get_widget_assert (de->xml, "transform_run-pending");
  GtkWidget *status_label  =
    get_widget_assert (de->xml, "case-counter-area");

  gtk_widget_set_sensitive (menuitem, transformations_pending);


  if ( transformations_pending)
    gtk_label_set_text (GTK_LABEL (status_label),
			_("Transformations Pending"));
  else
    gtk_label_set_text (GTK_LABEL (status_label), "");
}


static void open_data_file (const gchar *, struct data_editor *);


/* Puts FILE_NAME into the recent list.
   If it's already in the list, it moves it to the top
*/
static void
add_most_recent (const char *file_name)
{
#if RECENT_LISTS_AVAILABLE

  GtkRecentManager *manager = gtk_recent_manager_get_default();
  gchar *uri = g_filename_to_uri (file_name, NULL, NULL);

  gtk_recent_manager_remove_item (manager, uri, NULL);

  if ( ! gtk_recent_manager_add_item (manager, uri))
    g_warning ("Could not add item %s to recent list\n",uri);

  g_free (uri);
#endif
}



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
update_paste_menuitems (GtkWidget *w, gboolean x, gpointer data)
{
  struct data_editor *de = data;

  GtkWidget * edit_paste = get_widget_assert (de->xml, "edit_paste");

  gtk_widget_set_sensitive (edit_paste, x);
}

static void
update_cut_copy_menuitems (GtkWidget *w, gboolean x, gpointer data)
{
  struct data_editor *de = data;

  GtkWidget * edit_copy = get_widget_assert (de->xml, "edit_copy");
  GtkWidget * edit_cut = get_widget_assert (de->xml, "edit_cut");

  gtk_widget_set_sensitive (edit_copy, x);
  gtk_widget_set_sensitive (edit_cut, x);
}

extern PsppireVarStore *the_var_store;
extern struct dataset *the_dataset;
extern PsppireDataStore *the_data_store ;


/*
  Create a new data editor.
*/
struct data_editor *
new_data_editor (void)
{
  struct data_editor *de ;
  struct editor_window *e;
  PsppireVarStore *vs;
  GtkWidget *vbox ;

  de = g_malloc0 (sizeof (*de));

  e = (struct editor_window *) de;

  de->xml = XML_NEW ("data-editor.glade");


  vbox = get_widget_assert (de->xml, "vbox1");

  de->data_editor = PSPPIRE_DATA_EDITOR (psppire_data_editor_new (the_var_store, the_data_store));

  g_signal_connect (de->data_editor, "data-selection-changed",
		    G_CALLBACK (update_cut_copy_menuitems), de);

  g_signal_connect (de->data_editor, "data-available-changed",
		    G_CALLBACK (update_paste_menuitems), de);


  gtk_widget_show (GTK_WIDGET (de->data_editor));

  gtk_container_add (GTK_CONTAINER (vbox), GTK_WIDGET (de->data_editor));
  gtk_box_reorder_child (GTK_BOX (vbox) , GTK_WIDGET (de->data_editor), 2);
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

  connect_help (de->xml);



  g_signal_connect (get_widget_assert (de->xml, "edit_copy"),
		    "activate",
		    G_CALLBACK (on_edit_copy), de);

  g_signal_connect (get_widget_assert (de->xml, "edit_cut"),
		    "activate",
		    G_CALLBACK (on_edit_cut), de);


  register_data_editor_actions (de);

  de->toggle_value_labels =
    gtk_toggle_action_new ("toggle-value-labels",
			   _("_Labels"),
			   _("Show/hide value labels"),
			   "pspp-value-labels");

  g_signal_connect (de->toggle_value_labels, "toggled",
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

  g_signal_connect_swapped (de->delete_cases, "activate",
		    G_CALLBACK (psppire_data_editor_delete_cases),
		    de->data_editor);

  gtk_action_connect_proxy (de->delete_cases,
			    get_widget_assert (de->xml, "edit_clear-cases"));

  g_signal_connect (get_widget_assert (de->xml, "edit_paste"), "activate",
		    G_CALLBACK (on_edit_paste),
		    de);

  gtk_action_set_visible (de->delete_cases, FALSE);

  de->delete_variables =
    gtk_action_new ("clear-variables",
		    _("Clear"),
		    _("Delete the variables at the selected position(s)"),
		    "pspp-clear-variables");

  g_signal_connect_swapped (de->delete_variables, "activate",
			    G_CALLBACK (psppire_data_editor_delete_variables),
			    de->data_editor);

  gtk_action_connect_proxy (de->delete_variables,
			    get_widget_assert (de->xml, "edit_clear-variables")
			    );

  gtk_action_set_visible (de->delete_variables, FALSE);

  de->insert_variable =
    gtk_action_new ("insert-variable",
		    _("Insert _Variable"),
		    _("Create a new variable at the current position"),
		    "pspp-insert-variable");

  g_signal_connect (de->insert_variable, "activate",
		    G_CALLBACK (on_insert_variable), de->data_editor);


  gtk_action_connect_proxy (de->insert_variable,
			    get_widget_assert (de->xml, "button-insert-variable")
			    );

  gtk_action_connect_proxy (de->insert_variable,
			    get_widget_assert (de->xml, "edit_insert-variable")
			    );


  de->insert_case =
    gtk_action_new ("insert-case",
		    _("Insert Ca_se"),
		    _("Create a new case at the current position"),
		    "pspp-insert-case");

  g_signal_connect (de->insert_case, "activate",
		    G_CALLBACK (insert_case), de);


  gtk_action_connect_proxy (de->insert_case,
			    get_widget_assert (de->xml, "button-insert-case")
			    );


  gtk_action_connect_proxy (de->insert_case,
			    get_widget_assert (de->xml, "edit_insert-case")
			    );



  de->invoke_goto_dialog =
    gtk_action_new ("goto-case-dialog",
		    _("_Goto Case"),
		    _("Jump to a Case in the Data Sheet"),
		    "gtk-jump-to");


  gtk_action_connect_proxy (de->invoke_goto_dialog,
			    get_widget_assert (de->xml, "button-goto-case")
			    );

  gtk_action_connect_proxy (de->invoke_goto_dialog,
			    get_widget_assert (de->xml, "edit_goto-case")
			    );


  g_signal_connect (de->invoke_goto_dialog, "activate",
		    G_CALLBACK (goto_case_dialog), de);


  de->invoke_weight_cases_dialog =
    gtk_action_new ("weight-cases-dialog",
		    _("_Weights"),
		    _("Weight cases by variable"),
		    "pspp-weight-cases");

  g_signal_connect (de->invoke_weight_cases_dialog, "activate",
		    G_CALLBACK (weight_cases_dialog), de);


  de->invoke_transpose_dialog =
    gtk_action_new ("transpose-dialog",
		    _("_Transpose"),
		    _("Transpose the cases with the variables"),
		    NULL);


  g_signal_connect (de->invoke_transpose_dialog, "activate",
		    G_CALLBACK (transpose_dialog), de);



  de->invoke_split_file_dialog =
    gtk_action_new ("split-file-dialog",
		    _("S_plit"),
		    _("Split the active file"),
		    "pspp-split-file");

  g_signal_connect (de->invoke_split_file_dialog, "activate",
		    G_CALLBACK (split_file_dialog), de);



  de->invoke_sort_cases_dialog =
    gtk_action_new ("sort-cases-dialog",
		    _("_Sort"),
		    _("Sort cases in the active file"),
		    "pspp-sort-cases");

  g_signal_connect (de->invoke_sort_cases_dialog, "activate",
		    G_CALLBACK (sort_cases_dialog), de);

  de->invoke_select_cases_dialog =
    gtk_action_new ("select-cases-dialog",
		    _("Select _Cases"),
		    _("Select cases from the active file"),
		    "pspp-select-cases");

  g_signal_connect (de->invoke_select_cases_dialog, "activate",
		    G_CALLBACK (select_cases_dialog), de);


  de->invoke_compute_dialog =
    gtk_action_new ("compute-dialog",
		    _("_Compute"),
		    _("Compute new values for a variable"),
		    "pspp-compute");

  g_signal_connect (de->invoke_compute_dialog, "activate",
		    G_CALLBACK (compute_dialog), de);

  de->invoke_oneway_anova_dialog =
    gtk_action_new ("oneway-anova",
		    _("Oneway _ANOVA"),
		    _("Perform one way analysis of variance"),
		    NULL);

  g_signal_connect (de->invoke_oneway_anova_dialog, "activate",
		    G_CALLBACK (oneway_anova_dialog), de);

  de->invoke_t_test_independent_samples_dialog =
    gtk_action_new ("t-test-independent-samples",
		    _("_Independent Samples T Test"),
		    _("Calculate T Test for samples from independent groups"),
		    NULL);

  g_signal_connect (de->invoke_t_test_independent_samples_dialog, "activate",
		    G_CALLBACK (t_test_independent_samples_dialog), de);


  de->invoke_t_test_paired_samples_dialog =
    gtk_action_new ("t-test-paired-samples",
		    _("_Paired Samples T Test"),
		    _("Calculate T Test for paired samples"),
		    NULL);

  g_signal_connect (de->invoke_t_test_paired_samples_dialog, "activate",
		    G_CALLBACK (t_test_paired_samples_dialog), de);


  de->invoke_t_test_one_sample_dialog =
    gtk_action_new ("t-test-one-sample",
		    _("One _Sample T Test"),
		    _("Calculate T Test for sample from a single distribution"),
		    NULL);

  g_signal_connect (de->invoke_t_test_one_sample_dialog, "activate",
		    G_CALLBACK (t_test_one_sample_dialog), de);


  de->invoke_comments_dialog =
    gtk_action_new ("commments-dialog",
		    _("Data File _Comments"),
		    _("Commentary text for the data file"),
		    NULL);

  g_signal_connect (de->invoke_comments_dialog, "activate",
		    G_CALLBACK (comments_dialog), de);

  de->invoke_find_dialog  =
    gtk_action_new ("find-dialog",
		    _("_Find"),
		    _("Find Case"),
		    "gtk-find");

  g_signal_connect (de->invoke_find_dialog, "activate",
		    G_CALLBACK (find_dialog), de);


  de->invoke_rank_dialog  =
    gtk_action_new ("rank-dialog",
		    _("Ran_k Cases"),
		    _("Rank Cases"),
		    "pspp-rank-cases");

  g_signal_connect (de->invoke_rank_dialog, "activate",
		    G_CALLBACK (rank_dialog), de);


  de->invoke_recode_same_dialog  =
    gtk_action_new ("recode-same-dialog",
		    _("Recode into _Same Variables"),
		    _("Recode values into the same Variables"),
		    "pspp-recode-same");

  g_signal_connect (de->invoke_recode_same_dialog, "activate",
		    G_CALLBACK (recode_same_dialog), de);


  de->invoke_recode_different_dialog  =
    gtk_action_new ("recode-different-dialog",
		    _("Recode into _Different Variables"),
		    _("Recode values into different Variables"),
		    "pspp-recode-different");

  g_signal_connect (de->invoke_recode_different_dialog, "activate",
		    G_CALLBACK (recode_different_dialog), de);


  de->invoke_variable_info_dialog  =
    gtk_action_new ("variable-info-dialog",
		    _("_Variables"),
		    _("Jump to Variable"),
		    "pspp-goto-variable");

  g_signal_connect (de->invoke_variable_info_dialog, "activate",
		    G_CALLBACK (variable_info_dialog), de);

  de->invoke_descriptives_dialog =
    gtk_action_new ("descriptives-dialog",
		    _("_Descriptives"),
		    _("Calculate descriptive statistics (mean, variance, ...)"),
		    "pspp-descriptives");

  g_signal_connect (de->invoke_descriptives_dialog, "activate",
		    G_CALLBACK (descriptives_dialog), de);


  de->invoke_frequencies_dialog =
    gtk_action_new ("frequencies-dialog",
		    _("_Frequencies"),
		    _("Generate frequency statistics"),
		    "pspp-frequencies");

  g_signal_connect (de->invoke_frequencies_dialog, "activate",
		    G_CALLBACK (frequencies_dialog), de);

  de->invoke_crosstabs_dialog =
    gtk_action_new ("crosstabs-dialog",
		    _("_Crosstabs"),
		    _("Generate crosstabulations"),
		    "pspp-crosstabs");

  g_signal_connect (de->invoke_crosstabs_dialog, "activate",
		    G_CALLBACK (crosstabs_dialog), de);


  de->invoke_examine_dialog =
    gtk_action_new ("examine-dialog",
		    _("_Explore"),
		    _("Examine Data by Factors"),
		    "pspp-examine");

  g_signal_connect (de->invoke_examine_dialog, "activate",
		    G_CALLBACK (examine_dialog), de);


  de->invoke_regression_dialog =
    gtk_action_new ("regression-dialog",
		    _("Linear _Regression"),
		    _("Estimate parameters of the linear model"),
		    "pspp-regression");

  g_signal_connect (de->invoke_regression_dialog, "activate",
		    G_CALLBACK (regression_dialog), de);

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

  g_signal_connect_swapped (get_widget_assert (de->xml,"file_import-text"),
			    "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->invoke_text_import_assistant);

  g_signal_connect_swapped (get_widget_assert (de->xml,"file_save"),
			    "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->action_data_save);

  g_signal_connect_swapped (get_widget_assert (de->xml,"file_save_as"),
			    "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->action_data_save_as);

  gtk_action_connect_proxy (de->invoke_find_dialog,
			    get_widget_assert (de->xml, "edit_find")
			    );

  gtk_action_connect_proxy (de->invoke_find_dialog,
			    get_widget_assert (de->xml, "button-find")
			    );

  gtk_action_connect_proxy (de->invoke_rank_dialog,
			    get_widget_assert (de->xml, "transform_rank")
			    );

  gtk_action_connect_proxy (de->invoke_recode_same_dialog,
			    get_widget_assert (de->xml,
					       "transform_recode-same")
			    );

  gtk_action_connect_proxy (de->invoke_recode_different_dialog,
			    get_widget_assert (de->xml,
					       "transform_recode-different")
			    );

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

  gtk_action_connect_proxy (de->invoke_select_cases_dialog,
			    get_widget_assert (de->xml, "data_select-cases")
			    );

  gtk_action_connect_proxy (de->invoke_compute_dialog,
			    get_widget_assert (de->xml, "transform_compute")
			    );

  gtk_action_connect_proxy (de->invoke_t_test_independent_samples_dialog,
			    get_widget_assert (de->xml,
					       "indep-t-test")
			    );


  gtk_action_connect_proxy (de->invoke_t_test_paired_samples_dialog,
			    get_widget_assert (de->xml,
					       "paired-t-test")
			    );


  gtk_action_connect_proxy (de->invoke_t_test_one_sample_dialog,
			    get_widget_assert (de->xml,
					       "one-sample-t-test")
			    );


  gtk_action_connect_proxy (de->invoke_oneway_anova_dialog,
			    get_widget_assert (de->xml,
					       "oneway-anova")
			    );


  gtk_action_connect_proxy (de->invoke_comments_dialog,
			    get_widget_assert (de->xml, "utilities_comments")
			    );

  gtk_action_connect_proxy (de->invoke_variable_info_dialog,
			    get_widget_assert (de->xml, "utilities_variables")
			    );

  gtk_action_connect_proxy (de->invoke_descriptives_dialog,
			    get_widget_assert (de->xml, "analyze_descriptives")
			    );

  gtk_action_connect_proxy (de->invoke_crosstabs_dialog,
			    get_widget_assert (de->xml, "crosstabs")
			    );

  gtk_action_connect_proxy (de->invoke_frequencies_dialog,
			    get_widget_assert (de->xml, "analyze_frequencies")
			    );


  gtk_action_connect_proxy (de->invoke_examine_dialog,
			    get_widget_assert (de->xml, "analyze_explore")
			    );

  gtk_action_connect_proxy (de->invoke_regression_dialog,
			    get_widget_assert (de->xml, "linear-regression")
			    );

  g_signal_connect (get_widget_assert (de->xml,"help_about"),
		    "activate",
		    G_CALLBACK (about_new),
		    e->window);


  g_signal_connect (get_widget_assert (de->xml,"help_reference"),
		    "activate",
		    G_CALLBACK (reference_manual),
		    e->window);


  g_signal_connect (de->data_editor,
		    "cases-selected",
		    G_CALLBACK (enable_delete_cases),
		    de);


  g_signal_connect (de->data_editor,
		    "variables-selected",
		    G_CALLBACK (enable_delete_variables),
		    de);


  g_signal_connect (GTK_NOTEBOOK (de->data_editor),
		    "switch-page",
		    G_CALLBACK (on_switch_sheet), de);

  gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_VARIABLE_VIEW);
  gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_DATA_VIEW);

  g_signal_connect (get_widget_assert (de->xml, "view_statusbar"),
		    "activate",
		    G_CALLBACK (status_bar_activate), de);


  g_signal_connect (get_widget_assert (de->xml, "view_gridlines"),
		    "activate",
		    G_CALLBACK (grid_lines_activate), de);



  g_signal_connect (get_widget_assert (de->xml, "view_data"),
		    "activate",
		    G_CALLBACK (data_view_activate), de);

  g_signal_connect (get_widget_assert (de->xml, "view_variables"),
		    "activate",
		    G_CALLBACK (variable_view_activate), de);



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

  gtk_action_connect_proxy (de->invoke_select_cases_dialog,
			    get_widget_assert (de->xml, "button-select-cases")
			    );


  g_signal_connect (get_widget_assert (de->xml, "file_quit"),
		    "activate",
		    G_CALLBACK (file_quit), de);

  g_signal_connect (get_widget_assert (de->xml, "transform_run-pending"),
		    "activate",
		    G_CALLBACK (execute), de);


  g_signal_connect (get_widget_assert (de->xml, "windows_minimise_all"),
		    "activate",
		    G_CALLBACK (minimise_all_windows), NULL);


  de->data_sheet_variable_popup_menu =
    GTK_MENU (create_data_sheet_variable_popup_menu (de));

  de->data_sheet_cases_popup_menu =
    GTK_MENU (create_data_sheet_cases_popup_menu (de));


  g_object_set (de->data_editor,
		"column-menu", de->data_sheet_variable_popup_menu, NULL);


  g_object_set (de->data_editor,
		"row-menu", de->data_sheet_cases_popup_menu, NULL);

  return de;
}


void
new_data_window (GtkMenuItem *menuitem, gpointer parent)
{
  window_create (WINDOW_DATA, NULL);
}

/* Callback for when the datasheet/varsheet is selected */
static void
on_switch_sheet (GtkNotebook *notebook,
		GtkNotebookPage *page,
		guint page_num,
		gpointer user_data)
{
  struct data_editor *de = user_data;

  GtkWidget *view_data = get_widget_assert (de->xml, "view_data");
  GtkWidget *view_variables = get_widget_assert (de->xml, "view_variables");

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
  const gboolean grid_visible = gtk_check_menu_item_get_active (menuitem);

  psppire_data_editor_show_grid (de->data_editor, grid_visible);
}



static void
data_view_activate (GtkCheckMenuItem *menuitem, gpointer data)
{
  struct data_editor *de = data;

  gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_DATA_VIEW);
}


static void
variable_view_activate (GtkCheckMenuItem *menuitem, gpointer data)
{
  struct data_editor *de = data;

  gtk_notebook_set_current_page (GTK_NOTEBOOK (de->data_editor), PSPPIRE_DATA_EDITOR_VARIABLE_VIEW);
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
  struct data_editor *de = data;

  g_object_set (de->data_editor, "value-labels", gtk_toggle_action_get_active (ta), NULL);
}



static void
file_quit (GtkCheckMenuItem *menuitem, gpointer data)
{
  /* FIXME: Need to be more intelligent here.
     Give the user the opportunity to save any unsaved data.
  */
  g_object_unref (the_data_store);
  gtk_main_quit ();
}


static void
insert_case (GtkAction *action, gpointer data)
{
  struct data_editor *de = data;

  psppire_data_editor_insert_case (de->data_editor);
}

static void
on_insert_variable (GtkAction *action, gpointer data)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (data);
  psppire_data_editor_insert_variable (de);
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

  de->invoke_text_import_assistant =
    gtk_action_new ("file_import-text",
		    _("_Import Text Data"),
		    _("Import text data file"),
		    "");

  g_signal_connect (de->invoke_text_import_assistant, "activate",
		    G_CALLBACK (text_data_import_assistant), de);
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

  ds_init_empty (&file_name);
  syntax_gen_string (&file_name, ss_cstr (de->file_name));

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

	if ( de->save_as_portable)
	  append_filename_suffix (de, ".por");
	else
	  append_filename_suffix (de, ".sav");

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

  ds_init_empty (&filename);
  syntax_gen_string (&filename, ss_cstr (file_name));

  sss = create_syntax_string_source ("GET FILE=%s.",
				     ds_cstr (&filename));
  ds_destroy (&filename);

  if (execute_syntax (sss) )
  {
    window_set_name_from_filename ((struct editor_window *) de, file_name);
    add_most_recent (file_name);
  }
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
      }
      break;
    default:
      break;
    }

  gtk_widget_destroy (dialog);
}


static GtkWidget *
create_data_sheet_variable_popup_menu (struct data_editor *de)
{
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *sort_ascending =
    gtk_menu_item_new_with_label (_("Sort Ascending"));

  GtkWidget *sort_descending =
    gtk_menu_item_new_with_label (_("Sort Descending"));

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


  g_signal_connect_swapped (G_OBJECT (sort_ascending), "activate",
			    G_CALLBACK (psppire_data_editor_sort_ascending),
			    de->data_editor);

  g_signal_connect_swapped (G_OBJECT (sort_descending), "activate",
			    G_CALLBACK (psppire_data_editor_sort_descending),
			    de->data_editor);

  g_signal_connect_swapped (G_OBJECT (insert_variable), "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->insert_variable);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sort_descending);

  gtk_widget_show_all (menu);

  return menu;
}


static GtkWidget *
create_data_sheet_cases_popup_menu (struct data_editor *de)
{
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *insert_case =
    gtk_menu_item_new_with_label (_("Insert Case"));

  GtkWidget *delete_case =
    gtk_menu_item_new_with_label (_("Clear"));


  gtk_action_connect_proxy (de->delete_cases,
			    delete_case);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), insert_case);

  g_signal_connect_swapped (G_OBJECT (insert_case), "activate",
			    G_CALLBACK (gtk_action_activate),
			    de->insert_case);


  gtk_menu_shell_append (GTK_MENU_SHELL (menu),
			 gtk_separator_menu_item_new ());


  gtk_menu_shell_append (GTK_MENU_SHELL (menu), delete_case);


  gtk_widget_show_all (menu);

  return menu;
}




static void
on_edit_paste (GtkAction *a, gpointer data)
{
  struct data_editor *de = data;

  psppire_data_editor_clip_paste (de->data_editor);
}

static void
on_edit_copy (GtkMenuItem *m, gpointer data)
{
  struct data_editor *de = data;

  psppire_data_editor_clip_copy (de->data_editor);
}



static void
on_edit_cut (GtkMenuItem *m, gpointer data)
{
  struct data_editor *de = data;

  psppire_data_editor_clip_cut (de->data_editor);
}