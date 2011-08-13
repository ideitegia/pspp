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

#include <ui/syntax-gen.h>
#include <libpspp/str.h>

#include "ks-one-sample-dialog.h"
#include "psppire-selector.h"
#include "psppire-dictview.h"
#include "psppire-dialog.h"

#include "psppire-data-window.h"
#include "psppire-var-view.h"

#include "executor.h"
#include "helper.h"
#include "dialog-common.h"

#include <gtk/gtk.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


enum
  {
    CB_NORMAL,
    CB_POISSON,
    CB_UNIFORM,
    CB_EXPONENTIAL
  };

struct ks_one_sample
{
  GtkBuilder *xml;
  PsppireDict *dict;

  GtkWidget *variables;
  PsppireDataWindow *de ;

  GtkWidget *cb[4];
};

static char * generate_syntax (const struct ks_one_sample *rd);


static void
refresh (struct ks_one_sample *fd)
{
  int i;
  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (fd->variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  for (i = 0; i < 4; ++i)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->cb[i]), FALSE);
}


static gboolean
dialog_state_valid (gpointer data)
{
  int i;
  struct ks_one_sample *fd = data;

  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (fd->variables));

  if  (gtk_tree_model_iter_n_children (liststore, NULL) < 1)
    return FALSE;

  for (i = 0; i < 4; ++i)
    {
      if ( TRUE == gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->cb[i])))
	break;
    }
  if ( i >= 4)
    return FALSE;


  return TRUE;
}


/* Pops up the Ks_One_Sample dialog box */
void
ks_one_sample_dialog (PsppireDataWindow *dw)
{
  struct ks_one_sample fd;
  gint response;

  PsppireVarStore *vs;

  GtkWidget *dialog ;
  GtkWidget *source ;

  fd.xml = builder_new ("ks-one-sample.ui");

  dialog = get_widget_assert   (fd.xml, "ks-one-sample-dialog");
  source = get_widget_assert   (fd.xml, "dict-view");

  fd.cb[CB_NORMAL] = get_widget_assert (fd.xml, "checkbutton-normal");
  fd.cb[CB_POISSON] = get_widget_assert (fd.xml, "checkbutton-poisson");
  fd.cb[CB_UNIFORM] = get_widget_assert (fd.xml, "checkbutton-uniform");
  fd.cb[CB_EXPONENTIAL] = get_widget_assert (fd.xml, "checkbutton-exp");

  fd.de = dw;

  g_signal_connect_swapped (dialog, "refresh", G_CALLBACK (refresh),  &fd);


  fd.variables = get_widget_assert   (fd.xml, "psppire-var-view1");

  g_object_get (fd.de->data_editor, "var-store", &vs, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fd.de));

  g_object_get (vs, "dictionary", &fd.dict, NULL);
  g_object_set (source, "model", fd.dict,
		"predicate", var_is_numeric,
		NULL);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &fd);

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
append_fragment (GString *string, const gchar *dist, PsppireVarView *vv)
{
  g_string_append (string, "\n\t/KOLMOGOROV-SMIRNOV");

  g_string_append (string, " ( ");
  g_string_append (string, dist);
  g_string_append (string, " ) = ");

  psppire_var_view_append_names (vv, 0, string);
}


char *
generate_syntax (const struct ks_one_sample *rd)
{
  gchar *text;

  GString *string = g_string_new ("NPAR TEST");

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->cb[CB_NORMAL])))
    append_fragment (string, "NORMAL", PSPPIRE_VAR_VIEW (rd->variables));

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->cb[CB_UNIFORM])))
    append_fragment (string, "UNIFORM", PSPPIRE_VAR_VIEW (rd->variables));

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->cb[CB_POISSON])))
    append_fragment (string, "POISSON", PSPPIRE_VAR_VIEW (rd->variables));

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->cb[CB_EXPONENTIAL])))
    append_fragment (string, "EXPONENTIAL", PSPPIRE_VAR_VIEW (rd->variables));

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}
