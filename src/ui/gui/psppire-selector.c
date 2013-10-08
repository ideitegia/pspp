/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2009, 2010, 2012 Free Software Foundation

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

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

#include "psppire-dictview.h"
#include "psppire-var-view.h"
#include "psppire-dict.h"
#include "psppire-select-dest.h"

#include <gtk/gtk.h>

#include "psppire-selector.h"

static void psppire_selector_class_init    (PsppireSelectorClass *class);
static void psppire_selector_init          (PsppireSelector      *selector);


static void set_direction (PsppireSelector *, enum psppire_selector_dir);


enum  {SELECTED,    /* Emitted when an item is inserted into dest */
       DE_SELECTED, /* Emitted when an item is removed from dest */
       n_SIGNALS};

static guint signals [n_SIGNALS];

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


GType
psppire_selector_get_type (void)
{
  static GType psppire_selector_type = 0;

  if (!psppire_selector_type)
    {
      static const GTypeInfo psppire_selector_info =
      {
	sizeof (PsppireSelectorClass),
	(GBaseInitFunc) NULL, 
        (GBaseFinalizeFunc) NULL,
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

static GObjectClass * parent_class = NULL;



#define SELECTOR_DEBUGGING 0

static void
dump_hash_entry (gpointer key, gpointer value, gpointer obj)
{
  GList *item = NULL;
  g_print ("Source %p; ", key);

  for (item = g_list_first (value);
       item != NULL;
       item = g_list_next (item))
    {
      g_print ("%p(%p) ", item->data, item);
    }
  g_print ("\n");
}

/* This function is for debugging only */
void 
psppire_selector_show_map (PsppireSelector *obj)
{
  PsppireSelectorClass *class = g_type_class_peek (PSPPIRE_SELECTOR_TYPE);

  g_print ("%s %p\n", __FUNCTION__, obj);
  g_hash_table_foreach (class->source_hash, dump_hash_entry, obj);
}



static void
psppire_selector_dispose (GObject *obj)
{
  PsppireSelector *sel = PSPPIRE_SELECTOR (obj);
  PsppireSelectorClass *class = g_type_class_peek (PSPPIRE_SELECTOR_TYPE);
  GList *list;

  if (sel->dispose_has_run)
    return;

  /* Make sure dispose does not run twice. */
  sel->dispose_has_run = TRUE;

  /* Remove ourself from the source map. If we are the only entry for this source
     widget, that will result in an empty list for the source.  In that case, we
     remove the entire entry too. */
  if ((list = g_hash_table_lookup (class->source_hash, sel->source)))
    {
      GList *newlist = g_list_remove_link (list, sel->source_litem);
      g_list_free (sel->source_litem);
      if (newlist == NULL)
	g_hash_table_remove (class->source_hash, sel->source);
      else
	g_hash_table_replace (class->source_hash, sel->source, newlist);

      sel->source_litem = NULL;
    }
  
  g_object_unref (sel->dest);
  g_object_unref (sel->source);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}


/* Properties */
enum
{
  PROP_0,
  PROP_ORIENTATION,
  PROP_PRIMARY,
  PROP_SOURCE_WIDGET,
  PROP_DEST_WIDGET
};


static void on_click (GtkButton *b);
static void on_realize (GtkWidget *selector);


static void update_subjects (PsppireSelector *selector);


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
    case PROP_PRIMARY:
      selector->primary_requested = TRUE;
      update_subjects (selector);
      break;
    case PROP_SOURCE_WIDGET:
      selector->source = g_value_dup_object (value);
      update_subjects (selector);
      break;
    case PROP_DEST_WIDGET:
      selector->dest = g_value_dup_object (value);
      update_subjects (selector);
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
    case PROP_SOURCE_WIDGET:
      g_value_take_object (value, selector->source);
      break;
    case PROP_DEST_WIDGET:
      g_value_take_object (value, selector->dest);
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
  GtkButtonClass *button_class = GTK_BUTTON_CLASS (class);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);
  GParamSpec *orientation_spec =
    g_param_spec_enum ("orientation",
		       "Orientation",
		       "Where the selector is relative to its subjects",
		       PSPPIRE_TYPE_SELECTOR_ORIENTATION,
		       PSPPIRE_SELECT_SOURCE_BEFORE_DEST /* default value */,
		       G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE);


 /* Meaningfull only if more than one selector shares this selectors source */
  GParamSpec *primary_spec =
    g_param_spec_boolean ("primary",
			  "Primary",
			  "Whether this selector should be the primary selector for the source",
			  FALSE,
			  G_PARAM_READWRITE);

  GParamSpec *source_widget_spec = 
    g_param_spec_object ("source-widget",
			 "Source Widget",
			 "The widget to be used as the source for this selector",
			 GTK_TYPE_WIDGET,
			 G_PARAM_READWRITE);

  GParamSpec *dest_widget_spec = 
    g_param_spec_object ("dest-widget",
			 "Destination Widget",
			 "The widget to be used as the destination for this selector",
			 GTK_TYPE_WIDGET,
			 G_PARAM_READWRITE);


  button_class->clicked = on_click;
  widget_class->realize = on_realize;


  object_class->set_property = psppire_selector_set_property;
  object_class->get_property = psppire_selector_get_property;

  g_object_class_install_property (object_class,
                                   PROP_ORIENTATION,
                                   orientation_spec);

  g_object_class_install_property (object_class,
                                   PROP_PRIMARY,
                                   primary_spec);

  g_object_class_install_property (object_class,
                                   PROP_SOURCE_WIDGET,
                                   source_widget_spec);

  g_object_class_install_property (object_class,
                                   PROP_DEST_WIDGET,
                                   dest_widget_spec);

  parent_class = g_type_class_peek_parent (class);

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

  object_class->dispose = psppire_selector_dispose;

  class->source_hash = g_hash_table_new (g_direct_hash, g_direct_equal);
  class->default_selection_funcs = g_hash_table_new (g_direct_hash, g_direct_equal);
}


/* Callback for when the source treeview is activated (double clicked) */
static void
on_row_activate (GtkTreeView       *tree_view,
		 GtkTreePath       *path,
		 GtkTreeViewColumn *column,
		 gpointer           data)
{
  on_click (GTK_BUTTON (data));
}

/* Callback for when the source selection changes */
static void
on_source_select (GtkTreeSelection *treeselection, gpointer data)
{
  PsppireSelector *selector = data;

  set_direction (selector, PSPPIRE_SELECTOR_SOURCE_TO_DEST);

  if ( selector->allow_selection )
    {
      gtk_widget_set_sensitive (GTK_WIDGET (selector),
				selector->allow_selection (selector->source, selector->dest));
    }
  else if ( GTK_IS_ENTRY (selector->dest) )
    {
      gtk_widget_set_sensitive (GTK_WIDGET (selector),
				gtk_tree_selection_count_selected_rows
				(treeselection) <= 1 );
    }
}


static void
on_realize (GtkWidget *w)
{
  PsppireSelector *selector = PSPPIRE_SELECTOR (w);
  PsppireSelectorClass *class = g_type_class_peek (PSPPIRE_SELECTOR_TYPE);
  GtkTreeSelection* selection ;

  GList *list = g_hash_table_lookup (class->source_hash, selector->source);

  if (GTK_WIDGET_CLASS (parent_class)->realize)
    GTK_WIDGET_CLASS (parent_class)->realize (w);

  if ( NULL == list)
    return;

  if ( g_list_first (list)->data == selector)
    {
      if ( selector->row_activate_id )
	g_signal_handler_disconnect (selector->source, selector->row_activate_id);

      selector->row_activate_id =  
	g_signal_connect (selector->source, "row-activated", G_CALLBACK (on_row_activate), selector);
    }

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (selector->source));

  if ( selector->source_select_id )
    g_signal_handler_disconnect (selection, selector->source_select_id);

  selector->source_select_id = 
    g_signal_connect (selection, "changed", G_CALLBACK (on_source_select), selector);
}


