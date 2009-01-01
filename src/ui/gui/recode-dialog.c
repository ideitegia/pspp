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

/* This module implements the RECODE dialog.

   It has two forms.  One for recoding values into the same variable.
   The second for recoding into different variables.
*/

#include <config.h>

#include "recode-dialog.h"

#include <gtk/gtk.h>

#include <language/syntax-string-source.h>
#include <ui/gui/psppire-data-window.h>
#include <ui/gui/dialog-common.h>
#include <ui/gui/dict-display.h>
#include <ui/gui/helper.h>
#include <ui/gui/psppire-dialog.h>
#include <ui/gui/psppire-var-store.h>
#include <ui/gui/helper.h>
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
    copy->v.s = strdup (nv->v.s);

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
	gchar *text = g_strdup_printf ("%g", nv->v.v);
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





/* A boxed type representing a value, or a range of values which may
   potentially be replaced by something */

enum old_value_type
 {
   OV_NUMERIC,
   OV_STRING,
   OV_SYSMIS,
   OV_MISSING,
   OV_RANGE,
   OV_LOW_UP,
   OV_HIGH_DOWN,
   OV_ELSE
 };

struct old_value
 {
   enum old_value_type type;
   union {
     double v;
     gchar *s;
     double range[2];
   } v;
 };


static struct old_value *
old_value_copy (struct old_value *ov)
{
  struct old_value *copy = g_memdup (ov, sizeof (*copy));

  if ( ov->type == OV_STRING )
    copy->v.s = g_strdup (ov->v.s);

  return copy;
}


static void
old_value_free (struct old_value *ov)
{
  if (ov->type == OV_STRING)
    g_free (ov->v.s);
  g_free (ov);
}

static void
old_value_to_string (const GValue *src, GValue *dest)
{
  const struct old_value *ov = g_value_get_boxed (src);

  switch (ov->type)
    {
    case OV_NUMERIC:
      {
	gchar *text = g_strdup_printf ("%g", ov->v.v);
	g_value_set_string (dest, text);
	g_free (text);
      }
      break;
    case OV_STRING:
      g_value_set_string (dest, ov->v.s);
      break;
    case OV_MISSING:
      g_value_set_string (dest, "MISSING");
      break;
    case OV_SYSMIS:
      g_value_set_string (dest, "SYSMIS");
      break;
    case OV_ELSE:
      g_value_set_string (dest, "ELSE");
      break;
    case OV_RANGE:
      {
	gchar *text;
	char en_dash[6] = {0,0,0,0,0,0};

	g_unichar_to_utf8 (0x2013, en_dash);

	text = g_strdup_printf ("%g %s %g",
				       ov->v.range[0],
				       en_dash,
				       ov->v.range[1]);
	g_value_set_string (dest, text);
	g_free (text);
      }
      break;
    case OV_LOW_UP:
      {
	gchar *text;
	char en_dash[6] = {0,0,0,0,0,0};

	g_unichar_to_utf8 (0x2013, en_dash);

	text = g_strdup_printf ("LOWEST %s %g",
				en_dash,
				ov->v.range[1]);

	g_value_set_string (dest, text);
	g_free (text);
      }
      break;
    case OV_HIGH_DOWN:
      {
	gchar *text;
	char en_dash[6] = {0,0,0,0,0,0};

	g_unichar_to_utf8 (0x2013, en_dash);

	text = g_strdup_printf ("%g %s HIGHEST",
				ov->v.range[0],
				en_dash);

	g_value_set_string (dest, text);
	g_free (text);
      }
      break;
    default:
      g_warning ("Invalid type in old recode value");
      g_value_set_string (dest, "???");
      break;
    };
}

static GType
old_value_get_type (void)
{
  static GType t = 0;

  if (t == 0 )
    {
      t = g_boxed_type_register_static  ("psppire-recode-old-values",
					 (GBoxedCopyFunc) old_value_copy,
					 (GBoxedFreeFunc) old_value_free);

      g_value_register_transform_func     (t, G_TYPE_STRING,
					   old_value_to_string);
    }

  return t;
}



