/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2009, 2010, 2011, 2012, 2014  Free Software Foundation

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

/* This module implements the RECODE dialog.

   It has two forms.  One for recoding values into the same variable.
   The second for recoding into different variables.
*/

#include <config.h>

#include "recode-dialog.h"

#include "executor.h"

#include "psppire-var-view.h"

#include <gtk/gtk.h>

#include <float.h>
#include <xalloc.h>
#include <ui/gui/psppire-data-window.h>
#include <ui/gui/dialog-common.h>
#include <ui/gui/dict-display.h>
#include <ui/gui/builder-wrapper.h>
#include "helper.h"
#include <ui/gui/psppire-dialog.h>

#include "psppire-val-chooser.h"

#include <ui/syntax-gen.h>

#include "psppire-acr.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


/* Define a boxed type to represent a value which is a candidate
   to replace an existing value */

enum new_value_type
 {
   NV_NUMERIC,
   NV_STRING,
   NV_SYSMIS,
   NV_COPY
 };


struct new_value
{
  enum new_value_type type;
  union {
    double v;
    gchar *s;
  } v;
};


static struct new_value *
new_value_copy (struct new_value *nv)
{
  struct new_value *copy = g_memdup (nv, sizeof (*copy));

  if ( nv->type == NV_STRING )
    copy->v.s = xstrdup (nv->v.s);

  return copy;
}


static void
new_value_free (struct new_value *nv)
{
  if ( nv->type == NV_STRING )
    g_free (nv->v.s);

  g_free (nv);
}


static void
new_value_to_string (const GValue *src, GValue *dest)
{
  const struct new_value *nv = g_value_get_boxed (src);

  g_assert (nv);

  switch (nv->type)
    {
    case NV_NUMERIC:
      {
	gchar *text = g_strdup_printf ("%.*g", DBL_DIG + 1, nv->v.v);
	g_value_set_string (dest, text);
	g_free (text);
      }
      break;
    case NV_STRING:
      g_value_set_string (dest, nv->v.s);
      break;
    case NV_COPY:
      g_value_set_string (dest, "COPY");
      break;
    case NV_SYSMIS:
      g_value_set_string (dest, "SYSMIS");
      break;
    default:
      /* Shouldn't ever happen */
      g_warning ("Invalid type in new recode value");
      g_value_set_string (dest, "???");
      break;
    }
}

static GType
new_value_get_type (void)
{
  static GType t = 0;

  if (t == 0 )
    {
      t = g_boxed_type_register_static  ("psppire-recode-new-values",
					 (GBoxedCopyFunc) new_value_copy,
					 (GBoxedFreeFunc) new_value_free);

      g_value_register_transform_func (t, G_TYPE_STRING,
				       new_value_to_string);
    }

  return t;
}




enum
  {
    BUTTON_NEW_VALUE,
    BUTTON_NEW_COPY,
    BUTTON_NEW_SYSMIS,
    n_BUTTONS
  };

struct recode_dialog
{
  PsppireDict *dict;

  GtkWidget *dialog;
  PsppireDialog *old_and_new_dialog;

  GtkWidget *dict_treeview;
  GtkWidget *variable_treeview;
  GtkWidget *toggle[n_BUTTONS];

  GtkWidget *strings_box;
  GtkWidget *convert_button;
  GtkWidget *new_copy_label;

  GtkWidget *new_value_entry;

  GtkWidget *old_value_chooser;

  GtkListStore *value_map;

  /* Indicates that the INTO {new variables} form of the dialog
     is being used */
  gboolean different;

  PsppireAcr *acr;

  gboolean input_var_is_string;

  GtkWidget *new_name_entry;
  GtkWidget *new_label_entry;
  GtkWidget *change_button;

  GtkWidget *string_button;
  GtkWidget *width_entry;

  /* A hash table of struct nlp's indexed by variable */
  GHashTable *varmap;
};


static void run_old_and_new_dialog (struct recode_dialog *rd);

