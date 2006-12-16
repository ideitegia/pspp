/* 
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2006  Free Software Foundation
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

/*  This module describes the behaviour of the Sort Cases dialog box. */


/* This code is rather quick and dirty.  Some of the issues are:

   1. Character set conversion when displaying dictionary tree view.

   2. The interaction between the dictionary treeview, the criteria
      list treeview and the button, needs to be abstracted and made
      available as an external interface.

   3. There's no destroy function for this dialog.

   4. Some of the functionality might be better implemented with
      GtkAction.

   5. Double clicking the tree view rows should insert/delete them
      from the criteria list.

   6. Changing the Ascending/Descending flag ought to be possible for
      a criteria already in the criteria tree view.

   7. Variables which are in the criteria tree view should not be
      shown in the dictionary treeview.

   8. The dialog box structure itself ought to be a GtkWindow and
      abstracted better.
*/


#include <config.h>
#include "helper.h"
#include "sort-cases-dialog.h"
#include "psppire-dict.h"
#include <math/sort.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


enum {CRIT_TVM_IDX = 0, CRIT_TVM_DIR};

/* Occurs when the dictionary tree view selection changes */
static void
dictionary_selection_changed (GtkTreeSelection *selection,
			      gpointer data)
{
  GtkTreeSelection *otherselection ;
  struct sort_cases_dialog *dialog = (struct sort_cases_dialog*) data;

  if ( 0 == gtk_tree_selection_count_selected_rows(selection) ) 
    return ;

  gtk_arrow_set(dialog->arrow, GTK_ARROW_RIGHT, GTK_SHADOW_OUT);
  dialog->button_state = VAR_SELECT;
  
  otherselection = gtk_tree_view_get_selection(dialog->criteria_view);

  gtk_tree_selection_unselect_all(otherselection);
}


/* Occurs when the sort criteria tree view selection changes */
static void
criteria_selection_changed (GtkTreeSelection *selection,
			    gpointer data)
{
  GtkTreeSelection *otherselection ;
  struct sort_cases_dialog *dialog = (struct sort_cases_dialog*) data;

  if ( 0 == gtk_tree_selection_count_selected_rows(selection) ) 
    return ;

  otherselection = gtk_tree_view_get_selection(dialog->dict_view);

  gtk_arrow_set(dialog->arrow, GTK_ARROW_LEFT, GTK_SHADOW_OUT);
  dialog->button_state = VAR_DESELECT;

  gtk_tree_selection_unselect_all(otherselection);
}


/* Occurs when the dialog box is deleted (eg: closed via the title bar) */
static gint
delete_event_callback(GtkWidget *widget,
		      GdkEvent  *event,
		      gpointer   data)      
{
  struct sort_cases_dialog *dialog = (struct sort_cases_dialog*) data;

  g_main_loop_quit(dialog->loop);

  gtk_widget_hide_on_delete(widget);

  dialog->response = GTK_RESPONSE_DELETE_EVENT;

  return TRUE;
}

/* Occurs when the cancel button is clicked */
static void
sort_cases_cancel_callback(GObject *obj, gpointer data)
{
  struct sort_cases_dialog *dialog = (struct sort_cases_dialog*) data;

  gtk_widget_hide(dialog->window);

  g_main_loop_quit(dialog->loop);

  dialog->response = GTK_RESPONSE_CANCEL;
}

/* Occurs when the reset button is clicked */
static void
sort_cases_reset_callback(GObject *obj, gpointer data)
{
  struct sort_cases_dialog *dialog = (struct sort_cases_dialog*) data;
  
  gtk_arrow_set(dialog->arrow, GTK_ARROW_RIGHT, GTK_SHADOW_OUT);
  dialog->button_state = VAR_SELECT;

  gtk_list_store_clear(dialog->criteria_list);
}


/* Add variables currently selected in the dictionary tree view to the
   list of criteria */
static void
select_criteria(GtkTreeModel *model, GtkTreePath *path,
		GtkTreeIter *iter, gpointer data)
{
  GtkTreeIter new_iter;
  gint index;
  gint dir;
  struct variable *variable;
  struct sort_cases_dialog *dialog = (struct sort_cases_dialog*) data;

  /* Get the variable from the dictionary */
  gtk_tree_model_get (model, iter, 
		      DICT_TVM_COL_VAR, &variable,
		      -1);
	
  index = var_get_dict_index (variable);

  dir = gtk_toggle_button_get_active (dialog->ascending_button) ? 
    SRT_ASCEND:SRT_DESCEND;

  /* Append to the list of criteria */
  gtk_list_store_append(dialog->criteria_list, &new_iter);
  gtk_list_store_set(dialog->criteria_list, 
		     &new_iter, CRIT_TVM_IDX, index, -1);
  gtk_list_store_set(dialog->criteria_list, 
		     &new_iter, CRIT_TVM_DIR, dir, -1);
}