enum
  {
    BUTTON_NEW_VALUE,
    BUTTON_NEW_COPY,
    BUTTON_NEW_SYSMIS,
    BUTTON_OLD_VALUE,
    BUTTON_OLD_SYSMIS,
    BUTTON_OLD_MISSING,
    BUTTON_OLD_RANGE,
    BUTTON_OLD_LOW_UP,
    BUTTON_OLD_HIGH_DOWN,
    BUTTON_OLD_ELSE,
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

  GtkWidget *ov_value_entry;
  GtkWidget *new_value_entry;

  GtkWidget *ov_range_lower_entry;
  GtkWidget *ov_range_upper_entry;
  GtkWidget *ov_low_up_entry;
  GtkWidget *ov_high_down_entry;

  GtkListStore *value_map;

  /* Indicates that the INTO {new variables} form of the dialog
     is being used */
  gboolean different;

  PsppireAcr *acr;

  gboolean input_var_is_string;

  GtkListStore *var_map;
  GtkWidget *new_name_entry;
  GtkWidget *new_label_entry;
  GtkWidget *change_button;

  GtkWidget *string_button;
  GtkWidget *width_entry;
};


static void run_old_and_new_dialog (struct recode_dialog *rd);

static void
refresh (PsppireDialog *dialog, struct recode_dialog *rd)
{
  gtk_widget_set_sensitive (rd->change_button, FALSE);
  gtk_widget_set_sensitive (rd->new_name_entry, FALSE);
  gtk_widget_set_sensitive (rd->new_label_entry, FALSE);


  if ( rd->different )
    gtk_list_store_clear (GTK_LIST_STORE (rd->var_map));
  else
    {
      GtkTreeModel *vars =
	gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));

      gtk_list_store_clear (GTK_LIST_STORE (vars));
    }

  gtk_list_store_clear (GTK_LIST_STORE (rd->value_map));
}

static char * generate_syntax (const struct recode_dialog *rd);

enum {
  COL_OLD,
  COL_NEW_NAME,
  COL_NEW_LABEL,
  n_COL_VARS
};

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
      GtkTreeIter iter;

      gboolean ok;

      for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (rd->var_map),
					       &iter);
	   ok;
	   ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (rd->var_map),
					  &iter))
	{
	  gchar *name = NULL;

	  gtk_tree_model_get (GTK_TREE_MODEL (rd->var_map), &iter,
			      COL_NEW_NAME, &name, -1);

	  if ( name == NULL )
	    return FALSE;

	  g_free (name);
	}
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
    (GTK_TOGGLE_BUTTON (rd->toggle[BUTTON_OLD_VALUE]), TRUE);

  g_signal_emit_by_name (rd->toggle[BUTTON_OLD_VALUE], "toggled");

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
recode_same_dialog (GObject *o, gpointer data)
{
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);

  recode_dialog (de, FALSE);
}

/* Pops up the Recode Different version of the dialog box */
void
recode_different_dialog (GObject *o, gpointer data)
{
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);

  recode_dialog (de, TRUE);
}

static void
render_new_var_name (GtkTreeViewColumn *tree_column,
		     GtkCellRenderer *cell,
		     GtkTreeModel *tree_model,
		     GtkTreeIter *iter,
		     gpointer data)
{
  gchar *new_var_name = NULL;

  gtk_tree_model_get (tree_model, iter, COL_NEW_NAME, &new_var_name, -1);

  g_object_set (cell, "text", new_var_name, NULL);

  g_free (new_var_name);
}


/* This might need to be changed to something less naive.
   In particular, what happends with dates, etc?
 */
