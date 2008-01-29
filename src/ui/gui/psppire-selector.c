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

/*
  This module provides a widget, PsppireSelector derived from
  GtkButton.

  It contains a GtkArrow, and is used for selecting objects from a
  GtkTreeView and putting them into a destination widget (often
  another GtkTreeView).  Typically this is used in psppire for
  selecting variables, thus:


  +----------------------------------------------------------+
  |				    			     |
  |	Source Widget  	       	       	    Dest Widget	     |
  |   +----------------+       	       	 +----------------+  |
  |   |	Variable0      |	    	 | Variable2   	  |  |
  |   |	Variable1      |       	       	 |     	       	  |  |
  |   | Variable3      |	    	 |	       	  |  |
  |   |		       |    Selector	 |	       	  |  |
  |   |	       	       |       	       	 |     	       	  |  |
  |   |		       |    +------+   	 |     	       	  |  |
  |   |		       |    | |\   |	 |		  |  |
  |   |		       |    | | \  |	 |		  |  |
  |   |		       |    | |	/  |	 |		  |  |
  |   |		       |    | |/   |	 |		  |  |
  |   |		       |    +------+	 |		  |  |
  |   |		       |		 |		  |  |
  |   |		       |		 |		  |  |
  |   |		       |		 |		  |  |
  |   |	       	       |       	       	 |     	       	  |  |
  |   +----------------+		 +----------------+  |
  |							     |
  +----------------------------------------------------------+

  The Source Widget is always a GtkTreeView.  The Dest Widget may be a
  GtkTreeView or a GtkEntry (other destination widgets may be
  supported in the future).

  Widgets may be source to more than one PsppireSelector.
*/


#include <config.h>

#include <gtk/gtksignal.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkentry.h>

#include "psppire-selector.h"

#include <gtk/gtktreeview.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtktextview.h>
#include <gtk/gtkwidget.h>

static void psppire_selector_base_finalize (PsppireSelectorClass *, gpointer);
static void psppire_selector_base_init     (PsppireSelectorClass *class);
static void psppire_selector_class_init    (PsppireSelectorClass *class);
static void psppire_selector_init          (PsppireSelector      *selector);


static void set_direction (PsppireSelector *, enum psppire_selector_dir);


enum  {SELECTED,    /* Emitted when an item is inserted into dest */
       DE_SELECTED, /* Emitted when an item is removed from dest */
       n_SIGNALS};

static guint signals [n_SIGNALS];


GType
psppire_selector_get_type (void)
{
  static GType psppire_selector_type = 0;

  if (!psppire_selector_type)
    {
      static const GTypeInfo psppire_selector_info =
      {
	sizeof (PsppireSelectorClass),
	(GBaseInitFunc) psppire_selector_base_init,
        (GBaseFinalizeFunc) psppire_selector_base_finalize,
	(GClassInitFunc)psppire_selector_class_init,
	(GClassFinalizeFunc) NULL,
	NULL,
        sizeof (PsppireSelector),
	0,
	(GInstanceInitFunc) psppire_selector_init,
      };

      psppire_selector_type =
	g_type_register_static (GTK_TYPE_BUTTON, "PsppireSelector",
				&psppire_selector_info, 0);
    }

  return psppire_selector_type;
}


static void
psppire_selector_finalize (GObject *object)
{
}

/* Properties */
enum
{
  PROP_0,
  PROP_ORIENTATION
};


static void on_activate (PsppireSelector *selector, gpointer data);


