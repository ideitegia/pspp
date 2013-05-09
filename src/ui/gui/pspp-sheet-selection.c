/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011, 2012, 2013 Free Software Foundation, Inc.

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

/* gtktreeselection.h
 * Copyright (C) 2000  Red Hat, Inc.,  Jonathan Blandford <jrb@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include "ui/gui/pspp-sheet-private.h"

#include <gtk/gtk.h>
#include <string.h>

#include "ui/gui/pspp-sheet-selection.h"

#include "libpspp/range-set.h"

static void pspp_sheet_selection_finalize          (GObject               *object);
static gint pspp_sheet_selection_real_select_all   (PsppSheetSelection      *selection);
static gint pspp_sheet_selection_real_unselect_all (PsppSheetSelection      *selection);
static gint pspp_sheet_selection_real_select_node  (PsppSheetSelection      *selection,
						  int node,
						  gboolean               select);

enum
{
  CHANGED,
  LAST_SIGNAL
};

static guint tree_selection_signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (PsppSheetSelection, pspp_sheet_selection, G_TYPE_OBJECT)

static void
pspp_sheet_selection_class_init (PsppSheetSelectionClass *class)
{
  GObjectClass *object_class;

  object_class = (GObjectClass*) class;

  object_class->finalize = pspp_sheet_selection_finalize;
  class->changed = NULL;

  tree_selection_signals[CHANGED] =
    g_signal_new ("changed",
		  G_OBJECT_CLASS_TYPE (object_class),
		  G_SIGNAL_RUN_FIRST,
		  G_STRUCT_OFFSET (PsppSheetSelectionClass, changed),
		  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);
}

static void
pspp_sheet_selection_init (PsppSheetSelection *selection)
{
  selection->type = PSPP_SHEET_SELECTION_SINGLE;
}

static void
pspp_sheet_selection_finalize (GObject *object)
{
  G_OBJECT_CLASS (pspp_sheet_selection_parent_class)->finalize (object);
}

/**
 * _pspp_sheet_selection_new:
 *
 * Creates a new #PsppSheetSelection object.  This function should not be invoked,
 * as each #PsppSheetView will create its own #PsppSheetSelection.
 *
 * Return value: A newly created #PsppSheetSelection object.
 **/
PsppSheetSelection*
_pspp_sheet_selection_new (void)
{
  PsppSheetSelection *selection;

  selection = g_object_new (PSPP_TYPE_SHEET_SELECTION, NULL);

  return selection;
}

/**
 * _pspp_sheet_selection_new_with_tree_view:
 * @tree_view: The #PsppSheetView.
 *
 * Creates a new #PsppSheetSelection object.  This function should not be invoked,
 * as each #PsppSheetView will create its own #PsppSheetSelection.
 *
 * Return value: A newly created #PsppSheetSelection object.
 **/
PsppSheetSelection*
_pspp_sheet_selection_new_with_tree_view (PsppSheetView *tree_view)
{
  PsppSheetSelection *selection;

  g_return_val_if_fail (PSPP_IS_SHEET_VIEW (tree_view), NULL);

  selection = _pspp_sheet_selection_new ();
  _pspp_sheet_selection_set_tree_view (selection, tree_view);

  return selection;
}

/**
 * _pspp_sheet_selection_set_tree_view:
 * @selection: A #PsppSheetSelection.
 * @tree_view: The #PsppSheetView.
 *
 * Sets the #PsppSheetView of @selection.  This function should not be invoked, as
 * it is used internally by #PsppSheetView.
 **/
void
_pspp_sheet_selection_set_tree_view (PsppSheetSelection *selection,
                                   PsppSheetView      *tree_view)
{
  g_return_if_fail (PSPP_IS_SHEET_SELECTION (selection));
  if (tree_view != NULL)
    g_return_if_fail (PSPP_IS_SHEET_VIEW (tree_view));

  selection->tree_view = tree_view;
}

/**
 * pspp_sheet_selection_set_mode:
 * @selection: A #PsppSheetSelection.
 * @type: The selection mode
 *
 * Sets the selection mode of the @selection.  If the previous type was
 * #PSPP_SHEET_SELECTION_MULTIPLE or #PSPP_SHEET_SELECTION_RECTANGLE, then the
 * anchor is kept selected, if it was previously selected.
 **/
void
pspp_sheet_selection_set_mode (PsppSheetSelection *selection,
			     PsppSheetSelectionMode  type)
{
  g_return_if_fail (PSPP_IS_SHEET_SELECTION (selection));

  if (selection->type == type)
    return;

  if (type == PSPP_SHEET_SELECTION_NONE)
    {
      pspp_sheet_selection_unselect_all (selection);

      gtk_tree_row_reference_free (selection->tree_view->priv->anchor);
      selection->tree_view->priv->anchor = NULL;
    }
  else if (type == PSPP_SHEET_SELECTION_SINGLE ||
	   type == PSPP_SHEET_SELECTION_BROWSE)
    {
      int node = -1;
      gint selected = FALSE;
      GtkTreePath *anchor_path = NULL;

      if (selection->tree_view->priv->anchor)
	{
          anchor_path = gtk_tree_row_reference_get_path (selection->tree_view->priv->anchor);

          if (anchor_path)
            {
              _pspp_sheet_view_find_node (selection->tree_view,
                                        anchor_path,
                                        &node);

              if (node >= 0 && pspp_sheet_view_node_is_selected (selection->tree_view, node))
                selected = TRUE;
            }
	}

      /* We do this so that we unconditionally unset all rows
       */
      pspp_sheet_selection_unselect_all (selection);

      if (node >= 0 && selected)
	_pspp_sheet_selection_internal_select_node (selection,
						  node,
						  anchor_path,
                                                  0,
						  FALSE);
      if (anchor_path)
	gtk_tree_path_free (anchor_path);
    }

  /* XXX unselect all columns when switching to/from rectangular selection? */

  selection->type = type;
}

