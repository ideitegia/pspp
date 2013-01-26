/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2013  Free Software Foundation

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
#include <glib.h>

#include "psppire-spreadsheet-model.h"

#include "data/spreadsheet-reader.h"

static void psppire_spreadsheet_model_init           (PsppireSpreadsheetModel *spreadsheetModel);
static void psppire_spreadsheet_model_class_init     (PsppireSpreadsheetModelClass *class);

static void psppire_spreadsheet_model_finalize       (GObject   *object);
static void psppire_spreadsheet_model_dispose        (GObject   *object);

static GObjectClass *parent_class = NULL;


static void spreadsheet_tree_model_init (GtkTreeModelIface *iface);


GType
psppire_spreadsheet_model_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      static const GTypeInfo spreadsheet_model_info =
      {
	sizeof (PsppireSpreadsheetModelClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
        (GClassInitFunc) psppire_spreadsheet_model_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
        sizeof (PsppireSpreadsheetModel),
	0,
        (GInstanceInitFunc) psppire_spreadsheet_model_init,
      };

      static const GInterfaceInfo tree_model_info = {
	(GInterfaceInitFunc) spreadsheet_tree_model_init,
	NULL,
	NULL
      };
      
      object_type = g_type_register_static (G_TYPE_OBJECT,
						"PsppireSpreadsheetModel",
						&spreadsheet_model_info, 0);

      g_type_add_interface_static (object_type, GTK_TYPE_TREE_MODEL,
				   &tree_model_info);

    }

  return object_type;
}


/* Properties */
enum
{
  PROP_0,
  PROP_SPREADSHEET
};


static void
psppire_spreadsheet_model_set_property (GObject         *object,
					guint            prop_id,
					const GValue    *value,
					GParamSpec      *pspec)
{
  PsppireSpreadsheetModel *spreadsheetModel = PSPPIRE_SPREADSHEET_MODEL (object);

  switch (prop_id)
    {
    case PROP_SPREADSHEET:
      spreadsheetModel->spreadsheet = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}



static void
psppire_spreadsheet_model_dispose  (GObject *object)
{
}

static void
psppire_spreadsheet_model_finalize (GObject *object)
{
  //  PsppireSpreadsheetModel *spreadsheetModel = PSPPIRE_SPREADSHEET_MODEL (object);
}

static void
psppire_spreadsheet_model_class_init (PsppireSpreadsheetModelClass *class)
{
  GObjectClass *object_class;

  GParamSpec *spreadsheet_spec =
    g_param_spec_pointer ("spreadsheet",
			  "Spreadsheet",
			  "The spreadsheet that this model represents",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass*) class;

  object_class->set_property = psppire_spreadsheet_model_set_property;

  g_object_class_install_property (object_class,
                                   PROP_SPREADSHEET,
                                   spreadsheet_spec);



  object_class->finalize = psppire_spreadsheet_model_finalize;
  object_class->dispose = psppire_spreadsheet_model_dispose;
}


static void
psppire_spreadsheet_model_init (PsppireSpreadsheetModel *spreadsheetModel)
{
  spreadsheetModel->dispose_has_run = FALSE;
  spreadsheetModel->stamp = g_random_int ();
}


GtkTreeModel*
psppire_spreadsheet_model_new (struct spreadsheet *sp)
{
  return g_object_new (psppire_spreadsheet_model_get_type (), 
		       "spreadsheet", sp,
		       NULL);
}






static const gint N_COLS = 2;

static gint 
tree_model_n_columns (GtkTreeModel *model)
{
  g_print ("%s\n", __FUNCTION__);
  return N_COLS;
}

static GtkTreeModelFlags
tree_model_get_flags (GtkTreeModel *model)
{
  g_print ("%s\n", __FUNCTION__);
  g_return_val_if_fail (PSPPIRE_IS_SPREADSHEET_MODEL (model), (GtkTreeModelFlags) 0);

  return GTK_TREE_MODEL_LIST_ONLY;
}

static GType
tree_model_column_type (GtkTreeModel *model, gint index)
{
  g_print ("%s %d\n", __FUNCTION__, index);
  g_return_val_if_fail (PSPPIRE_IS_SPREADSHEET_MODEL (model), (GType) 0);
  g_return_val_if_fail (index < N_COLS, (GType) 0);
 
  return G_TYPE_STRING;
}


static gboolean
tree_model_get_iter (GtkTreeModel *model, GtkTreeIter *iter, GtkTreePath *path)
{
  gint *indices, depth;

  PsppireSpreadsheetModel *spreadsheetModel = PSPPIRE_SPREADSHEET_MODEL (model);

  g_return_val_if_fail (path, FALSE);

  indices = gtk_tree_path_get_indices (path);

  depth = gtk_tree_path_get_depth (path);

  g_return_val_if_fail (depth == 1, FALSE);

  g_print ("%s %d\n", __FUNCTION__, *indices);

  iter->stamp = spreadsheetModel->stamp;
  iter->user_data = *indices; // kludge

  return TRUE;
}

static gboolean
tree_model_iter_next (GtkTreeModel *model, GtkTreeIter *iter)
{
  PsppireSpreadsheetModel *spreadsheetModel = PSPPIRE_SPREADSHEET_MODEL (model);
  g_return_val_if_fail (iter->stamp == spreadsheetModel->stamp, FALSE);

  g_print ("%s %d\n", __FUNCTION__, iter->user_data);

  if ( iter->user_data >= spreadsheetModel->spreadsheet->sheets - 1)
    return FALSE;

  iter->user_data++;

  return TRUE;
}


static void
tree_model_get_value (GtkTreeModel *model, GtkTreeIter *iter,
		      gint column, GValue *value)
{
  PsppireSpreadsheetModel *spreadsheetModel = PSPPIRE_SPREADSHEET_MODEL (model);
  g_return_if_fail (column < N_COLS);
  g_return_if_fail (iter->stamp == spreadsheetModel->stamp);
  g_print ("%s col %d\n", __FUNCTION__, column);

  g_value_init (value, G_TYPE_STRING);
  if ( column > 0)
    g_value_set_string (value, "foo");
  else
    g_value_set_string (value, "bar");
}


static gboolean
tree_model_nth_child (GtkTreeModel *model, GtkTreeIter *iter,
		      GtkTreeIter *parent, gint n)
{
  PsppireSpreadsheetModel *spreadsheetModel = PSPPIRE_SPREADSHEET_MODEL (model);

  if ( parent )
    return FALSE;

  if ( n >= spreadsheetModel->spreadsheet->sheets)
    return FALSE;

  iter->stamp = spreadsheetModel->stamp;

  return TRUE;
}


static void
spreadsheet_tree_model_init (GtkTreeModelIface *iface)
{
  iface->get_flags = tree_model_get_flags;
  iface->get_n_columns = tree_model_n_columns;
  iface->get_column_type = tree_model_column_type;
  iface->get_iter = tree_model_get_iter;
  iface->iter_next = tree_model_iter_next;

  iface->get_value = tree_model_get_value;

#if 0
  iface->get_path = tree_model_get_path;
  iface->iter_children = tree_model_iter_children ;
  iface->iter_has_child = tree_model_iter_has_child ;
  iface->iter_n_children = tree_model_n_children ;

  iface->iter_parent = tree_model_iter_parent ;
#endif

  iface->iter_nth_child = tree_model_nth_child ;
}
