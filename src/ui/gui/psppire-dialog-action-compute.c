/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012, 2013  Free Software Foundation

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

#include "psppire-dialog-action-compute.h"

#include <language/expressions/public.h>

#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "psppire-keypad.h"
#include "psppire-selector.h"
#include "builder-wrapper.h"

static void psppire_dialog_action_compute_init            (PsppireDialogActionCompute      *act);
static void psppire_dialog_action_compute_class_init      (PsppireDialogActionComputeClass *class);

G_DEFINE_TYPE (PsppireDialogActionCompute, psppire_dialog_action_compute, PSPPIRE_TYPE_DIALOG_ACTION);


static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionCompute *cd = PSPPIRE_DIALOG_ACTION_COMPUTE (act);
  gchar *text;
  GString *string;

  const gchar *target_name ;
  gchar *expression;
  const gchar *label;
  GtkTextIter start, end;

  GtkTextBuffer *buffer =
    gtk_text_view_get_buffer (GTK_TEXT_VIEW (cd->textview));

  gtk_text_buffer_get_start_iter (buffer, &start);
  gtk_text_buffer_get_end_iter (buffer, &end);

  target_name = gtk_entry_get_text (GTK_ENTRY (cd->target));

  expression = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);

  string = g_string_sized_new (64);

  if ( cd->use_type &&
       NULL == psppire_dict_lookup_var (act->dict, target_name ))
    {
      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (cd->str_btn)))
	{
	  const char *w = gtk_entry_get_text (GTK_ENTRY (cd->width_entry));
	  g_string_append_printf (string,
				  "STRING %s (a%s).\n", target_name, w);
	}
      else
	g_string_append_printf (string, "NUMERIC %s.\n", target_name);
    }

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (cd->user_label)))
    label = gtk_entry_get_text (GTK_ENTRY (cd->entry));
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

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}


static gboolean
dialog_state_valid (gpointer data)
{

  return TRUE;
}

static void
on_target_change (GObject *obj, gpointer rd_)
{
  PsppireDialogActionCompute *cd = PSPPIRE_DIALOG_ACTION_COMPUTE (rd_);

  const gchar *var_name = gtk_entry_get_text (GTK_ENTRY (cd->target));
  gboolean valid = var_name && strcmp ("", var_name);

  gtk_widget_set_sensitive (cd->type_and_label, valid);
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionCompute *cd = PSPPIRE_DIALOG_ACTION_COMPUTE (rd_);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (rd_);
  GtkTextIter start, end;
  GtkTreeSelection *selection;
  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (cd->textview));

  cd->use_type = FALSE;

  /* Clear the target variable entry box */
  gtk_entry_set_text (GTK_ENTRY (cd->target), "");
  g_signal_emit_by_name (cd->target, "changed");

  /* Clear the syntax area textbuffer */
  gtk_text_buffer_get_start_iter (buffer, &start);
  gtk_text_buffer_get_end_iter (buffer, &end);
  gtk_text_buffer_delete (buffer, &start, &end);

  /* Unselect all items in the treeview */
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (pda->source));
  gtk_tree_selection_unselect_all (selection);

  /* And the other one */
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (cd->functions));
  gtk_tree_selection_unselect_all (selection);
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
  g_object_unref (liststore);
}



static void
reset_type_label_dialog (PsppireDialogActionCompute *cd)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (cd);

  const gchar *target_name;
  struct variable *target_var;


  target_name = gtk_entry_get_text (GTK_ENTRY (cd->target));


  if ( (target_var = psppire_dict_lookup_var (pda->dict, target_name)) )
    {
      /* Existing Variable */
      const gchar *label = var_get_label (target_var);

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cd->user_label), TRUE);

      if ( label )
	{
	  gtk_entry_set_text (GTK_ENTRY (cd->entry), label);
	}

      gtk_widget_set_sensitive (cd->width_entry, FALSE);

      if ( var_is_numeric (target_var))
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cd->numeric_target),
				      TRUE);
      else
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cd->str_btn),
				      TRUE);

      gtk_widget_set_sensitive (cd->numeric_target, FALSE);
      gtk_widget_set_sensitive (cd->str_btn, FALSE);
    }
  else
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cd->expression), TRUE);

      gtk_widget_set_sensitive (cd->width_entry, TRUE);
      gtk_widget_set_sensitive (cd->numeric_target, TRUE);
      gtk_widget_set_sensitive (cd->str_btn, TRUE);

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cd->numeric_target),
				    TRUE);
    }

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
  PsppireDialogActionCompute *cd = PSPPIRE_DIALOG_ACTION_COMPUTE (data);

  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (cd->textview));

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
  PsppireDialogActionCompute *cd = PSPPIRE_DIALOG_ACTION_COMPUTE (data);

  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (cd->textview));

  erase_selection (buffer);
}