static gchar *
num_to_string (gdouble x)
{
  return g_strdup_printf ("%g", x);
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

  if ( ov )
    {
    switch (ov->type)
      {
      case OV_STRING:
	  gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_OLD_VALUE]), TRUE);

	  gtk_entry_set_text (GTK_ENTRY (rd->ov_value_entry), ov->v.s);
	break;

      case OV_NUMERIC:
	{
	  gchar *str;

	  gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_OLD_VALUE]), TRUE);

	  str = num_to_string (ov->v.v);

	  gtk_entry_set_text (GTK_ENTRY (rd->ov_value_entry), str);

	  g_free (str);
	}
	break;

      case OV_SYSMIS:
	gtk_toggle_button_set_active
	  (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_OLD_SYSMIS]), TRUE);
	break;

      case OV_MISSING:
	gtk_toggle_button_set_active
	  (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_OLD_MISSING]), TRUE);
	break;

      case OV_RANGE:
	{
	  gchar *str;

	  gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_OLD_RANGE]), TRUE);

	  str = num_to_string (ov->v.range[0]);

	  gtk_entry_set_text (GTK_ENTRY (rd->ov_range_lower_entry), str);

	  g_free (str);


	  str = num_to_string (ov->v.range[1]);

	  gtk_entry_set_text (GTK_ENTRY (rd->ov_range_upper_entry), str);

	  g_free (str);
	}
	break;

      case OV_LOW_UP:
	{
	  gchar *str = num_to_string (ov->v.range[1]);

	  gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_OLD_LOW_UP]), TRUE);

	  gtk_entry_set_text (GTK_ENTRY (rd->ov_low_up_entry), str);

	  g_free (str);
	}
	break;

      case OV_HIGH_DOWN:
	{
	  gchar *str = num_to_string (ov->v.range[0]);

	  gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_OLD_HIGH_DOWN]), TRUE);

	  gtk_entry_set_text (GTK_ENTRY (rd->ov_high_down_entry), str);

	  g_free (str);
	}
	break;

      case OV_ELSE:
	gtk_toggle_button_set_active
	  (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_OLD_ELSE]), TRUE);
	break;

      default:
	g_warning ("Unknown old value type");
	break;
      };
    g_value_unset (&ov_value);
    }
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
  GtkTreeModel *model = GTK_TREE_MODEL (rd->var_map);

  GList *rows = gtk_tree_selection_get_selected_rows (selection, &model);

  if ( rows && !rows->next)
    {
      /* Exactly one row is selected */

      gboolean ok;
      GtkTreeIter iter;
      gchar *name = NULL;
      gchar *label = NULL;

      gtk_widget_set_sensitive  (rd->change_button, TRUE);
      gtk_widget_set_sensitive  (rd->new_name_entry, TRUE);
      gtk_widget_set_sensitive  (rd->new_label_entry, TRUE);

      ok = gtk_tree_model_get_iter (model, &iter, (GtkTreePath*) rows->data);

      gtk_tree_model_get (GTK_TREE_MODEL (rd->var_map), &iter,
			  COL_NEW_NAME, &name,
			  COL_NEW_LABEL, &label,
			  -1);

      gtk_entry_set_text (GTK_ENTRY (rd->new_name_entry), name ? name : "");
      gtk_entry_set_text (GTK_ENTRY (rd->new_label_entry), label ? label : "");

      g_free (name);
      g_free (label);
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
  GtkTreeModel *model = GTK_TREE_MODEL (rd->var_map);
  GtkTreeIter iter;
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (rd->variable_treeview));

  GList *rows = gtk_tree_selection_get_selected_rows (selection, &model);

  const gchar *dest_var_name =
    gtk_entry_get_text (GTK_ENTRY (rd->new_name_entry));

  const gchar *dest_var_label =
    gtk_entry_get_text (GTK_ENTRY (rd->new_label_entry));

  if ( NULL == rows )
    return;

  gtk_tree_model_get_iter (model, &iter, rows->data);

  gtk_list_store_set (rd->var_map, &iter,
		      COL_NEW_NAME, dest_var_name,
		      COL_NEW_LABEL, dest_var_label,
		      -1);

  g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (rows);
}


/* If there's nothing selected in the variable treeview,
   then automatically select the first item */
