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
#include <gettext.h>
#include <gtk/gtk.h>

#include "psppire-conf.h"
#include "dict-display.h"

#include "psppire-dict.h"
#include "psppire-dictview.h"
#include "psppire-means-layer.h"
#include "psppire-var-ptr.h"
#include "psppire-var-view.h"
#include "psppire-select-dest.h"
#include <libpspp/i18n.h>
#include "helper.h"
#include <data/variable.h>
#include <data/format.h>

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


void
get_base_model (GtkTreeModel *top_model, GtkTreeIter *top_iter,
		GtkTreeModel **model, GtkTreeIter *iter)
{
  *model = top_model;

  if ( iter)
    *iter = *top_iter;

  while ( ! PSPPIRE_IS_DICT (*model))
    {
      GtkTreeIter parent_iter;
      if (iter)
	parent_iter = *iter;

      if ( GTK_IS_TREE_MODEL_FILTER (*model))
	{
	  GtkTreeModelFilter *parent_model = GTK_TREE_MODEL_FILTER (*model);

	  *model = gtk_tree_model_filter_get_model (parent_model);

	  if (iter)
	    gtk_tree_model_filter_convert_iter_to_child_iter (parent_model,
							      iter,
							      &parent_iter);
	}
      else if (GTK_IS_TREE_MODEL_SORT (*model))
	{
	  GtkTreeModelSort *parent_model = GTK_TREE_MODEL_SORT (*model);

	  *model = gtk_tree_model_sort_get_model (parent_model);

	  if (iter)
	    gtk_tree_model_sort_convert_iter_to_child_iter (parent_model,
							    iter,
							    &parent_iter);
	}
    }
}


void
insert_source_row_into_entry (GtkTreeIter iter,
			      GtkWidget *dest,
			      GtkTreeModel *model,
			      gpointer data
			      )
{
  GtkTreePath *path;
  GtkTreeModel *dict;
  gint *idx;
  struct variable *var;
  GtkTreeIter dict_iter;

  g_return_if_fail (GTK_IS_ENTRY(dest));

  get_base_model (model, &iter, &dict, &dict_iter);

  path = gtk_tree_model_get_path (GTK_TREE_MODEL (dict), &dict_iter);

  idx = gtk_tree_path_get_indices (path);

  var =  psppire_dict_get_variable (PSPPIRE_DICT (dict), *idx);

  gtk_tree_path_free (path);

  gtk_entry_set_text (GTK_ENTRY (dest),  var_get_name (var));
}



static void
insert_source_row_into_tree_model (GtkTreeIter source_iter,
				     GtkTreeModel *dest_model,
				     GtkTreeModel *source_model,
				     gpointer data)
{
  GtkTreePath *path;
  GtkTreeIter dest_iter;
  GtkTreeIter dict_iter;
  gint *row ;

  const struct variable *var;
  GtkTreeModel *dict;

  get_base_model (source_model, &source_iter, &dict, &dict_iter);

  path = gtk_tree_model_get_path (dict, &dict_iter);

  row = gtk_tree_path_get_indices (path);

  var = psppire_dict_get_variable (PSPPIRE_DICT (dict), *row);

  gtk_list_store_append (GTK_LIST_STORE (dest_model),  &dest_iter);

  gtk_list_store_set (GTK_LIST_STORE (dest_model), &dest_iter, 0, var, -1);

  gtk_tree_path_free (path);
}



void
insert_source_row_into_tree_view (GtkTreeIter iter,
				  GtkWidget *dest,
				  GtkTreeModel *model,
				  gpointer data)
{
  GtkTreeModel *destmodel = gtk_tree_view_get_model (GTK_TREE_VIEW (dest));

  insert_source_row_into_tree_model (iter, destmodel, model, data);
}


void
insert_source_row_into_layers (GtkTreeIter iter,
			       GtkWidget *dest,
			       GtkTreeModel *model,
			       gpointer data)
{
  GtkTreeModel *destmodel = psppire_means_layer_get_model (PSPPIRE_MEANS_LAYER (dest));

  insert_source_row_into_tree_model (iter, destmodel, model, data);

  psppire_means_layer_update (PSPPIRE_MEANS_LAYER (dest));
}




gboolean
is_currently_in_entry (GtkTreeModel *model, GtkTreeIter *iter,
		       PsppireSelector *selector)
{
  gboolean result;
  GtkTreeIter dict_iter;
  GtkTreeModel *dict;
  struct variable *var;
  gint dict_index;
  gint *indeces;
  GtkTreePath *path;
  GtkWidget *entry = NULL;
  const gchar *text = NULL;

  g_object_get (selector, "dest-widget", &entry, NULL);

  text = gtk_entry_get_text (GTK_ENTRY (entry));

  get_base_model (model, iter, &dict, &dict_iter);

  path = gtk_tree_model_get_path (dict, &dict_iter);

  indeces = gtk_tree_path_get_indices (path);

  dict_index = indeces [0];

  var = psppire_dict_get_variable (PSPPIRE_DICT (dict), dict_index);

  gtk_tree_path_free (path);

  result = ( 0 == strcmp (text, var_get_name (var) ));

  return result;
}

gboolean
is_currently_in_varview (GtkTreeModel *model, GtkTreeIter *iter, PsppireSelector *sel)
{
  gboolean ret = false;

  /* First, fetch the variable from the source */

  PsppireDictView *dv = PSPPIRE_DICT_VIEW (sel->source);

  GtkTreePath *path = gtk_tree_model_get_path (model, iter);

  gint *idx = gtk_tree_path_get_indices (path);

  const struct variable *var =  psppire_dict_get_variable (dv->dict, *idx);


  /* Now test if that variable exists in the destination */

  GValue value = {0};

  g_value_init (&value, PSPPIRE_VAR_PTR_TYPE);
  g_value_set_boxed (&value, var);

  ret = psppire_select_dest_widget_contains_var (PSPPIRE_SELECT_DEST_WIDGET (sel->dest), &value);

  g_value_unset (&value);

  return ret ;
}

