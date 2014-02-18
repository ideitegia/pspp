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

#include "psppire-var-ptr.h"

#include "helper.h"


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
  enum val_type type;
  GList *list, *l;
  bool have_type;

  while (GTK_IS_TREE_MODEL_FILTER (model))
    {
      model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (model));
    }

  dict = PSPPIRE_DICT (model);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (source));

  list = gtk_tree_selection_get_selected_rows (selection, &model);

  /* Iterate through the selection of the source treeview */
  have_type = false;
  for (l = list; l ; l = l->next)
    {
      GtkTreePath *path = l->data;

      GtkTreePath *fpath =
	gtk_tree_model_filter_convert_path_to_child_path (GTK_TREE_MODEL_FILTER (model), path);

      gint *idx = gtk_tree_path_get_indices (fpath);

      const struct variable *v = psppire_dict_get_variable (dict, idx[0]);

      gtk_tree_path_free (fpath);

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