static void
psppire_selector_set_property (GObject         *object,
			       guint            prop_id,
			       const GValue    *value,
			       GParamSpec      *pspec)
{
  PsppireSelector *selector = PSPPIRE_SELECTOR (object);

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      selector->orientation = g_value_get_enum (value);
      set_direction (selector, selector->direction);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_selector_get_property (GObject         *object,
			       guint            prop_id,
			       GValue          *value,
			       GParamSpec      *pspec)
{
  PsppireSelector *selector = PSPPIRE_SELECTOR (object);

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      g_value_set_enum (value, selector->orientation);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}



static void
psppire_selector_class_init (PsppireSelectorClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GParamSpec *orientation_spec =
    g_param_spec_enum ("orientation",
		       "Orientation",
		       "Where the selector is relative to its subjects",
		       G_TYPE_PSPPIRE_SELECTOR_ORIENTATION,
		       PSPPIRE_SELECT_SOURCE_BEFORE_DEST /* default value */,
		       G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE);


  object_class->set_property = psppire_selector_set_property;
  object_class->get_property = psppire_selector_get_property;

  g_object_class_install_property (object_class,
                                   PROP_ORIENTATION,
                                   orientation_spec);

  signals [SELECTED] =
    g_signal_new ("selected",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE,
		  0);

  signals [DE_SELECTED] =
    g_signal_new ("de-selected",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE,
		  0);
}


static void
psppire_selector_base_init (PsppireSelectorClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = psppire_selector_finalize;

  class->source_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
}



static void
psppire_selector_base_finalize(PsppireSelectorClass *class,
				gpointer class_data)
{
  g_hash_table_destroy (class->source_hash);
}



static void
psppire_selector_init (PsppireSelector *selector)
{
  selector->arrow = gtk_arrow_new (GTK_ARROW_LEFT, GTK_SHADOW_NONE);
  selector->filtered_source = NULL;

  selector->action = gtk_action_new ("select", NULL, NULL, "pspp-stock-select");

  gtk_action_connect_proxy (selector->action, GTK_WIDGET (selector));

  gtk_container_add (GTK_CONTAINER (selector), selector->arrow);

  gtk_widget_show (selector->arrow);

  g_signal_connect_swapped (selector->action, "activate", G_CALLBACK (on_activate), selector);

  selector->selecting = FALSE;
}


GtkWidget*
psppire_selector_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_selector_get_type (), NULL));
}


static void
set_direction (PsppireSelector *selector, enum psppire_selector_dir d)
{
  selector->direction = d;

  /* FIXME: Need to reverse the arrow direction if an RTL locale is in
     effect */
  if ( d == PSPPIRE_SELECTOR_SOURCE_TO_DEST )
    {
      switch (selector->orientation)
	{
	case   PSPPIRE_SELECT_SOURCE_BEFORE_DEST:
	  g_object_set (selector->arrow, "arrow-type", GTK_ARROW_RIGHT, NULL);
	  break;
	case   PSPPIRE_SELECT_SOURCE_AFTER_DEST:
	  g_object_set (selector->arrow, "arrow-type", GTK_ARROW_LEFT, NULL);
	  break;
	case   PSPPIRE_SELECT_SOURCE_ABOVE_DEST:
	  g_object_set (selector->arrow, "arrow-type", GTK_ARROW_DOWN, NULL);
	  break;
	case   PSPPIRE_SELECT_SOURCE_BELOW_DEST:
	  g_object_set (selector->arrow, "arrow-type", GTK_ARROW_UP, NULL);
	  break;
	default:
	  g_assert_not_reached ();
	  break;
	};
    }
  else
    {
      switch (selector->orientation)
	{
	case   PSPPIRE_SELECT_SOURCE_BEFORE_DEST:
	  g_object_set (selector->arrow, "arrow-type", GTK_ARROW_LEFT, NULL);
	  break;
	case   PSPPIRE_SELECT_SOURCE_AFTER_DEST:
	  g_object_set (selector->arrow, "arrow-type", GTK_ARROW_RIGHT, NULL);
	  break;
	case   PSPPIRE_SELECT_SOURCE_ABOVE_DEST:
	  g_object_set (selector->arrow, "arrow-type", GTK_ARROW_UP, NULL);
	  break;
	case   PSPPIRE_SELECT_SOURCE_BELOW_DEST:
	  g_object_set (selector->arrow, "arrow-type", GTK_ARROW_DOWN, NULL);
	  break;
	default:
	  g_assert_not_reached ();
	  break;
	};

    }
}

/* Callback for when the source selection changes */
static void
on_source_select (GtkTreeSelection *treeselection, gpointer data)
{
  PsppireSelector *selector = data;

  set_direction (selector, PSPPIRE_SELECTOR_SOURCE_TO_DEST);

  if ( selector->allow_selection )
    {
      gtk_action_set_sensitive (selector->action,
				selector->allow_selection (selector->source, selector->dest));
    }
  else if ( GTK_IS_ENTRY (selector->dest) )
    {
      gtk_action_set_sensitive (selector->action,
				gtk_tree_selection_count_selected_rows
				(treeselection) <= 1 );
    }
}

/* Callback for when the destination treeview selection changes */
static void
on_dest_treeview_select (GtkTreeSelection *treeselection, gpointer data)
{
  PsppireSelector *selector = data;

  gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (selector->source)));

  set_direction (selector, PSPPIRE_SELECTOR_DEST_TO_SOURCE);
}

/* Callback for source deselection, when the dest is GtkEntry */
static void
de_select_selection_entry (PsppireSelector *selector)
{
  gtk_entry_set_text (GTK_ENTRY (selector->dest), "");
}

