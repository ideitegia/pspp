/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011, 2012  Free Software Foundation

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
#include "autorecode-dialog.h"
#include "psppire-selector.h"
#include "psppire-dictview.h"
#include "psppire-var-ptr.h"
#include "psppire-dialog.h"

#include "psppire-data-window.h"
#include "psppire-var-view.h"

#include "executor.h"
#include "builder-wrapper.h"
#include "helper.h"

#include <gtk/gtk.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Name-Label pair */
struct nlp
{
  char *name;
  char *label;
};

struct autorecode
{
  PsppireDict *dict;
  GtkWidget *change_button;
  GtkWidget *new_name_entry;
  GtkWidget *var_view;

  /* A hash table of struct nlp's indexed by variable */
  GHashTable *varmap;

  GtkWidget *ascending;
  GtkWidget *group;
  GtkWidget *blank;
};

static struct nlp *
nlp_create (const char *name, const char *label)
{
  struct nlp *nlp = xmalloc (sizeof *nlp);

  nlp->name = g_strdup (name);

  nlp->label = NULL;

  if ( label != NULL && 0 != strcmp ("", label))
    nlp->label = g_strdup (label);

  return nlp;
}

static void
nlp_destroy (gpointer data)
{
  struct nlp *nlp = data ;
  if ( ! nlp )
    return;

  g_free (nlp->name);
  g_free (nlp->label);
  g_free (nlp);
}

static void
on_change_clicked (GObject *obj, gpointer data)
{
  struct autorecode *rd = data;
  struct variable *var = NULL;
  struct nlp *nlp;
  GtkTreeModel *model = psppire_var_view_get_current_model (PSPPIRE_VAR_VIEW (rd->var_view));
  GtkTreeIter iter;
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (rd->var_view));

  GList *rows = gtk_tree_selection_get_selected_rows (selection, &model);

  const gchar *dest_var_name =
    gtk_entry_get_text (GTK_ENTRY (rd->new_name_entry));

  if ( NULL == rows || rows->next != NULL)
    goto finish;

  gtk_tree_model_get_iter (model, &iter, rows->data);

  gtk_tree_model_get (model, &iter, 0, &var, -1);

  g_hash_table_remove (rd->varmap, var);

  nlp = nlp_create (dest_var_name, NULL);

  g_hash_table_insert (rd->varmap, var, nlp);

  gtk_tree_model_row_changed (model, rows->data, &iter);

 finish:
  g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (rows);
}


static char * generate_syntax (const struct autorecode *rd);


static void
refresh (PsppireDialog *dialog, struct autorecode *rd)
{
  GtkTreeModel *target_list = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->var_view));

  gtk_entry_set_text (GTK_ENTRY (rd->new_name_entry), "");
  gtk_widget_set_sensitive  (rd->new_name_entry, FALSE);
  gtk_widget_set_sensitive  (rd->change_button, FALSE);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->ascending), TRUE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->group), FALSE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->blank), FALSE);

  if (rd->varmap )
    g_hash_table_remove_all (rd->varmap);

  gtk_list_store_clear (GTK_LIST_STORE (target_list));
}


static gboolean
dialog_state_valid (gpointer data)
{
  struct autorecode *rd = data;

  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->var_view));

  if (g_hash_table_size (rd->varmap) != gtk_tree_model_iter_n_children (model, NULL) )
    return FALSE;

 return TRUE;
}


static void
on_entry_change (struct autorecode *rd)
{
  gboolean valid = TRUE;
  const char *text = gtk_entry_get_text (GTK_ENTRY (rd->new_name_entry));

  if ( 0 == strcmp ("", text))
    valid = FALSE;
  else if (psppire_dict_lookup_var (rd->dict, text))
    valid = FALSE;
  else
    {
      GHashTableIter iter;
      gpointer key, value;

      g_hash_table_iter_init (&iter, rd->varmap);
      while (g_hash_table_iter_next (&iter, &key, &value)) 
	{
	  struct nlp *nlp = value;
	  
	  if ( 0 == strcmp (nlp->name, text))
	    {
	      valid = FALSE;
	      break;
	    }
	}
    }

  gtk_widget_set_sensitive  (rd->change_button, valid);
}

/* Callback which gets called when a new row is selected
   in the variable treeview.
   It sets the name and label entry widgets to reflect the
   currently selected row.
 */
