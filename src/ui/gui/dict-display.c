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
#include "helper.h"
#include <data/variable.h>
#include <data/format.h>

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

/* A GtkTreeCellDataFunc which sets the icon appropriate to the type
   of variable */
static void
var_icon_cell_data_func (GtkTreeViewColumn *col,
		       GtkCellRenderer *cell,
		       GtkTreeModel *model,
		       GtkTreeIter *iter,
		       gpointer data)
{
  struct variable *var;
  gtk_tree_model_get (model, iter, DICT_TVM_COL_VAR, &var, -1);

  if ( var_is_alpha (var))
    {
      g_object_set (cell, "stock-id", "var-string", NULL);
    }
  else
    {
      const struct fmt_spec *fs = var_get_write_format (var);
      int cat = fmt_get_category (fs->type);
      switch ( var_get_measure (var))
	{
	case MEASURE_NOMINAL:
	  g_object_set (cell, "stock-id", "var-nominal", NULL);
	  break;
	case MEASURE_ORDINAL:
	  g_object_set (cell, "stock-id", "var-ordinal", NULL);
	  break;
	case MEASURE_SCALE:
	  if ( ( FMT_CAT_DATE | FMT_CAT_TIME ) & cat )
	    g_object_set (cell, "stock-id", "var-date-scale", NULL);
	  else
	    g_object_set (cell, "stock-id", "var-scale", NULL);
	  break;
	default:
	  g_assert_not_reached ();
	};
    }
}


void
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

/* A GtkTreeCellDataFunc which renders the name and/or label of the
   variable */
static void
var_description_cell_data_func (GtkTreeViewColumn *col,
				GtkCellRenderer *cell,
				GtkTreeModel *top_model,
				GtkTreeIter *top_iter,
				gpointer data)
{
  struct variable *var;
  GtkTreeIter iter;
  GtkTreeModel *model;
  gboolean prefer_labels = FALSE;

  PsppireConf *conf = psppire_conf_new ();

  psppire_conf_get_boolean (conf, "dialog-boxes", "prefer-labels",
			    &prefer_labels);

  get_base_model (top_model, top_iter, &model, &iter);

  g_assert (PSPPIRE_IS_DICT (model));


  gtk_tree_model_get (model,
		      &iter, DICT_TVM_COL_VAR, &var, -1);

  if ( var_has_label (var) && prefer_labels)
    {
      gchar *text = g_strdup_printf (
				     "<span stretch=\"condensed\">%s</span>",
				     var_get_label (var));

      char *utf8 = pspp_locale_to_utf8 (text, -1, NULL);

      g_free (text);
      g_object_set (cell, "markup", utf8, NULL);
      g_free (utf8);
    }
  else
    {
      char *name = pspp_locale_to_utf8 (var_get_name (var), -1, NULL);
      g_object_set (cell, "text", name, NULL);
      g_free (name);
    }
}


/* Sets the tooltip to be the name of the variable under the cursor */
static gboolean
set_tooltip_for_variable (GtkTreeView  *treeview,
			  gint        x,
			  gint        y,
			  gboolean    keyboard_mode,
			  GtkTooltip *tooltip,
			  gpointer    user_data)

{
  gint bx, by;
  GtkTreeIter iter;
  GtkTreePath *path;
  GtkTreeModel *tree_model;
  struct variable *var = NULL;
  gboolean ok;


  gtk_tree_view_convert_widget_to_bin_window_coords (treeview,
                                                     x, y, &bx, &by);

  if (!gtk_tree_view_get_path_at_pos (treeview, bx, by,
                                      &path, NULL, NULL, NULL))
    return FALSE;

  tree_model = gtk_tree_view_get_model (treeview);


  gtk_tree_view_set_tooltip_row (treeview, tooltip, path);

  ok = gtk_tree_model_get_iter (tree_model, &iter, path);

  gtk_tree_path_free (path);
  if (!ok)
    return FALSE;


  gtk_tree_model_get (tree_model, &iter, DICT_TVM_COL_VAR,  &var, -1);

  if ( ! var_has_label (var))
    return FALSE;

  {
    gchar *tip ;
    gboolean prefer_labels = FALSE;

    PsppireConf *conf = psppire_conf_new ();

    psppire_conf_get_boolean (conf, "dialog-boxes", "prefer-labels",
			      &prefer_labels);

    if ( prefer_labels )
      tip = pspp_locale_to_utf8 (var_get_name (var), -1, NULL);
    else
      tip = pspp_locale_to_utf8 (var_get_label (var), -1, NULL);

    gtk_tooltip_set_text (tooltip, tip);

    g_free (tip);
  }

  return TRUE;
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

  GtkCellRenderer *renderer;

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


  col = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (col, _("Variable"));

  renderer = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (col, renderer, FALSE);

  gtk_tree_view_column_set_cell_data_func (col, renderer,
					   var_icon_cell_data_func,
					   NULL, NULL);


  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (col, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (col, renderer,
					   var_description_cell_data_func,
					   NULL, NULL);

  g_object_set (renderer, "ellipsize-set", TRUE, NULL);
  g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, NULL);

  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);

  /* FIXME: make this a value in terms of character widths */
  gtk_tree_view_column_set_min_width (col, 150);

  gtk_tree_view_append_column (treeview, col);

  gtk_tree_selection_set_mode (selection, mode);

  g_object_set (treeview, "has-tooltip", TRUE, NULL);

  g_signal_connect (treeview, "query-tooltip", G_CALLBACK (set_tooltip_for_variable), NULL);
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

  name = pspp_locale_to_utf8 (var_get_name (var), -1, NULL);
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

  name = pspp_locale_to_utf8 (var_get_name (var), -1, NULL);
  result = ( 0 == strcmp (text, name));
  g_free (name);

  return result;
}


