/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

/* Some common routines used in the implementation of dialog boxes */


#ifndef DIALOG_COMMON_H
#define DIALOG_COMMON_H

#include <gtk/gtk.h>
#include "psppire-dict.h"

/* A (*GtkTreeCellDataFunc) function.
   This function expects TREEMODEL to hold G_TYPE_INT.  The ints it holds
   are the indices of the variables in the dictionary, which DATA points to.
   It renders the name of the variable into CELL.
*/
void XXX_cell_var_name (GtkTreeViewColumn *tree_column,
		    GtkCellRenderer *cell,
		    GtkTreeModel *tree_model,
		    GtkTreeIter *iter,
		    gpointer data);


/* Returns FALSE if the variables represented by the union of the rows
   currently selected by SOURCE widget, and contents of the DEST
   widget, are of different types.

   In other words, this function when passed as the argument to
   psppire_selector_set_allow, ensures that the selector selects only
   string  variables, or only numeric variables, not a mixture.
*/
gboolean homogeneous_types (GtkWidget *source, GtkWidget *dest);

/* Returns TRUE if all of the variable(s) represented by the rows
   currently selected by SOURCE widget, are numeric. DEST is ignored.

   In other words, this function when passed as the argument to
   psppire_selector_set_allow, ensures that the selector selects only
   numeric variables.
*/
gboolean numeric_only (GtkWidget *source, GtkWidget *dest);

/*
  A pair of functions intended to be used as callbacks for the "toggled" signal
  of a GtkToggleButton widget.  They make the sensitivity of W follow the status
  of the togglebutton.
*/
void set_sensitivity_from_toggle (GtkToggleButton *togglebutton,  GtkWidget *w);
void set_sensitivity_from_toggle_invert (GtkToggleButton *togglebutton,  GtkWidget *w);


#endif
