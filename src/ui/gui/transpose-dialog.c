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

#include "transpose-dialog.h"
#include "psppire-selector.h"
#include "psppire-dialog.h"
#include "helper.h"
#include "data-editor.h"
#include "dict-display.h"
#include <language/syntax-string-source.h>
#include "syntax-editor.h"

#include <gtk/gtk.h>
#include <glade/glade.h>

#include <gettext.h>

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


/* FIXME: These shouldn't be here */
#include <gtksheet/gtksheet.h>
#include "psppire-var-store.h"



static struct variable *
get_selected_variable (GtkTreeModel *treemodel,
		       GtkTreeIter *iter,
		       PsppireDict *dict)
{
  struct variable *var;
  GValue value = {0};

  GtkTreePath *path = gtk_tree_model_get_path (treemodel, iter);

  gtk_tree_model_get_value (treemodel, iter, 0, &value);

  gtk_tree_path_free (path);

  var =  psppire_dict_get_variable (dict, g_value_get_int (&value));

  g_value_unset (&value);

  return var;
}


/* A (*GtkTreeCellDataFunc) function.
   This function expects TREEMODEL to hold G_TYPE_INT.  The ints it holds
   are the indices of the variables in the dictionary, which DATA points to.
   It renders the name of the variable into CELL.
*/
static void
cell_var_name (GtkTreeViewColumn *tree_column,
	       GtkCellRenderer *cell,
	       GtkTreeModel *tree_model,
	       GtkTreeIter *iter,
	       gpointer data)
{
  PsppireDict *dict = data;
  struct variable *var;
  gchar *name;

  var = get_selected_variable (tree_model, iter, dict);

  name = pspp_locale_to_utf8 (var_get_name (var), -1, NULL);
  g_object_set (cell, "text", name, NULL);
  g_free (name);
}

static gchar * generate_syntax (PsppireDict *dict, GladeXML *xml);

void
transpose_dialog (GObject *o, gpointer data)
{
  gint response ;
  struct data_editor *de = data;

  GladeXML *xml = glade_xml_new (PKGDATADIR "/psppire.glade",
				 "transpose-dialog", NULL);

  GtkSheet *var_sheet =
    GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

  PsppireVarStore *vs = PSPPIRE_VAR_STORE (gtk_sheet_get_model (var_sheet));

  GtkWidget *dialog = get_widget_assert (xml, "transpose-dialog");
  GtkWidget *source = get_widget_assert (xml, "source-treeview");
  GtkWidget *dest = get_widget_assert (xml, "variables-treeview");
  GtkWidget *selector1 = get_widget_assert (xml, "psppire-selector2");
  GtkWidget *selector2 = get_widget_assert (xml, "psppire-selector3");
  GtkWidget *new_name_entry = get_widget_assert (xml, "new-name-entry");

  attach_dictionary_to_treeview (GTK_TREE_VIEW (source),
				 vs->dict,
				 GTK_SELECTION_MULTIPLE, NULL);
  {
    GtkTreeViewColumn *col;
    GtkListStore *dest_list = gtk_list_store_new (1, G_TYPE_INT);
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

    gtk_tree_view_set_model (GTK_TREE_VIEW (dest), GTK_TREE_MODEL (dest_list));

    col = gtk_tree_view_column_new_with_attributes (_("Var"),
						    renderer,
						    "text",
						    0,
						    NULL);

    gtk_tree_view_column_set_cell_data_func (col, renderer,
					     cell_var_name,
					     vs->dict, 0);

    /* FIXME: make this a value in terms of character widths */
    g_object_set (col, "min-width",  100, NULL);

    gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);

    gtk_tree_view_append_column (GTK_TREE_VIEW(dest), col);
  }


  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector1),
				 source, dest,
				 insert_source_row_into_tree_view,
				 NULL);


  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector2),
				 source, new_name_entry,
				 insert_source_row_into_entry,
				 is_currently_in_entry);


  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (vs->dict, xml);
	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (vs->dict, xml);

	struct syntax_editor *se =
	  (struct syntax_editor *) window_create (WINDOW_SYNTAX, NULL);

	gtk_text_buffer_insert_at_cursor (se->buffer, syntax, -1);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (xml);
}


  /*
     FLIP /VARIABLES=var_list /NEWNAMES=var_name.
  */
static gchar *
generate_syntax (PsppireDict *dict, GladeXML *xml)
{
  GtkTreeIter iter;
  const gchar *text;
  GString *string = g_string_new ("FLIP");
  gchar *syntax ;

  GtkWidget *dest = get_widget_assert (xml, "variables-treeview");
  GtkWidget *entry = get_widget_assert (xml, "new-name-entry");

  GtkTreeModel *list_store = gtk_tree_view_get_model (GTK_TREE_VIEW (dest));

  if ( gtk_tree_model_get_iter_first (list_store, &iter) )
    {
      g_string_append (string, " /VARIABLES =");
      do
	{
	  GValue value = {0};
	  struct variable *var;
	  GtkTreePath *path = gtk_tree_model_get_path (list_store, &iter);

	  gtk_tree_model_get_value (list_store, &iter, 0, &value);

	  var = psppire_dict_get_variable (dict, g_value_get_int (&value));
	  g_value_unset (&value);

	  g_string_append (string, " ");
	  g_string_append (string, var_get_name (var));

	  gtk_tree_path_free (path);
	}
      while (gtk_tree_model_iter_next (list_store, &iter));
    }

  text = gtk_entry_get_text (GTK_ENTRY (entry));

  if ( text)
    g_string_append_printf (string, " /NEWNAME = %s", text);

  g_string_append (string, ".");

  syntax = string->str;

  g_string_free (string, FALSE);

  return syntax;
}
