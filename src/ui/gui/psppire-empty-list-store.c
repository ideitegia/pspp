/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011, 2012 Free Software Foundation, Inc.

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

#include "psppire-empty-list-store.h"

static void psppire_empty_list_store_class_init (PsppireEmptyListStoreClass *);
static void psppire_empty_list_store_init (PsppireEmptyListStore *);

/* GtkTreeModel interface. */
static void gtk_tree_model_interface_init (GtkTreeModelIface *iface);
static GtkTreeModelFlags empty_list_store_get_flags (GtkTreeModel *tree_model);
static gint empty_list_store_get_n_columns (GtkTreeModel *tree_model);
static GType empty_list_store_get_column_type (GtkTreeModel *tree_model,
                                               gint          index_);
static gboolean empty_list_store_get_iter (GtkTreeModel *tree_model,
                                           GtkTreeIter  *iter,
                                           GtkTreePath  *path);
static GtkTreePath * empty_list_store_get_path (GtkTreeModel *tree_model,
                                                GtkTreeIter  *iter);
static void empty_list_store_get_value (GtkTreeModel *tree_model,
                                        GtkTreeIter  *iter,
                                        gint          column,
                                        GValue       *value);
static gboolean empty_list_store_iter_next (GtkTreeModel *tree_model,
                                            GtkTreeIter  *iter);
static gboolean empty_list_store_iter_children (GtkTreeModel *tree_model,
                                                GtkTreeIter  *iter,
                                                GtkTreeIter  *parent);
static gboolean empty_list_store_iter_has_child (GtkTreeModel *tree_model,
                                                 GtkTreeIter  *iter);
static gint empty_list_store_iter_n_children (GtkTreeModel *tree_model,
                                              GtkTreeIter  *iter);
static gboolean empty_list_store_iter_nth_child (GtkTreeModel *tree_model,
                                                 GtkTreeIter  *iter,
                                                 GtkTreeIter  *parent,
                                                 gint          n);
static gboolean empty_list_store_iter_parent (GtkTreeModel *tree_model,
                                              GtkTreeIter  *iter,
                                              GtkTreeIter  *child);

GType
psppire_empty_list_store_get_type (void)
{
  static GType type = 0;
  if (!type)
    {
      static const GTypeInfo psppire_empty_list_store_info =
        {
          sizeof(PsppireEmptyListStoreClass),
          NULL,    /* base init */
          NULL,    /* base finalize */
          (GClassInitFunc) psppire_empty_list_store_class_init,
          NULL,    /* class finalize */
          NULL,    /* class data */
          sizeof(PsppireEmptyListStore),
          0,    /* n_preallocs, ignored since 2.10 */
          (GInstanceInitFunc) psppire_empty_list_store_init,
          NULL
        };
      static const GInterfaceInfo gtk_tree_model_info =
        {
          (GInterfaceInitFunc) gtk_tree_model_interface_init,
          (GInterfaceFinalizeFunc) NULL,
          NULL
        };
      type = g_type_register_static (G_TYPE_OBJECT,
                                 "PsppireEmptyListStore",
                                 &psppire_empty_list_store_info, 0);
      g_type_add_interface_static (type, GTK_TYPE_TREE_MODEL,
                                   &gtk_tree_model_info);
    }
  return type;
}

enum
  {
    PROP_0,
    PROP_N_ROWS
  };