/**
 * pspp_sheet_selection_get_mode:
 * @selection: a #PsppSheetSelection
 *
 * Gets the selection mode for @selection. See
 * pspp_sheet_selection_set_mode().
 *
 * Return value: the current selection mode
 **/
PsppSheetSelectionMode
pspp_sheet_selection_get_mode (PsppSheetSelection *selection)
{
  g_return_val_if_fail (PSPP_IS_SHEET_SELECTION (selection), PSPP_SHEET_SELECTION_SINGLE);

  return selection->type;
}

/**
 * pspp_sheet_selection_get_tree_view:
 * @selection: A #PsppSheetSelection
 * 
 * Returns the tree view associated with @selection.
 * 
 * Return value: A #PsppSheetView
 **/
PsppSheetView *
pspp_sheet_selection_get_tree_view (PsppSheetSelection *selection)
{
  g_return_val_if_fail (PSPP_IS_SHEET_SELECTION (selection), NULL);

  return selection->tree_view;
}

/**
 * pspp_sheet_selection_get_selected:
 * @selection: A #PsppSheetSelection.
 * @model: (out) (allow-none): A pointer to set to the #GtkTreeModel, or NULL.
 * @iter: (allow-none): The #GtkTreeIter, or NULL.
 *
 * Sets @iter to the currently selected node if @selection is set to
 * #PSPP_SHEET_SELECTION_SINGLE or #PSPP_SHEET_SELECTION_BROWSE.  @iter may be
 * NULL if you just want to test if @selection has any selected nodes.  @model
 * is filled with the current model as a convenience.  This function will not
 * work if @selection's mode is #PSPP_SHEET_SELECTION_MULTIPLE or
 * #PSPP_SHEET_SELECTION_RECTANGLE.
 *
 * Return value: TRUE, if there is a selected node.
 **/
gboolean
pspp_sheet_selection_get_selected (PsppSheetSelection  *selection,
				 GtkTreeModel     **model,
				 GtkTreeIter       *iter)
{
  int node;
  GtkTreePath *anchor_path;
  gboolean retval;

  g_return_val_if_fail (PSPP_IS_SHEET_SELECTION (selection), FALSE);
  g_return_val_if_fail (selection->type != PSPP_SHEET_SELECTION_MULTIPLE &&
                        selection->type != PSPP_SHEET_SELECTION_RECTANGLE,
                        FALSE);
  g_return_val_if_fail (selection->tree_view != NULL, FALSE);

  /* Clear the iter */
  if (iter)
    memset (iter, 0, sizeof (GtkTreeIter));

  if (model)
    *model = selection->tree_view->priv->model;

  if (selection->tree_view->priv->anchor == NULL)
    return FALSE;

  anchor_path = gtk_tree_row_reference_get_path (selection->tree_view->priv->anchor);

  if (anchor_path == NULL)
    return FALSE;

  retval = FALSE;

  _pspp_sheet_view_find_node (selection->tree_view,
                              anchor_path,
                              &node);

  if (pspp_sheet_view_node_is_selected (selection->tree_view, node))
    {
      /* we only want to return the anchor if it exists in the rbtree and
       * is selected.
       */
      if (iter == NULL)
	retval = TRUE;
      else
        retval = gtk_tree_model_get_iter (selection->tree_view->priv->model,
                                          iter,
                                          anchor_path);
    }
  else
    {
      /* We don't want to return the anchor if it isn't actually selected.
       */
      retval = FALSE;
    }

  gtk_tree_path_free (anchor_path);

  return retval;
}

/**
 * pspp_sheet_selection_get_selected_rows:
 * @selection: A #PsppSheetSelection.
 * @model: (allow-none): A pointer to set to the #GtkTreeModel, or NULL.
 *
 * Creates a list of path of all selected rows. Additionally, if you are
 * planning on modifying the model after calling this function, you may
 * want to convert the returned list into a list of #GtkTreeRowReference<!-- -->s.
 * To do this, you can use gtk_tree_row_reference_new().
 *
 * To free the return value, use:
 * |[
 * g_list_foreach (list, (GFunc) gtk_tree_path_free, NULL);
 * g_list_free (list);
 * ]|
 *
 * Return value: (element-type GtkTreePath) (transfer full): A #GList containing a #GtkTreePath for each selected row.
 *
 * Since: 2.2
 **/
