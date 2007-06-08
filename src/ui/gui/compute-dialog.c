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


#include <gtk/gtk.h>
#include "compute-dialog.h"
#include "helper.h"
#include "psppire-dialog.h"
#include "psppire-keypad.h"
#include "data-editor.h"
#include <gtksheet/gtksheet.h>
#include "psppire-var-store.h"
#include "dialog-common.h"
#include "dict-display.h"

#include <language/expressions/public.h>
#include <language/syntax-string-source.h>
#include "syntax-editor.h"

static void function_list_populate (GtkTreeView *tv);

static void insert_function_into_syntax_area (GtkTreeIter iter,
					      GtkWidget *text_view,
					      GtkTreeModel *model
					      );

static void insert_source_row_into_text_view (GtkTreeIter iter,
					      GtkWidget *dest,
					      GtkTreeModel *model
					      );



struct compute_dialog
{
  GladeXML *xml;  /* The xml that generated the widgets */
};


static void
on_target_change (GObject *obj, const struct compute_dialog *cd)
{
  GtkWidget *target = get_widget_assert (cd->xml, "compute-entry1");
  GtkWidget *type_and_label = get_widget_assert (cd->xml, "compute-button1");

  const gchar *var_name = gtk_entry_get_text (GTK_ENTRY (target));
  gboolean valid = var_name && strcmp ("", var_name);

  gtk_widget_set_sensitive (type_and_label, valid);
}

static void
refresh (GObject *obj, const struct compute_dialog *cd)
{
  GtkTextIter start, end;
  GtkWidget *target = get_widget_assert (cd->xml, "compute-entry1");
  GtkWidget *syntax_area = get_widget_assert (cd->xml, "compute-textview1");
  GtkWidget *varlist = get_widget_assert (cd->xml, "compute-treeview1");
  GtkWidget *funclist = get_widget_assert (cd->xml, "compute-treeview2");

  GtkTextBuffer *buffer =
    gtk_text_view_get_buffer (GTK_TEXT_VIEW (syntax_area));

  GtkTreeSelection *selection;


  /* Clear the target variable entry box */
  gtk_entry_set_text (GTK_ENTRY (target), "");
  g_signal_emit_by_name (target, "changed");


  /* Clear the syntax area textbuffer */
  gtk_text_buffer_get_start_iter (buffer, &start);
  gtk_text_buffer_get_end_iter (buffer, &end);
  gtk_text_buffer_delete (buffer, &start, &end);


  /* Unselect all items in the treeview */
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (varlist));
  gtk_tree_selection_unselect_all (selection);

  /* And the other one */
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (funclist));
  gtk_tree_selection_unselect_all (selection);
}


static void
erase_selection (GtkTextBuffer *buffer)
{
  GtkTextIter start, end;

  gtk_text_buffer_get_selection_bounds (buffer, &start, &end);

  gtk_text_buffer_delete (buffer, &start, &end);
}



static void
on_keypad_button (PsppireKeypad *kp, const gchar *syntax, gpointer data)
{
  GladeXML *xml = data;

  GtkWidget *rhs = get_widget_assert (xml, "compute-textview1");

  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (rhs));

  erase_selection (buffer);

  gtk_text_buffer_insert_at_cursor (buffer, syntax, strlen (syntax));

  if (0 == strcmp (syntax, "()"))
    {
      GtkTextIter iter;
      GtkTextMark *cursor = gtk_text_buffer_get_insert (buffer);
      gtk_text_buffer_get_iter_at_mark (buffer, &iter, cursor);
      gtk_text_iter_backward_cursor_position (&iter);
      gtk_text_buffer_move_mark (buffer, cursor, &iter);
    }

}

static void
erase (PsppireKeypad *kp, gpointer data)
{
  GladeXML *xml = data;

  GtkWidget *rhs = get_widget_assert (xml, "compute-textview1");

  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (rhs));

  erase_selection (buffer);
}

static char *
generate_syntax (const struct compute_dialog *cd)
{
  gchar *text;
  GString *string ;
  GtkTextIter start, end;
  GtkWidget *target =    get_widget_assert   (cd->xml, "compute-entry1");
  GtkWidget *syntax_area = get_widget_assert (cd->xml, "compute-textview1");

  GtkTextBuffer *buffer =
    gtk_text_view_get_buffer (GTK_TEXT_VIEW (syntax_area));

  gtk_text_buffer_get_start_iter (buffer, &start);
  gtk_text_buffer_get_end_iter (buffer, &end);


  string = g_string_new ("COMPUTE ");

  g_string_append (string, gtk_entry_get_text (GTK_ENTRY (target)));

  g_string_append (string, " = ");

  g_string_append (string,
		   gtk_text_buffer_get_text (buffer, &start, &end, FALSE));

  g_string_append (string, ".");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}


