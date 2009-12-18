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

#include <config.h>
#include <gtk/gtk.h>
#include "compute-dialog.h"
#include "helper.h"
#include "psppire-dialog.h"
#include "psppire-keypad.h"
#include "psppire-data-window.h"
#include "psppire-var-store.h"
#include "psppire-selector.h"
#include "dialog-common.h"
#include <libpspp/i18n.h>

#include <language/expressions/public.h>
#include <language/syntax-string-source.h>
#include "executor.h"

static void function_list_populate (GtkTreeView *tv);

static void insert_function_into_syntax_area (GtkTreeIter iter,
					      GtkWidget *text_view,
					      GtkTreeModel *model,
					      gpointer data
					      );

static void insert_source_row_into_text_view (GtkTreeIter iter,
					      GtkWidget *dest,
					      GtkTreeModel *model,
					      gpointer data
					      );



struct compute_dialog
{
  GtkBuilder *xml;  /* The xml that generated the widgets */
  PsppireDict *dict;
  gboolean use_type;
};


static void
on_target_change (GObject *obj, struct compute_dialog *cd)
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
  GtkBuilder *xml = data;

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
  GtkBuilder *xml = data;

  GtkWidget *rhs = get_widget_assert (xml, "compute-textview1");

  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (rhs));

  erase_selection (buffer);
}

static char *
generate_syntax (const struct compute_dialog *cd)
{
  gchar *text;
  GString *string ;
  const gchar *target_name ;
  gchar *expression;
  const gchar *label;
  GtkTextIter start, end;
  GtkWidget *target = get_widget_assert   (cd->xml, "compute-entry1");
  GtkWidget *syntax_area = get_widget_assert (cd->xml, "compute-textview1");
  GtkWidget *string_toggle = get_widget_assert (cd->xml, "radio-button-string");
  GtkWidget *user_label_toggle =
    get_widget_assert (cd->xml, "radio-button-user-label");
  GtkWidget *width_entry = get_widget_assert (cd->xml, "type-and-label-width");
  GtkWidget *label_entry = get_widget_assert (cd->xml,
					      "type-and-label-label-entry");


  GtkTextBuffer *buffer =
    gtk_text_view_get_buffer (GTK_TEXT_VIEW (syntax_area));

  gtk_text_buffer_get_start_iter (buffer, &start);
  gtk_text_buffer_get_end_iter (buffer, &end);

  target_name = gtk_entry_get_text (GTK_ENTRY (target));

  expression = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

  string = g_string_sized_new (64);

  if ( cd-> use_type &&
       NULL == psppire_dict_lookup_var (cd->dict, target_name ))
    {
      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (string_toggle)))
	{
	  const char *w = gtk_entry_get_text (GTK_ENTRY(width_entry));
	  g_string_append_printf (string,
				  "STRING %s (a%s).\n", target_name, w);
	}
      else
	g_string_append_printf (string, "NUMERIC %s.\n", target_name);
    }

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (user_label_toggle)))
    label = gtk_entry_get_text (GTK_ENTRY (label_entry));
  else
    label = expression;

  if ( strlen (label) > 0 )
    g_string_append_printf (string, "VARIABLE LABEL %s '%s'.\n",
			    target_name,
			    label);

  g_string_append_printf (string, "COMPUTE %s = %s.\n",
			  target_name,
			  expression
			  );

  g_string_append (string, "EXECUTE.\n");


  g_free (expression);

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}

static void
reset_type_label_dialog (struct compute_dialog *cd)
{
  const gchar *target_name;
  struct variable *target_var;

  GtkWidget *width_entry =
    get_widget_assert (cd->xml, "type-and-label-width");

  GtkWidget *label_entry =
    get_widget_assert (cd->xml, "type-and-label-label-entry");

  GtkWidget *numeric_target =
    get_widget_assert (cd->xml, "radio-button-numeric");

  GtkWidget *string_target =
    get_widget_assert (cd->xml, "radio-button-string");


  target_name = gtk_entry_get_text
    (GTK_ENTRY (get_widget_assert (cd->xml, "compute-entry1")));


  if ( (target_var = psppire_dict_lookup_var (cd->dict, target_name)) )
    {
      /* Existing Variable */
      const gchar *label ;
      GtkWidget *user_label =
	get_widget_assert (cd->xml, "radio-button-user-label");

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (user_label), TRUE);

      label = var_get_label (target_var);

      if ( label )
	{
	  gtk_entry_set_text (GTK_ENTRY (label_entry), label);
	}

      gtk_widget_set_sensitive (width_entry, FALSE);

      if ( var_is_numeric (target_var))
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (numeric_target),
				      TRUE);
      else
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (string_target),
				      TRUE);

      gtk_widget_set_sensitive (numeric_target, FALSE);
      gtk_widget_set_sensitive (string_target, FALSE);
    }
  else
    {
      GtkWidget *expression =
	get_widget_assert (cd->xml, "radio-button-expression-label");

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (expression), TRUE);

      gtk_widget_set_sensitive (width_entry, TRUE);
      gtk_widget_set_sensitive (numeric_target, TRUE);
      gtk_widget_set_sensitive (string_target, TRUE);

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (numeric_target),
				    TRUE);
    }

}

static void
run_type_label_dialog (GtkButton *b, gpointer data)
{
  struct compute_dialog *cd = data;
  gint response;

  GtkWidget *subdialog = get_widget_assert (cd->xml, "type-and-label-dialog");
  GtkWidget *dialog = get_widget_assert (cd->xml, "compute-variable-dialog");

  gtk_window_set_transient_for (GTK_WINDOW (subdialog), GTK_WINDOW (dialog));

  reset_type_label_dialog (cd);
  response = psppire_dialog_run (PSPPIRE_DIALOG (subdialog));
  if ( response == PSPPIRE_RESPONSE_CONTINUE)
    cd->use_type = TRUE;
}


