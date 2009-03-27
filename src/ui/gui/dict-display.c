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
#include <libpspp/i18n.h>
#include "helper.h"
#include <data/variable.h>
#include <data/format.h>

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void
get_base_model (GtkTreeModel *top_model, GtkTreeIter *top_iter,
		GtkTreeModel **model, GtkTreeIter *iter
		)
{
  *model = top_model;
  *iter = *top_iter;
  while (GTK_IS_TREE_MODEL_FILTER (*model))
    {
      GtkTreeIter parent_iter = *iter;
      GtkTreeModelFilter *parent_model = GTK_TREE_MODEL_FILTER (*model);

      *model = gtk_tree_model_filter_get_model (parent_model);

      gtk_tree_model_filter_convert_iter_to_child_iter (parent_model,
							iter,
							&parent_iter);
    }

  g_assert (PSPPIRE_IS_DICT (*model));
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
  gchar *name;

  g_return_if_fail (GTK_IS_ENTRY(dest));

  get_base_model (model, &iter, &dict, &dict_iter);

  path = gtk_tree_model_get_path (GTK_TREE_MODEL (dict), &dict_iter);

  idx = gtk_tree_path_get_indices (path);

  var =  psppire_dict_get_variable (PSPPIRE_DICT (dict), *idx);

  gtk_tree_path_free (path);

  name = recode_string (UTF8, psppire_dict_encoding (PSPPIRE_DICT (dict)),
			var_get_name (var), -1);
  gtk_entry_set_text (GTK_ENTRY (dest),  name);
  g_free (name);
}



void
insert_source_row_into_tree_view (GtkTreeIter iter,
				  GtkWidget *dest,
				  GtkTreeModel *model,
				  gpointer data
				  )
{
  GtkTreePath *path;
  GtkTreeIter dest_iter;
  GtkTreeIter dict_iter;
  gint *row ;
  GtkTreeModel *destmodel = gtk_tree_view_get_model ( GTK_TREE_VIEW (dest));

  GtkTreeModel *dict;


  get_base_model (model, &iter, &dict, &dict_iter);

  path = gtk_tree_model_get_path (dict, &dict_iter);

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
  GtkTreeModel *dict;
  struct variable *var;
  gint dict_index;
  gint *indeces;
  GtkTreePath *path;
  const gchar *text =   gtk_entry_get_text (GTK_ENTRY (selector->dest));

  get_base_model (model, iter, &dict, &dict_iter);

  path = gtk_tree_model_get_path (dict, &dict_iter);

  indeces = gtk_tree_path_get_indices (path);

  dict_index = indeces [0];

  var = psppire_dict_get_variable (PSPPIRE_DICT (dict), dict_index);

  gtk_tree_path_free (path);

  name = recode_string (UTF8, psppire_dict_encoding (PSPPIRE_DICT (dict)),
			var_get_name (var), -1);
  result = ( 0 == strcmp (text, name));
  g_free (name);

  return result;
}