static void
psppire_selector_init (PsppireSelector *selector)
{
  selector->primary_requested = FALSE;
  selector->select_user_data = NULL;
  selector->select_items = NULL;
  selector->allow_selection = NULL;
  selector->filter = NULL;

  selector->arrow = gtk_arrow_new (GTK_ARROW_LEFT, GTK_SHADOW_NONE);


  gtk_container_add (GTK_CONTAINER (selector), selector->arrow);

  gtk_widget_show (selector->arrow);

  selector->selecting = FALSE;

  selector->source = NULL;
  selector->dest = NULL;
  selector->dispose_has_run = FALSE;


  selector->row_activate_id = 0;
  selector->source_select_id  = 0;

  selector->source_litem = NULL;
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


/* Callback which causes the filter to be refiltered.
   Called when the DEST GtkEntry is activated (Enter is pressed), or when it
   looses focus.
*/
static gboolean
refilter (PsppireSelector *selector)
{
  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (selector->source));
  if (GTK_IS_TREE_MODEL_FILTER (model))
    gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (model));
  return FALSE;
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

  refilter (selector);

  g_signal_emit (selector, signals [DE_SELECTED], 0);
}


/* Puts something into the DEST widget */
static void
select_selection (PsppireSelector *selector)
{
  GList *item ;
  GtkTreeSelection* selection =
    gtk_tree_view_get_selection ( GTK_TREE_VIEW (selector->source));

  GList *selected_rows = gtk_tree_selection_get_selected_rows (selection, NULL);

  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (selector->source));

  GtkTreeModel *childmodel = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (model));

  g_return_if_fail (selector->select_items);

  if (selector->allow_selection && 
      ! selector->allow_selection (selector->source, selector->dest))
    return;

  selector->selecting = TRUE;

  for (item = g_list_first (selected_rows);
       item != NULL;
       item = g_list_next (item))
    {
      GtkTreeIter child_iter;
      GtkTreeIter iter;
      GtkTreePath *path  = item->data;

      g_return_if_fail (model);

      gtk_tree_model_get_iter (model, &iter, path);

      gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model),
							&child_iter, &iter);
      selector->select_items (child_iter,
			      selector->dest,
			      childmodel,
			      selector->select_user_data
			      );
    }

  g_list_foreach (selected_rows, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (selected_rows);

  refilter (selector);

  g_signal_emit (selector, signals [SELECTED], 0);

  selector->selecting = FALSE;
}

