/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009, 2010, 2011, 2012, 2013  Free Software Foundation

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

#include <gtk/gtk.h>
#include "psppire-dictview.h"
#include "psppire-dict.h"
#include "psppire-conf.h"
#include <data/format.h>
#include <libpspp/i18n.h>
#include "helper.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_dict_view_base_finalize (PsppireDictViewClass *, gpointer);
static void psppire_dict_view_base_init     (PsppireDictViewClass *class);
static void psppire_dict_view_class_init    (PsppireDictViewClass *class);
static void psppire_dict_view_init          (PsppireDictView      *dict_view);


GType
psppire_dict_view_get_type (void)
{
  static GType psppire_dict_view_type = 0;

  if (!psppire_dict_view_type)
    {
      static const GTypeInfo psppire_dict_view_info =
      {
	sizeof (PsppireDictViewClass),
	(GBaseInitFunc) psppire_dict_view_base_init,
        (GBaseFinalizeFunc) psppire_dict_view_base_finalize,
	(GClassInitFunc)psppire_dict_view_class_init,
	(GClassFinalizeFunc) NULL,
	NULL,
        sizeof (PsppireDictView),
	0,
	(GInstanceInitFunc) psppire_dict_view_init,
      };

      psppire_dict_view_type =
	g_type_register_static (GTK_TYPE_TREE_VIEW, "PsppireDictView",
				&psppire_dict_view_info, 0);
    }

  return psppire_dict_view_type;
}


static void
psppire_dict_view_finalize (GObject *object)
{
  PsppireDictView *dict_view = PSPPIRE_DICT_VIEW (object);

  gtk_widget_destroy (dict_view->menu);
}

/* Properties */
enum
{
  PROP_0,
  PROP_DICTIONARY,
  PROP_PREDICATE,
  PROP_SELECTION_MODE
};


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

static void
set_model (PsppireDictView *dict_view)
{
  GtkTreeModel *model ;

  if ( dict_view->dict == NULL)
    return;

   if ( dict_view->predicate )
    {
      model = gtk_tree_model_filter_new (GTK_TREE_MODEL (dict_view->dict),
					 NULL);

      gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (model),
					      filter_variables,
					      dict_view->predicate,
					      NULL);
    }
  else
    {
      model = GTK_TREE_MODEL (dict_view->dict);
      g_object_ref (model);
    }

  gtk_tree_view_set_model (GTK_TREE_VIEW (dict_view), model);
  g_object_unref (model);
}