static void
select_something (GtkTreeModel *treemodel,
		  GtkTreePath  *arg1,
		  GtkTreeIter  *arg2,
		  gpointer      data)
{
  struct recode_dialog *rd = data;
  GtkTreeSelection *sel;

  sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (rd->variable_treeview));

  if ( gtk_tree_selection_count_selected_rows (sel) < 1)
    {
      GtkTreeIter iter;

      gtk_tree_model_get_iter_first   (treemodel, &iter);

      gtk_tree_selection_select_iter  (sel, &iter);
    }
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
recode_dialog (PsppireDataWindow *de, gboolean diff)
{
  gint response;

  struct recode_dialog rd;

  GladeXML *xml = XML_NEW ("recode.glade");


  GtkWidget *selector = get_widget_assert (xml, "psppire-selector1");

  GtkWidget *oldandnew = get_widget_assert (xml, "button1");


  GtkWidget *output_variable_box = get_widget_assert (xml,"frame4");


  PsppireVarStore *vs = NULL;

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  rd.change_button = get_widget_assert (xml, "change-button");

  rd.dialog = get_widget_assert   (xml, "recode-dialog");
  rd.dict_treeview = get_widget_assert (xml, "treeview1");
  rd.variable_treeview =   get_widget_assert (xml, "treeview2");
  rd.new_name_entry = get_widget_assert (xml, "dest-name-entry");
  rd.new_label_entry = get_widget_assert (xml, "dest-label-entry");

  rd.dict = vs->dict;

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


  attach_dictionary_to_treeview (GTK_TREE_VIEW (rd.dict_treeview),
				 vs->dict,
				 GTK_SELECTION_MULTIPLE, NULL);


  if ( ! rd.different )
    {
      set_dest_model (GTK_TREE_VIEW (rd.variable_treeview), vs->dict);
    }
  else
    {
      GtkTreeSelection *sel;
      GtkTreeViewColumn *col;
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

      rd.var_map = gtk_list_store_new (n_COL_VARS, G_TYPE_INT,
						    G_TYPE_STRING,
						    G_TYPE_STRING);



      gtk_tree_view_set_model (GTK_TREE_VIEW (rd.variable_treeview),
			       GTK_TREE_MODEL (rd.var_map));

      col = gtk_tree_view_column_new_with_attributes (_("Old"),
						  renderer,
						  "text", NULL,
						  NULL);

      gtk_tree_view_column_set_cell_data_func (col, renderer,
					       cell_var_name,
					       vs->dict, 0);


      gtk_tree_view_append_column (GTK_TREE_VIEW (rd.variable_treeview), col);


      renderer = gtk_cell_renderer_text_new ();

      col = gtk_tree_view_column_new_with_attributes (_("New"),
						  renderer,
						  "text", NULL,
						  NULL);

      gtk_tree_view_column_set_cell_data_func (col, renderer,
					       render_new_var_name,
					       NULL, NULL);


      gtk_tree_view_append_column (GTK_TREE_VIEW (rd.variable_treeview), col);

      g_object_set (rd.variable_treeview, "headers-visible", TRUE, NULL);

      g_signal_connect (rd.change_button, "clicked",
			G_CALLBACK (on_change_clicked),  &rd);

      sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (rd.variable_treeview));
      g_signal_connect (sel, "changed",
			G_CALLBACK (on_selection_change), &rd);

      g_signal_connect (rd.var_map, "row-inserted",
			G_CALLBACK (select_something), &rd);
    }



  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector),
				 rd.dict_treeview,
				 rd.variable_treeview,
				 insert_source_row_into_tree_view,
				 NULL,
				 NULL);

  psppire_selector_set_allow (PSPPIRE_SELECTOR (selector), homogeneous_types);

  /* Set up the Old & New Values subdialog */
  {
    rd.string_button = get_widget_assert (xml, "checkbutton1");
    rd.width_entry   = get_widget_assert (xml, "spinbutton1");

    rd.convert_button           = get_widget_assert (xml, "checkbutton2");

    rd.ov_range_lower_entry = get_widget_assert (xml, "entry5");
    rd.ov_range_upper_entry  = get_widget_assert (xml, "entry3");
    rd.ov_low_up_entry       = get_widget_assert (xml, "entry6");
    rd.ov_high_down_entry    = get_widget_assert (xml, "entry7");

    rd.new_value_entry = get_widget_assert (xml, "entry1");
    rd.ov_value_entry  = get_widget_assert (xml, "entry2");

    rd.toggle[BUTTON_NEW_VALUE]  = get_widget_assert (xml, "radiobutton1");
    rd.toggle[BUTTON_NEW_SYSMIS] = get_widget_assert (xml, "radiobutton2");
    rd.toggle[BUTTON_NEW_COPY]   = get_widget_assert (xml, "radiobutton3");
    rd.toggle[BUTTON_OLD_VALUE]  = get_widget_assert (xml, "radiobutton4");
    rd.toggle[BUTTON_OLD_SYSMIS] = get_widget_assert (xml, "radiobutton6");
    rd.toggle[BUTTON_OLD_MISSING]= get_widget_assert (xml, "radiobutton7");
    rd.toggle[BUTTON_OLD_RANGE]  = get_widget_assert (xml, "radiobutton8");
    rd.toggle[BUTTON_OLD_LOW_UP] = get_widget_assert (xml, "radiobutton10");
    rd.toggle[BUTTON_OLD_HIGH_DOWN] = get_widget_assert (xml, "radiobutton5");
    rd.toggle[BUTTON_OLD_ELSE]   = get_widget_assert (xml, "radiobutton11");

    rd.new_copy_label = get_widget_assert (xml, "label3");
    rd.strings_box    = get_widget_assert (xml, "table3");

    rd.old_and_new_dialog =
      PSPPIRE_DIALOG (get_widget_assert (xml, "old-new-values-dialog"));

    gtk_window_set_transient_for (GTK_WINDOW (rd.old_and_new_dialog),
				  GTK_WINDOW (de));

    rd.acr = PSPPIRE_ACR (get_widget_assert (xml, "psppire-acr1"));

    g_signal_connect_swapped (rd.toggle[BUTTON_NEW_VALUE], "toggled",
		      G_CALLBACK (set_acr), &rd);

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

    g_signal_connect (rd.toggle[BUTTON_OLD_VALUE], "toggled",
		      G_CALLBACK (toggle_sensitivity), rd.ov_value_entry);

    g_signal_connect (rd.toggle[BUTTON_OLD_RANGE], "toggled",
		      G_CALLBACK (toggle_sensitivity),
		      get_widget_assert (xml, "entry3"));

    g_signal_connect (rd.toggle[BUTTON_OLD_RANGE], "toggled",
		      G_CALLBACK (toggle_sensitivity),
		      get_widget_assert (xml, "entry5"));

    g_signal_connect (rd.toggle[BUTTON_OLD_LOW_UP], "toggled",
		      G_CALLBACK (toggle_sensitivity), rd.ov_low_up_entry);

    g_signal_connect (rd.toggle[BUTTON_OLD_HIGH_DOWN], "toggled",
		      G_CALLBACK (toggle_sensitivity), rd.ov_high_down_entry);

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
      {
	gchar *syntax = generate_syntax (&rd);

	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (&rd);
        paste_syntax_in_new_window (syntax);

	g_free (syntax);
      }
      break;
    default:
      break;
    }


  gtk_list_store_clear (GTK_LIST_STORE (rd.value_map));
  g_object_unref (rd.value_map);

  g_object_unref (xml);
}