/* Callback for source deselection, when the dest is GtkTreeView */
static void
de_select_selection_tree_view (PsppireSelector *selector)
{
  GList *item;

  GtkTreeSelection* selection =
    gtk_tree_view_get_selection ( GTK_TREE_VIEW (selector->dest));

  GtkTreeModel *model =
    gtk_tree_view_get_model (GTK_TREE_VIEW (selector->dest));

  GList *selected_rows =
    gtk_tree_selection_get_selected_rows (selection, NULL);

  g_return_if_fail (selector->select_items);

  /* Convert paths to RowRefs */
  for (item = g_list_first (selected_rows);
       item != NULL;
       item = g_list_next (item))
    {
      GtkTreeRowReference* rowref;
      GtkTreePath *path  = item->data;

      rowref = gtk_tree_row_reference_new (GTK_TREE_MODEL (model), path);

      item->data = rowref ;
      gtk_tree_path_free (path);
    }

  /* Remove each selected row from the dest widget */
  for (item = g_list_first (selected_rows);
       item != NULL;
       item = g_list_next (item))
    {
      GtkTreeIter iter;
      GtkTreeRowReference *rr = item->data;

      GtkTreePath *path = gtk_tree_row_reference_get_path (rr);

      gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path);

      gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

      gtk_tree_path_free (path);
    }

  /* Delete list of RowRefs and its contents */
  g_list_foreach (selected_rows, (GFunc) gtk_tree_row_reference_free, NULL);
  g_list_free (selected_rows);
}


/* Removes something from the DEST widget */
static void
de_select_selection (PsppireSelector *selector)
{
  selector->selecting = TRUE;

  if ( GTK_IS_TREE_VIEW (selector->dest ) )
    de_select_selection_tree_view (selector);

  else if ( GTK_IS_ENTRY (selector->dest))
    de_select_selection_entry (selector);

  else
    g_assert_not_reached ();

  selector->selecting = FALSE;

  gtk_tree_model_filter_refilter (selector->filtered_source);

  g_signal_emit (selector, signals [DE_SELECTED], 0);
}


/* Puts something into the DEST widget */
static void
select_selection (PsppireSelector *selector)
{
  GList *item ;
  GtkTreeSelection* selection =
    gtk_tree_view_get_selection ( GTK_TREE_VIEW (selector->source));

  GList *selected_rows =
    gtk_tree_selection_get_selected_rows (selection, NULL);

  GtkTreeModel *childmodel  = gtk_tree_model_filter_get_model
    (selector->filtered_source);

  g_return_if_fail (selector->select_items);

  selector->selecting = TRUE;

  for (item = g_list_first (selected_rows);
       item != NULL;
       item = g_list_next (item))
    {
      GtkTreeIter child_iter;
      GtkTreeIter iter;
      GtkTreePath *path  = item->data;

      gtk_tree_model_get_iter (GTK_TREE_MODEL (selector->filtered_source),
			       &iter, path);

      gtk_tree_model_filter_convert_iter_to_child_iter
	(selector->filtered_source,
	 &child_iter,
	 &iter);

      selector->select_items (child_iter,
			      selector->dest,
			      childmodel,
			      selector->select_user_data
			      );
    }

  g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (selected_rows);

  gtk_tree_model_filter_refilter (selector->filtered_source);

  g_signal_emit (selector, signals [SELECTED], 0);

  selector->selecting = FALSE;
}

/* Callback for when the source treeview is activated (double clicked) */
static void
on_row_activate (GtkTreeView       *tree_view,
		 GtkTreePath       *path,
		 GtkTreeViewColumn *column,
		 gpointer           data)
{
  PsppireSelector *selector  = data;

  gtk_action_activate (selector->action);
}

/* Callback for when the selector button is clicked,
   or other event which causes the selector's action to occur.
 */
static void
on_activate (PsppireSelector *selector, gpointer data)
{
  switch (selector->direction)
    {
    case PSPPIRE_SELECTOR_SOURCE_TO_DEST:
      select_selection (selector);
      break;
    case PSPPIRE_SELECTOR_DEST_TO_SOURCE:
      de_select_selection (selector);
      break;
    default:
      g_assert_not_reached ();
      break;
    }
}