static void
on_expression_toggle (GtkToggleButton *button, gpointer data)
{
  struct compute_dialog *cd = data;

  GtkWidget *entry =
    get_widget_assert (cd->xml, "type-and-label-label-entry");

  if ( gtk_toggle_button_get_active (button))
    {
      gtk_entry_set_text (GTK_ENTRY (entry), "");
      gtk_widget_set_sensitive (entry, FALSE);
    }
  else
    {
      const char *label;
      struct variable *target_var;
      const gchar *target_name = gtk_entry_get_text
	(GTK_ENTRY (get_widget_assert (cd->xml, "compute-entry1")));

      target_var = psppire_dict_lookup_var (cd->dict, target_name);
      if ( target_var )
	{
	  label = var_get_label (target_var);

	  if ( label )
	    gtk_entry_set_text (GTK_ENTRY (entry), label);
	}
      else
	gtk_entry_set_text (GTK_ENTRY (entry), "");

      gtk_widget_set_sensitive (entry, TRUE);
    }
}


/* Return TRUE if the dialog box's widgets' state are such that clicking OK
   might not result in erroneous syntax being generated */
static gboolean
contents_plausible (gpointer data)
{
  struct compute_dialog *cd = data;

  GtkWidget *target      = get_widget_assert (cd->xml, "compute-entry1");
  GtkWidget *syntax_area = get_widget_assert (cd->xml, "compute-textview1");
  GtkTextBuffer *buffer  =
    gtk_text_view_get_buffer (GTK_TEXT_VIEW (syntax_area));

  if ( 0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (target))))
    return FALSE;

  if ( gtk_text_buffer_get_char_count (buffer) == 0 )
    return FALSE;

  return TRUE;
}

/* Pops up the Compute dialog box */
void
compute_dialog (GObject *o, gpointer data)
{
  gint response;
  PsppireDataWindow *de = data;

  PsppireVarStore *vs = NULL;
  struct compute_dialog scd;

  GtkBuilder *xml = builder_new ("psppire.ui");

  GtkWidget *dialog = get_widget_assert   (xml, "compute-variable-dialog");

  GtkWidget *dict_view = get_widget_assert (xml, "compute-treeview1");
  GtkWidget *functions = get_widget_assert (xml, "compute-treeview2");
  GtkWidget *keypad    = get_widget_assert (xml, "psppire-keypad1");
  GtkWidget *target    = get_widget_assert (xml, "compute-entry1");
  GtkWidget *var_selector = get_widget_assert (xml, "compute-selector1");
  GtkWidget *func_selector = get_widget_assert (xml, "compute-selector2");
  GtkWidget *type_and_label = get_widget_assert (xml, "compute-button1");

  GtkWidget *expression =
	get_widget_assert (xml, "radio-button-expression-label");


  g_object_get (de->data_editor, "var-store", &vs, NULL);
  g_object_get (vs, "dictionary", &scd.dict, NULL);
  scd.use_type = FALSE;

  g_signal_connect (expression, "toggled",
		    G_CALLBACK(on_expression_toggle), &scd);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  
  g_object_set (dict_view, "model", scd.dict,
		"selection-mode", GTK_SELECTION_SINGLE,
		NULL);

  psppire_selector_set_select_func (PSPPIRE_SELECTOR (var_selector),
				    insert_source_row_into_text_view, NULL);

  function_list_populate (GTK_TREE_VIEW (functions));

  psppire_selector_set_select_func (PSPPIRE_SELECTOR (func_selector),
				    insert_function_into_syntax_area, NULL);

  scd.xml = xml;

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      contents_plausible, &scd);

  g_signal_connect (target, "changed", G_CALLBACK (on_target_change), &scd);

  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &scd);

  g_signal_connect (keypad, "insert-syntax",
		    G_CALLBACK (on_keypad_button),  xml);

  g_signal_connect (keypad, "erase",
		    G_CALLBACK (erase),  xml);


  g_signal_connect (type_and_label, "clicked",
		    G_CALLBACK (run_type_label_dialog),  &scd);



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

	paste_syntax_in_new_window (syntax);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (xml);
}


enum {
  COMPUTE_COL_NAME,
  COMPUTE_COL_USAGE,
  COMPUTE_COL_ARITY
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
			  COMPUTE_COL_NAME, expr_operation_get_name (op),
			  COMPUTE_COL_USAGE, expr_operation_get_prototype (op),
			  COMPUTE_COL_ARITY, expr_operation_get_arg_cnt (op),
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

    gtk_tree_view_column_add_attribute (col, renderer, "text", COMPUTE_COL_USAGE);
  }

  gtk_tree_view_set_model (tv, GTK_TREE_MODEL (liststore));
}




static void
insert_function_into_syntax_area (GtkTreeIter iter,
				  GtkWidget *text_view,
				  GtkTreeModel *model,
				  gpointer data
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

  gtk_tree_model_get_value (model, &iter, COMPUTE_COL_NAME, &name_value);
  gtk_tree_model_get_value (model, &iter, COMPUTE_COL_ARITY, &arity_value);

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
				  GtkTreeModel *model,
				  gpointer data
				  )
{
  GtkTreePath *path;
  PsppireDict *dict;
  gint *idx;
  struct variable *var;
  GtkTreeIter dict_iter;
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

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (dest));

  erase_selection (buffer);

  gtk_text_buffer_insert_at_cursor (buffer, var_get_name (var), -1);

}