GList *
pspp_sheet_selection_get_selected_rows (PsppSheetSelection   *selection,
                                      GtkTreeModel      **model)
{
  const struct range_tower_node *node;
  unsigned long int start;
  GList *list = NULL;

  g_return_val_if_fail (PSPP_IS_SHEET_SELECTION (selection), NULL);
  g_return_val_if_fail (selection->tree_view != NULL, NULL);

  if (model)
    *model = selection->tree_view->priv->model;

  if (selection->tree_view->priv->row_count == 0)
    return NULL;

  if (selection->type == PSPP_SHEET_SELECTION_NONE)
    return NULL;
  else if (selection->type != PSPP_SHEET_SELECTION_MULTIPLE &&
           selection->type != PSPP_SHEET_SELECTION_RECTANGLE)
    {
      GtkTreeIter iter;

      if (pspp_sheet_selection_get_selected (selection, NULL, &iter))
        {
	  GtkTreePath *path;

	  path = gtk_tree_model_get_path (selection->tree_view->priv->model, &iter);
	  list = g_list_append (list, path);

	  return list;
	}

      return NULL;
    }

  RANGE_TOWER_FOR_EACH (node, start, selection->tree_view->priv->selected)
    {
      unsigned long int width = range_tower_node_get_width (node);
      unsigned long int index;

      for (index = start; index < start + width; index++)
        list = g_list_prepend (list, gtk_tree_path_new_from_indices (index, -1));
    }

  return g_list_reverse (list);
}

/**
 * pspp_sheet_selection_count_selected_rows:
 * @selection: A #PsppSheetSelection.
 *
 * Returns the number of rows that have been selected in @tree.
 *
 * Return value: The number of rows selected.
 * 
 * Since: 2.2
 **/
gint
pspp_sheet_selection_count_selected_rows (PsppSheetSelection *selection)
{
  const struct range_tower_node *node;
  unsigned long int start;
  gint count = 0;

  g_return_val_if_fail (PSPP_IS_SHEET_SELECTION (selection), 0);
  g_return_val_if_fail (selection->tree_view != NULL, 0);

  if (selection->tree_view->priv->row_count == 0)
    return 0;

  if (selection->type == PSPP_SHEET_SELECTION_SINGLE ||
      selection->type == PSPP_SHEET_SELECTION_BROWSE)
    {
      if (pspp_sheet_selection_get_selected (selection, NULL, NULL))
	return 1;
      else
	return 0;
    }

  count = 0;
  RANGE_TOWER_FOR_EACH (node, start, selection->tree_view->priv->selected)
    count += range_tower_node_get_width (node);

  return count;
}

/* pspp_sheet_selection_selected_foreach helper */
static void
model_changed (gpointer data)
{
  gboolean *stop = (gboolean *)data;

  *stop = TRUE;
}

/**
 * pspp_sheet_selection_selected_foreach:
 * @selection: A #PsppSheetSelection.
 * @func: The function to call for each selected node.
 * @data: user data to pass to the function.
 *
 * Calls a function for each selected node. Note that you cannot modify
 * the tree or selection from within this function. As a result,
 * pspp_sheet_selection_get_selected_rows() might be more useful.
 **/
void
pspp_sheet_selection_selected_foreach (PsppSheetSelection            *selection,
				     PsppSheetSelectionForeachFunc  func,
				     gpointer                     data)
{
  const struct range_tower_node *node;
  unsigned long int start;
  GtkTreePath *path;
  GtkTreeIter iter;
  GtkTreeModel *model;

  gulong inserted_id, deleted_id, reordered_id, changed_id;
  gboolean stop = FALSE;

  g_return_if_fail (PSPP_IS_SHEET_SELECTION (selection));
  g_return_if_fail (selection->tree_view != NULL);

  if (func == NULL ||
      selection->tree_view->priv->row_count == 0)
    return;

  if (selection->type == PSPP_SHEET_SELECTION_SINGLE ||
      selection->type == PSPP_SHEET_SELECTION_BROWSE)
    {
      if (gtk_tree_row_reference_valid (selection->tree_view->priv->anchor))
	{
	  path = gtk_tree_row_reference_get_path (selection->tree_view->priv->anchor);
	  gtk_tree_model_get_iter (selection->tree_view->priv->model, &iter, path);
	  (* func) (selection->tree_view->priv->model, path, &iter, data);
	  gtk_tree_path_free (path);
	}
      return;
    }

  model = selection->tree_view->priv->model;
  g_object_ref (model);

  /* connect to signals to monitor changes in treemodel */
  inserted_id = g_signal_connect_swapped (model, "row-inserted",
					  G_CALLBACK (model_changed),
				          &stop);
  deleted_id = g_signal_connect_swapped (model, "row-deleted",
					 G_CALLBACK (model_changed),
				         &stop);
  reordered_id = g_signal_connect_swapped (model, "rows-reordered",
					   G_CALLBACK (model_changed),
				           &stop);
  changed_id = g_signal_connect_swapped (selection->tree_view, "notify::model",
					 G_CALLBACK (model_changed), 
					 &stop);

  RANGE_TOWER_FOR_EACH (node, start, selection->tree_view->priv->selected)
    {
      unsigned long int width = range_tower_node_get_width (node);
      unsigned long int index;

      for (index = start; index < start + width; index++)
        {
          GtkTreePath *path;
          GtkTreeIter iter;

          path = gtk_tree_path_new_from_indices (index, -1);
          gtk_tree_model_get_iter (model, &iter, path);
	  (* func) (model, path, &iter, data);
          gtk_tree_path_free (path);
        }
    }

  g_signal_handler_disconnect (model, inserted_id);
  g_signal_handler_disconnect (model, deleted_id);
  g_signal_handler_disconnect (model, reordered_id);
  g_signal_handler_disconnect (selection->tree_view, changed_id);
  g_object_unref (model);

  /* check if we have to spew a scary message */
  if (stop)
    g_warning ("The model has been modified from within pspp_sheet_selection_selected_foreach.\n"
	       "This function is for observing the selections of the tree only.  If\n"
	       "you are trying to get all selected items from the tree, try using\n"
	       "pspp_sheet_selection_get_selected_rows instead.\n");
}