/* Default visibility filter for GtkTreeView DEST widget */
static gboolean
is_item_in_dest (GtkTreeModel *model, GtkTreeIter *iter,
		 PsppireSelector *selector)
{
  GtkTreeModel *dest_model;
  GtkTreeIter dest_iter;
  GtkTreeIter source_iter;
  gint index;
  GtkTreePath *path ;
  GtkTreeModel *source_model;

  if ( GTK_IS_TREE_MODEL_FILTER (model) )
    {
      source_model = gtk_tree_model_filter_get_model
	(GTK_TREE_MODEL_FILTER (model));

      gtk_tree_model_filter_convert_iter_to_child_iter
	( GTK_TREE_MODEL_FILTER (model),  &source_iter,  iter  );
    }
  else
    {
      source_model = model;
      source_iter = *iter;
    }

  dest_model = gtk_tree_view_get_model (GTK_TREE_VIEW (selector->dest));

  path = gtk_tree_model_get_path (source_model, &source_iter);

  index = *gtk_tree_path_get_indices (path);

  gtk_tree_path_free (path);

  if ( ! gtk_tree_model_get_iter_first (dest_model, &dest_iter) )
    return FALSE;

  do
    {
      int x;
      GValue value = {0};
      GValue int_value = {0};
      gtk_tree_model_get_value (dest_model, &dest_iter, 0, &value);

      g_value_init (&int_value, G_TYPE_INT);

      g_value_transform (&value, &int_value);

      x = g_value_get_int (&int_value);

      g_value_unset (&int_value);
      g_value_unset (&value);

      if ( x == index )
	return TRUE;
    }
  while (gtk_tree_model_iter_next (dest_model, &dest_iter));

  return FALSE;
}

/* Visibility function for items in the SOURCE widget.
   Returns TRUE iff *all* the selectors for which SOURCE is associated
   are visible */
static gboolean
is_source_item_visible (GtkTreeModel *childmodel,
			GtkTreeIter *iter, gpointer data)
{
  PsppireSelector *selector = data;
  PsppireSelectorClass *class = g_type_class_peek (PSPPIRE_SELECTOR_TYPE);

  GList *list = NULL;

  list = g_hash_table_lookup (class->source_hash, selector->source);

  while (list)
    {
      PsppireSelector *selector = list->data;

      if ( selector->filter && selector->filter (childmodel, iter, selector))
	return FALSE;

      list = list->next;
    }


  return TRUE;
}

/* set the source widget to SOURCE */
static void
set_tree_view_source (PsppireSelector *selector,
		      GtkTreeView *source)
{
  GtkTreeSelection* selection ;
  GList *list = NULL;

  PsppireSelectorClass *class = g_type_class_peek (PSPPIRE_SELECTOR_TYPE);

  if ( ! (list = g_hash_table_lookup (class->source_hash, source)))
    {
      selector->filtered_source =
	GTK_TREE_MODEL_FILTER (gtk_tree_model_filter_new
			       (gtk_tree_view_get_model (source),  NULL));

      gtk_tree_view_set_model (source, NULL);

      gtk_tree_view_set_model (source,
			       GTK_TREE_MODEL (selector->filtered_source));

      list = g_list_append (list, selector);
      g_hash_table_insert (class->source_hash, source, list);


      gtk_tree_model_filter_set_visible_func (selector->filtered_source,
					      is_source_item_visible,
					      selector,
					      NULL);
    }
  else
    {  /* Append this selector to the list and push the <source,list>
	  pair onto the hash table */

      selector->filtered_source = GTK_TREE_MODEL_FILTER (
	gtk_tree_view_get_model (source));

      list = g_list_append (list, selector);
      g_hash_table_replace (class->source_hash, source, list);
    }

  selection = gtk_tree_view_get_selection (source);

  g_signal_connect (source, "row-activated", G_CALLBACK (on_row_activate),
		    selector);

  g_signal_connect (selection, "changed", G_CALLBACK (on_source_select),
		    selector);
}


/*
   Callback for when the destination treeview's data changes
 */
static void
on_dest_data_change (GtkTreeModel *tree_model,
		     GtkTreePath  *path,
		     GtkTreeIter  *iter,
		     gpointer      user_data)
{
  PsppireSelector *selector = user_data;

  if ( selector->selecting) return;

  gtk_tree_model_filter_refilter (selector->filtered_source);
}


static void
on_dest_data_delete (GtkTreeModel *tree_model,
		     GtkTreePath  *path,
		     gpointer      user_data)
{
  PsppireSelector *selector = user_data;

  if ( selector->selecting ) return;

  gtk_tree_model_filter_refilter (selector->filtered_source);
}




/* Set the destination widget to DEST */
static void
set_tree_view_dest (PsppireSelector *selector,
		    GtkTreeView *dest)
{
  GtkTreeSelection* selection = gtk_tree_view_get_selection (dest);
  GtkTreeModel *model = gtk_tree_view_get_model (dest);

  gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

  g_signal_connect (selection, "changed", G_CALLBACK (on_dest_treeview_select),
		    selector);

  g_signal_connect (model, "row-changed", G_CALLBACK (on_dest_data_change),
		      selector);

  g_signal_connect (model, "row-deleted", G_CALLBACK (on_dest_data_delete),
		      selector);

}

