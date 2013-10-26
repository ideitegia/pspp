/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2010, 2011, 2012, 2013  Free Software Foundation

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

#include "dialog-common.h"

#include <gl/c-xvasprintf.h>
#include <language/stats/aggregate.h>

#include <ui/syntax-gen.h>
#include <libpspp/str.h>

#include "aggregate-dialog.h"
#include "psppire-selector.h"
#include "psppire-dictview.h"
#include "psppire-dialog.h"

#include "psppire-data-window.h"
#include "psppire-var-view.h"
#include "psppire-acr.h"

#include "dict-display.h"

#include "executor.h"
#include "builder-wrapper.h"
#include "helper.h"

#include <gtk/gtk.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


enum
  {
    COMBO_MODEL_COL_DESC = 0,
    COMBO_MODEL_COL_SYNTAX,
    COMBO_MODEL_COL_SRC_VARS,
    COMBO_MODEL_COL_ARITY
  };


struct aggregate
{
  GtkBuilder *xml;
  PsppireDataWindow *de ;
  PsppireDict *dict;
  GtkWidget *break_variables;

  GtkWidget *replace_radiobutton;
  GtkWidget *add_radiobutton;
  GtkWidget *filename_radiobutton;
  GtkWidget *filename_button;
  GtkWidget *filename_box;
  GtkWidget *filename_label;

  GtkWidget *function_combo;

  GtkWidget *summary_acr;
  GtkWidget *summary_var_name_entry;
  GtkWidget *summary_var_label_entry;

  GtkWidget *summary_sv;
  GtkWidget *summary_sv_entry;

  GtkWidget *summary_arg1;
  GtkWidget *summary_arg2;

  GtkWidget *summary_arg1_entry;
  GtkWidget *summary_arg2_entry;

  GtkWidget *sorted_button;
  GtkWidget *needs_sort_button;

  GtkWidget *pane;
};


static char * generate_syntax (const struct aggregate *rd);


static void update_arguments (struct aggregate *agg);


static void
refresh (struct aggregate *agg)
{
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (agg->break_variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (agg->add_radiobutton), TRUE);
  gtk_label_set_text (GTK_LABEL (agg->filename_label), "");


  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (agg->needs_sort_button), TRUE);

  gtk_entry_set_text (GTK_ENTRY (agg->summary_sv_entry), "");
  gtk_entry_set_text (GTK_ENTRY (agg->summary_arg1_entry), "");
  gtk_entry_set_text (GTK_ENTRY (agg->summary_arg2_entry), "");
  gtk_entry_set_text (GTK_ENTRY (agg->summary_var_label_entry), "");
  gtk_entry_set_text (GTK_ENTRY (agg->summary_var_name_entry), "N_BREAK");
  gtk_editable_select_region (GTK_EDITABLE (agg->summary_var_name_entry), 0, -1);

  gtk_combo_box_set_active (GTK_COMBO_BOX (agg->function_combo), N);

  gtk_list_store_clear (PSPPIRE_ACR (agg->summary_acr)->list_store);

  update_arguments (agg);
}


static gboolean
dialog_state_valid (gpointer data)
{
  GtkTreeIter iter;
  const struct aggregate *agg = data;

  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (agg->break_variables));

  if ( ! gtk_tree_model_get_iter_first  (liststore, &iter))
    return FALSE;

  liststore = GTK_TREE_MODEL (PSPPIRE_ACR (agg->summary_acr)->list_store);
  
  if ( ! gtk_tree_model_get_iter_first (liststore, &iter))
    return FALSE;

  return TRUE;
}


static void
choose_filename (struct aggregate *fd)
{
  GtkFileFilter *filter;

  GtkWidget *dialog = gtk_file_chooser_dialog_new (_("Aggregate destination file"),
							   GTK_WINDOW (fd->de),
							   GTK_FILE_CHOOSER_ACTION_SAVE,
							   GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
							   GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
							   NULL);
  g_object_set (dialog, "local-only", FALSE, NULL);

  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);


  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("System Files (*.sav)"));
  gtk_file_filter_add_mime_type (filter, "application/x-spss-sav");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Compressed System Files (*.zsav)"));
  gtk_file_filter_add_pattern (filter, "*.zsav");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Portable Files (*.por) "));
  gtk_file_filter_add_mime_type (filter, "application/x-spss-por");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);


  if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
      char *filename;

      filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

      gtk_label_set_text (GTK_LABEL (fd->filename_label), filename);

      g_free (filename);
    }


  gtk_widget_destroy (dialog);
}