/* Callback for when the selector button is clicked,
   or other event which causes the selector's action to occur.
 */
static void
on_click (GtkButton *b)
{
  PsppireSelector *selector = PSPPIRE_SELECTOR (b);

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

  if (GTK_BUTTON_CLASS (parent_class)->clicked)
    GTK_BUTTON_CLASS (parent_class)->clicked (b);
}

static gboolean
is_item_in_dest (GtkTreeModel *model, GtkTreeIter *iter, PsppireSelector *selector)
{
  gboolean result = FALSE;
  GtkTreeIter source_iter;
  GtkTreeModel *source_model;
  GValue value = {0};

  if (GTK_IS_TREE_MODEL_FILTER (model))
    {
      source_model = gtk_tree_model_filter_get_model
	(GTK_TREE_MODEL_FILTER (model));

      gtk_tree_model_filter_convert_iter_to_child_iter
	(GTK_TREE_MODEL_FILTER (model),  &source_iter, iter);
    }
  else
    {
      source_model = model;
      source_iter = *iter;
    }

  gtk_tree_model_get_value (source_model, &source_iter, DICT_TVM_COL_VAR, &value);

  result = psppire_select_dest_widget_contains_var (PSPPIRE_SELECT_DEST_WIDGET (selector->dest),
						    &value);

  g_value_unset (&value);

  return result;
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
set_tree_view_source (PsppireSelector *selector)
{
  GList *list = NULL;

  PsppireSelectorClass *class = g_type_class_peek (PSPPIRE_SELECTOR_TYPE);
  
  if ( ! (list = g_hash_table_lookup (class->source_hash, selector->source)))
    {
      /* Base case:  This widget is currently not the source of 
	 any selector.  Create a hash entry and make this selector
	 the first selector in the list */

      list = g_list_append (list, selector);
      g_hash_table_insert (class->source_hash, selector->source, list);

      /* Save the list item so that it can be removed later */
      selector->source_litem = list;
    }
  else
    {  /* Append this selector to the list and push the <source,list>
	  pair onto the hash table */

      if ( NULL == g_list_find (list, selector) )
	{
	  if ( selector->primary_requested )
	    {
	      list = g_list_prepend (list, selector);
	      selector->source_litem = list;
	    }
	  else
	    {
	      list = g_list_append (list, selector);
	      selector->source_litem = g_list_last (list);
	    }
	  g_hash_table_replace (class->source_hash, selector->source, list);
	}
    }
}



/* This function is a callback which occurs when the
   SOURCE's model has changed */
static void
update_model (
              GtkTreeView *source,
              GParamSpec *psp,
              PsppireSelector *selector
              )
{
  GtkTreeModel *model = gtk_tree_view_get_model (source);

  g_assert (source == GTK_TREE_VIEW (selector->source));

  if (model && (model == g_object_get_data (G_OBJECT (source), "model-copy")))
    return;

  if (model != NULL) 
    {      
      GtkTreeModel *new_model = gtk_tree_model_filter_new (model, NULL); 

      g_object_set_data (G_OBJECT (source), "model-copy", new_model);  

      gtk_tree_view_set_model (source, new_model);
  
      gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (new_model),
                                              is_source_item_visible,
                                              selector,
                                              NULL);

      g_signal_connect_swapped (new_model,
				"row-deleted",
				G_CALLBACK (on_row_deleted), selector);

      g_signal_connect_swapped (new_model,
				"row-inserted",
				G_CALLBACK (on_row_inserted), selector);

      g_object_unref (new_model);
    }
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

  refilter (selector);
}