/* Create a list of the RowRefs which are to be removed from the
   criteria list */
static void
path_to_row_ref(GtkTreeModel *model, GtkTreePath *path,
		GtkTreeIter *iter, gpointer data)
{
  GList **rrlist = data;
  GtkTreeRowReference *rowref = gtk_tree_row_reference_new(model, path);

  *rrlist = g_list_append(*rrlist, rowref);
}


/* Remove a row from the list of criteria */
static void
deselect_criteria(gpointer data, 
		  gpointer user_data)
{
  GtkTreeIter iter;
  GtkTreeRowReference *row_ref = data;
  GtkTreePath *path;
  struct sort_cases_dialog *dialog = (struct sort_cases_dialog*) user_data;

  path = gtk_tree_row_reference_get_path(row_ref);

  gtk_tree_model_get_iter(GTK_TREE_MODEL(dialog->criteria_list), &iter, path);

  gtk_list_store_remove(dialog->criteria_list, &iter);

  gtk_tree_row_reference_free(row_ref);
}



/* Callback which occurs when the button to remove variables from the list
   of criteria is clicked. */
static void
sort_cases_button_callback(GObject *obj, gpointer data)
{
  struct sort_cases_dialog *dialog = (struct sort_cases_dialog*) data;

  if ( dialog->button_state == VAR_SELECT) /* Right facing arrow */
    {
      GtkTreeSelection *selection = 
	gtk_tree_view_get_selection(dialog->dict_view);

      gtk_tree_selection_selected_foreach(selection, select_criteria, dialog);
    }
  else   /* Left facing arrow */
    {
      GList *selectedRows = NULL;
      GtkTreeSelection *selection = 
	gtk_tree_view_get_selection(dialog->criteria_view);

      /* Make a list of rows to be deleted */
      gtk_tree_selection_selected_foreach(selection, path_to_row_ref, 
					  &selectedRows);

      /* ... and delete them */
      g_list_foreach(selectedRows, deselect_criteria, dialog);

      g_list_free(selectedRows);
    }
}


/* Callback which occurs when the OK button is clicked */
static void
sort_cases_ok_callback(GObject *obj, gpointer data)
{
  struct sort_cases_dialog *dialog = (struct sort_cases_dialog*) data;

  gtk_widget_hide(dialog->window);
  g_main_loop_quit(dialog->loop);

  dialog->response = GTK_RESPONSE_OK;
}


/* This function is responsible for rendering a criterion in the
   criteria list */
static void
criteria_render_func(GtkTreeViewColumn *column, GtkCellRenderer *renderer,
		     GtkTreeModel *model, GtkTreeIter *iter,
		     gpointer data)
{
  gint var_index;
  struct variable *variable ;
  gint direction;
  gchar *buf;
  gchar *varname;
  PsppireDict *dict  = data;

  gtk_tree_model_get(model, iter, 
		     CRIT_TVM_IDX, &var_index, 
		     CRIT_TVM_DIR, &direction, -1);

  variable = psppire_dict_get_variable(dict, var_index);

  varname = pspp_locale_to_utf8 (var_get_name(variable),
				-1, 0);

  if ( direction == SRT_ASCEND) 
      buf = g_strdup_printf("%s: %s", varname, _("Ascending"));
  else
      buf = g_strdup_printf("%s: %s", varname, _("Descending"));
  
  g_free(varname);

  g_object_set(renderer, "text", buf, NULL);

  g_free(buf);
}