static void
populate_combo_model (GtkComboBox *cb)
{
  GtkListStore *list =  gtk_list_store_new (4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
  GtkTreeIter iter;
  const struct agr_func *af = agr_func_tab;
  GtkCellRenderer *renderer ;

  for (af = agr_func_tab; af->name; ++af)
    {
      const gchar *s = af->description;
      if (s == NULL)
	continue;

      gtk_list_store_append (list, &iter);
      gtk_list_store_set (list, &iter,
                          COMBO_MODEL_COL_DESC, gettext (s),
			  COMBO_MODEL_COL_SYNTAX, af->name,
			  COMBO_MODEL_COL_SRC_VARS, af->src_vars,
			  COMBO_MODEL_COL_ARITY, af->n_args,
                          -1);
    }

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (cb), renderer, FALSE);

  gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (cb), renderer, "text", 0);

  gtk_combo_box_set_model (GTK_COMBO_BOX (cb), GTK_TREE_MODEL (list));
  g_object_unref (list);
}



/* Returns TRUE iff all the necessary controls have been set to
   completely specify a summary function */
static gboolean
summary_complete (const struct aggregate *agg)
{
  GtkTreeIter iter;
  int n_args;
  enum agr_src_vars src_vars;
  gboolean ok;
  GtkTreeModel *model = gtk_combo_box_get_model (GTK_COMBO_BOX (agg->function_combo));

  if ( 0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (agg->summary_var_name_entry))))
    return FALSE;

  ok = gtk_combo_box_get_active_iter (GTK_COMBO_BOX (agg->function_combo), &iter);

  if (! ok)
    return FALSE;



  gtk_tree_model_get  (model,
		       &iter,
		       COMBO_MODEL_COL_ARITY,   &n_args,
		       COMBO_MODEL_COL_SRC_VARS, &src_vars,
		       -1);

  if ( src_vars == AGR_SV_YES )
    {
      if (0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (agg->summary_sv_entry))))
	return FALSE;
    }

  if ( n_args >= 2)
    {
      if (0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (agg->summary_arg2_entry))))
	return FALSE;
    }

  if ( n_args >= 1)
    {
      if (0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (agg->summary_arg1_entry))))
	return FALSE;
    }


  return TRUE;
}


static void
append_summary_spec (const struct aggregate *agg, GtkTreeIter *iter, GString *string);


static void
render_summary   (GtkTreeViewColumn *tree_column,
		  GtkCellRenderer *cell,
		  GtkTreeModel *tree_model,
		  GtkTreeIter *iter,
		  gpointer data)
{
  struct aggregate *agg = data;
  
  GString *string = g_string_new ("");

  append_summary_spec (agg, iter, string);

  
  g_object_set (cell, "text", string->str, NULL);

  g_string_free (string, TRUE);
}


/* Enable/Disable the summary variable ACR */
static void
update_acr (struct aggregate *agg)
{
  gboolean ready = summary_complete (agg);

  psppire_acr_set_enabled (PSPPIRE_ACR (agg->summary_acr), ready);
}


/* Update the sensitivity of the summary variable argument fields */
static void
update_arguments (struct aggregate *agg)
{
  GtkTreeIter iter;

  gboolean ok = gtk_combo_box_get_active_iter (GTK_COMBO_BOX (agg->function_combo), &iter);

  if ( ok)
    {
      GtkTreeModel *model = gtk_combo_box_get_model (GTK_COMBO_BOX (agg->function_combo));
      int n_args;
      enum agr_src_vars src_vars;
      gtk_tree_model_get  (model,
			   &iter,
			   COMBO_MODEL_COL_ARITY,   &n_args,
			   COMBO_MODEL_COL_SRC_VARS, &src_vars,
			   -1);

      gtk_widget_set_sensitive (agg->summary_sv, src_vars != AGR_SV_NO);
      gtk_widget_set_sensitive (agg->summary_arg2, n_args >= 2);
      gtk_widget_set_sensitive (agg->summary_arg1, n_args >= 1);
    }
  else
    {
      gtk_widget_set_sensitive (agg->summary_sv,   FALSE);
      gtk_widget_set_sensitive (agg->summary_arg2, FALSE);
      gtk_widget_set_sensitive (agg->summary_arg1, FALSE);
    }
}