static void
refresh (PsppireDialog *dialog, struct recode_dialog *rd)
{
  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));

  gtk_list_store_clear (GTK_LIST_STORE (vars));

  gtk_widget_set_sensitive (rd->change_button, FALSE);
  gtk_widget_set_sensitive (rd->new_name_entry, FALSE);
  gtk_widget_set_sensitive (rd->new_label_entry, FALSE);

  if ( rd->different && rd->varmap )
    g_hash_table_remove_all (rd->varmap);

  gtk_list_store_clear (GTK_LIST_STORE (rd->value_map));
}

static char * generate_syntax (const struct recode_dialog *rd);

enum {
  COL_VALUE_OLD,
  COL_VALUE_NEW,
  n_COL_VALUES
};

/* Dialog is valid iff at least one variable has been selected,
   AND the list of mappings is not empty.
 */
static gboolean
dialog_state_valid (gpointer data)
{
  GtkTreeIter not_used;
  struct recode_dialog *rd = data;

  if ( ! rd->value_map )
    return FALSE;

  if ( ! gtk_tree_model_get_iter_first (GTK_TREE_MODEL (rd->value_map),
					&not_used) )
    return FALSE;

  if ( rd->different )
    {
      GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));

      if (g_hash_table_size (rd->varmap) != gtk_tree_model_iter_n_children (model, NULL) )
	return FALSE;
    }
  else
    {
      GtkTreeModel *vars =
	gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));

      if ( !gtk_tree_model_get_iter_first (vars, &not_used))
	return FALSE;
    }

  return TRUE;
}

static void
on_old_new_show (struct recode_dialog *rd)
{
  gtk_toggle_button_set_active
    (GTK_TOGGLE_BUTTON (rd->toggle[BUTTON_NEW_VALUE]), TRUE);

  g_signal_emit_by_name (rd->toggle[BUTTON_NEW_VALUE], "toggled");

  g_object_set (rd->toggle[BUTTON_NEW_COPY],
		"visible", rd->different, NULL);

  g_object_set (rd->new_copy_label,
		"visible", rd->different, NULL);

  g_object_set (rd->strings_box,
		"visible", rd->different, NULL);
}

/* Sets the sensitivity of TARGET dependent upon the active status
   of BUTTON */
static void
toggle_sensitivity (GtkToggleButton *button, GtkWidget *target)
{
  gboolean state = gtk_toggle_button_get_active (button);

  /*  g_print ("%s Setting %p (%s) to %d because of %p\n",
      __FUNCTION__, target, gtk_widget_get_name (target), state, button); */

  gtk_widget_set_sensitive (target, state);
}

static void recode_dialog (PsppireDataWindow *de, gboolean diff);


/* Pops up the Recode Same version of the dialog box */
void
recode_same_dialog (PsppireDataWindow *de)
{
  recode_dialog (de, FALSE);
}

/* Pops up the Recode Different version of the dialog box */
void
recode_different_dialog (PsppireDataWindow *de)
{
  recode_dialog (de, TRUE);
}


/* This might need to be changed to something less naive.
   In particular, what happends with dates, etc?
 */
static gchar *
num_to_string (gdouble x)
{
  return g_strdup_printf ("%.*g", DBL_DIG + 1, x);
}

/* Callback which gets called when a new row is selected
   in the acr's variable treeview.
   We use if to set the togglebuttons and entries to correspond to the
   selected row.
*/
static void
on_acr_selection_change (GtkTreeSelection *selection, gpointer data)
{
  struct recode_dialog *rd = data;
  GtkTreeModel *model = NULL;
  GtkTreeIter iter;

  GValue ov_value = {0};
  GValue nv_value = {0};
  struct old_value *ov = NULL;
  struct new_value *nv = NULL;

  if ( ! gtk_tree_selection_get_selected (selection, &model, &iter) )
    return;


  gtk_tree_model_get_value (GTK_TREE_MODEL (model), &iter,
			    COL_VALUE_OLD, &ov_value);

  gtk_tree_model_get_value (GTK_TREE_MODEL (model), &iter,
			    COL_VALUE_NEW, &nv_value);

  ov = g_value_get_boxed (&ov_value);
  nv = g_value_get_boxed (&nv_value);

  if (nv)
    {
      switch (nv->type)
	{
	case NV_NUMERIC:
	  {
	    gchar *str = num_to_string (nv->v.v);

	    gtk_toggle_button_set_active
	      (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_VALUE]), TRUE);

	    gtk_entry_set_text (GTK_ENTRY (rd->new_value_entry), str);
	    g_free (str);
	  }
	  break;
	case NV_STRING:
	  gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_VALUE]), TRUE);

	  gtk_entry_set_text (GTK_ENTRY (rd->new_value_entry), nv->v.s);
	  break;
	case NV_SYSMIS:
	  gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_SYSMIS]), TRUE);

	  break;
	case NV_COPY:
	  gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_COPY]), TRUE);

	  break;
	default:
	  g_warning ("Invalid new value type");
	  break;
	}

      g_value_unset (&nv_value);
    }

  psppire_val_chooser_set_status (PSPPIRE_VAL_CHOOSER (rd->old_value_chooser), ov);
}

