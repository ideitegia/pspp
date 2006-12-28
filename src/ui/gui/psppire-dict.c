/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2004, 2006  Free Software Foundation

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA. */

#include <config.h>
#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <gtksheet/gtkextra-marshal.h>

#include "psppire-object.h"
#include "psppire-dict.h"
#include <data/format.h>
#include <data/dictionary.h>
#include <data/missing-values.h>
#include <data/value-labels.h>
#include <data/variable.h>

#include "message-dialog.h"

/* --- prototypes --- */
static void psppire_dict_class_init	(PsppireDictClass	*class);
static void psppire_dict_init	(PsppireDict		*dict);
static void psppire_dict_finalize	(GObject		*object);

static void dictionary_tree_model_init (GtkTreeModelIface *iface);


/* --- variables --- */
static GObjectClass     *parent_class = NULL;

enum  {VARIABLE_CHANGED,
       VARIABLE_RESIZED,
       VARIABLE_INSERTED,
       VARIABLES_DELETED,
       n_SIGNALS};

static guint signal[n_SIGNALS];

#define CACHE_CHUNK 5

/* --- functions --- */
/**
 * psppire_dict_get_type:
 * @returns: the type ID for accelerator groups.
 */
GType
psppire_dict_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      static const GTypeInfo object_info = {
	sizeof (PsppireDictClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) psppire_dict_class_init,
	NULL,   /* class_finalize */
	NULL,   /* class_data */
	sizeof (PsppireDict),
	0,      /* n_preallocs */
	(GInstanceInitFunc) psppire_dict_init,
      };

      static const GInterfaceInfo tree_model_info = {
	(GInterfaceInitFunc) dictionary_tree_model_init,
	NULL,
	NULL
      };

      object_type = g_type_register_static (G_TYPE_PSPPIRE_OBJECT,
					    "PsppireDict",
					    &object_info, 0);

      g_type_add_interface_static (object_type, GTK_TYPE_TREE_MODEL,
				  &tree_model_info);


    }

  return object_type;
}


