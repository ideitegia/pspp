/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011  Free Software Foundation

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
#include <ui/gui/helper.h>
#include "psppire-dialog.h"
#include "dict-display.h"

#include "psppire-var-view.h"
#include "psppire-selector.h"
#include "psppire-dictview.h"
#include <ui/gui/dialog-common.h>

#include "executor.h"

#include "univariate-dialog.h"

struct uni_dialog
{
  struct dictionary *dict;

  /* Entry box for the dependent variable */
  GtkWidget *dep_entry;

  GtkWidget *factor_list;
};


/* Dialog is valid iff at least one variable has been selected in both
   the dependent variable box and the factor list. */
static gboolean
dialog_state_valid (gpointer data)
{
  struct uni_dialog *uv_d = data;
  GtkTreeModel *vars;
  GtkTreeIter notused;

  if ( 0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (uv_d->dep_entry))))
    return false;

  vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (uv_d->factor_list));

  return gtk_tree_model_get_iter_first (vars, &notused);
}

/* Reset the dialog to its default state */
static void
refresh (PsppireDialog *dialog, struct uni_dialog *uv_d)
{
  GtkTreeModel *liststore ;

  gtk_entry_set_text (GTK_ENTRY (uv_d->dep_entry), "");

  liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (uv_d->factor_list));

  gtk_list_store_clear (GTK_LIST_STORE (liststore));
}


static char *
generate_syntax (const struct uni_dialog *uvd)
{
  gchar *text = NULL;
  GString *str = g_string_new ("GLM ");


  g_string_append (str, gtk_entry_get_text (GTK_ENTRY (uvd->dep_entry)));
  

  g_string_append (str, " BY ");

  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (uvd->factor_list), 0, str);


  g_string_append (str, ".");

  text = str->str;

  g_string_free (str, FALSE);

  return text;
}

void 
univariate_dialog (PsppireDataWindow * de)
{
  struct uni_dialog uv_d;

  gint response;

  GtkBuilder *xml = builder_new ("univariate.ui");

  GtkWidget *dialog = get_widget_assert   (xml, "univariate-dialog");
  GtkWidget *source = get_widget_assert   (xml, "dict-view");

  GtkWidget *dep_selector = get_widget_assert (xml, "dep-selector");
  GtkWidget *factor_selector = get_widget_assert (xml, "factor-selector");


  PsppireVarStore *vs = NULL;

  uv_d.dep_entry = get_widget_assert (xml, "dep-entry");
  uv_d.factor_list = get_widget_assert (xml, "factors-view");

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));
  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &uv_d);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &uv_d);


  g_object_get (vs, "dictionary", &uv_d.dict, NULL);
  g_object_set (source, "model", uv_d.dict, NULL);

  psppire_selector_set_allow (PSPPIRE_SELECTOR (dep_selector),
			      numeric_only);

  psppire_selector_set_filter_func (PSPPIRE_SELECTOR (dep_selector),
				    is_currently_in_entry);


  psppire_selector_set_filter_func (PSPPIRE_SELECTOR (factor_selector),
				    is_currently_in_varview);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (de, generate_syntax (&uv_d)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (&uv_d)));
      break;
    default:
      break;
    }

  g_object_unref (xml);
}