static void
run_type_label_dialog (GtkButton *b, gpointer data)
{
  PsppireDialogActionCompute *cd = PSPPIRE_DIALOG_ACTION_COMPUTE (data);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (data);
  gint response;

  gtk_window_set_transient_for (GTK_WINDOW (cd->subdialog), GTK_WINDOW (pda->dialog));

  reset_type_label_dialog (cd);
  response = psppire_dialog_run (PSPPIRE_DIALOG (cd->subdialog));
  if ( response == PSPPIRE_RESPONSE_CONTINUE)
    cd->use_type = TRUE;
}

static void
on_type_toggled (GtkToggleButton *button, gpointer data)
{
  PsppireDialogActionCompute *cd = PSPPIRE_DIALOG_ACTION_COMPUTE (data);
  if ( gtk_toggle_button_get_active (button))
    {
      gtk_widget_set_sensitive (cd->width_entry, TRUE);
      gtk_widget_grab_focus (cd->width_entry);
    }
  else
    {
      gtk_widget_set_sensitive (cd->width_entry, FALSE);
    }
}


static void
on_expression_toggle (GtkToggleButton *button, gpointer data)
{
  PsppireDialogActionCompute *cd = PSPPIRE_DIALOG_ACTION_COMPUTE (data);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (data);

  if ( gtk_toggle_button_get_active (button))
    {
      gtk_entry_set_text (GTK_ENTRY (cd->entry), "");
      gtk_widget_set_sensitive (cd->entry, FALSE);
    }
  else
    {
      const gchar *target_name = gtk_entry_get_text (GTK_ENTRY (cd->target));
      const struct variable *target_var = psppire_dict_lookup_var (pda->dict, target_name);
      if ( target_var )
	{
	  const char *label = var_get_label (target_var);

	  if ( label )
	    gtk_entry_set_text (GTK_ENTRY (cd->entry), label);
	}
      else
	gtk_entry_set_text (GTK_ENTRY (cd->entry), "");

      gtk_widget_set_sensitive (cd->entry, TRUE);
      gtk_widget_grab_focus (cd->entry);
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

static void
insert_function_into_syntax_area (GtkTreeIter iter,
				  GtkWidget *text_view,
				  GtkTreeModel *model,
				  gpointer data)
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



static void
psppire_dialog_action_compute_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionCompute *act = PSPPIRE_DIALOG_ACTION_COMPUTE (a);

  GtkBuilder *xml = builder_new ("compute.ui");
  pda->dialog = get_widget_assert   (xml, "compute-variable-dialog");
  pda->source = get_widget_assert   (xml, "compute-treeview1");

  act->textview = get_widget_assert (xml, "compute-textview1");
  act->entry =
    get_widget_assert (xml, "type-and-label-label-entry");

  act->width_entry =
    get_widget_assert (xml, "type-and-label-width");

  act->functions = get_widget_assert (xml, "compute-treeview2");
  act->keypad    = get_widget_assert (xml, "psppire-keypad1");
  act->target    = get_widget_assert (xml, "compute-entry1");
  act->var_selector = get_widget_assert (xml, "compute-selector1");
  act->func_selector = get_widget_assert (xml, "compute-selector2");
  act->type_and_label = get_widget_assert (xml, "compute-button1");

  act->subdialog = get_widget_assert (xml, "type-and-label-dialog");

  act->numeric_target = get_widget_assert (xml, "radio-button-numeric");
  act->expression = get_widget_assert (xml, "radio-button-expression-label");
  act->user_label  = get_widget_assert (xml, "radio-button-user-label");
  act->str_btn    = get_widget_assert (xml, "radio-button-string");

  g_signal_connect (act->expression, "toggled",
		    G_CALLBACK (on_expression_toggle), pda);

  g_signal_connect (act->str_btn, "toggled",
  		    G_CALLBACK (on_type_toggled), pda);


  g_object_set (pda->source,
		"selection-mode", GTK_SELECTION_SINGLE,
		NULL);

  psppire_selector_set_select_func (PSPPIRE_SELECTOR (act->var_selector),
				    insert_source_row_into_text_view, NULL);


  function_list_populate (GTK_TREE_VIEW (act->functions));

  psppire_selector_set_select_func (PSPPIRE_SELECTOR (act->func_selector),
				    insert_function_into_syntax_area, NULL);

  g_signal_connect (act->target, "changed", G_CALLBACK (on_target_change), act);

  g_signal_connect (act->keypad, "insert-syntax",
		    G_CALLBACK (on_keypad_button),  act);

  g_signal_connect (act->keypad, "erase",
		    G_CALLBACK (erase),  act);

  g_signal_connect (act->type_and_label, "clicked",
		    G_CALLBACK (run_type_label_dialog),  pda);

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

  g_object_unref (xml);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_compute_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_compute_parent_class)->activate (pda);
}

static void
psppire_dialog_action_compute_class_init (PsppireDialogActionComputeClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_compute_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_compute_init (PsppireDialogActionCompute *act)
{
}