static void
psppire_dict_class_init (PsppireDictClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  object_class->finalize = psppire_dict_finalize;

  signal[VARIABLE_CHANGED] =
    g_signal_new ("variable_changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);



  signal[VARIABLE_INSERTED] =
    g_signal_new ("variable_inserted",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  signal[VARIABLES_DELETED] =
    g_signal_new ("variables_deleted",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  gtkextra_VOID__INT_INT,
		  G_TYPE_NONE,
		  2,
		  G_TYPE_INT,
		  G_TYPE_INT);


  signal[VARIABLE_RESIZED] =
    g_signal_new ("dict-size-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  gtkextra_VOID__INT_INT,
		  G_TYPE_NONE,
		  2,
		  G_TYPE_INT,
		  G_TYPE_INT);

}

static void
psppire_dict_finalize (GObject *object)
{
  PsppireDict *d = PSPPIRE_DICT (object);

  dict_destroy (d->dict);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* Pass on callbacks from src/data/dictionary, as
   signals in the Gtk library */
static void
addcb (struct dictionary *d, int idx, void *pd)
{
  g_signal_emit (pd, signal[VARIABLE_INSERTED], 0, idx);
}

static void
delcb (struct dictionary *d, int idx, void *pd)
{
  g_signal_emit (pd, signal[VARIABLES_DELETED], 0, idx, 1);
}

static void
mutcb (struct dictionary *d, int idx, void *pd)
{
  g_signal_emit (pd, signal[VARIABLE_CHANGED], 0, idx);
}

static const struct dict_callbacks gui_callbacks =
  {
    addcb,
    delcb,
    mutcb
  };

static void
psppire_dict_init (PsppireDict *psppire_dict)
{
  psppire_dict->stamp = g_random_int ();
}

/**
 * psppire_dict_new_from_dict:
 * @returns: a new #PsppireDict object
 *
 * Creates a new #PsppireDict.
 */
PsppireDict*
psppire_dict_new_from_dict (struct dictionary *d)
{
  PsppireDict *new_dict = g_object_new (G_TYPE_PSPPIRE_DICT, NULL);
  new_dict->dict = d;

  dict_set_callbacks (new_dict->dict, &gui_callbacks, new_dict);

  return new_dict;
}


/* Returns a valid name for a new variable in DICT.
   The return value is statically allocated */
static gchar *
auto_generate_var_name (PsppireDict *dict)
{
  gint d = 0;
  static gchar name[10];

  while (g_snprintf (name, 10, "VAR%05d",d++),
	 psppire_dict_lookup_var (dict, name))
    ;

  return name;
}

/* Insert a new variable at posn IDX, with the name NAME.
   If NAME is null, then a name will be automatically assigned.
 */
void
psppire_dict_insert_variable (PsppireDict *d, gint idx, const gchar *name)
{
  struct variable *var ;
  g_return_if_fail (idx >= 0);
  g_return_if_fail (d);
  g_return_if_fail (G_IS_PSPPIRE_DICT (d));

  if ( ! name )
    name = auto_generate_var_name (d);

  var = dict_create_var (d->dict, name, 0);

  dict_reorder_var (d->dict, var, idx);
}

/* Delete N variables beginning at FIRST */
void
psppire_dict_delete_variables (PsppireDict *d, gint first, gint n)
{
  gint idx;
  g_return_if_fail (d);
  g_return_if_fail (d->dict);
  g_return_if_fail (G_IS_PSPPIRE_DICT (d));

  for (idx = 0 ; idx < n ; ++idx )
    {
      struct variable *var;

      /* Do nothing if it's out of bounds */
      if ( first >= dict_get_var_cnt (d->dict))
	break;

      var = dict_get_var (d->dict, first);
      dict_delete_var (d->dict, var);
    }
  dict_compact_values (d->dict);
}


void
psppire_dict_set_name (PsppireDict* d, gint idx, const gchar *name)
{
  struct variable *var;
  g_assert (d);
  g_assert (G_IS_PSPPIRE_DICT (d));


  if ( idx < dict_get_var_cnt (d->dict))
    {
      /* This is an existing variable? */
      var = dict_get_var (d->dict, idx);
      dict_rename_var (d->dict, var, name);
    }
  else
    {
      /* new variable */
      dict_create_var (d->dict, name, 0);
    }
}



/* Return the IDXth variable */
struct variable *
psppire_dict_get_variable (PsppireDict *d, gint idx)
{
  g_return_val_if_fail (d, NULL);
  g_return_val_if_fail (d->dict, NULL);

  if ( dict_get_var_cnt (d->dict) <= idx )
    return NULL;

  return dict_get_var (d->dict, idx);
}


/* Return the number of variables in the dictionary */
gint
psppire_dict_get_var_cnt (const PsppireDict *d)
{
  g_return_val_if_fail (d, -1);
  g_return_val_if_fail (d->dict, -1);

  return dict_get_var_cnt (d->dict);
}


/* Return a variable by name.
   Return NULL if it doesn't exist
*/
struct variable *
psppire_dict_lookup_var (const PsppireDict *d, const gchar *name)
{
  g_return_val_if_fail (d, NULL);
  g_return_val_if_fail (d->dict, NULL);

  return dict_lookup_var (d->dict, name);
}


/* Clears the contents of D */
void
psppire_dict_clear (PsppireDict *d)
{
  g_return_if_fail (d);
  g_return_if_fail (d->dict);

  {
    const gint n_vars = dict_get_var_cnt (d->dict);

    dict_clear (d->dict);
  }
}



/* Return true is NAME would be a valid name of a variable to add to the
   dictionary.  False otherwise.
   If REPORT is true, then invalid names will be reported as such as errors
*/
gboolean
psppire_dict_check_name (const PsppireDict *dict,
		     const gchar *name, gboolean report)
{
  if ( ! var_is_valid_name (name, report ) )
      return FALSE;

  if (psppire_dict_lookup_var (dict, name))
    {
      if ( report )
	msg (ME,"Duplicate variable name.");
      return FALSE;
    }

  return TRUE;
}


inline gint
psppire_dict_get_next_value_idx (const PsppireDict *dict)
{
  return dict_get_next_value_idx (dict->dict);
}


void
psppire_dict_resize_variable (PsppireDict *d, const struct variable *pv,
			      gint old_size, gint new_size)
{
  gint fv;
  g_return_if_fail (d);
  g_return_if_fail (d->dict);

  if ( old_size == new_size )
    return ;

  dict_compact_values (d->dict);

  fv = var_get_case_index (pv);

  g_signal_emit (d, signal[VARIABLE_RESIZED], 0,
		fv + old_size,
		new_size - old_size );
}


/* Tree Model Stuff */

static GtkTreeModelFlags tree_model_get_flags (GtkTreeModel *model);

static gint tree_model_n_columns (GtkTreeModel *model);

static GType tree_model_column_type (GtkTreeModel *model, gint index);

static gboolean tree_model_get_iter (GtkTreeModel *model, GtkTreeIter *iter,
				     GtkTreePath *path);

static gboolean tree_model_iter_next (GtkTreeModel *model, GtkTreeIter *iter);

static GtkTreePath * tree_model_get_path (GtkTreeModel *model,
					  GtkTreeIter *iter);

static void tree_model_get_value (GtkTreeModel *model, GtkTreeIter *iter,
				  gint column, GValue *value);

static gboolean tree_model_nth_child (GtkTreeModel *model, GtkTreeIter *iter,
				      GtkTreeIter *parent, gint n);


static void
dictionary_tree_model_init (GtkTreeModelIface *iface)
{
  iface->get_flags = tree_model_get_flags;
  iface->get_n_columns = tree_model_n_columns;
  iface->get_column_type = tree_model_column_type;
  iface->get_iter = tree_model_get_iter;
  iface->iter_next = tree_model_iter_next;
  iface->get_path = tree_model_get_path;
  iface->get_value = tree_model_get_value;

  iface->iter_children = 0;
  iface->iter_has_child =0;
  iface->iter_n_children =0;
  iface->iter_nth_child = tree_model_nth_child ;
  iface->iter_parent =0;
}

static GtkTreeModelFlags
tree_model_get_flags (GtkTreeModel *model)
{
  g_return_val_if_fail (G_IS_PSPPIRE_DICT (model), (GtkTreeModelFlags) 0);

  return GTK_TREE_MODEL_LIST_ONLY;
}


static gint
tree_model_n_columns (GtkTreeModel *model)
{
  return n_DICT_COLS;
}

static GType
tree_model_column_type (GtkTreeModel *model, gint index)
{
  g_return_val_if_fail (G_IS_PSPPIRE_DICT (model), (GType) 0);

  switch (index)
    {
    case DICT_TVM_COL_NAME:
      return G_TYPE_STRING;
      break;
    case DICT_TVM_COL_VAR:
      return G_TYPE_POINTER;
      break;
    default:
      g_return_val_if_reached ((GType)0);
      break;
    }

  g_assert_not_reached ();
  return ((GType)0);
}

static gboolean
tree_model_get_iter (GtkTreeModel *model, GtkTreeIter *iter, GtkTreePath *path)
{
  gint *indices, depth;
  gint n;
  struct variable *variable;

  PsppireDict *dict = PSPPIRE_DICT (model);

  g_return_val_if_fail (path, FALSE);

  indices = gtk_tree_path_get_indices (path);
  depth = gtk_tree_path_get_depth (path);

  g_return_val_if_fail (depth == 1, FALSE);

  n = indices[0];

  if ( n < 0 || n >= psppire_dict_get_var_cnt (dict))
    return FALSE;

  variable = dict_get_var (dict->dict, n);

  g_assert (var_get_dict_index (variable) == n);

  iter->stamp = dict->stamp;
  iter->user_data = variable;

  return TRUE;
}


static gboolean
tree_model_iter_next (GtkTreeModel *model, GtkTreeIter *iter)
{
  PsppireDict *dict = PSPPIRE_DICT (model);
  struct variable *variable;
  gint idx;

  g_return_val_if_fail (iter->stamp == dict->stamp, FALSE);

  if ( iter == NULL || iter->user_data == NULL)
    return FALSE;

  variable = (struct variable *) iter->user_data;

  idx = var_get_dict_index (variable);

  if ( idx + 1 >= psppire_dict_get_var_cnt (dict))
    return FALSE;

  variable = psppire_dict_get_variable (dict, idx + 1);

  g_assert (var_get_dict_index (variable) == idx + 1);

  iter->user_data = variable;

  return TRUE;
}

static GtkTreePath *
tree_model_get_path (GtkTreeModel *model, GtkTreeIter *iter)
{
  GtkTreePath *path;
  struct variable *variable;
  PsppireDict *dict = PSPPIRE_DICT (model);

  g_return_val_if_fail (iter->stamp == dict->stamp, FALSE);

  variable = (struct variable *) iter->user_data;

  path = gtk_tree_path_new ();
  gtk_tree_path_append_index (path, var_get_dict_index (variable));

  return path;
}


static void
tree_model_get_value (GtkTreeModel *model, GtkTreeIter *iter,
		     gint column, GValue *value)
{
  struct variable *variable;
  PsppireDict *dict = PSPPIRE_DICT (model);

  g_return_if_fail (iter->stamp == dict->stamp);

  variable = (struct variable *) iter->user_data;

  switch (column)
    {
    case DICT_TVM_COL_NAME:
      g_value_init (value, G_TYPE_STRING);
      g_value_set_string (value, var_get_name (variable));
      break;
    case DICT_TVM_COL_VAR:
      g_value_init (value, G_TYPE_POINTER);
      g_value_set_pointer (value, variable);
      break;
    default:
      g_return_if_reached ();
      break;
    }
}


static gboolean
tree_model_nth_child (GtkTreeModel *model, GtkTreeIter *iter,
		     GtkTreeIter *parent, gint n)
{
  PsppireDict *dict;
  g_return_val_if_fail (G_IS_PSPPIRE_DICT (model), FALSE);

  dict = PSPPIRE_DICT (model);

  if ( parent )
    return FALSE;

  if ( n >= psppire_dict_get_var_cnt (dict) )
    return FALSE;

  iter->stamp = dict->stamp;
  iter->user_data = psppire_dict_get_variable (dict, n);

  if ( !iter->user_data)
    return FALSE;


  return TRUE;
}


void
psppire_dict_rename_var (PsppireDict *dict, struct variable *v,
			 const gchar *text)
{
  dict_rename_var (dict->dict, v, text);
}
