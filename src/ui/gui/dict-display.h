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


/* This module provides various functions necessary for displaying a
   dictionary in  GTK widgets.
*/

#ifndef DICT_DISPLAY 
#define DICT_DISPLAY 1

#include <gtk/gtk.h>

#include "psppire-selector.h"


void
get_base_model (GtkTreeModel *top_model, GtkTreeIter *top_iter,
		GtkTreeModel **model, GtkTreeIter *iter);

/* A SelectItemsFunc function for GtkTreeView widgets */
void insert_source_row_into_tree_view (GtkTreeIter source_iter,
				       GtkWidget *dest,
				       GtkTreeModel *source_model,
				       gpointer data);


/* A SelectItemsFunc function for PsppireMeansLayers widgets */
void insert_source_row_into_layers (GtkTreeIter source_iter,
				    GtkWidget *dest,
				    GtkTreeModel *source_model,
				    gpointer data);


/* A SelectItemsFunc function for GtkEntry widgets */
void insert_source_row_into_entry (GtkTreeIter source_iter,
				   GtkWidget *dest,
				   GtkTreeModel *source_model,
				   gpointer data
				   );



/* A FilterItemsFunc function for GtkEntry widgets */
gboolean is_currently_in_entry (GtkTreeModel *model, GtkTreeIter *iter,
				PsppireSelector *selector);


/* A FilterItemsFunc function for PsppireVarview widgets */
gboolean is_currently_in_varview (GtkTreeModel *model, GtkTreeIter *iter,
				  PsppireSelector *sel);

#endif