/* Name-Label pair */
struct nlp
{
  char *name;
  char *label;
};

static struct nlp *
nlp_create (const char *name, const char *label)
{
  struct nlp *nlp = xmalloc (sizeof *nlp);

  nlp->name = g_strdup (name);

  nlp->label = NULL;

  if ( 0 != strcmp ("", label))
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


/* Callback which gets called when a new row is selected
   in the variable treeview.
   It sets the name and label entry widgets to reflect the
   currently selected row.
 */
static void
on_selection_change (GtkTreeSelection *selection, gpointer data)
{
  struct recode_dialog *rd = data;

  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));

  GList *rows = gtk_tree_selection_get_selected_rows (selection, &model);

  if ( rows && !rows->next)
    {
      /* Exactly one row is selected */
      struct nlp *nlp;
      struct variable *var;
      gboolean ok;
      GtkTreeIter iter;

      gtk_widget_set_sensitive  (rd->change_button, TRUE);
      gtk_widget_set_sensitive  (rd->new_name_entry, TRUE);
      gtk_widget_set_sensitive  (rd->new_label_entry, TRUE);

      ok = gtk_tree_model_get_iter (model, &iter, (GtkTreePath*) rows->data);
      g_return_if_fail (ok);

      gtk_tree_model_get (model, &iter,
			  0, &var, 
			  -1);

      nlp = g_hash_table_lookup (rd->varmap, var);

      if (nlp)
	{
	  gtk_entry_set_text (GTK_ENTRY (rd->new_name_entry), nlp->name ? nlp->name : "");
	  gtk_entry_set_text (GTK_ENTRY (rd->new_label_entry), nlp->label ? nlp->label : "");
	}
      else
	{
	  gtk_entry_set_text (GTK_ENTRY (rd->new_name_entry), "");
	  gtk_entry_set_text (GTK_ENTRY (rd->new_label_entry), "");
	}
    }
  else
    {
      gtk_widget_set_sensitive  (rd->change_button, FALSE);
      gtk_widget_set_sensitive  (rd->new_name_entry, FALSE);
      gtk_widget_set_sensitive  (rd->new_label_entry, FALSE);

      gtk_entry_set_text (GTK_ENTRY (rd->new_name_entry), "");
      gtk_entry_set_text (GTK_ENTRY (rd->new_label_entry), "");
    }


  g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (rows);
}

static void
on_string_toggled (GtkToggleButton *b, struct recode_dialog *rd)
{
  gboolean active;
  if (! rd->input_var_is_string )
    return ;

  active = gtk_toggle_button_get_active (b);
  gtk_widget_set_sensitive (rd->convert_button, !active);
}


static void
on_convert_toggled (GtkToggleButton *b, struct recode_dialog *rd)
{
  gboolean active;

  g_return_if_fail (rd->input_var_is_string);

  active = gtk_toggle_button_get_active (b);
  gtk_widget_set_sensitive (rd->string_button, !active);
}