/* Initialise VAL to reflect the current status of RD */
static gboolean
set_old_value (GValue *val, const struct recode_dialog *rd)
{
  const gchar *text = NULL;
  struct old_value ov;
  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
				     (rd->toggle [BUTTON_OLD_VALUE])))
    {
      text = gtk_entry_get_text (GTK_ENTRY (rd->ov_value_entry));
      if ( rd->input_var_is_string )
	{
	  ov.type = OV_STRING;
	  ov.v.s = g_strdup (text);
	}
      else
	{
	  ov.type = OV_NUMERIC;
	  ov.v.v = g_strtod (text, 0);
	}
    }
  else if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
					  (rd->toggle [BUTTON_OLD_MISSING])))
    {
      ov.type = OV_MISSING;
    }
  else if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
					  (rd->toggle [BUTTON_OLD_SYSMIS])))
    {
      ov.type = OV_SYSMIS;
    }
  else if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
					  (rd->toggle [BUTTON_OLD_ELSE])))
    {
      ov.type = OV_ELSE;
    }
  else if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
					  (rd->toggle [BUTTON_OLD_RANGE])))
    {
      const gchar *text;
      text = gtk_entry_get_text (GTK_ENTRY (rd->ov_range_lower_entry));

      ov.type = OV_RANGE;
      ov.v.range[0] = g_strtod (text, 0);

      text = gtk_entry_get_text (GTK_ENTRY (rd->ov_range_upper_entry));
      ov.v.range[1] = g_strtod (text, 0);
    }
  else if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
					  (rd->toggle [BUTTON_OLD_LOW_UP])))
    {
      const gchar *text =
	gtk_entry_get_text (GTK_ENTRY (rd->ov_low_up_entry));

      ov.type = OV_LOW_UP;
      ov.v.range[1] = g_strtod (text, 0);
    }
  else if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
					  (rd->toggle [BUTTON_OLD_HIGH_DOWN])))
    {
      const gchar *text =
	gtk_entry_get_text (GTK_ENTRY (rd->ov_high_down_entry));

      ov.type = OV_HIGH_DOWN;
      ov.v.range[0] = g_strtod (text, 0);
    }
  else
    return FALSE;

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
gboolean
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
    gint idx;
    GtkTreeIter iter;
    GtkTreeModel *model =
      gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));

    gboolean not_empty = gtk_tree_model_get_iter_first (model, &iter);

    g_return_if_fail (not_empty);

    gtk_tree_model_get (model, &iter, 0, &idx, -1);

    v = psppire_dict_get_variable (rd->dict, idx);

    rd->input_var_is_string = var_is_alpha (v);

    gtk_widget_set_sensitive (rd->toggle [BUTTON_OLD_SYSMIS],
			      var_is_numeric (v));
    gtk_widget_set_sensitive (rd->toggle [BUTTON_OLD_RANGE],
			      var_is_numeric (v));
    gtk_widget_set_sensitive (rd->toggle [BUTTON_OLD_LOW_UP],
			      var_is_numeric (v));
    gtk_widget_set_sensitive (rd->toggle [BUTTON_OLD_HIGH_DOWN],
			      var_is_numeric (v));
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
new_value_append_syntax (GString *str, const struct new_value *nv)
{
  switch (nv->type)
    {
    case NV_NUMERIC:
      g_string_append_printf (str, "%g", nv->v.v);
      break;
    case NV_STRING:
      {
	struct string ds = DS_EMPTY_INITIALIZER;
	syntax_gen_string (&ds, ss_cstr (nv->v.s));
	g_string_append (str, ds_cstr (&ds));
	ds_destroy (&ds);
      }
      break;
    case NV_COPY:
      g_string_append (str, "COPY");
      break;
    case NV_SYSMIS:
      g_string_append (str, "SYSMIS");
      break;
    default:
      /* Shouldn't ever happen */
      g_warning ("Invalid type in new recode value");
      g_string_append (str, "???");
      break;
    }
}


