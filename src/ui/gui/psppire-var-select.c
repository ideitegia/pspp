/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2007  Free Software Foundation

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

#include "psppire-var-select.h"
#include "psppire-object.h"

#include "psppire-dict.h"

#include <gettext.h>

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


/* This object is an attempt to abstract a situation commonly found in PSPP
   dialogs, where two widgets (typically GtkTreeViews) contain a list
   of variables,  and the variables may be selected by the user and
   transfered to between the widgets, in preparation for some
   operation.

   Currently it assumes that the first widget is  GtkTreeView and the
   second is a GtkEntry (as required for the Weight Cases dialog).
   It needs to be generalized further to make  it useful.
*/

static void setup_dictionary_treeview (GtkTreeView *,
				       const PsppireDict *,
				       GtkSelectionMode);


/* --- prototypes --- */
static void psppire_var_select_class_init (PsppireVarSelectClass *);
static void psppire_var_select_init	     (PsppireVarSelect *);
static void psppire_var_select_finalize   (GObject *);


enum  {VARIABLE_SELECTED,
       DESELECT_ALL,
       n_SIGNALS};

static guint signal[n_SIGNALS];


/* --- variables --- */
static GObjectClass     *parent_class = NULL;

/* --- functions --- */
/**
 * psppire_var_select_get_type:
 * @returns: the type ID for accelerator groups.
 */
GType
psppire_var_select_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      static const GTypeInfo object_info = {
	sizeof (PsppireVarSelectClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) psppire_var_select_class_init,
	NULL,   /* class_finalize */
	NULL,   /* class_data */
	sizeof (PsppireVarSelect),
	0,      /* n_preallocs */
	(GInstanceInitFunc) psppire_var_select_init,
      };

      object_type = g_type_register_static (G_TYPE_PSPPIRE_OBJECT,
					    "PsppireVarSelect",
					    &object_info, 0);
    }

  return object_type;
}


static void
psppire_var_select_class_init (PsppireVarSelectClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);


  signal [VARIABLE_SELECTED] =
    g_signal_new ("variable_selected",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


  signal [DESELECT_ALL] =
    g_signal_new ("deselect_all",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE,
		  0);

  object_class->finalize = psppire_var_select_finalize;
}

static void
psppire_var_select_finalize (GObject *object)
{
  PsppireVarSelect *vs = PSPPIRE_VAR_SELECT (object);

  g_list_free (vs->list);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
psppire_var_select_init (PsppireVarSelect *vs)
{
  vs->list = NULL;
  vs->mode = GTK_SELECTION_SINGLE;
}

/* Return list of all the currently selected variables */
const GList *
psppire_var_select_get_variables (PsppireVarSelect *vs)
{
  return vs->list;
}


static void
add_variable_to_selection (PsppireVarSelect *vs, struct variable *var)
{
  gtk_entry_set_text (GTK_ENTRY (vs->dest), var_get_name (var) );

  if ( vs->mode == GTK_SELECTION_SINGLE)
    {
      g_list_free (vs->list);
      vs->list = NULL;
    }
  vs->list = g_list_append (vs->list, var);

  g_signal_emit (vs, signal [VARIABLE_SELECTED], 0, var_get_dict_index (var));
}


/* Add VAR to the list of selected variables */
void
psppire_var_select_set_variable (PsppireVarSelect *vs,
				 struct variable *var)
{
  add_variable_to_selection (vs, var);
}


static void
on_source_activate (GtkTreeView       *tree_view,
		    GtkTreePath       *path,
		    GtkTreeViewColumn *column,
		    gpointer           user_data)

{
  PsppireVarSelect *vs = user_data;
  GtkTreeModel *model = gtk_tree_view_get_model (tree_view);

  GtkTreeSelection *selection = gtk_tree_view_get_selection (tree_view);

  GList *list = gtk_tree_selection_get_selected_rows (selection, &model);

  while (list)
    {
      struct variable *var;
      GtkTreeIter iter;
      GtkTreePath *path = list->data;

      gtk_tree_model_get_iter (model, &iter, path);

      gtk_tree_model_get (model, &iter, DICT_TVM_COL_VAR, &var, -1);

      add_variable_to_selection (vs, var);

      list = list->next;
    }
}


/**
 * psppire_var_select_new:
 * @returns: a new #PsppireVarSelect object
 *
 * Creates a new #PsppireVarSelect.
 */
PsppireVarSelect*
psppire_var_select_new (GtkWidget *source, GtkWidget *dest,
			const PsppireDict *dict)
{
  PsppireVarSelect *vs
    = g_object_new (G_TYPE_PSPPIRE_VAR_SELECT, NULL);

  GtkTreeSelection *src_selection;

  vs->source = source;
  vs->dest = dest;
  vs->dict = dict;


  setup_dictionary_treeview ( GTK_TREE_VIEW (source),
			      vs->dict, vs->mode);


  src_selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (vs->source));

  g_signal_connect (source, "row-activated",
		    G_CALLBACK (on_source_activate), vs);

  return vs;
}

void
psppire_var_select_deselect_all (PsppireVarSelect *vs)
{
  g_list_free (vs->list);
  vs->list = NULL;

  gtk_entry_set_text ( GTK_ENTRY(vs->dest), "");

  g_signal_emit (vs, signal [DESELECT_ALL], 0);
}


static void
setup_dictionary_treeview (GtkTreeView *treeview, const PsppireDict *dict,
			   GtkSelectionMode mode)
{
  /* Set up the dictionary treeview */
  GtkTreeViewColumn *col;

  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (treeview);

  GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();


  gtk_tree_view_set_model (GTK_TREE_VIEW (treeview), GTK_TREE_MODEL (dict));

  col = gtk_tree_view_column_new_with_attributes (_("Var"),
						  renderer,
						  "text",
						  0,
						  NULL);

  /* FIXME: make this a value in terms of character widths */
  g_object_set (col, "min-width",  100, NULL);

  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);

  gtk_tree_view_append_column (treeview, col);

  gtk_tree_selection_set_mode (selection, mode);
}