static void
on_change_clicked (GObject *obj, gpointer data)
{
  struct recode_dialog *rd = data;
  struct variable *var = NULL;
  struct nlp *nlp;

  GtkTreeModel *model =  gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));

  GtkTreeIter iter;
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (rd->variable_treeview));

  GList *rows = gtk_tree_selection_get_selected_rows (selection, &model);

  const gchar *dest_var_name =
    gtk_entry_get_text (GTK_ENTRY (rd->new_name_entry));

  const gchar *dest_var_label =
    gtk_entry_get_text (GTK_ENTRY (rd->new_label_entry));

  if ( NULL == rows || rows->next != NULL)
    goto finish;

  gtk_tree_model_get_iter (model, &iter, rows->data);

  gtk_tree_model_get (model, &iter, 0, &var, -1);

  g_hash_table_remove (rd->varmap, var);

  nlp = nlp_create (dest_var_name, dest_var_label);

  g_hash_table_insert (rd->varmap, var, nlp);

  gtk_tree_model_row_changed (model, rows->data, &iter);

 finish:
  g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (rows);
}


static void
focus_value_entry (GtkWidget *w, struct recode_dialog *rd)
{
  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)))
    gtk_widget_grab_focus (rd->new_value_entry);
}


/* Callback for the new_value_entry and new_value_togglebutton widgets.
   It's used to enable/disable the acr. */
static void
set_acr (struct recode_dialog *rd)
{
  const gchar *text;

  if ( !gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (rd->toggle[BUTTON_NEW_VALUE])))
    {
      psppire_acr_set_enabled (rd->acr, TRUE);
      return;
    }

  text = gtk_entry_get_text (GTK_ENTRY (rd->new_value_entry));

  psppire_acr_set_enabled (rd->acr, !g_str_equal (text, ""));
}