enum 
  {
    SUMMARY_COL_VARNAME = 0,
    SUMMARY_COL_VARLABEL,
    SUMMARY_COL_FUNCIDX,
    SUMMARY_COL_SRCVAR,
    SUMMARY_COL_ARG1,
    SUMMARY_COL_ARG2
  };

/* Set VAL to the value appropriate for COL according to the
   current state of the dialog */
static gboolean
get_summary_spec (gint col, GValue *val, gpointer data)
{
  const struct aggregate *agg = data;
  switch (col)
    {
    case SUMMARY_COL_VARNAME:
      g_value_init (val, G_TYPE_STRING);
      g_value_set_string (val, gtk_entry_get_text (GTK_ENTRY (agg->summary_var_name_entry)));
      break;
    case SUMMARY_COL_VARLABEL:
      g_value_init (val, G_TYPE_STRING);
      g_value_set_string (val, gtk_entry_get_text (GTK_ENTRY (agg->summary_var_label_entry)));
      break;
    case SUMMARY_COL_SRCVAR:
      g_value_init (val, G_TYPE_STRING);
      g_value_set_string (val, gtk_entry_get_text (GTK_ENTRY (agg->summary_sv_entry)));
      break;
    case SUMMARY_COL_FUNCIDX:
      g_value_init (val, G_TYPE_INT);
      g_value_set_int (val, gtk_combo_box_get_active (GTK_COMBO_BOX (agg->function_combo)));
      break;
    case SUMMARY_COL_ARG1:
      {
	const gchar *text = gtk_entry_get_text (GTK_ENTRY (agg->summary_arg1_entry));
	g_value_init (val, G_TYPE_DOUBLE);
	g_value_set_double (val, g_strtod (text, 0));
      }
      break;
    case SUMMARY_COL_ARG2:
      {
	const gchar *text = gtk_entry_get_text (GTK_ENTRY (agg->summary_arg2_entry));
	g_value_init (val, G_TYPE_DOUBLE);
	g_value_set_double (val, g_strtod (text, 0));
      }
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  return TRUE;
}


/* Update the status of the dialog box according to what row of the ACR's treeview
   is selected */
static  void
on_acr_change (const struct aggregate *agg, GtkTreeView *tv)
{
  const gchar *varname;
  const gchar *label;
  const gchar *srcvar;
  gint f_idx;
  double arg1, arg2;
  gchar *text = NULL;
    
  GtkTreeIter iter;
  GtkTreePath *path = NULL;
  GtkTreeModel *model = gtk_tree_view_get_model (tv);
  gtk_tree_view_get_cursor (tv, &path, NULL);
  
  gtk_tree_model_get_iter (model, &iter, path);

  gtk_tree_model_get (model, &iter,
		      SUMMARY_COL_VARNAME, &varname,
		      SUMMARY_COL_VARLABEL, &label,
		      SUMMARY_COL_FUNCIDX, &f_idx,
		      SUMMARY_COL_SRCVAR, &srcvar,
		      SUMMARY_COL_ARG1, &arg1,
		      SUMMARY_COL_ARG2, &arg2,
		      -1);

  gtk_entry_set_text (GTK_ENTRY (agg->summary_var_name_entry), varname);
  gtk_entry_set_text (GTK_ENTRY (agg->summary_var_label_entry), label);
  gtk_entry_set_text (GTK_ENTRY (agg->summary_sv_entry), srcvar);
  
  text = c_xasprintf ("%g", arg1);
  gtk_entry_set_text (GTK_ENTRY (agg->summary_arg1_entry), text);
  g_free (text);

  text = c_xasprintf ("%g", arg2);
  gtk_entry_set_text (GTK_ENTRY (agg->summary_arg2_entry), text);
  g_free (text);

  gtk_combo_box_set_active (GTK_COMBO_BOX (agg->function_combo), f_idx);
}


/* Set the pane to 50% of its maximum size */
static void
set_initial_pos (GtkPaned *pane)
{
  int max_pos;
  g_object_get (pane, 
		"max-position", &max_pos,
		NULL);

  gtk_paned_set_position (pane, max_pos);
}



/* Pops up the Aggregate dialog box */
void
aggregate_dialog (PsppireDataWindow *dw)
{
  struct aggregate fd;
  gint response;

  GtkWidget *dialog ;
  GtkWidget *source ;

  GtkWidget *break_selector ;

  fd.xml = builder_new ("aggregate.ui");

  fd.de = dw;

  dialog = get_widget_assert (fd.xml, "psppire-dialog1");
  source = get_widget_assert   (fd.xml, "dict-view");
  break_selector = get_widget_assert   (fd.xml, "break-selector");

  fd.pane = get_widget_assert (fd.xml, "hbox1");
  
  fd.break_variables = get_widget_assert (fd.xml, "psppire-var-view1");
  fd.filename_radiobutton = get_widget_assert (fd.xml, "filename-radiobutton");
  fd.filename_button = get_widget_assert (fd.xml, "filename-button");
  fd.filename_box = get_widget_assert (fd.xml, "filename-box");
  fd.filename_label = get_widget_assert (fd.xml, "filename-label");
  fd.replace_radiobutton = get_widget_assert (fd.xml, "replace-radiobutton");
  fd.add_radiobutton = get_widget_assert (fd.xml, "add-radiobutton");
  fd.function_combo = get_widget_assert (fd.xml, "function-combo");

  fd.summary_acr = get_widget_assert (fd.xml, "psppire-acr1");
  fd.summary_var_name_entry = get_widget_assert (fd.xml, "summary-var-name-entry");

  fd.summary_arg1 = get_widget_assert (fd.xml, "summary-arg1");
  fd.summary_arg2 = get_widget_assert (fd.xml, "summary-arg2");

  fd.summary_arg1_entry = get_widget_assert (fd.xml, "summary-arg-entry1");
  fd.summary_arg2_entry = get_widget_assert (fd.xml, "summary-arg-entry2");

  fd.summary_var_label_entry = get_widget_assert (fd.xml, "summary-var-label-entry");

  fd.summary_sv = get_widget_assert (fd.xml, "source-var");
  fd.summary_sv_entry = get_widget_assert (fd.xml, "source-var-entry");

  fd.sorted_button = get_widget_assert (fd.xml, "sorted-radiobutton");
  fd.needs_sort_button = get_widget_assert (fd.xml, "needs-sort-radiobutton");

  {
    GtkTreeViewColumn *column ;

    GList *l ;

    GtkCellRenderer *cell_renderer ;

    GtkListStore *list = gtk_list_store_new (6,
					     G_TYPE_STRING,
					     G_TYPE_STRING,
					     G_TYPE_INT, 
					     G_TYPE_STRING,
					     G_TYPE_DOUBLE,
					     G_TYPE_DOUBLE);

    psppire_acr_set_model (PSPPIRE_ACR (fd.summary_acr), list);
    g_object_unref (list);

    psppire_acr_set_get_value_func (PSPPIRE_ACR (fd.summary_acr),
						 get_summary_spec, &fd);

    column = gtk_tree_view_get_column (PSPPIRE_ACR (fd.summary_acr)->tv, 0);

    l = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));

    cell_renderer = l->data;

    gtk_tree_view_column_set_cell_data_func (column,
					     cell_renderer,
					     render_summary, 
					     &fd,
					     NULL);

    g_signal_connect_swapped (PSPPIRE_ACR (fd.summary_acr)->tv,
			      "cursor-changed", G_CALLBACK (on_acr_change), &fd);
  }
  
  g_signal_connect_swapped (fd.summary_var_name_entry, "changed", G_CALLBACK (update_acr),  &fd);
  g_signal_connect_swapped (fd.function_combo, "changed", G_CALLBACK (update_acr),  &fd);
  g_signal_connect_swapped (fd.summary_sv_entry, "changed", G_CALLBACK (update_acr),  &fd);  
  g_signal_connect_swapped (fd.summary_arg1_entry, "changed", G_CALLBACK (update_acr),  &fd);  
  g_signal_connect_swapped (fd.summary_arg2_entry, "changed", G_CALLBACK (update_acr),  &fd);  


  g_signal_connect (fd.pane, "realize", G_CALLBACK (set_initial_pos),  &fd);  


  g_signal_connect_swapped (fd.function_combo, "changed",
			    G_CALLBACK (update_arguments),  &fd);

  populate_combo_model (GTK_COMBO_BOX (fd.function_combo));

  g_signal_connect_swapped (dialog, "refresh", G_CALLBACK (refresh),  &fd);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fd.de));

  g_object_get (fd.de->data_editor, "dictionary", &fd.dict, NULL);
  g_object_set (source, "model", fd.dict, NULL);


  psppire_selector_set_filter_func (PSPPIRE_SELECTOR (break_selector), NULL);


  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &fd);

  g_signal_connect (fd.filename_radiobutton, "toggled",
		    G_CALLBACK (set_sensitivity_from_toggle), fd.filename_box );

  g_signal_connect_swapped (fd.filename_button, "clicked",
		    G_CALLBACK (choose_filename), &fd);


  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (dw, generate_syntax (&fd)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (&fd)));
      break;
    default:
      break;
    }

  g_object_unref (fd.xml);
}




