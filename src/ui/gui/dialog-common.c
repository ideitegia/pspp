/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2014  Free Software Foundation

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

#include <libpspp/i18n.h>
#include "dialog-common.h"
#include "dict-display.h"

#include "psppire-var-ptr.h"

#include "helper.h"

/* 
   If m is not a base TreeModel type (ie, is a filter or sorter) then 
   convert OP to a TreePath for the base and return it.
   The return value must be freed by the caller.
*/
static GtkTreePath *
get_base_tree_path (GtkTreeModel *m, GtkTreePath *op)
{
  GtkTreePath *p = gtk_tree_path_copy (op);
  while ( ! PSPPIRE_IS_DICT (m))
    {
      GtkTreePath *oldp = p;
      
      if (GTK_IS_TREE_MODEL_FILTER (m))
	{
	  p = gtk_tree_model_filter_convert_path_to_child_path (GTK_TREE_MODEL_FILTER (m), oldp);
	  m = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (m));
	}
      else if (GTK_IS_TREE_MODEL_SORT (m))
	{
	  p = gtk_tree_model_sort_convert_path_to_child_path (GTK_TREE_MODEL_SORT (m), oldp);
	  m = gtk_tree_model_sort_get_model (GTK_TREE_MODEL_SORT (m));
	}
      else
	{
	  g_error ("Unexpected model type: %s", G_OBJECT_TYPE_NAME (m));
	}
      
      gtk_tree_path_free (oldp);
    }

  return p;
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

  GtkTreeModel *top_model = gtk_tree_view_get_model (GTK_TREE_VIEW (source));
  GtkTreeModel *model;

  PsppireDict *dict;
  GtkTreeSelection *selection;
  enum val_type type;
  GList *list, *l;
  bool have_type;


  get_base_model (top_model, NULL, &model, NULL);

  dict = PSPPIRE_DICT (model);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (source));

  list = gtk_tree_selection_get_selected_rows (selection, &model);

  /* Iterate through the selection of the source treeview */
  have_type = false;
  for (l = list; l ; l = l->next)
    {
      GtkTreePath *p = get_base_tree_path (top_model, l->data);
      gint *idx = gtk_tree_path_get_indices (p);
      const struct variable *v = psppire_dict_get_variable (dict, idx[0]);

      gtk_tree_path_free (p);

      if (have_type && var_get_type (v) != type)
        {
          retval = FALSE;
          break;
        }

      type = var_get_type (v);
      have_type = true;
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
      const struct variable *v;
      gtk_tree_model_get (model, &iter, 0, &v, -1);

      if ( have_type && var_get_type (v) != type )
        {
          retval = FALSE;
          break;
        }

      type = var_get_type (v);
      have_type = true;
    }

  return retval;
}



/* Returns true iff the variable selected by SOURCE is numeric */
gboolean
numeric_only (GtkWidget *source, GtkWidget *dest)
{
  gboolean retval = TRUE;

  GtkTreeModel *top_model = gtk_tree_view_get_model (GTK_TREE_VIEW (source));
  GtkTreeModel *model = NULL;

  PsppireDict *dict;
  GtkTreeSelection *selection;
  GList *list, *l;

  get_base_model (top_model, NULL, &model, NULL);

  dict = PSPPIRE_DICT (model);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (source));

  list = gtk_tree_selection_get_selected_rows (selection, &top_model);

  /* Iterate through the selection of the source treeview */
  for (l = list; l ; l = l->next)
    {
      GtkTreePath *p = get_base_tree_path (top_model, l->data);
      gint *idx = gtk_tree_path_get_indices (p);
      const struct variable *v = psppire_dict_get_variable (dict, idx[0]);
      gtk_tree_path_free (p);

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

/*
  A pair of functions intended to be used as callbacks for the "toggled" signal
  of a GtkToggleButton widget.  They make the sensitivity of W follow the status
  of the togglebutton.
*/
void
set_sensitivity_from_toggle (GtkToggleButton *togglebutton,  GtkWidget *w)
{
  gboolean active = gtk_toggle_button_get_active (togglebutton);

  gtk_widget_set_sensitive (w, active);
  if (active)
    gtk_widget_grab_focus (w);
}

/* */
void
set_sensitivity_from_toggle_invert (GtkToggleButton *togglebutton,
				    GtkWidget *w)
{
  gboolean active = gtk_toggle_button_get_active (togglebutton);

  gtk_widget_set_sensitive (w, !active);
}