static void
on_selection_change (GtkTreeSelection *selection, gpointer data)
{
  struct autorecode *rd = data;
  GtkTreeModel *model = psppire_var_view_get_current_model (PSPPIRE_VAR_VIEW (rd->var_view));

  GList *rows = gtk_tree_selection_get_selected_rows (selection, &model);

  if ( rows && !rows->next)
    {
      /* Exactly one row is selected */
      struct nlp *nlp;
      struct variable *var;
      gboolean ok;
      GtkTreeIter iter;

      gtk_widget_set_sensitive  (rd->new_name_entry, TRUE);
      gtk_widget_set_sensitive  (rd->change_button, TRUE);      


      ok = gtk_tree_model_get_iter (model, &iter, (GtkTreePath*) rows->data);
      g_return_if_fail (ok);

      gtk_tree_model_get (model, &iter, 0, &var, -1);

      nlp = g_hash_table_lookup (rd->varmap, var);

      if (nlp)
	gtk_entry_set_text (GTK_ENTRY (rd->new_name_entry), nlp->name ? nlp->name : "");
      else
	gtk_entry_set_text (GTK_ENTRY (rd->new_name_entry), "");
    }
  else
    {
      gtk_entry_set_text (GTK_ENTRY (rd->new_name_entry), "");
      gtk_widget_set_sensitive  (rd->new_name_entry, FALSE);
      gtk_widget_set_sensitive  (rd->change_button, FALSE);
    }

  g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (rows);
}

static void
render_new_var_name (GtkTreeViewColumn *tree_column,
		     GtkCellRenderer *cell,
		     GtkTreeModel *tree_model,
		     GtkTreeIter *iter,
		     gpointer data)
{
  struct nlp *nlp = NULL;
  struct autorecode *rd = data;

  struct variable *var = NULL;

  gtk_tree_model_get (tree_model, iter, 
		      0, &var,
		      -1);

  nlp = g_hash_table_lookup (rd->varmap, var);

  if ( nlp )
    g_object_set (cell, "text", nlp->name, NULL);
  else
    g_object_set (cell, "text", "", NULL);
}



/* Pops up the Autorecode dialog box */
void
autorecode_dialog (PsppireDataWindow *de)
{
  struct autorecode rd;
  gint response;

  GtkBuilder *xml = builder_new ("autorecode.ui");

  GtkWidget *dialog = get_widget_assert   (xml, "autorecode-dialog");
  GtkWidget *source = get_widget_assert   (xml, "dict-view");

  rd.var_view = get_widget_assert   (xml, "var-view");

  rd.new_name_entry = get_widget_assert (xml, "entry1");
  rd.change_button = get_widget_assert (xml, "button1");
  rd.ascending = get_widget_assert (xml, "radiobutton1");
  rd.group = get_widget_assert (xml, "checkbutton1");
  rd.blank = get_widget_assert (xml, "checkbutton2");

    {
      GtkTreeSelection *sel;

      GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

      GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes (_("New"),
									 renderer,
									 "text", NULL,
									 NULL);

      gtk_tree_view_column_set_cell_data_func (col, renderer,
					       render_new_var_name,
					       &rd, NULL);

      gtk_tree_view_append_column (GTK_TREE_VIEW (rd.var_view), col);


      col = gtk_tree_view_get_column (GTK_TREE_VIEW (rd.var_view), 0);

      g_object_set (col, "title", _("Old"), NULL);

      g_object_set (rd.var_view, "headers-visible", TRUE, NULL);

      rd.varmap = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, nlp_destroy);


      sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (rd.var_view));


      g_signal_connect (sel, "changed",
			G_CALLBACK (on_selection_change), &rd);

      g_signal_connect (rd.change_button, "clicked",
			G_CALLBACK (on_change_clicked),  &rd);

      g_signal_connect_swapped (rd.new_name_entry, "changed",
			G_CALLBACK (on_entry_change),  &rd);

    }


  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  g_object_get (de->data_editor, "dictionary", &rd.dict, NULL);
  g_object_set (source, "model", rd.dict, NULL);


  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &rd);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &rd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (de, generate_syntax (&rd)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (&rd)));
      break;
    default:
      break;
    }

  g_object_unref (xml);
}




static char *
generate_syntax (const struct autorecode *rd)
{
  GHashTableIter iter;
  gpointer key, value;
  gchar *text;
  GString *string = g_string_new ("AUTORECODE");

  g_string_append (string, "\n\tVARIABLES =");

  g_hash_table_iter_init (&iter, rd->varmap);
  while (g_hash_table_iter_next (&iter, &key, &value)) 
  {
    struct variable *var = key;
    g_string_append (string, " ");
    g_string_append (string, var_get_name (var));
  }

  g_string_append (string, " INTO");

  g_hash_table_iter_init (&iter, rd->varmap);
  while (g_hash_table_iter_next (&iter, &key, &value)) 
  {
    struct nlp *nlp  = value;
    g_string_append (string, " ");
    g_string_append (string, nlp->name);
  }

  if ( ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->ascending)))
    g_string_append (string, "\n\t/DESCENDING");

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->group)))
    g_string_append (string, "\n\t/GROUP");

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->blank)))
    g_string_append (string, "\n\t/BLANK");

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}