/* Generate a syntax fragment for NV and append it to STR */
static void
old_value_append_syntax (GString *str, const struct old_value *ov)
{
  switch (ov->type)
    {
    case OV_NUMERIC:
      g_string_append_printf (str, "%g", ov->v.v);
      break;
    case OV_STRING:
      {
	struct string ds = DS_EMPTY_INITIALIZER;
	syntax_gen_string (&ds, ss_cstr (ov->v.s));
	g_string_append (str, ds_cstr (&ds));
	ds_destroy (&ds);
      }
      break;
    case OV_MISSING:
      g_string_append (str, "MISSING");
      break;
    case OV_SYSMIS:
      g_string_append (str, "SYSMIS");
      break;
    case OV_ELSE:
      g_string_append (str, "ELSE");
      break;
    case OV_RANGE:
      g_string_append_printf (str, "%g THRU %g",
			      ov->v.range[0],
			      ov->v.range[1]);
      break;
    case OV_LOW_UP:
      g_string_append_printf (str, "LOWEST THRU %g",
			      ov->v.range[1]);
      break;
    case OV_HIGH_DOWN:
      g_string_append_printf (str, "%g THRU HIGHEST",
			      ov->v.range[0]);
      break;
    default:
      g_warning ("Invalid type in old recode value");
      g_string_append (str, "???");
      break;
    };
}