static void
render_new_var_name (GtkTreeViewColumn *tree_column,
		     GtkCellRenderer *cell,
		     GtkTreeModel *tree_model,
		     GtkTreeIter *iter,
		     gpointer data)
{
  struct nlp *nlp = NULL;
  struct recode_dialog *rd = data;

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



static void
recode_dialog (PsppireDataWindow *de, gboolean diff)
{
  gint response;

  struct recode_dialog rd;

  GtkBuilder *builder = builder_new ("recode.ui");

  GtkWidget *selector = get_widget_assert (builder, "psppire-selector1");

  GtkWidget *oldandnew = get_widget_assert (builder, "button1");


  GtkWidget *output_variable_box = get_widget_assert (builder,"frame4");

  rd.change_button = get_widget_assert (builder, "change-button");
  rd.varmap = NULL;
  rd.dialog = get_widget_assert   (builder, "recode-dialog");
  rd.dict_treeview = get_widget_assert (builder, "treeview1");
  rd.variable_treeview =   get_widget_assert (builder, "treeview2");
  rd.new_name_entry = get_widget_assert (builder, "dest-name-entry");
  rd.new_label_entry = get_widget_assert (builder, "dest-label-entry");

  g_object_get (de->data_editor, "dictionary", &rd.dict, NULL);

  rd.value_map = gtk_list_store_new (2,
				     old_value_get_type (),
				     new_value_get_type ()
				     );

  g_object_set (output_variable_box, "visible", diff, NULL);

  if ( diff )
    gtk_window_set_title (GTK_WINDOW (rd.dialog),
			  _("Recode into Different Variables"));
  else
    gtk_window_set_title (GTK_WINDOW (rd.dialog),
			  _("Recode into Same Variables"));

  rd.different = diff;

  gtk_window_set_transient_for (GTK_WINDOW (rd.dialog), GTK_WINDOW (de));

  g_object_set (rd.dict_treeview, "model", rd.dict, NULL);

  if (rd.different)
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


      gtk_tree_view_append_column (GTK_TREE_VIEW (rd.variable_treeview), col);


      col = gtk_tree_view_get_column (GTK_TREE_VIEW (rd.variable_treeview), 0);

      g_object_set (col, "title", _("Old"), NULL);

      g_object_set (rd.variable_treeview, "headers-visible", TRUE, NULL);

      rd.varmap = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, nlp_destroy);

      sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (rd.variable_treeview));

      g_signal_connect (sel, "changed",
			G_CALLBACK (on_selection_change), &rd);

      g_signal_connect (rd.change_button, "clicked",
			G_CALLBACK (on_change_clicked),  &rd);

    }

  psppire_selector_set_allow (PSPPIRE_SELECTOR (selector), homogeneous_types);

  /* Set up the Old & New Values subdialog */
  {
    rd.string_button = get_widget_assert (builder, "checkbutton1");
    rd.width_entry   = get_widget_assert (builder, "spinbutton1");

    rd.convert_button           = get_widget_assert (builder, "checkbutton2");

    rd.old_value_chooser = get_widget_assert (builder, "val-chooser");

    rd.new_value_entry = get_widget_assert (builder, "entry1");


    rd.toggle[BUTTON_NEW_VALUE]  = get_widget_assert (builder, "radiobutton1");
    rd.toggle[BUTTON_NEW_SYSMIS] = get_widget_assert (builder, "radiobutton2");
    rd.toggle[BUTTON_NEW_COPY]   = get_widget_assert (builder, "radiobutton3");

    rd.new_copy_label = get_widget_assert (builder, "label3");
    rd.strings_box    = get_widget_assert (builder, "table3");

    rd.old_and_new_dialog =
      PSPPIRE_DIALOG (get_widget_assert (builder, "old-new-values-dialog"));

    gtk_window_set_transient_for (GTK_WINDOW (rd.old_and_new_dialog),
				  GTK_WINDOW (de));

    rd.acr = PSPPIRE_ACR (get_widget_assert (builder, "psppire-acr1"));

    g_signal_connect_swapped (rd.toggle[BUTTON_NEW_VALUE], "toggled",
		      G_CALLBACK (set_acr), &rd);

    g_signal_connect_after (rd.toggle[BUTTON_NEW_VALUE], "toggled",
		      G_CALLBACK (focus_value_entry), &rd);

    g_signal_connect_swapped (rd.new_value_entry, "changed",
		      G_CALLBACK (set_acr), &rd);

    {
      GtkTreeSelection *sel;
      /* Remove the ACR's default column.  We don't like it */
      GtkTreeViewColumn *column = gtk_tree_view_get_column (rd.acr->tv, 0);
      gtk_tree_view_remove_column (rd.acr->tv, column);


      column =
	gtk_tree_view_column_new_with_attributes (_("Old"),
						  gtk_cell_renderer_text_new (),
						  "text", 0,
						  NULL);

      gtk_tree_view_append_column (rd.acr->tv, column);

      column =
	gtk_tree_view_column_new_with_attributes (_("New"),
						  gtk_cell_renderer_text_new (),
						  "text", 1,
						  NULL);

      gtk_tree_view_append_column (rd.acr->tv, column);
      g_object_set (rd.acr->tv, "headers-visible", TRUE, NULL);


      sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (rd.acr->tv));
      g_signal_connect (sel, "changed",
			G_CALLBACK (on_acr_selection_change), &rd);
    }


    g_signal_connect_swapped (oldandnew, "clicked",
			      G_CALLBACK (run_old_and_new_dialog), &rd);


    g_signal_connect (rd.toggle[BUTTON_NEW_VALUE], "toggled",
		      G_CALLBACK (toggle_sensitivity), rd.new_value_entry);

    g_signal_connect (rd.string_button, "toggled",
		      G_CALLBACK (toggle_sensitivity), rd.width_entry);

    g_signal_connect (rd.string_button, "toggled",
		      G_CALLBACK (on_string_toggled), &rd);

    g_signal_connect (rd.convert_button, "toggled",
		      G_CALLBACK (on_convert_toggled), &rd);

    g_signal_connect_swapped (rd.old_and_new_dialog, "show",
			      G_CALLBACK (on_old_new_show), &rd);
  }

  g_signal_connect (rd.dialog, "refresh", G_CALLBACK (refresh),  &rd);


  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (rd.dialog),
				      dialog_state_valid, &rd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (rd.dialog));

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

  if (rd.varmap)
    g_hash_table_destroy (rd.varmap);

  gtk_list_store_clear (GTK_LIST_STORE (rd.value_map));
  g_object_unref (rd.value_map);

  g_object_unref (builder);
}