/**
 * pspp_sheet_selection_select_path:
 * @selection: A #PsppSheetSelection.
 * @path: The #GtkTreePath to be selected.
 *
 * Select the row at @path.
 **/
void
pspp_sheet_selection_select_path (PsppSheetSelection *selection,
				GtkTreePath      *path)
{
  int node;
  PsppSheetSelectMode mode = 0;

  g_return_if_fail (PSPP_IS_SHEET_SELECTION (selection));
  g_return_if_fail (selection->tree_view != NULL);
  g_return_if_fail (path != NULL);

   _pspp_sheet_view_find_node (selection->tree_view,
                               path,
                               &node);

  if (node < 0 || pspp_sheet_view_node_is_selected (selection->tree_view, node)) 
    return;

  if (selection->type == PSPP_SHEET_SELECTION_MULTIPLE ||
      selection->type == PSPP_SHEET_SELECTION_RECTANGLE)
    mode = PSPP_SHEET_SELECT_MODE_TOGGLE;

  _pspp_sheet_selection_internal_select_node (selection,
					    node,
					    path,
                                            mode,
					    FALSE);
}

/**
 * pspp_sheet_selection_unselect_path:
 * @selection: A #PsppSheetSelection.
 * @path: The #GtkTreePath to be unselected.
 *
 * Unselects the row at @path.
 **/
void
pspp_sheet_selection_unselect_path (PsppSheetSelection *selection,
				  GtkTreePath      *path)
{
  int node;

  g_return_if_fail (PSPP_IS_SHEET_SELECTION (selection));
  g_return_if_fail (selection->tree_view != NULL);
  g_return_if_fail (path != NULL);

  _pspp_sheet_view_find_node (selection->tree_view,
                              path,
                              &node);

  if (node < 0 || !pspp_sheet_view_node_is_selected (selection->tree_view, node)) 
    return;

  _pspp_sheet_selection_internal_select_node (selection,
					    node,
					    path,
                                            PSPP_SHEET_SELECT_MODE_TOGGLE,
					    TRUE);
}

/**
 * pspp_sheet_selection_select_iter:
 * @selection: A #PsppSheetSelection.
 * @iter: The #GtkTreeIter to be selected.
 *
 * Selects the specified iterator.
 **/
void
pspp_sheet_selection_select_iter (PsppSheetSelection *selection,
				GtkTreeIter      *iter)
{
  GtkTreePath *path;

  g_return_if_fail (PSPP_IS_SHEET_SELECTION (selection));
  g_return_if_fail (selection->tree_view != NULL);
  g_return_if_fail (selection->tree_view->priv->model != NULL);
  g_return_if_fail (iter != NULL);

  path = gtk_tree_model_get_path (selection->tree_view->priv->model,
				  iter);

  if (path == NULL)
    return;

  pspp_sheet_selection_select_path (selection, path);
  gtk_tree_path_free (path);
}


/**
 * pspp_sheet_selection_unselect_iter:
 * @selection: A #PsppSheetSelection.
 * @iter: The #GtkTreeIter to be unselected.
 *
 * Unselects the specified iterator.
 **/
void
pspp_sheet_selection_unselect_iter (PsppSheetSelection *selection,
				  GtkTreeIter      *iter)
{
  GtkTreePath *path;

  g_return_if_fail (PSPP_IS_SHEET_SELECTION (selection));
  g_return_if_fail (selection->tree_view != NULL);
  g_return_if_fail (selection->tree_view->priv->model != NULL);
  g_return_if_fail (iter != NULL);

  path = gtk_tree_model_get_path (selection->tree_view->priv->model,
				  iter);

  if (path == NULL)
    return;

  pspp_sheet_selection_unselect_path (selection, path);
  gtk_tree_path_free (path);
}

/**
 * pspp_sheet_selection_path_is_selected:
 * @selection: A #PsppSheetSelection.
 * @path: A #GtkTreePath to check selection on.
 * 
 * Returns %TRUE if the row pointed to by @path is currently selected.  If @path
 * does not point to a valid location, %FALSE is returned
 * 
 * Return value: %TRUE if @path is selected.
 **/