static void
append_destination_filename (const struct aggregate *agg, GString *gs)
{
  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (agg->filename_radiobutton)))
    {
      struct string ss;
      const gchar *s = gtk_label_get_text (GTK_LABEL (agg->filename_label));
      ds_init_empty (&ss);
      syntax_gen_string (&ss, ss_cstr (s));
      g_string_append (gs, ds_cstr (&ss));
      ds_destroy (&ss);
    }
  else
    {
      g_string_append (gs, "* ");

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (agg->replace_radiobutton)))
	g_string_append (gs, "MODE=REPLACE");
      else
	g_string_append (gs, "MODE=ADDVARIABLES");
    }
}

/* Append the syntax of the summary function pointed to by ITER to STRING */
static void
append_summary_spec (const struct aggregate *agg, GtkTreeIter *iter, GString *string)
{
  GtkTreeIter combo_iter;
  char *varname = NULL;
  char *funcname = NULL;

  GtkTreeModel *acr_model = GTK_TREE_MODEL (PSPPIRE_ACR (agg->summary_acr)->list_store);
  GtkTreeModel *combo_model = gtk_combo_box_get_model (GTK_COMBO_BOX (agg->function_combo));


  /* This is an index into the combo_model.  Its used to get the function name */
  int f_idx;
  double arg1, arg2;
  int arity;
  enum agr_src_vars has_src_vars;
  gchar *label = NULL;
  gchar *srcvar = NULL;

  gtk_tree_model_get (acr_model, iter,
		      SUMMARY_COL_VARNAME, &varname,
		      SUMMARY_COL_VARLABEL, &label,
		      SUMMARY_COL_FUNCIDX, &f_idx,
		      SUMMARY_COL_SRCVAR, &srcvar,
		      SUMMARY_COL_ARG1, &arg1,
		      SUMMARY_COL_ARG2, &arg2,
		      -1);

  gtk_tree_model_iter_nth_child (combo_model, &combo_iter, NULL, f_idx);

  gtk_tree_model_get (combo_model, &combo_iter,
		      COMBO_MODEL_COL_SYNTAX, &funcname,
		      COMBO_MODEL_COL_ARITY, &arity,
		      COMBO_MODEL_COL_SRC_VARS, &has_src_vars,
		      -1);

  g_string_append (string, varname);

  if (0 != strcmp ("", label))
    {
      struct string ss;
      ds_init_empty (&ss);
      syntax_gen_string (&ss, ss_cstr (label));
      g_string_append (string, " ");
      g_string_append (string, ds_cstr (&ss));
      ds_destroy (&ss);
    }
    
  g_string_append_printf (string, " = %s", funcname);

  if ( has_src_vars != AGR_SV_NO)
    {
      struct string dss;
      ds_init_cstr (&dss, " (");
      
      ds_put_cstr (&dss, srcvar);

      if ( arity > 0)
	ds_put_c_format (&dss, ", %g", arg1);

      if ( arity > 1)
	ds_put_c_format (&dss, ", %g", arg2);

      ds_put_cstr (&dss, ")");

      g_string_append (string, ds_cstr (&dss));

      ds_destroy (&dss);
    }

   free (label);
   free (srcvar);
   free (varname);
   free (funcname);
}



static void
append_summary_variable_syntax (const struct aggregate *agg,  GString *string)
{
  GtkTreeIter iter;
  GtkTreeModel *acr_model = GTK_TREE_MODEL (PSPPIRE_ACR (agg->summary_acr)->list_store);


  gboolean ok;

  for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (acr_model), &iter);
       ok ;
       ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (acr_model), &iter)
       )
    {
      g_string_append (string, "\n\t/");

      append_summary_spec (agg, &iter, string);
    }
}


static char *
generate_syntax (const struct aggregate *agg)
{
  gchar *text;

  GString *string = g_string_new ("AGGREGATE OUTFILE=");

  append_destination_filename (agg, string);

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (agg->sorted_button)))
    g_string_append (string, "\n\t/PRESORTED");

  g_string_append (string, "\n\t/BREAK=");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (agg->break_variables), 0, string);

  append_summary_variable_syntax (agg, string);

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}