/* Initialise VAL to reflect the current status of RD */
static gboolean
set_old_value (GValue *val, const struct recode_dialog *rd)
{
  PsppireValChooser *vc = PSPPIRE_VAL_CHOOSER (rd->old_value_chooser);

  struct old_value ov;

  psppire_val_chooser_get_status (vc, &ov);

  g_value_init (val, old_value_get_type ());
  g_value_set_boxed (val, &ov);

  return TRUE;
}


/* Initialse VAL to reflect the current status of RD */
static gboolean
set_new_value (GValue *val, const struct recode_dialog *rd)
{
  const gchar *text = NULL;
  struct new_value nv;

  if ( gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_VALUE])))
    {
      text = gtk_entry_get_text (GTK_ENTRY (rd->new_value_entry));

      nv.type = NV_NUMERIC;
      if (
	  (! rd->different && rd->input_var_is_string) ||
	  ( rd->different &&
	    gtk_toggle_button_get_active
	       (GTK_TOGGLE_BUTTON (rd->string_button)))
	  )
	{
	  nv.type = NV_STRING;
	}

      if ( nv.type == NV_STRING )
	nv.v.s = g_strdup (text);
      else
	nv.v.v = g_strtod (text, 0);
    }
  else if ( gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_COPY])))
    {
      nv.type = NV_COPY;
    }

  else if ( gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_SYSMIS])))
    {
      nv.type = NV_SYSMIS;
    }
  else
    return FALSE;

  g_value_init (val, new_value_get_type ());
  g_value_set_boxed (val, &nv);

  return TRUE;
}


/* A function to set a value in a column in the ACR */
static gboolean
set_value (gint col, GValue  *val, gpointer data)
{
  struct recode_dialog *rd = data;

  switch ( col )
    {
    case COL_VALUE_OLD:
      set_old_value (val, rd);
      break;
    case COL_VALUE_NEW:
      set_new_value (val, rd);
      break;
    default:
      return FALSE;
    }

  return TRUE;
}

static void
run_old_and_new_dialog (struct recode_dialog *rd)
{
  gint response;
  GtkListStore *local_store = clone_list_store (rd->value_map);

  psppire_acr_set_model (rd->acr, local_store);
  psppire_acr_set_get_value_func (rd->acr, set_value, rd);

  gtk_window_set_title (GTK_WINDOW (rd->old_and_new_dialog),
			rd->different
			? _("Recode into Different Variables: Old and New Values ")
			: _("Recode into Same Variables: Old and New Values")
			);


  {
    /* Find the type of the first variable (it's invariant that
       all variables are of the same type) */
    const struct variable *v;
    GtkTreeIter iter;
    GtkTreeModel *model =
      gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));

    gboolean not_empty = gtk_tree_model_get_iter_first (model, &iter);

    g_return_if_fail (not_empty);

    gtk_tree_model_get (model, &iter, 0, &v, -1);

    rd->input_var_is_string = var_is_alpha (v);

    g_object_set (rd->old_value_chooser, "is-string", rd->input_var_is_string, NULL);

    gtk_widget_set_sensitive (rd->toggle [BUTTON_NEW_SYSMIS],
			      var_is_numeric (v));

    gtk_widget_set_sensitive (rd->convert_button, var_is_alpha (v));
  }


  response = psppire_dialog_run (rd->old_and_new_dialog);
  psppire_acr_set_model (rd->acr, NULL);


  if ( response == PSPPIRE_RESPONSE_CONTINUE )
    {
      g_object_unref (rd->value_map);
      rd->value_map = clone_list_store (local_store);
    }
  else
    g_object_unref (local_store);


  psppire_dialog_notify_change (PSPPIRE_DIALOG (rd->dialog));
}


/* Generate a syntax fragment for NV and append it to STR */
static void
new_value_append_syntax (struct string *dds, const struct new_value *nv)
{
  switch (nv->type)
    {
    case NV_NUMERIC:
      ds_put_c_format (dds, "%.*g", DBL_DIG + 1, nv->v.v);
      break;
    case NV_STRING:
      syntax_gen_string (dds, ss_cstr (nv->v.s));
      break;
    case NV_COPY:
      ds_put_cstr (dds, "COPY");
      break;
    case NV_SYSMIS:
      ds_put_cstr (dds, "SYSMIS");
      break;
    default:
      /* Shouldn't ever happen */
      g_warning ("Invalid type in new recode value");
      ds_put_cstr (dds, "???");
      break;
    }
}


