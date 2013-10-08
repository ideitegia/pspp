/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012  Free Software Foundation

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

#include "psppire-dialog-action-means.h"

#include "psppire-means-layer.h"

#include "psppire-var-view.h"
#include "psppire-dict.h"
#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_dialog_action_means_class_init      (PsppireDialogActionMeansClass *class);

G_DEFINE_TYPE (PsppireDialogActionMeans, psppire_dialog_action_means, PSPPIRE_TYPE_DIALOG_ACTION);


static char *
generate_syntax (PsppireDialogAction *act)
{
  gint l;
  PsppireDialogActionMeans *scd = PSPPIRE_DIALOG_ACTION_MEANS (act);
  gchar *text;
  GString *string = g_string_new ("MEANS TABLES = ");
  PsppireMeansLayer *layer = PSPPIRE_MEANS_LAYER (scd->layer);
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (scd->variables), 0, string);

  for (l = 0; l < layer->n_layers; ++l)
    {
      GtkTreeIter iter;

      GtkTreeModel *m = psppire_means_layer_get_model_n (layer, l);
      gboolean ok = gtk_tree_model_get_iter_first (m, &iter);
      if (ok)
	g_string_append (string, "\n\tBY");
      for (; ok; ok = gtk_tree_model_iter_next (m, &iter))
	  {
	    const struct variable *var = psppire_var_view_get_var_from_model (m, 0, &iter);
	    g_string_append (string, " ");
	    g_string_append (string, var_get_name (var));
	  }
    }

  g_string_append (string, ".\n");
  text = string->str;

  g_string_free (string, FALSE);

  return text;
}

static gboolean
dialog_state_valid (PsppireDialogAction *da)
{
  PsppireDialogActionMeans *pdm  = PSPPIRE_DIALOG_ACTION_MEANS (da);
  GtkTreeIter notused;
  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (pdm->variables));

  return gtk_tree_model_get_iter_first (vars, &notused);
}

static void
dialog_refresh (PsppireDialogAction *da)
{
  PsppireDialogActionMeans *pdm  = PSPPIRE_DIALOG_ACTION_MEANS (da);
  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (pdm->variables));

  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  psppire_means_layer_clear (PSPPIRE_MEANS_LAYER (pdm->layer));
}

static void
psppire_dialog_action_means_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionMeans *act = PSPPIRE_DIALOG_ACTION_MEANS (a);

  GtkBuilder *xml = builder_new ("means.ui");

  GtkWidget *vb =   get_widget_assert   (xml, "alignment3");
  act->layer = psppire_means_layer_new ();
  gtk_container_add (GTK_CONTAINER (vb), act->layer);
  gtk_widget_show (act->layer);

  pda->dialog = get_widget_assert   (xml, "means-dialog");
  pda->source = get_widget_assert   (xml, "all-variables");
  act->variables = get_widget_assert   (xml, "stat-variables");

  g_object_set (pda->source,
		"predicate", var_is_numeric,
		NULL);

  psppire_means_layer_set_source (PSPPIRE_MEANS_LAYER (act->layer), pda->source);

  psppire_dialog_action_set_valid_predicate (pda, (void *) dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, dialog_refresh);

  PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_means_parent_class)->activate (pda);

  g_object_unref (xml);
}

static void
psppire_dialog_action_means_class_init (PsppireDialogActionMeansClass *class)
{
  GTK_ACTION_CLASS (class)->activate = psppire_dialog_action_means_activate;

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}

static void
psppire_dialog_action_means_init (PsppireDialogActionMeans *act)
{
}
