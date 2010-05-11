/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009  Free Software Foundation

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
#include <language/syntax-string-source.h>
#include <ui/syntax-gen.h>
#include <libpspp/str.h>

#include "roc-dialog.h"
#include "psppire-selector.h"
#include "psppire-dictview.h"
#include "psppire-dialog.h"

#include "psppire-data-window.h"
#include "psppire-var-view.h"

#include "executor.h"
#include "helper.h"

#include <gtk/gtk.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


struct roc
{
  PsppireDict *dict;

  GtkWidget *test_variables;
  GtkWidget *state_variable;
  GtkWidget *state_value;

  GtkWidget *curve;
  GtkWidget *reference;
  GtkWidget *standard_error;
  GtkWidget *coordinates;
};


static char * generate_syntax (const struct roc *rd);


static void
refresh (struct roc *rd)
{
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (rd->test_variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_entry_set_text (GTK_ENTRY (rd->state_variable), "");
  gtk_entry_set_text (GTK_ENTRY (rd->state_value), "");

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->curve),          TRUE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->reference),      FALSE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->standard_error), FALSE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->coordinates),    FALSE);
}


static gboolean
dialog_state_valid (gpointer data)
{
  struct roc *rd = data;
  const gchar *text;

  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (rd->test_variables));

  if  (gtk_tree_model_iter_n_children (liststore, NULL) < 1)
    return FALSE;

  
  text = gtk_entry_get_text (GTK_ENTRY (rd->state_variable));
  if ( 0 == strcmp ("", text))
    return FALSE;


  text = gtk_entry_get_text (GTK_ENTRY (rd->state_value));
  if ( 0 == strcmp ("", text))
    return FALSE;


  return TRUE;
}

static void
on_curve_button_toggle  (GtkCheckButton *curve, struct roc *rd)
{
  if ( !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (curve)))
    {
      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->reference)))
	g_object_set (rd->reference, "inconsistent", TRUE, NULL);
      g_object_set (rd->reference, "sensitive", FALSE, NULL);
    }
  else 
    {
      g_object_set (rd->reference, "inconsistent", FALSE, NULL);
      g_object_set (rd->reference, "sensitive", TRUE, NULL);
    }
}


/* Pops up the Roc dialog box */
void
roc_dialog (PsppireDataWindow *de)
{
  struct roc rd;
  gint response;

  GtkBuilder *xml = builder_new ("roc.ui");
  PsppireVarStore *vs;

  GtkWidget *dialog = get_widget_assert   (xml, "roc-dialog");
  GtkWidget *source = get_widget_assert   (xml, "dict-view");

  rd.test_variables    = get_widget_assert   (xml, "psppire-var-view1");
  rd.state_variable    = get_widget_assert   (xml, "entry1");
  rd.state_value       = get_widget_assert   (xml, "entry2");

  rd.curve          = get_widget_assert   (xml, "curve");
  rd.reference      = get_widget_assert   (xml, "reference-line");
  rd.standard_error = get_widget_assert   (xml, "standard-error");
  rd.coordinates    = get_widget_assert   (xml, "co-ordinates");


  g_object_get (de->data_editor, "var-store", &vs, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  g_object_get (vs, "dictionary", &rd.dict, NULL);
  g_object_set (source, "model", rd.dict, NULL);

  g_signal_connect (rd.curve, "toggled", G_CALLBACK (on_curve_button_toggle), &rd);

  g_signal_connect_swapped (dialog, "refresh", G_CALLBACK (refresh),  &rd);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &rd);

  psppire_selector_set_allow (PSPPIRE_SELECTOR (get_widget_assert (xml, "dep-selector")),
			      numeric_only);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

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

  g_object_unref (xml);
}




static char *
generate_syntax (const struct roc *rd)
{
  gchar *text;
  const gchar *var_name = gtk_entry_get_text (GTK_ENTRY (rd->state_variable));
  GString *string = g_string_new ("ROC");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (rd->test_variables), 0, string);

  g_string_append (string, " BY ");

  g_string_append (string, var_name);

  g_string_append (string, " (");
  {
    const gchar *value = gtk_entry_get_text (GTK_ENTRY (rd->state_value));

    const struct variable *var = psppire_dict_lookup_var (rd->dict, var_name);

    g_return_val_if_fail (var, NULL);

    if ( var_is_alpha (var))
      {
	struct string xx;
	ds_init_empty (&xx);
	syntax_gen_string (&xx, ss_cstr (value));
	g_string_append (string, ds_cstr (&xx));
	ds_destroy (&xx);
      }
    else
      g_string_append (string, value);
  }
  g_string_append (string, ")");


  /* The /PLOT subcommand */
  g_string_append (string, "\n\t/PLOT ");
  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->curve)))
    {
      g_string_append (string, "CURVE");
      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->reference)))
	g_string_append (string, " (REFERENCE)");
    }
  else
    g_string_append (string, "NONE");


  /* The /PRINT subcommand */
  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->standard_error)) ||
       gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->coordinates)) )
    {
      g_string_append (string, "\n\t/PRINT");

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->standard_error)))
	g_string_append (string, " SE");

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->coordinates)))
	g_string_append (string, " COORDINATES");
    }

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}