/* Pops up the Compute dialog box */
void
compute_dialog (GObject *o, gpointer data)
{
  gint response;
  struct data_editor *de = data;

  PsppireVarStore *vs;
  struct compute_dialog scd;

  GladeXML *xml = XML_NEW ("psppire.glade");

  GtkWidget *dialog = get_widget_assert   (xml, "compute-variable-dialog");

  GtkWidget *dict_view = get_widget_assert   (xml, "compute-treeview1");
  GtkWidget *functions = get_widget_assert   (xml, "compute-treeview2");
  GtkWidget *keypad =    get_widget_assert   (xml, "psppire-keypad1");
  GtkWidget *target =    get_widget_assert   (xml, "compute-entry1");
  GtkWidget *syntax_area = get_widget_assert (xml, "compute-textview1");
  GtkWidget *var_selector = get_widget_assert (xml, "compute-selector1");
  GtkWidget *func_selector = get_widget_assert (xml, "compute-selector2");

  GtkSheet *var_sheet =
    GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

  vs = PSPPIRE_VAR_STORE (gtk_sheet_get_model (var_sheet));


  gtk_window_set_transient_for (GTK_WINDOW (dialog), de->parent.window);


  attach_dictionary_to_treeview (GTK_TREE_VIEW (dict_view),
				 vs->dict,
				 GTK_SELECTION_SINGLE, NULL);


  psppire_selector_set_subjects (PSPPIRE_SELECTOR (var_selector),
				 dict_view, syntax_area,
				 insert_source_row_into_text_view,
				 NULL);


  function_list_populate (GTK_TREE_VIEW (functions));

  psppire_selector_set_subjects (PSPPIRE_SELECTOR (func_selector),
				 functions, syntax_area,
				 insert_function_into_syntax_area,
				 NULL);


  scd.xml = xml;

  g_signal_connect (target, "changed", G_CALLBACK (on_target_change), &scd);

  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &scd);

  g_signal_connect (keypad, "insert-syntax",
		    G_CALLBACK (on_keypad_button),  xml);

  g_signal_connect (keypad, "erase",
		    G_CALLBACK (erase),  xml);


  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (&scd);
	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (&scd);

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


enum {
  COL_NAME,
  COL_USAGE,
  COL_ARITY
};


static void
function_list_populate (GtkTreeView *tv)
{
  GtkListStore *liststore;
  GtkTreeIter iter;
  gint i;

  const gint n_funcs = expr_get_function_cnt ();

  liststore = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);

  for (i = 0 ; i < n_funcs ; ++i)
    {
      const struct operation *op = expr_get_function (i);

      gtk_list_store_append (liststore, &iter);

      gtk_list_store_set (liststore, &iter,
			  COL_NAME, expr_operation_get_name (op),
			  COL_USAGE, expr_operation_get_prototype (op),
			  COL_ARITY, expr_operation_get_arg_cnt (op),
			  -1);
    }



  /* Set the cell rendering */

  {
    GtkTreeViewColumn *col;
    GtkCellRenderer *renderer;


    col = gtk_tree_view_column_new ();

    gtk_tree_view_append_column (tv, col);

    renderer = gtk_cell_renderer_text_new ();

    gtk_tree_view_column_pack_start (col, renderer, TRUE);

    gtk_tree_view_column_add_attribute (col, renderer, "text", COL_USAGE);
  }

  gtk_tree_view_set_model (tv, GTK_TREE_MODEL (liststore));
}




static void
insert_function_into_syntax_area (GtkTreeIter iter,
				  GtkWidget *text_view,
				  GtkTreeModel *model
				  )
{
  GString *string;
  GValue name_value = {0};
  GValue arity_value = {0};
  gint arity;
  gint i;

  GtkTextBuffer *buffer ;

  g_return_if_fail (GTK_IS_TEXT_VIEW (text_view));

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));

  gtk_tree_model_get_value (model, &iter, COL_NAME, &name_value);
  gtk_tree_model_get_value (model, &iter, COL_ARITY, &arity_value);

  arity = g_value_get_int (&arity_value);

  string = g_string_new (g_value_get_string (&name_value));

  g_string_append (string, "(");
  for ( i = 0 ; i < arity -1 ; ++i )
    {
      g_string_append (string, "?,");
    }
  g_string_append (string, "?)");


  erase_selection (buffer);

  gtk_text_buffer_insert_at_cursor (buffer, string->str, string->len);

  g_value_unset (&name_value);
  g_value_unset (&arity_value);
  g_string_free (string, TRUE);

  /* Now position the cursor over the first '?' */
  {
    GtkTextIter insert;
    GtkTextIter selectbound;
    GtkTextMark *cursor = gtk_text_buffer_get_insert (buffer);
    gtk_text_buffer_get_iter_at_mark (buffer, &insert, cursor);
    for ( i = 0 ; i < arity ; ++i )
      {
	gtk_text_iter_backward_cursor_position (&insert);
	gtk_text_iter_backward_cursor_position (&insert);
      }
    selectbound = insert;
    gtk_text_iter_forward_cursor_position (&selectbound);

    gtk_text_buffer_select_range (buffer, &insert, &selectbound);
  }

}

/* Inserts the name of the selected variable into the destination widget.
   The destination widget must be a GtkTextView
 */
static void
insert_source_row_into_text_view (GtkTreeIter iter,
				  GtkWidget *dest,
				  GtkTreeModel *model
				  )
{
  GtkTreePath *path;
  PsppireDict *dict;
  gint *idx;
  struct variable *var;
  GtkTreeIter dict_iter;
  gchar *name;
  GtkTextBuffer *buffer;

  g_return_if_fail (GTK_IS_TEXT_VIEW (dest));

  if ( GTK_IS_TREE_MODEL_FILTER (model))
    {
      dict = PSPPIRE_DICT (gtk_tree_model_filter_get_model
			   (GTK_TREE_MODEL_FILTER(model)));

      gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER
							(model),
							&dict_iter, &iter);
    }
  else
    {
      dict = PSPPIRE_DICT (model);
      dict_iter = iter;
    }

  path = gtk_tree_model_get_path (GTK_TREE_MODEL (dict), &dict_iter);

  idx = gtk_tree_path_get_indices (path);

  var =  psppire_dict_get_variable (dict, *idx);

  gtk_tree_path_free (path);

  name = pspp_locale_to_utf8 (var_get_name (var), -1, NULL);

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (dest));

  erase_selection (buffer);

  gtk_text_buffer_insert_at_cursor (buffer, name, -1);

  g_free (name);
}
