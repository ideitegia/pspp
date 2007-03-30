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


/* Some common routines used in the implementation of dialog boxes */


#ifndef DIALOG_COMMON_H
#define DIALOG_COMMON_H

#include <gtk/gtk.h>
#include "psppire-dict.h"

/* Append the names of selected variables to STRING.
   TREEVIEW is the treeview containing the variables.
   DICT is the dictionary for those variables.
*/
gint append_variable_names (GString *, PsppireDict *, GtkTreeView *);


/* Returns the variable currently selected by the iterator
   pointing to TREEMODEL */
struct variable * get_selected_variable (GtkTreeModel *treemodel,
					 GtkTreeIter *iter,
					 PsppireDict *dict);



/* A (*GtkTreeCellDataFunc) function.
   This function expects TREEMODEL to hold G_TYPE_INT.  The ints it holds
   are the indices of the variables in the dictionary, which DATA points to.
   It renders the name of the variable into CELL.
*/
void cell_var_name (GtkTreeViewColumn *tree_column,
		    GtkCellRenderer *cell,
		    GtkTreeModel *tree_model,
		    GtkTreeIter *iter,
		    gpointer data);


/* Set a model for DEST, which is an GtkListStore of g_int's
   whose values are the indices into DICT */
void set_dest_model (GtkTreeView *dest, PsppireDict *dict);


#endif
