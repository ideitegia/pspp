/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007  Free Software Foundation

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

#include "dialog-common.h"

#include "psppire-var-ptr.h"

#include "helper.h"


/* Append the names of selected variables to STRING.
   TREEVIEW is the treeview containing the variables.
   COLUMN is the column in the treeview containing the variables.
   DICT is the dictionary for those variables.
*/
gint
append_variable_names (GString *string,
		       PsppireDict *dict, GtkTreeView *treeview, gint column)
{
  gint n_vars = 0;
  GtkTreeIter iter;

  GtkTreeModel *list_store =
    gtk_tree_view_get_model (treeview);

  if ( gtk_tree_model_get_iter_first (list_store, &iter) )
    {
      do
	{
	  GValue value = {0};
	  struct variable *var = NULL;
	  GtkTreePath *path = gtk_tree_model_get_path (list_store, &iter);

	  gtk_tree_model_get_value (list_store, &iter, column, &value);

	  /* FIXME:  G_TYPE_INT should be deprecated.
	     As well as being simpler, it'd be unecessary to pass dict */
	  if ( G_VALUE_TYPE (&value) == G_TYPE_INT )
	  var = psppire_dict_get_variable (dict, g_value_get_int (&value));

	  else if ( G_VALUE_TYPE (&value) == PSPPIRE_VAR_PTR_TYPE)
	    var = g_value_get_boxed (&value);

	  else
	    g_critical ("Unsupported type \"%s\", in variable name treeview.",
			G_VALUE_TYPE_NAME (&value));

	  g_value_unset (&value);

	  g_string_append (string, " ");
	  g_string_append (string, var_get_name (var));

	  gtk_tree_path_free (path);
	  n_vars++;
	}
      while (gtk_tree_model_iter_next (list_store, &iter));
    }

  return n_vars;
}



struct variable *
get_selected_variable (GtkTreeModel *treemodel,
		       GtkTreeIter *iter,
		       PsppireDict *dict)
{
  struct variable *var;
  GValue value = {0};

  GtkTreePath *path = gtk_tree_model_get_path (treemodel, iter);

  gtk_tree_model_get_value (treemodel, iter, 0, &value);

  gtk_tree_path_free (path);

  var =  psppire_dict_get_variable (dict, g_value_get_int (&value));

  g_value_unset (&value);

  return var;
}




/* A (*GtkTreeCellDataFunc) function.
   This function expects TREEMODEL to hold G_TYPE_INT.  The ints it holds
   are the indices of the variables in the dictionary, which DATA points to.
   It renders the name of the variable into CELL.
*/
void
cell_var_name (GtkTreeViewColumn *tree_column,
	       GtkCellRenderer *cell,
	       GtkTreeModel *tree_model,
	       GtkTreeIter *iter,
	       gpointer data)
{
  PsppireDict *dict = data;
  struct variable *var;
  gchar *name;

  var = get_selected_variable (tree_model, iter, dict);

  name = pspp_locale_to_utf8 (var_get_name (var), -1, NULL);
  g_object_set (cell, "text", name, NULL);
  g_free (name);
}



/* Set a model for DEST, which is an GtkListStore of g_int's
   whose values are the indices into DICT */
void
set_dest_model (GtkTreeView *dest, PsppireDict *dict)
{
  GtkTreeViewColumn *col;
  GtkListStore *dest_list = gtk_list_store_new (1, G_TYPE_INT);
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

  gtk_tree_view_set_model (GTK_TREE_VIEW (dest), GTK_TREE_MODEL (dest_list));

  col = gtk_tree_view_column_new_with_attributes ("Var",
						  renderer,
						  "text",
						  0,
						  NULL);

  gtk_tree_view_column_set_cell_data_func (col, renderer,
					   cell_var_name,
					   dict, 0);

  /* FIXME: make this a value in terms of character widths */
  g_object_set (col, "min-width",  100, NULL);

  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);

  gtk_tree_view_append_column (GTK_TREE_VIEW (dest), col);
}



/* Returns FALSE if the variables represented by the union of the rows
   currently selected by SOURCE widget, and contents of the DEST
   widget, are of different types.

   In other words, this function when passed as the argument to
   psppire_selector_set_allow, ensures that the selector selects only
   string  variables, or only numeric variables, not a mixture.
*/
gboolean
homogeneous_types (GtkWidget *source, GtkWidget *dest)
{
  gboolean ok;
  GtkTreeIter iter;
  gboolean retval = TRUE;

  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (source));

  PsppireDict *dict;
  GtkTreeSelection *selection;
  enum val_type type = -1;
  GList *list, *l;

  while (GTK_IS_TREE_MODEL_FILTER (model))
    {
      model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (model));
    }

  dict = PSPPIRE_DICT (model);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (source));

  list = gtk_tree_selection_get_selected_rows (selection, &model);

  /* Iterate through the selection of the source treeview */
  for (l = list; l ; l = l->next)
    {
      GtkTreePath *path = l->data;

      GtkTreePath *fpath =
	gtk_tree_model_filter_convert_path_to_child_path (GTK_TREE_MODEL_FILTER (model), path);

      gint *idx = gtk_tree_path_get_indices (fpath);

      const struct variable *v = psppire_dict_get_variable (dict, idx[0]);

      gtk_tree_path_free (fpath);

      if ( type != -1 )
	{
	  if ( var_get_type (v) != type )
	    {
	      retval = FALSE;
	      break;
	    }
	}

      type = var_get_type (v);
    }

  g_list_foreach (list, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (list);

  if ( retval == FALSE )
    return FALSE;

  /* now deal with the dest widget */
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (dest));

  for (ok = gtk_tree_model_get_iter_first (model, &iter);
       ok;
       ok = gtk_tree_model_iter_next (model, &iter))
    {
      gint idx;
      const struct variable *v;
      gtk_tree_model_get (model, &iter, 0, &idx, -1);

      v = psppire_dict_get_variable (dict, idx);

      if ( type != -1 )
	{
	  if ( var_get_type (v) != type )
	    {
	      retval = FALSE;
	      break;
	    }
	}

      type = var_get_type (v);
    }


  return retval;
}



/* Returns true iff the variable selected by SOURCE is numeric */
gboolean
numeric_only (GtkWidget *source, GtkWidget *dest)
{
  gboolean retval = TRUE;

  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (source));

  PsppireDict *dict;
  GtkTreeSelection *selection;
  GList *list, *l;

  while (GTK_IS_TREE_MODEL_FILTER (model))
    {
      model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (model));
    }

  dict = PSPPIRE_DICT (model);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (source));

  list = gtk_tree_selection_get_selected_rows (selection, &model);

  /* Iterate through the selection of the source treeview */
  for (l = list; l ; l = l->next)
    {
      GtkTreePath *path = l->data;
      GtkTreePath *fpath = gtk_tree_model_filter_convert_path_to_child_path
	(GTK_TREE_MODEL_FILTER (model), path);

      gint *idx = gtk_tree_path_get_indices (fpath);

      const struct variable *v = psppire_dict_get_variable (dict, idx[0]);

      gtk_tree_path_free (fpath);

      if ( var_is_alpha (v))
	{
	  retval = FALSE;
	  break;
	}
    }

  g_list_foreach (list, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (list);

  return retval;
}