static char *
generate_syntax (const struct recode_dialog *rd)
{
  gboolean ok;
  GtkTreeIter iter;
  gchar *text;

  GString *str = g_string_sized_new (100);

  /* Declare new string variables if applicable */
  if ( rd->different &&
       gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->string_button)))
    {
      GtkTreeIter iter;


      for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (rd->var_map),
					       &iter);
	   ok;
	   ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (rd->var_map), &iter))
	{
	  gchar *name = NULL;

	  gtk_tree_model_get (GTK_TREE_MODEL (rd->var_map), &iter,
			      COL_NEW_NAME, &name, -1);

	  g_string_append (str, "\nSTRING ");
	  g_string_append (str, name);
	  g_string_append_printf (str, " (A%d).",
				  (int)
				  gtk_spin_button_get_value (GTK_SPIN_BUTTON (rd->width_entry) )
				  );

	  g_free (name);
	}
    }

  g_string_append (str, "\nRECODE ");

  append_variable_names (str, rd->dict, GTK_TREE_VIEW (rd->variable_treeview), 0);

  g_string_append (str, "\n\t");

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->convert_button)))
    {
      g_string_append (str, "(CONVERT) ");
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

      g_string_append (str, "(");

      old_value_append_syntax (str, ov);
      g_string_append (str, " = ");
      new_value_append_syntax (str, nv);

      g_string_append (str, ") ");
      g_value_unset (&ov_value);
      g_value_unset (&nv_value);
    }


  if ( rd->different )
    {
      GtkTreeIter iter;
      g_string_append (str, "\n\tINTO ");

      for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (rd->var_map),
					       &iter);
	   ok;
	   ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (rd->var_map), &iter))
	{
	  gchar *name = NULL;

	  gtk_tree_model_get (GTK_TREE_MODEL (rd->var_map), &iter,
			      COL_NEW_NAME, &name, -1);

	  g_string_append (str, name);
	  g_string_append (str, " ");

	  g_free (name);
	}
    }

  g_string_append (str, ".");


  /* If applicable, set labels for the new variables. */
  if ( rd->different )
    {
      GtkTreeIter iter;

      for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (rd->var_map),
					       &iter);
	   ok;
	   ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (rd->var_map), &iter))
	{
	  struct string ls;
	  gchar *label = NULL;
	  gchar *name = NULL;

	  gtk_tree_model_get (GTK_TREE_MODEL (rd->var_map), &iter,
			      COL_NEW_NAME, &name,
			      COL_NEW_LABEL, &label, -1);

	  if ( 0 == strcmp (label, "") )
	    {
	      g_free (name);
	      g_free (label);
	      continue;
	    }

	  ds_init_empty (&ls);
	  syntax_gen_string (&ls, ss_cstr (label));
	  g_free (label);

	  g_string_append_printf (str, "\nVARIABLE LABELS %s %s.",
				  name, ds_cstr (&ls));

	  g_free (name);
	  ds_destroy (&ls);
	}
    }


  g_string_append (str, "\nEXECUTE.\n");


  text = str->str;

  g_string_free (str, FALSE);

  return text;
}