static void
on_dest_data_delete (GtkTreeModel *tree_model,
		     GtkTreePath  *path,
		     gpointer      user_data)
{
  PsppireSelector *selector = user_data;

  if ( selector->selecting ) return;

  refilter (selector);
}


static void
on_dest_model_changed (PsppireSelector *selector)
{
  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (selector->dest));

  if (model == NULL) 
    return;

  g_signal_connect (model, "row-changed", G_CALLBACK (on_dest_data_change),
		    selector);
  
  g_signal_connect (model, "row-deleted", G_CALLBACK (on_dest_data_delete),
		    selector);
  
  if ( selector->selecting ) return;
  
  refilter (selector);
}

/* Set the destination widget to DEST */
static void
set_tree_view_dest (PsppireSelector *selector,
		    GtkTreeView *dest)
{
  GtkTreeSelection* selection = gtk_tree_view_get_selection (dest);


  gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

  g_signal_connect (selection, "changed", G_CALLBACK (on_dest_treeview_select),
		    selector);

  on_dest_model_changed (selector);
  g_signal_connect_swapped (dest, "notify::model",
			    G_CALLBACK (on_dest_model_changed), selector);
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


}

static void
set_default_filter (PsppireSelector *selector)
{
  if ( selector->filter == NULL)
    {
      if  (GTK_IS_TREE_VIEW (selector->dest))
	selector->filter = is_item_in_dest;
    }
}


static void
update_subjects (PsppireSelector *selector)
{
  if ( NULL == selector->dest )
    return;

  set_default_filter (selector);

  if ( NULL == selector->source )
    return;

  if ( GTK_IS_TREE_VIEW (selector->source))
    {
      set_tree_view_source (selector);

      g_signal_connect (selector->source, "notify::model", 
                              G_CALLBACK (update_model), selector); 

      update_model (GTK_TREE_VIEW (selector->source), 0, selector);
    }
  else
    g_error ("Unsupported source widget: %s", G_OBJECT_TYPE_NAME (selector->source));

  if ( NULL == selector->dest)
    ;
  else if  ( GTK_IS_TREE_VIEW (selector->dest))
    {
      set_tree_view_dest (selector, GTK_TREE_VIEW (selector->dest));
    }

  else if ( GTK_IS_ENTRY (selector->dest))
    set_entry_dest (selector, GTK_ENTRY (selector->dest));

  else if (GTK_IS_TEXT_VIEW (selector->dest))
    {
      /* Nothing to be done */
    }
  else
    g_error ("Unsupported destination widget: %s", G_OBJECT_TYPE_NAME (selector->dest));


  /* FIXME: Remove this dependency */
  if ( PSPPIRE_IS_DICT_VIEW (selector->source) )
    {
      GObjectClass *class = G_OBJECT_GET_CLASS (selector);
      GType type = G_OBJECT_TYPE (selector->dest);

      SelectItemsFunc *func  = 
	g_hash_table_lookup (PSPPIRE_SELECTOR_CLASS (class)->default_selection_funcs, (gpointer) type);

      if ( func )
	psppire_selector_set_select_func (PSPPIRE_SELECTOR (selector),
					  func, NULL);
    }
}


void
psppire_selector_set_default_selection_func (GType type, SelectItemsFunc *func)
{
  GObjectClass *class = g_type_class_ref (PSPPIRE_SELECTOR_TYPE);

  g_hash_table_insert (PSPPIRE_SELECTOR_CLASS (class)->default_selection_funcs, (gpointer) type, func);

  g_type_class_unref (class);
}




/* Set FILTER_FUNC for this selector */
void
psppire_selector_set_filter_func (PsppireSelector *selector,
				  FilterItemsFunc *filter_func)
{
  selector->filter = filter_func ;
}


/* Set SELECT_FUNC for this selector */
void
psppire_selector_set_select_func (PsppireSelector *selector,
			       SelectItemsFunc *select_func,
			       gpointer user_data)
{
  selector->select_user_data = user_data;
  selector->select_items = select_func;
}



void
psppire_selector_set_allow (PsppireSelector *selector, AllowSelectionFunc *allow)
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