static char *
generate_syntax (const struct recode_dialog *rd)
{
  gboolean ok;
  GtkTreeIter iter;
  gchar *text;
  struct string dds;

  ds_init_empty (&dds);


  /* Declare new string variables if applicable */
  if ( rd->different &&
       gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->string_button)))
    {
      GHashTableIter iter;

      struct variable *var = NULL;
      struct nlp *nlp = NULL;

      g_hash_table_iter_init (&iter, rd->varmap);
      while (g_hash_table_iter_next (&iter, (void**) &var, (void**) &nlp))
	{
	  ds_put_cstr (&dds, "\nSTRING ");
	  ds_put_cstr (&dds, nlp->name);
	  ds_put_c_format (&dds, " (A%d).",
				  (int)
				  gtk_spin_button_get_value (GTK_SPIN_BUTTON (rd->width_entry) )
				  );
	}
    }

  ds_put_cstr (&dds, "\nRECODE ");

  psppire_var_view_append_names_str (PSPPIRE_VAR_VIEW (rd->variable_treeview), 0, &dds);

  ds_put_cstr (&dds, "\n\t");

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->convert_button)))
    {
      ds_put_cstr (&dds, "(CONVERT) ");
    }

  for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (rd->value_map),
					   &iter);
       ok;
       ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (rd->value_map), &iter))
    {
      GValue ov_value = {0};
      GValue nv_value = {0};
      struct old_value *ov;
      struct new_value *nv;
      gtk_tree_model_get_value (GTK_TREE_MODEL (rd->value_map), &iter,
				COL_VALUE_OLD, &ov_value);

      gtk_tree_model_get_value (GTK_TREE_MODEL (rd->value_map), &iter,
				COL_VALUE_NEW, &nv_value);

      ov = g_value_get_boxed (&ov_value);
      nv = g_value_get_boxed (&nv_value);

      ds_put_cstr (&dds, "(");

      old_value_append_syntax (&dds, ov);
      ds_put_cstr (&dds, " = ");
      new_value_append_syntax (&dds, nv);

      ds_put_cstr (&dds, ") ");
      g_value_unset (&ov_value);
      g_value_unset (&nv_value);
    }


  if ( rd->different )
    {

      GtkTreeIter iter;
      ds_put_cstr (&dds, "\n\tINTO ");

      for (ok = psppire_var_view_get_iter_first (PSPPIRE_VAR_VIEW (rd->variable_treeview), &iter);
	   ok;
	   ok = psppire_var_view_get_iter_next (PSPPIRE_VAR_VIEW (rd->variable_treeview), &iter))
	  {
	    struct nlp *nlp = NULL;
	    const struct variable *var = psppire_var_view_get_variable (PSPPIRE_VAR_VIEW (rd->variable_treeview), 0, &iter);

	    nlp = g_hash_table_lookup (rd->varmap, var);
	    
	    ds_put_cstr (&dds, nlp->name);
	    ds_put_cstr (&dds, " ");
	  }
    }

  ds_put_cstr (&dds, ".");

  /* If applicable, set labels for the new variables. */
  if ( rd->different )
    {
      GHashTableIter iter;

      struct variable *var = NULL;
      struct nlp *nlp = NULL;

      g_hash_table_iter_init (&iter, rd->varmap);
      while (g_hash_table_iter_next (&iter, (void**) &var, (void**) &nlp))
	{
	  if (nlp->label)
	    {
	      struct string sl;
	      ds_init_empty (&sl);
	      syntax_gen_string (&sl, ss_cstr (nlp->label));
	      ds_put_c_format (&dds, "\nVARIABLE LABELS %s %s.",
				      nlp->name, ds_cstr (&sl));

	      ds_destroy (&sl);
	    }
	}
    }

  ds_put_cstr (&dds, "\nEXECUTE.\n");


  text = ds_steal_cstr (&dds);

  ds_destroy (&dds);

  return text;
}