static void
psppire_empty_list_store_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  PsppireEmptyListStore *obj = PSPPIRE_EMPTY_LIST_STORE (object);

  switch (prop_id)
    {
    case PROP_N_ROWS:
      obj->n_rows = g_value_get_int (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_empty_list_store_get_property (GObject      *object,
                                       guint         prop_id,
                                       GValue       *value,
                                       GParamSpec   *pspec)
{
  PsppireEmptyListStore *obj = PSPPIRE_EMPTY_LIST_STORE (object);

  switch (prop_id)
    {
    case PROP_N_ROWS:
      g_value_set_int (value, obj->n_rows);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_empty_list_store_class_init (PsppireEmptyListStoreClass *class)
{
  GObjectClass *gobject_class;
  gobject_class = G_OBJECT_CLASS (class);

  gobject_class->set_property = psppire_empty_list_store_set_property;
  gobject_class->get_property = psppire_empty_list_store_get_property;

  g_object_class_install_property (gobject_class,
                                   PROP_N_ROWS,
                                   g_param_spec_int ("n-rows",
						     ("Number of rows"),
						     ("Number of rows in GtkTreeModel"),
						     0,
						     G_MAXINT,
						     0,
						     G_PARAM_READWRITE));
}

static void
psppire_empty_list_store_init (PsppireEmptyListStore *obj)
{
  obj->n_rows = 0;
}

PsppireEmptyListStore *
psppire_empty_list_store_new (gint n_rows)
{
  return PSPPIRE_EMPTY_LIST_STORE (g_object_new (PSPPIRE_TYPE_EMPTY_LIST_STORE,
                                                 "n-rows", n_rows,
                                                 NULL));
}

gint
psppire_empty_list_store_get_n_rows (const PsppireEmptyListStore *obj)
{
  return obj->n_rows;
}

void
psppire_empty_list_store_set_n_rows (PsppireEmptyListStore *obj,
                                     gint n_rows)
{
  obj->n_rows = n_rows;
}

void
psppire_empty_list_store_row_changed (PsppireEmptyListStore *obj,
                                      gint row)
{
  GtkTreeModel *tree_model = GTK_TREE_MODEL (obj);
  GtkTreeIter iter;
  GtkTreePath *path;

  path = gtk_tree_path_new_from_indices (row, -1);
  gtk_tree_model_get_iter (tree_model, &iter, path);
  gtk_tree_model_row_changed (tree_model, path, &iter);
  gtk_tree_path_free (path);
}

void
psppire_empty_list_store_row_inserted (PsppireEmptyListStore *obj,
                                       gint row)
{
  GtkTreeModel *tree_model = GTK_TREE_MODEL (obj);
  GtkTreeIter iter;
  GtkTreePath *path;

  path = gtk_tree_path_new_from_indices (row, -1);
  gtk_tree_model_get_iter (tree_model, &iter, path);
  gtk_tree_model_row_inserted (tree_model, path, &iter);
  gtk_tree_path_free (path);
}

void
psppire_empty_list_store_row_deleted (PsppireEmptyListStore *obj,
                                      gint row)
{
  GtkTreeModel *tree_model = GTK_TREE_MODEL (obj);
  GtkTreePath *path;

  path = gtk_tree_path_new_from_indices (row, -1);
  gtk_tree_model_row_deleted (tree_model, path);
  gtk_tree_path_free (path);
}

/* GtkTreeModel interface. */

/* Random number used in 'stamp' member of GtkTreeIter. */
#define TREE_MODEL_STAMP 0x10c44c13

static gboolean
empty_list_store_init_iter (GtkTreeModel *model, gint idx, GtkTreeIter *iter)
{
  const PsppireEmptyListStore *store = PSPPIRE_EMPTY_LIST_STORE (model);

  if (idx < 0 || idx >= store->n_rows)
    {
      iter->stamp = 0;
      iter->user_data = GINT_TO_POINTER (-1);
      return FALSE;
    }
  else
    {
      iter->stamp = TREE_MODEL_STAMP;
      iter->user_data = GINT_TO_POINTER (idx);
      return TRUE;
    }
}

static void
gtk_tree_model_interface_init (GtkTreeModelIface *iface)
{
  g_return_if_fail (iface != NULL);

  iface->get_flags = empty_list_store_get_flags;
  iface->get_n_columns = empty_list_store_get_n_columns;
  iface->get_column_type = empty_list_store_get_column_type;
  iface->get_iter = empty_list_store_get_iter;
  iface->get_path = empty_list_store_get_path;
  iface->get_value = empty_list_store_get_value;
  iface->iter_next = empty_list_store_iter_next;
  iface->iter_children = empty_list_store_iter_children;
  iface->iter_has_child = empty_list_store_iter_has_child;
  iface->iter_n_children = empty_list_store_iter_n_children;
  iface->iter_nth_child = empty_list_store_iter_nth_child;
  iface->iter_parent = empty_list_store_iter_parent;
}

static GtkTreeModelFlags
empty_list_store_get_flags (GtkTreeModel *tree_model G_GNUC_UNUSED)
{
  return GTK_TREE_MODEL_LIST_ONLY;
}

static gint
empty_list_store_get_n_columns (GtkTreeModel *tree_model G_GNUC_UNUSED)
{
  return 0;
}

static GType
empty_list_store_get_column_type (GtkTreeModel *tree_model,
                                  gint          index_)
{
  g_return_val_if_reached (G_TYPE_NONE);
}

static gboolean
empty_list_store_get_iter (GtkTreeModel *tree_model,
                           GtkTreeIter  *iter,
                           GtkTreePath  *path)
{
  gint *indices, depth;

  g_return_val_if_fail (path, FALSE);

  indices = gtk_tree_path_get_indices (path);
  depth = gtk_tree_path_get_depth (path);

  g_return_val_if_fail (depth == 1, FALSE);

  return empty_list_store_init_iter (tree_model, indices[0], iter);
}

static GtkTreePath *
empty_list_store_get_path (GtkTreeModel *tree_model,
                           GtkTreeIter  *iter)
{
  GtkTreePath *path;

  g_return_val_if_fail (iter->stamp == TREE_MODEL_STAMP, FALSE);

  path = gtk_tree_path_new ();
  gtk_tree_path_append_index (path, GPOINTER_TO_INT (iter->user_data));

  return path;
}

static void
empty_list_store_get_value (GtkTreeModel *tree_model,
                            GtkTreeIter  *iter,
                            gint          column,
                            GValue       *value)
{
  g_return_if_reached ();
}

static gboolean
empty_list_store_iter_next (GtkTreeModel *tree_model,
                            GtkTreeIter  *iter)
{
  gint idx;

  g_return_val_if_fail (iter->stamp == TREE_MODEL_STAMP, FALSE);

  idx = GPOINTER_TO_INT (iter->user_data);
  return empty_list_store_init_iter (tree_model, idx + (idx >= 0), iter);
}

static gboolean
empty_list_store_iter_children (GtkTreeModel *tree_model,
                                GtkTreeIter  *iter,
                                GtkTreeIter  *parent)
{
  return FALSE;
}

static gboolean
empty_list_store_iter_has_child (GtkTreeModel *tree_model,
                                 GtkTreeIter  *iter)
{
  return FALSE;
}

static gint
empty_list_store_iter_n_children (GtkTreeModel *tree_model,
                                  GtkTreeIter  *iter)
{
  return iter == NULL ? PSPPIRE_EMPTY_LIST_STORE (tree_model)->n_rows : 0;
}

static gboolean
empty_list_store_iter_nth_child (GtkTreeModel *tree_model,
                                 GtkTreeIter  *iter,
                                 GtkTreeIter  *parent,
                                 gint          n)
{
  g_return_val_if_fail (parent == NULL, FALSE);

  return empty_list_store_init_iter (tree_model, n, iter);
}

static gboolean
empty_list_store_iter_parent (GtkTreeModel *tree_model,
                              GtkTreeIter  *iter,
                              GtkTreeIter  *child)
{
  return FALSE;
}

gint
empty_list_store_iter_to_row (const GtkTreeIter *iter)
{
  g_return_val_if_fail (iter->stamp == TREE_MODEL_STAMP, 0);
  return GPOINTER_TO_INT (iter->user_data);
}
