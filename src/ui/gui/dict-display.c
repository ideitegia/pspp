/*
   PSPPIRE --- A Graphical User Interface for PSPP
   Copyright (C) 2007  Free Software Foundation

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
#include <gettext.h>
#include <gtk/gtk.h>

#include "dict-display.h"

#include "psppire-dict.h"
#include "helper.h"
#include <data/variable.h>

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


/* A GtkTreeModelFilterVisibleFunc to filter lines in the treeview */
static gboolean
filter_variables (GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
  var_predicate_func *predicate = data;
  struct variable *var;
  PsppireDict *dict = PSPPIRE_DICT (model);

  GtkTreePath *path = gtk_tree_model_get_path (model, iter);

  gint *idx = gtk_tree_path_get_indices (path);

  var =  psppire_dict_get_variable (dict, *idx);

  gtk_tree_path_free (path);

  return predicate (var);
}

/* Sets up TREEVIEW to display the variables of DICT.
   MODE is the selection mode for TREEVIEW.
   PREDICATE determines which variables should be visible, or NULL if
   all are to be visible.
 */
void
attach_dictionary_to_treeview (GtkTreeView *treeview, PsppireDict *dict,
			       GtkSelectionMode mode,
			       var_predicate_func *predicate
			       )
{
  GtkTreeViewColumn *col;

  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (treeview);

  GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
  GtkTreeModel *model ;

  if ( predicate )
    {
      model = gtk_tree_model_filter_new (GTK_TREE_MODEL (dict),
					  NULL);

      gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (model),
					      filter_variables,
					      predicate,
					      NULL);
    }
  else
    {
      model = GTK_TREE_MODEL (dict);
    }

  gtk_tree_view_set_model (GTK_TREE_VIEW (treeview), model);



  col = gtk_tree_view_column_new_with_attributes (_("Var"),
						  renderer,
						  "text",
						  0,
						  NULL);

  /* FIXME: make this a value in terms of character widths */
  g_object_set (col, "min-width",  100, NULL);

  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);

  gtk_tree_view_append_column (treeview, col);

  gtk_tree_selection_set_mode (selection, mode);
}


void
insert_source_row_into_entry (GtkTreeIter iter,
			      GtkWidget *dest,
			      GtkTreeModel *model
			      )
{
  GtkTreePath *path;
  PsppireDict *dict;
  gint *idx;
  struct variable *var;
  GtkTreeIter dict_iter;
  gchar *name;

  g_return_if_fail (GTK_IS_ENTRY(dest));


  if ( GTK_IS_TREE_MODEL_FILTER (model))
    {
      dict = PSPPIRE_DICT (gtk_tree_model_filter_get_model
			   (GTK_TREE_MODEL_FILTER(model)));
      gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER
							(model),
							&dict_iter, &iter);
    }
  else
    {
      dict = PSPPIRE_DICT (model);
      dict_iter = iter;
    }


  path = gtk_tree_model_get_path (GTK_TREE_MODEL (dict), &dict_iter);

  idx = gtk_tree_path_get_indices (path);

  var =  psppire_dict_get_variable (dict, *idx);

  gtk_tree_path_free (path);

  name = pspp_locale_to_utf8 (var_get_name (var), -1, NULL);
  gtk_entry_set_text (GTK_ENTRY (dest),  name);
  g_free (name);
}



void
insert_source_row_into_tree_view (GtkTreeIter iter,
				  GtkWidget *dest,
				  GtkTreeModel *model
				  )
{
  GtkTreePath *path;
  GtkTreeIter dest_iter;
  GtkTreeIter dict_iter;
  gint *row ;
  GtkTreeModel *destmodel = gtk_tree_view_get_model ( GTK_TREE_VIEW (dest));

  PsppireDict *dict;

  if ( GTK_IS_TREE_MODEL_FILTER (model))
    {
      dict = PSPPIRE_DICT (gtk_tree_model_filter_get_model
			   (GTK_TREE_MODEL_FILTER(model)));
      gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER
							(model),
							&dict_iter, &iter);
    }
  else
    {
      dict = PSPPIRE_DICT (model);
      dict_iter = iter;
    }


  path = gtk_tree_model_get_path (GTK_TREE_MODEL (dict), &dict_iter);

  row = gtk_tree_path_get_indices (path);

  gtk_list_store_append (GTK_LIST_STORE (destmodel),  &dest_iter);
  gtk_list_store_set (GTK_LIST_STORE (destmodel), &dest_iter, 0, *row, -1);

  gtk_tree_path_free (path);
}


gboolean
is_currently_in_entry (GtkTreeModel *model, GtkTreeIter *iter,
		       PsppireSelector *selector)
{
  gboolean result;
  gchar *name;
  GtkTreeIter dict_iter;
  PsppireDict *dict;
  struct variable *var;
  gint dict_index;
  gint *indeces;
  GtkTreePath *path;
  const gchar *text =   gtk_entry_get_text (GTK_ENTRY (selector->dest));


  if ( GTK_IS_TREE_MODEL_FILTER (model))
    {
      dict = PSPPIRE_DICT (gtk_tree_model_filter_get_model
			   (GTK_TREE_MODEL_FILTER(model)));
      gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER
							(model),
							&dict_iter, iter);
    }
  else
    {
      dict = PSPPIRE_DICT (model);
      dict_iter = *iter;
    }


  path = gtk_tree_model_get_path (GTK_TREE_MODEL(dict),
				  &dict_iter);

  indeces = gtk_tree_path_get_indices (path);

  dict_index = indeces [0];

  var = psppire_dict_get_variable (dict, dict_index);

  gtk_tree_path_free (path);

  name = pspp_locale_to_utf8 (var_get_name (var), -1, NULL);
  result = ( 0 == strcmp (text, name));
  g_free (name);

  return result;
}


