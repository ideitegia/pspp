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


#ifndef DATA_EDITOR_H
#define DATA_EDITOR_H

#include <glade/glade.h>
#include <gtk/gtk.h>
#include "window-manager.h"

struct data_editor
{
  struct editor_window parent;

  GtkAction *action_data_new;
  GtkAction *action_data_open;
  GtkAction *action_data_save_as;
  GtkAction *action_data_save;


  /* Actions which invoke dialog boxes */
  GtkAction *invoke_weight_cases_dialog;
  GtkAction *invoke_transpose_dialog;
  GtkAction *invoke_split_file_dialog;
  GtkAction *invoke_sort_cases_dialog;
  GtkAction *invoke_compute_dialog;
  GtkAction *invoke_comments_dialog;
  GtkAction *invoke_select_cases_dialog;
  GtkAction *invoke_goto_dialog;
  GtkAction *invoke_variable_info_dialog;
  GtkAction *invoke_find_dialog;
  GtkAction *invoke_rank_dialog;
  GtkAction *invoke_recode_same_dialog;
  GtkAction *invoke_recode_different_dialog;

  GtkAction *invoke_crosstabs_dialog;
  GtkAction *invoke_descriptives_dialog;
  GtkAction *invoke_frequencies_dialog;
  GtkAction *invoke_examine_dialog;
  GtkAction *invoke_regression_dialog;

  GtkAction *invoke_t_test_independent_samples_dialog;
  GtkAction *invoke_t_test_paired_samples_dialog;
  GtkAction *invoke_oneway_anova_dialog;
  GtkAction *invoke_t_test_one_sample_dialog;


  /* Actions which do things */
  GtkAction *insert_variable;
  GtkAction *insert_case;
  GtkAction *delete_variables;
  GtkAction *delete_cases;

  GtkToggleAction *toggle_value_labels;

  GladeXML *xml;

  GtkMenu *data_sheet_variable_popup_menu;
  GtkMenu *data_sheet_cases_popup_menu;


  gboolean save_as_portable;

  /* Name of the file this data is associated with (ie, was loaded from or
     has been  saved to), in "filename encoding",  or NULL, if it's not
     associated with any file */
  gchar *file_name;
};


struct data_editor * new_data_editor (void);

void new_data_window (GtkMenuItem *, gpointer);

void data_editor_select_sheet (struct data_editor *de, gint page);

enum {PAGE_DATA_SHEET = 0, PAGE_VAR_SHEET};


#endif