static void
psppire_dict_view_set_property (GObject         *object,
			       guint            prop_id,
			       const GValue    *value,
			       GParamSpec      *pspec)
{
  PsppireDictView *dict_view = PSPPIRE_DICT_VIEW (object);

  switch (prop_id)
    {
    case PROP_DICTIONARY:
      dict_view->dict = g_value_get_object (value);
      break;
    case PROP_PREDICATE:
      dict_view->predicate = g_value_get_pointer (value);
      break;
    case PROP_SELECTION_MODE:
      {
	GtkTreeSelection *selection =
	  gtk_tree_view_get_selection (GTK_TREE_VIEW (dict_view));

	GtkSelectionMode mode = g_value_get_enum (value);

	gtk_tree_selection_set_mode (selection, mode);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };


  set_model (dict_view);
}


static void
psppire_dict_view_get_property (GObject         *object,
			       guint            prop_id,
			       GValue          *value,
			       GParamSpec      *pspec)
{
  PsppireDictView *dict_view = PSPPIRE_DICT_VIEW (object);

  switch (prop_id)
    {
    case PROP_DICTIONARY:
      g_value_set_object (value, dict_view->dict);
      break;
    case PROP_PREDICATE:
      g_value_set_pointer (value, dict_view->predicate);
      break;
    case PROP_SELECTION_MODE:
      {
	GtkTreeSelection *selection =
	  gtk_tree_view_get_selection (GTK_TREE_VIEW (dict_view));

	g_value_set_enum (value, gtk_tree_selection_get_mode (selection));
      }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}



static void
psppire_dict_view_class_init (PsppireDictViewClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  GParamSpec *predicate_spec =
    g_param_spec_pointer ("predicate",
			  "Predicate",
			  "A predicate function",
			  G_PARAM_READABLE | G_PARAM_WRITABLE);


  GParamSpec *selection_mode_spec =
    g_param_spec_enum ("selection-mode",
		       "Selection Mode",
		       "How many things can be selected",
		       GTK_TYPE_SELECTION_MODE,
		       GTK_SELECTION_MULTIPLE,
		       G_PARAM_CONSTRUCT | G_PARAM_READABLE | G_PARAM_WRITABLE);

  object_class->set_property = psppire_dict_view_set_property;
  object_class->get_property = psppire_dict_view_get_property;

  g_object_class_override_property (object_class,
				    PROP_DICTIONARY,
				    "model");

  g_object_class_install_property (object_class,
                                   PROP_PREDICATE,
                                   predicate_spec);

  g_object_class_install_property (object_class,
                                   PROP_SELECTION_MODE,
                                   selection_mode_spec);
}


static void
psppire_dict_view_base_init (PsppireDictViewClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = psppire_dict_view_finalize;
}



static void
psppire_dict_view_base_finalize (PsppireDictViewClass *class,
				 gpointer class_data)
{

}


static void
dv_get_base_model (GtkTreeModel *top_model, GtkTreeIter *top_iter,
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



/* A GtkTreeCellDataFunc which renders the name and/or label of the
   variable */
static void
var_description_cell_data_func (GtkTreeViewColumn *col,
				GtkCellRenderer *cell,
				GtkTreeModel *top_model,
				GtkTreeIter *top_iter,
				gpointer data)
{
  PsppireDictView *dv = PSPPIRE_DICT_VIEW (data);
  struct variable *var;
  GtkTreeIter iter;
  GtkTreeModel *model;

  dv_get_base_model (top_model, top_iter, &model, &iter);

  gtk_tree_model_get (model,
		      &iter, DICT_TVM_COL_VAR, &var, -1);

  if ( var_has_label (var) && dv->prefer_labels)
    {
      gchar *text = g_markup_printf_escaped (
				     "<span stretch=\"condensed\">%s</span>",
				     var_get_label (var));

      g_object_set (cell, "markup", text, NULL);
      g_free (text);
    }
  else
    {
      g_object_set (cell, "text", var_get_name (var), NULL);
    }
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

  g_object_set (cell, "stock_id",
                psppire_dict_view_get_var_measurement_stock_id (var), NULL);
}

const char *
psppire_dict_view_get_var_measurement_stock_id (const struct variable *var)
{
  if ( var_is_alpha (var))
    return "var-string";
  else
    {
      const struct fmt_spec *fs = var_get_print_format (var);
      int cat = fmt_get_category (fs->type);

      switch ( var_get_measure (var))
	{
	case MEASURE_NOMINAL:
          return "var-nominal";

	case MEASURE_ORDINAL:
          return "var-ordinal";

	case MEASURE_SCALE:
	  if ( ( FMT_CAT_DATE | FMT_CAT_TIME ) & cat )
            return "var-date-scale";
	  else
            return "var-scale";
	  break;

	default:
	  g_return_val_if_reached ("");
	}
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
    const gchar *tip ;
    GtkTreeModel *m;

    dv_get_base_model (tree_model, NULL, &m, NULL);

    if ( PSPPIRE_DICT_VIEW (treeview)->prefer_labels )
      tip = var_get_name (var);
    else
      tip = var_get_label (var);

    gtk_tooltip_set_text (tooltip, tip);
  }

  return TRUE;
}

static gboolean
show_menu (PsppireDictView *dv, GdkEventButton *event, gpointer data)
{
  if (event->button != 3)
    return FALSE;

  gtk_menu_popup (GTK_MENU (dv->menu), NULL, NULL, NULL, NULL,
		  event->button, event->time);

  return TRUE;
}

static void
toggle_label_preference (GtkCheckMenuItem *checkbox, gpointer data)
{
  PsppireDictView *dv = PSPPIRE_DICT_VIEW (data);

  dv->prefer_labels = gtk_check_menu_item_get_active (checkbox);

  gtk_widget_queue_draw (GTK_WIDGET (dv));
}



static void
psppire_dict_view_init (PsppireDictView *dict_view)
{
  GtkTreeViewColumn *col = gtk_tree_view_column_new ();

  GtkCellRenderer *renderer = gtk_cell_renderer_pixbuf_new ();

  dict_view->prefer_labels = TRUE;

  psppire_conf_get_boolean (psppire_conf_new (),
			    G_OBJECT_TYPE_NAME (dict_view),
			    "prefer-labels",
			    &dict_view->prefer_labels);

  gtk_tree_view_column_set_title (col, _("Variable"));

  gtk_tree_view_column_pack_start (col, renderer, FALSE);

  gtk_tree_view_column_set_cell_data_func (col, renderer,
					   var_icon_cell_data_func,
					   NULL, NULL);

  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (col, renderer, TRUE);
  gtk_tree_view_column_set_cell_data_func (col, renderer,
					   var_description_cell_data_func,
					   dict_view, NULL);

  g_object_set (renderer, "ellipsize-set", TRUE, NULL);
  g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_MIDDLE, NULL);

  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);

  /* FIXME: make this a value in terms of character widths */
  gtk_tree_view_column_set_min_width (col, 150);

  gtk_tree_view_append_column (GTK_TREE_VIEW (dict_view), col);

  g_object_set (dict_view,
		"has-tooltip", TRUE,
		"headers-visible", FALSE,
		NULL);

  g_signal_connect (dict_view, "query-tooltip",
		    G_CALLBACK (set_tooltip_for_variable), NULL);

  dict_view->menu = gtk_menu_new ();


  {
    GtkWidget *checkbox =
      gtk_check_menu_item_new_with_label  (_("Prefer variable labels"));

    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (checkbox),
				    dict_view->prefer_labels);

    g_signal_connect (checkbox, "toggled",
		      G_CALLBACK (toggle_label_preference), dict_view);


    gtk_menu_shell_append (GTK_MENU_SHELL (dict_view->menu), checkbox);

  }

  gtk_widget_show_all (dict_view->menu);

  g_signal_connect (dict_view, "button-press-event",
		    G_CALLBACK (show_menu), NULL);
}


GtkWidget*
psppire_dict_view_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_dict_view_get_type (), NULL));
}



struct variable *
psppire_dict_view_get_selected_variable (PsppireDictView *treeview)
{
  struct variable *var;
  GtkTreeModel *top_model;
  GtkTreeIter top_iter;

  GtkTreeModel *model;
  GtkTreeIter iter;

  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview));

  if (! gtk_tree_selection_get_selected (selection,
					 &top_model, &top_iter))
    return NULL;

  dv_get_base_model (top_model, &top_iter, &model, &iter);

  g_assert (PSPPIRE_IS_DICT (model));

  gtk_tree_model_get (model,
		      &iter, DICT_TVM_COL_VAR, &var, -1);

  return var;
}