/* Create the dialog */
struct sort_cases_dialog * 
sort_cases_dialog_create(GladeXML *xml)
{
  struct sort_cases_dialog *dialog = g_malloc(sizeof(*dialog));

  dialog->loop = g_main_loop_new(NULL, FALSE);

  dialog->window = get_widget_assert(xml, "sort-cases-dialog");

  dialog->dict_view = GTK_TREE_VIEW(get_widget_assert
				    (xml, "sort-cases-treeview-dict"));
  dialog->criteria_view = GTK_TREE_VIEW(get_widget_assert
				  (xml, "sort-cases-treeview-criteria"));

  dialog->arrow = GTK_ARROW(get_widget_assert(xml, "sort-cases-arrow"));
  dialog->button = GTK_BUTTON(get_widget_assert(xml, "sort-cases-button"));
  
  dialog->ascending_button = 
    GTK_TOGGLE_BUTTON(get_widget_assert(xml, "sort-cases-button-ascending"));

  g_signal_connect(dialog->window, "delete-event",
		   G_CALLBACK(delete_event_callback), dialog);

  g_signal_connect(get_widget_assert(xml, "sort-cases-cancel"),
		   "clicked", G_CALLBACK(sort_cases_cancel_callback), dialog);

  g_signal_connect(get_widget_assert(xml, "sort-cases-ok"),
		   "clicked", G_CALLBACK(sort_cases_ok_callback), dialog);


  g_signal_connect(get_widget_assert(xml, "sort-cases-reset"),
		   "clicked", G_CALLBACK(sort_cases_reset_callback), dialog);


  g_signal_connect(get_widget_assert(xml, "sort-cases-button"),
		   "clicked", G_CALLBACK(sort_cases_button_callback), dialog);


  {
  /* Set up the dictionary treeview */
    GtkTreeViewColumn *col;

    GtkTreeSelection *selection = 
      gtk_tree_view_get_selection(dialog->dict_view);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

    col = gtk_tree_view_column_new_with_attributes(_("Var"),
						   renderer,
						   "text",
						   0,
						   NULL);

    /* FIXME: make this a value in terms of character widths */
    g_object_set(col, "min-width",  100, NULL);

    gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);

    gtk_tree_view_append_column(dialog->dict_view, col);

    gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

    g_signal_connect(selection, "changed", 
		     G_CALLBACK(dictionary_selection_changed), dialog);
  }

  {
    /* Set up the variable list treeview */
    GtkTreeSelection *selection = 
      gtk_tree_view_get_selection(dialog->criteria_view);

    gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

    dialog->crit_renderer = gtk_cell_renderer_text_new();

    dialog->crit_col = gtk_tree_view_column_new_with_attributes(_("Criteria"),
						   dialog->crit_renderer,
						   "text",
						   0,
						   NULL);

    gtk_tree_view_column_set_sizing (dialog->crit_col, GTK_TREE_VIEW_COLUMN_FIXED);

    gtk_tree_view_append_column(GTK_TREE_VIEW(dialog->criteria_view), 
				dialog->crit_col);

    g_signal_connect(selection, "changed", 
		     G_CALLBACK(criteria_selection_changed), dialog);
  }

  {
    /* Create the list of criteria */
    dialog->criteria_list = gtk_list_store_new(2, 
			    G_TYPE_INT, /* index of the variable */
			    G_TYPE_INT  /* Ascending/Descending */
			    );

    gtk_tree_view_set_model(dialog->criteria_view, 
			    GTK_TREE_MODEL(dialog->criteria_list));
  }

  dialog->response = GTK_RESPONSE_NONE;

  return dialog;
}


static void
convert_list_store_to_criteria(GtkListStore *list,
			       PsppireDict *dict,
			       struct sort_criteria *criteria);


/* Run the dialog.
   If the return value is GTK_RESPONSE_OK, then CRITERIA gets filled
   with a valid sort criteria which can be used to sort the data.
   This structure and its contents must be freed by the caller. */
gint
sort_cases_dialog_run(struct sort_cases_dialog *dialog, 
		      PsppireDict *dict,
		      struct sort_criteria *criteria
		      )
{
  g_assert(! g_main_loop_is_running(dialog->loop));

  gtk_tree_view_set_model(GTK_TREE_VIEW(dialog->dict_view), 
			  GTK_TREE_MODEL(dict));

  
  gtk_tree_view_column_set_cell_data_func(dialog->crit_col, 
					  dialog->crit_renderer, 
					  criteria_render_func, dict, 0);

  gtk_list_store_clear(dialog->criteria_list);

  gtk_arrow_set(dialog->arrow, GTK_ARROW_RIGHT, GTK_SHADOW_OUT);
  dialog->button_state = VAR_SELECT;

  gtk_widget_show(dialog->window);

  g_main_loop_run(dialog->loop);

  if ( GTK_RESPONSE_OK == dialog->response) 
    convert_list_store_to_criteria(dialog->criteria_list, 
				   dict, criteria);

  return dialog->response;
}



/* Convert the GtkListStore to a struct sort_criteria*/
static void
convert_list_store_to_criteria(GtkListStore *list,
			       PsppireDict *dict,
			       struct sort_criteria *criteria)
{
  GtkTreeIter iter;
  gboolean valid;
  gint n = 0;

  GtkTreeModel *model = GTK_TREE_MODEL(list);
  
  criteria->crit_cnt = gtk_tree_model_iter_n_children (model, NULL);

  criteria->crits = g_malloc(sizeof(struct sort_criterion) * 
			     criteria->crit_cnt);

  for(valid = gtk_tree_model_get_iter_first(model, &iter);
      valid;
      valid = gtk_tree_model_iter_next(model, &iter))
    {
      struct variable *variable;
      gint index;
      struct sort_criterion *scn = &criteria->crits[n];
      g_assert ( n < criteria->crit_cnt);
      n++;
      
      gtk_tree_model_get(model, &iter, 
			 CRIT_TVM_IDX, &index, 
			 CRIT_TVM_DIR, &scn->dir,
			 -1);

      variable = psppire_dict_get_variable(dict, index);

      scn->fv    = var_get_case_index (variable);
      scn->width = var_get_width(variable);
    }
}