/* Callback which causes the filter to be refiltered.
   Called when the DEST GtkEntry is activated (Enter is pressed), or when it
   looses focus.
*/
static gboolean
refilter (PsppireSelector *selector)
{
  gtk_tree_model_filter_refilter (selector->filtered_source);
  return FALSE;
}

/* Callback for when the DEST GtkEntry is selected (clicked) */
static gboolean
on_entry_dest_select (GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
  PsppireSelector * selector = data;

  gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (GTK_TREE_VIEW (selector->source)));
  set_direction (selector, PSPPIRE_SELECTOR_DEST_TO_SOURCE);

  return FALSE;
}



/* Callback for when an item disappears from the source list.
   By implication, this means that the item has been inserted into the
   destination.
 */
static void
on_row_deleted (PsppireSelector *selector)
{
  g_signal_emit (selector, signals [SELECTED], 0);
}

/* Callback for when a new item appears in the source list.
   By implication, this means that an item has been deleted from the
   destination.
 */
static void
on_row_inserted (PsppireSelector *selector)
{
  g_signal_emit (selector, signals [DE_SELECTED], 0);
}



/* Set DEST to be the destination GtkEntry widget */
static void
set_entry_dest (PsppireSelector *selector,
		GtkEntry *dest)
{
  g_signal_connect_swapped (dest, "activate", G_CALLBACK (refilter),
		    selector);

  g_signal_connect_swapped (dest, "changed", G_CALLBACK (refilter),
		    selector);

  g_signal_connect (dest, "focus-in-event", G_CALLBACK (on_entry_dest_select),
		    selector);

  g_signal_connect_swapped (dest, "focus-out-event", G_CALLBACK (refilter),
		    selector);


  g_signal_connect_swapped (selector->filtered_source, "row-deleted",
		    G_CALLBACK (on_row_deleted), selector);

  g_signal_connect_swapped (selector->filtered_source, "row-inserted",
		    G_CALLBACK (on_row_inserted), selector);
}


/* Set SOURCE and DEST for this selector, and
   set SELECT_FUNC and FILTER_FUNC */
void
psppire_selector_set_subjects (PsppireSelector *selector,
			       GtkWidget *source,
			       GtkWidget *dest,
			       SelectItemsFunc *select_func,
			       FilterItemsFunc *filter_func,
			       gpointer user_data)
{
  g_assert(selector);

  selector->filter = filter_func ;

  selector->source = source;
  selector->dest = dest;
  selector->select_user_data = user_data;

  if ( filter_func == NULL)
    {
      if  (GTK_IS_TREE_VIEW (dest))
	selector->filter = is_item_in_dest;
    }

  if ( GTK_IS_TREE_VIEW (source))
    set_tree_view_source (selector, GTK_TREE_VIEW (source) );
  else
    g_error ("Unsupported source widget: %s", G_OBJECT_TYPE_NAME (source));

  g_assert ( GTK_IS_TREE_MODEL_FILTER (selector->filtered_source));

  if ( NULL == dest)
    ;
  else if  ( GTK_IS_TREE_VIEW (dest))
    set_tree_view_dest (selector, GTK_TREE_VIEW (dest));

  else if ( GTK_IS_ENTRY (dest))
    set_entry_dest (selector, GTK_ENTRY (dest));

  else if (GTK_IS_TEXT_VIEW (dest))
    {
      /* Nothing to be done */
    }

  else
    g_error ("Unsupported destination widget: %s", G_OBJECT_TYPE_NAME (dest));

  selector->select_items = select_func;
}



void
psppire_selector_set_allow        (PsppireSelector *selector , AllowSelectionFunc *allow)
{
  selector->allow_selection = allow;
}



GType
psppire_selector_orientation_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { PSPPIRE_SELECT_SOURCE_BEFORE_DEST, "PSPPIRE_SELECT_SOURCE_BEFORE_DEST", "source before destination" },
      { PSPPIRE_SELECT_SOURCE_AFTER_DEST, "PSPPIRE_SELECT_SOURCE_AFTER_DEST", "source after destination" },
      { PSPPIRE_SELECT_SOURCE_ABOVE_DEST, "PSPPIRE_SELECT_SOURCE_ABOVE_DEST", "source above destination" },
      { PSPPIRE_SELECT_SOURCE_BELOW_DEST, "PSPPIRE_SELECT_SOURCE_BELOW_DEST", "source below destination" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static (g_intern_static_string ("PsppireSelectorOrientation"), values);
  }
  return etype;
}