gboolean
pspp_sheet_selection_path_is_selected (PsppSheetSelection *selection,
				     GtkTreePath      *path)
{
  int node;

  g_return_val_if_fail (PSPP_IS_SHEET_SELECTION (selection), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (selection->tree_view != NULL, FALSE);

  if (selection->tree_view->priv->model == NULL)
    return FALSE;

  _pspp_sheet_view_find_node (selection->tree_view,
				  path,
				  &node);

  if (node < 0 || !pspp_sheet_view_node_is_selected (selection->tree_view, node)) 
    return FALSE;

  return TRUE;
}

/**
 * pspp_sheet_selection_iter_is_selected:
 * @selection: A #PsppSheetSelection
 * @iter: A valid #GtkTreeIter
 * 
 * Returns %TRUE if the row at @iter is currently selected.
 * 
 * Return value: %TRUE, if @iter is selected
 **/
gboolean
pspp_sheet_selection_iter_is_selected (PsppSheetSelection *selection,
				     GtkTreeIter      *iter)
{
  GtkTreePath *path;
  gboolean retval;

  g_return_val_if_fail (PSPP_IS_SHEET_SELECTION (selection), FALSE);
  g_return_val_if_fail (iter != NULL, FALSE);
  g_return_val_if_fail (selection->tree_view != NULL, FALSE);
  g_return_val_if_fail (selection->tree_view->priv->model != NULL, FALSE);

  path = gtk_tree_model_get_path (selection->tree_view->priv->model, iter);
  if (path == NULL)
    return FALSE;

  retval = pspp_sheet_selection_path_is_selected (selection, path);
  gtk_tree_path_free (path);

  return retval;
}


/* We have a real_{un,}select_all function that doesn't emit the signal, so we
 * can use it in other places without fear of the signal being emitted.
 */
static gint
pspp_sheet_selection_real_select_all (PsppSheetSelection *selection)
{
  const struct range_tower_node *node;
  int row_count = selection->tree_view->priv->row_count;

  if (row_count == 0)
    return FALSE;

  node = range_tower_first (selection->tree_view->priv->selected);
  if (node
      && range_tower_node_get_start (node) == 0
      && range_tower_node_get_width (node) >= row_count)
    return FALSE;

  range_tower_set1 (selection->tree_view->priv->selected, 0, row_count);
  pspp_sheet_selection_select_all_columns (selection);

  /* XXX we could invalidate individual visible rows instead */
  gdk_window_invalidate_rect (selection->tree_view->priv->bin_window, NULL, TRUE);

  return TRUE;
}

/**
 * pspp_sheet_selection_select_all:
 * @selection: A #PsppSheetSelection.
 *
 * Selects all the nodes and column. @selection must be set to
 * #PSPP_SHEET_SELECTION_MULTIPLE or  #PSPP_SHEET_SELECTION_RECTANGLE mode.
 **/
void
pspp_sheet_selection_select_all (PsppSheetSelection *selection)
{
  g_return_if_fail (PSPP_IS_SHEET_SELECTION (selection));
  g_return_if_fail (selection->tree_view != NULL);

  if (selection->tree_view->priv->row_count == 0 || selection->tree_view->priv->model == NULL)
    return;

  g_return_if_fail (selection->type == PSPP_SHEET_SELECTION_MULTIPLE ||
                    selection->type == PSPP_SHEET_SELECTION_RECTANGLE);

  if (pspp_sheet_selection_real_select_all (selection))
    g_signal_emit (selection, tree_selection_signals[CHANGED], 0);
}

static gint
pspp_sheet_selection_real_unselect_all (PsppSheetSelection *selection)
{
  if (selection->type == PSPP_SHEET_SELECTION_SINGLE ||
      selection->type == PSPP_SHEET_SELECTION_BROWSE)
    {
      int node = -1;
      GtkTreePath *anchor_path;

      if (selection->tree_view->priv->anchor == NULL)
	return FALSE;

      anchor_path = gtk_tree_row_reference_get_path (selection->tree_view->priv->anchor);

      if (anchor_path == NULL)
        return FALSE;

      _pspp_sheet_view_find_node (selection->tree_view,
                                anchor_path,
				&node);

      gtk_tree_path_free (anchor_path);

      if (node < 0)
        return FALSE;

      if (pspp_sheet_view_node_is_selected (selection->tree_view, node))
	{
	  if (pspp_sheet_selection_real_select_node (selection, node, FALSE))
	    {
	      gtk_tree_row_reference_free (selection->tree_view->priv->anchor);
	      selection->tree_view->priv->anchor = NULL;
	      return TRUE;
	    }
	}
      return FALSE;
    }
  else if (range_tower_is_empty (selection->tree_view->priv->selected))
    return FALSE;
  else
    {
      range_tower_set0 (selection->tree_view->priv->selected, 0, ULONG_MAX);
      pspp_sheet_selection_unselect_all_columns (selection);

      /* XXX we could invalidate individual visible rows instead */
      gdk_window_invalidate_rect (selection->tree_view->priv->bin_window, NULL, TRUE);

      return TRUE;
    }
}

/**
 * pspp_sheet_selection_unselect_all:
 * @selection: A #PsppSheetSelection.
 *
 * Unselects all the nodes and columns.
 **/
void
pspp_sheet_selection_unselect_all (PsppSheetSelection *selection)
{
  g_return_if_fail (PSPP_IS_SHEET_SELECTION (selection));
  g_return_if_fail (selection->tree_view != NULL);

  if (selection->tree_view->priv->row_count == 0 || selection->tree_view->priv->model == NULL)
    return;
  
  if (pspp_sheet_selection_real_unselect_all (selection))
    g_signal_emit (selection, tree_selection_signals[CHANGED], 0);
}

enum
{
  RANGE_SELECT,
  RANGE_UNSELECT
};

static gint
pspp_sheet_selection_real_modify_range (PsppSheetSelection *selection,
                                      gint              mode,
				      GtkTreePath      *start_path,
				      GtkTreePath      *end_path)
{
  int start_node, end_node;
  GtkTreePath *anchor_path = NULL;
  gboolean dirty = FALSE;

  switch (gtk_tree_path_compare (start_path, end_path))
    {
    case 1:
      _pspp_sheet_view_find_node (selection->tree_view,
				end_path,
				&start_node);
      _pspp_sheet_view_find_node (selection->tree_view,
				start_path,
				&end_node);
      anchor_path = start_path;
      break;
    case 0:
      _pspp_sheet_view_find_node (selection->tree_view,
				start_path,
				&start_node);
      end_node = start_node;
      anchor_path = start_path;
      break;
    case -1:
      _pspp_sheet_view_find_node (selection->tree_view,
				start_path,
				&start_node);
      _pspp_sheet_view_find_node (selection->tree_view,
				end_path,
				&end_node);
      anchor_path = start_path;
      break;
    }

  g_return_val_if_fail (start_node >= 0, FALSE);
  g_return_val_if_fail (end_node >= 0, FALSE);

  if (anchor_path)
    {
      if (selection->tree_view->priv->anchor)
	gtk_tree_row_reference_free (selection->tree_view->priv->anchor);

      selection->tree_view->priv->anchor =
	gtk_tree_row_reference_new_proxy (G_OBJECT (selection->tree_view),
	                                  selection->tree_view->priv->model,
					  anchor_path);
    }

  do
    {
      dirty |= pspp_sheet_selection_real_select_node (selection, start_node, (mode == RANGE_SELECT)?TRUE:FALSE);

      if (start_node == end_node)
	break;

      start_node = pspp_sheet_view_node_next (selection->tree_view, start_node);
      if (start_node < 0)
        {
          /* we just ran out of tree.  That means someone passed in bogus values.
           */
          return dirty;
        }
    }
  while (TRUE);

  return dirty;
}

/**
 * pspp_sheet_selection_select_range:
 * @selection: A #PsppSheetSelection.
 * @start_path: The initial node of the range.
 * @end_path: The final node of the range.
 *
 * Selects a range of nodes, determined by @start_path and @end_path inclusive.
 * @selection must be set to #PSPP_SHEET_SELECTION_MULTIPLE or
 * #PSPP_SHEET_SELECTION_RECTANGLE mode.
 **/
void
pspp_sheet_selection_select_range (PsppSheetSelection *selection,
				 GtkTreePath      *start_path,
				 GtkTreePath      *end_path)
{
  g_return_if_fail (PSPP_IS_SHEET_SELECTION (selection));
  g_return_if_fail (selection->tree_view != NULL);
  g_return_if_fail (selection->type == PSPP_SHEET_SELECTION_MULTIPLE ||
                    selection->type == PSPP_SHEET_SELECTION_RECTANGLE);
  g_return_if_fail (selection->tree_view->priv->model != NULL);

  if (pspp_sheet_selection_real_modify_range (selection, RANGE_SELECT, start_path, end_path))
    g_signal_emit (selection, tree_selection_signals[CHANGED], 0);
}

/**
 * pspp_sheet_selection_unselect_range:
 * @selection: A #PsppSheetSelection.
 * @start_path: The initial node of the range.
 * @end_path: The initial node of the range.
 *
 * Unselects a range of nodes, determined by @start_path and @end_path
 * inclusive.
 *
 * Since: 2.2
 **/
void
pspp_sheet_selection_unselect_range (PsppSheetSelection *selection,
                                   GtkTreePath      *start_path,
				   GtkTreePath      *end_path)
{
  g_return_if_fail (PSPP_IS_SHEET_SELECTION (selection));
  g_return_if_fail (selection->tree_view != NULL);
  g_return_if_fail (selection->tree_view->priv->model != NULL);

  if (pspp_sheet_selection_real_modify_range (selection, RANGE_UNSELECT, start_path, end_path))
    g_signal_emit (selection, tree_selection_signals[CHANGED], 0);
}

struct range_set *
pspp_sheet_selection_get_range_set (PsppSheetSelection *selection)
{
  const struct range_tower_node *node;
  unsigned long int start;
  struct range_set *set;

  g_return_val_if_fail (PSPP_IS_SHEET_SELECTION (selection),
                        range_set_create ());
  g_return_val_if_fail (selection->tree_view != NULL, range_set_create ());

  set = range_set_create ();
  RANGE_TOWER_FOR_EACH (node, start, selection->tree_view->priv->selected)
    range_set_set1 (set, start, range_tower_node_get_width (node));
  return set;
}

/* Called internally by gtktreeview.c It handles actually selecting the tree.
 */

/*
 * docs about the 'override_browse_mode', we set this flag when we want to
 * unset select the node and override the select browse mode behaviour (that is
 * 'one node should *always* be selected').
 */
void
_pspp_sheet_selection_internal_select_node (PsppSheetSelection *selection,
					  int               node,
					  GtkTreePath      *path,
                                          PsppSheetSelectMode mode,
					  gboolean          override_browse_mode)
{
  gint dirty = FALSE;
  GtkTreePath *anchor_path = NULL;

  if (selection->type == PSPP_SHEET_SELECTION_NONE)
    return;

  if (selection->tree_view->priv->anchor)
    anchor_path = gtk_tree_row_reference_get_path (selection->tree_view->priv->anchor);

  if (selection->type == PSPP_SHEET_SELECTION_SINGLE ||
      selection->type == PSPP_SHEET_SELECTION_BROWSE)
    {
      /* just unselect */
      if (selection->type == PSPP_SHEET_SELECTION_BROWSE && override_browse_mode)
        {
	  dirty = pspp_sheet_selection_real_unselect_all (selection);
	}
      /* Did we try to select the same node again? */
      else if (selection->type == PSPP_SHEET_SELECTION_SINGLE &&
	       anchor_path && gtk_tree_path_compare (path, anchor_path) == 0)
	{
	  if ((mode & PSPP_SHEET_SELECT_MODE_TOGGLE) == PSPP_SHEET_SELECT_MODE_TOGGLE)
	    {
	      dirty = pspp_sheet_selection_real_unselect_all (selection);
	    }
	}
      else
	{
	  if (anchor_path)
	    {
              dirty = pspp_sheet_selection_real_unselect_all (selection);

	      /* if dirty is TRUE at this point, we successfully unselected the
	       * old one, and can then select the new one */
	      if (dirty)
		{
		  if (selection->tree_view->priv->anchor)
                    {
                      gtk_tree_row_reference_free (selection->tree_view->priv->anchor);
                      selection->tree_view->priv->anchor = NULL;
                    }

		  if (pspp_sheet_selection_real_select_node (selection, node, TRUE))
		    {
		      selection->tree_view->priv->anchor =
			gtk_tree_row_reference_new_proxy (G_OBJECT (selection->tree_view), selection->tree_view->priv->model, path);
		    }
		}
	    }
	  else
	    {
	      if (pspp_sheet_selection_real_select_node (selection, node, TRUE))
		{
		  dirty = TRUE;
		  if (selection->tree_view->priv->anchor)
		    gtk_tree_row_reference_free (selection->tree_view->priv->anchor);

		  selection->tree_view->priv->anchor =
		    gtk_tree_row_reference_new_proxy (G_OBJECT (selection->tree_view), selection->tree_view->priv->model, path);
		}
	    }
	}
    }
  else if (selection->type == PSPP_SHEET_SELECTION_MULTIPLE ||
           selection->type == PSPP_SHEET_SELECTION_RECTANGLE)
    {
      if ((mode & PSPP_SHEET_SELECT_MODE_EXTEND) == PSPP_SHEET_SELECT_MODE_EXTEND
          && (anchor_path == NULL))
	{
	  if (selection->tree_view->priv->anchor)
	    gtk_tree_row_reference_free (selection->tree_view->priv->anchor);

	  selection->tree_view->priv->anchor =
	    gtk_tree_row_reference_new_proxy (G_OBJECT (selection->tree_view), selection->tree_view->priv->model, path);
	  dirty = pspp_sheet_selection_real_select_node (selection, node, TRUE);
	}
      else if ((mode & (PSPP_SHEET_SELECT_MODE_EXTEND | PSPP_SHEET_SELECT_MODE_TOGGLE)) == (PSPP_SHEET_SELECT_MODE_EXTEND | PSPP_SHEET_SELECT_MODE_TOGGLE))
	{
	  pspp_sheet_selection_select_range (selection,
					   anchor_path,
					   path);
	}
      else if ((mode & PSPP_SHEET_SELECT_MODE_TOGGLE) == PSPP_SHEET_SELECT_MODE_TOGGLE)
	{
          bool selected = pspp_sheet_view_node_is_selected (selection->tree_view, node);
	  if (selection->tree_view->priv->anchor)
	    gtk_tree_row_reference_free (selection->tree_view->priv->anchor);

	  selection->tree_view->priv->anchor =
	    gtk_tree_row_reference_new_proxy (G_OBJECT (selection->tree_view), selection->tree_view->priv->model, path);

	  if (selected)
	    dirty |= pspp_sheet_selection_real_select_node (selection, node, FALSE);
	  else
	    dirty |= pspp_sheet_selection_real_select_node (selection, node, TRUE);
	}
      else if ((mode & PSPP_SHEET_SELECT_MODE_EXTEND) == PSPP_SHEET_SELECT_MODE_EXTEND)
	{
	  dirty = pspp_sheet_selection_real_unselect_all (selection);
	  dirty |= pspp_sheet_selection_real_modify_range (selection,
                                                         RANGE_SELECT,
							 anchor_path,
							 path);
	}
      else
	{
	  dirty = pspp_sheet_selection_real_unselect_all (selection);

	  if (selection->tree_view->priv->anchor)
	    gtk_tree_row_reference_free (selection->tree_view->priv->anchor);

	  selection->tree_view->priv->anchor =
	    gtk_tree_row_reference_new_proxy (G_OBJECT (selection->tree_view), selection->tree_view->priv->model, path);

	  dirty |= pspp_sheet_selection_real_select_node (selection, node, TRUE);
	}
    }

  if (anchor_path)
    gtk_tree_path_free (anchor_path);

  if (dirty)
    g_signal_emit (selection, tree_selection_signals[CHANGED], 0);  
}


void 
_pspp_sheet_selection_emit_changed (PsppSheetSelection *selection)
{
  g_signal_emit (selection, tree_selection_signals[CHANGED], 0);  
}

/* NOTE: Any {un,}selection ever done _MUST_ be done through this function!
 */

static gint
pspp_sheet_selection_real_select_node (PsppSheetSelection *selection,
				     int               node,
				     gboolean          select)
{
  select = !! select;
  if (pspp_sheet_view_node_is_selected (selection->tree_view, node) != select)
    {
      if (select)
        pspp_sheet_view_node_select (selection->tree_view, node);
      else
        pspp_sheet_view_node_unselect (selection->tree_view, node);

      _pspp_sheet_view_queue_draw_node (selection->tree_view, node, NULL);
      
      return TRUE;
    }

  return FALSE;
}

void
pspp_sheet_selection_unselect_all_columns (PsppSheetSelection *selection)
{
  PsppSheetView *sheet_view = selection->tree_view;
  gboolean changed;
  GList *list;

  changed = FALSE;
  for (list = sheet_view->priv->columns; list; list = list->next)
    {
      PsppSheetViewColumn *column = list->data;
      if (column->selected)
        {
          column->selected = FALSE;
          changed = TRUE;
        }
    }
  if (changed && selection->type == PSPP_SHEET_SELECTION_RECTANGLE)
    {
      gtk_widget_queue_draw (GTK_WIDGET (selection->tree_view));
      _pspp_sheet_selection_emit_changed (selection);
    }
}

GList *
pspp_sheet_selection_get_selected_columns (PsppSheetSelection *selection)
{
  PsppSheetView *sheet_view = selection->tree_view;
  GList *selected_columns = NULL;
  GList *iter;

  g_return_val_if_fail (PSPP_IS_SHEET_SELECTION (selection), NULL);
  g_return_val_if_fail (selection->tree_view != NULL, NULL);

  if (selection->type != PSPP_SHEET_SELECTION_RECTANGLE)
    return NULL;

  for (iter = sheet_view->priv->columns; iter; iter = iter->next)
    {
      PsppSheetViewColumn *column = iter->data;
      if (column->selected)
        selected_columns = g_list_prepend (selected_columns, column);
    }
  return g_list_reverse (selected_columns);
}

gint
pspp_sheet_selection_count_selected_columns (PsppSheetSelection *selection)
{
  PsppSheetView *sheet_view = selection->tree_view;
  GList *list;
  gint n;

  n = 0;
  for (list = sheet_view->priv->columns; list; list = list->next)
    {
      PsppSheetViewColumn *column = list->data;
      if (column->selected)
        n++;
    }
  return n;
}

void
pspp_sheet_selection_select_all_columns (PsppSheetSelection *selection)
{
  PsppSheetView *sheet_view = selection->tree_view;
  gboolean changed;
  GList *list;

  changed = FALSE;
  for (list = sheet_view->priv->columns; list; list = list->next)
    {
      PsppSheetViewColumn *column = list->data;
      if (!column->selected && column->selectable)
        {
          /* XXX should use pspp_sheet_view_column_set_selected() here (and
             elsewhere) but we want to call
             _pspp_sheet_selection_emit_changed() only once for all the
             columns. */
          column->selected = TRUE;
          changed = TRUE;
        }
    }
  if (changed && selection->type == PSPP_SHEET_SELECTION_RECTANGLE)
    {
      _pspp_sheet_selection_emit_changed (selection);
      gtk_widget_queue_draw (GTK_WIDGET (selection->tree_view));
    }
}

void
pspp_sheet_selection_select_column (PsppSheetSelection        *selection,
                                    PsppSheetViewColumn       *column)
{
  if (!column->selected && column->selectable)
    {
      column->selected = TRUE;
      if (selection->type == PSPP_SHEET_SELECTION_RECTANGLE)
        {
          _pspp_sheet_selection_emit_changed (selection);
          gtk_widget_queue_draw (GTK_WIDGET (selection->tree_view));
        }
    }
}

void
pspp_sheet_selection_select_column_range  (PsppSheetSelection        *selection,
                                           PsppSheetViewColumn       *first,
                                           PsppSheetViewColumn       *last)
{
  PsppSheetView *sheet_view = selection->tree_view;
  gboolean in_range;
  gboolean changed;
  GList *list;

  in_range = FALSE;
  changed = FALSE;
  for (list = sheet_view->priv->columns; list; list = list->next)
    {
      PsppSheetViewColumn *column = list->data;
      gboolean c0 = column == first;
      gboolean c1 = column == last;

      if (in_range || c0 || c1)
        {
          if (!column->selected && column->selectable)
            {
              column->selected = TRUE;
              changed = TRUE;
            }
        }

      in_range = in_range ^ c0 ^ c1;
    }
  if (changed && selection->type == PSPP_SHEET_SELECTION_RECTANGLE)
    {
      _pspp_sheet_selection_emit_changed (selection);
      gtk_widget_queue_draw (GTK_WIDGET (selection->tree_view));
    }
}
