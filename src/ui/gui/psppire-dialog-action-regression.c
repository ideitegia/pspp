/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2010, 2011, 2012  Free Software Foundation

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

#include "psppire-dialog-action-regression.h"
#include "psppire-value-entry.h"

#include "dialog-common.h"
#include "helper.h"
#include <ui/syntax-gen.h>
#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"
#include "psppire-checkbox-treeview.h"
#include "psppire-dict.h"
#include "libpspp/str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


#define REGRESSION_STATS       \
  RG (COEFF, N_("Coeff"),          N_("Show the regression coefficients"))	\
  RG (CI,    N_("Conf. Interval"), N_("Show the confidence interval for the regression coefficients"))   \
  RG (R,     N_("R"),              N_("Show the correlation between observed and predicted values")) \
  RG (ANOVA, N_("Anova"),          N_("Show the analysis of variance table"))  \
  RG (BCOV,  N_("Bcov"),           N_("Show the variance coefficient matrix"))

enum
  {
#define RG(NAME, LABEL, TOOLTIP) RG_##NAME,
    REGRESSION_STATS
#undef RG
    N_REGRESSION_STATS
  };

enum
  {
#define RG(NAME, LABEL, TOOLTIP) B_RG_##NAME = 1u << RG_##NAME,
    REGRESSION_STATS
#undef RG
    B_RG_STATS_ALL = (1u << N_REGRESSION_STATS) - 1,
    B_RG_STATS_DEFAULT = B_RG_ANOVA | B_RG_COEFF | B_RG_R
  };

static const struct checkbox_entry_item stats[] =
  {
#define RG(NAME, LABEL, TOOLTIP) {#NAME, LABEL, TOOLTIP},
    REGRESSION_STATS
#undef RG
  };

static void
psppire_dialog_action_regression_class_init (PsppireDialogActionRegressionClass *class);

G_DEFINE_TYPE (PsppireDialogActionRegression, psppire_dialog_action_regression, PSPPIRE_TYPE_DIALOG_ACTION);

static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionRegression *rd = PSPPIRE_DIALOG_ACTION_REGRESSION (data);
  GtkTreeModel *dep_vars = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->dep_vars));
  GtkTreeModel *indep_vars = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->indep_vars));

  GtkTreeIter notused;

  return (gtk_tree_model_get_iter_first (dep_vars, &notused)
    && gtk_tree_model_get_iter_first (indep_vars, &notused));
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionRegression *rd = PSPPIRE_DIALOG_ACTION_REGRESSION (rd_);

  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->dep_vars));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->indep_vars));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));
}

static void
on_statistics_clicked (PsppireDialogActionRegression *rd)
{
  int ret;
  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->stat_view));

  /* Take a backup copy of the existing model */
  GtkListStore *backup_model = clone_list_store (GTK_LIST_STORE (model));

  ret = psppire_dialog_run (PSPPIRE_DIALOG (rd->stat_dialog));

  if ( ret != PSPPIRE_RESPONSE_CONTINUE )
    {
      /* If the user chose to abandon his changes, then replace the model, from the backup */
      gtk_tree_view_set_model (GTK_TREE_VIEW (rd->stat_view) , GTK_TREE_MODEL (backup_model));
    }
  g_object_unref (backup_model);
}

static void
on_save_clicked (PsppireDialogActionRegression *rd)
{
  int ret;
  if (rd->pred)
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->pred_button), TRUE);
    }
  if (rd->resid)
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->resid_button), TRUE);
    }

  ret = psppire_dialog_run (PSPPIRE_DIALOG (rd->save_dialog));

  if ( ret == PSPPIRE_RESPONSE_CONTINUE )
    {
      rd->pred = (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->pred_button)) == TRUE)
	? TRUE : FALSE;
      rd->resid = (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->resid_button)) == TRUE)
	? TRUE : FALSE;
    }
}


static void
psppire_dialog_action_regression_activate (GtkAction *a)
{
  PsppireDialogActionRegression *act = PSPPIRE_DIALOG_ACTION_REGRESSION (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);

  GtkBuilder *xml = builder_new ("regression.ui");
  GtkWidget *stat_button = get_widget_assert (xml, "stat-button");
  GtkWidget *save_button = get_widget_assert (xml, "save-button");

  pda->dialog = get_widget_assert   (xml, "regression-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");

  act->dep_vars  = get_widget_assert   (xml, "dep-view");
  act->indep_vars  = get_widget_assert   (xml, "indep-view");
  act->stat_view = get_widget_assert (xml, "stat-view");
  act->stat_dialog = get_widget_assert (xml, "statistics-dialog");
  act->save_dialog = get_widget_assert (xml, "save-dialog");
  act->pred_button = get_widget_assert (xml, "pred-button");
  act->resid_button = get_widget_assert (xml, "resid-button");

  g_object_unref (xml);

  psppire_checkbox_treeview_populate (PSPPIRE_CHECKBOX_TREEVIEW (act->stat_view),
  				  B_RG_STATS_DEFAULT,
  				  N_REGRESSION_STATS,
  				  stats);

  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda,
					dialog_state_valid);

  g_signal_connect_swapped (stat_button, "clicked",
			    G_CALLBACK (on_statistics_clicked),  act);

  g_signal_connect_swapped (save_button, "clicked",
			    G_CALLBACK (on_save_clicked),  act);


  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_regression_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_regression_parent_class)->activate (pda);
}



static char *
generate_syntax (PsppireDialogAction *a)
{
  PsppireDialogActionRegression *rd = PSPPIRE_DIALOG_ACTION_REGRESSION (a);
  gchar *text = NULL;

  gint i;
  int n;
  guint selected;
  GtkTreeIter iter;
  gboolean ok;

  GString *string = g_string_new ("REGRESSION");

  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->stat_view));

  g_string_append (string, "\n\t/VARIABLES=");
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (rd->indep_vars), 0, string);
  g_string_append (string, "\n\t/DEPENDENT=\t");
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (rd->dep_vars), 0, string);

  selected = 0;
  for (i = 0, ok = gtk_tree_model_get_iter_first (model, &iter); ok; 
       i++, ok = gtk_tree_model_iter_next (model, &iter))
    {
      gboolean toggled;
      gtk_tree_model_get (model, &iter,
			  CHECKBOX_COLUMN_SELECTED, &toggled, -1); 
      if (toggled) 
	selected |= 1u << i; 
      else 
	selected &= ~(1u << i);
    }

  if (selected)
    {
      g_string_append (string, "\n\t/STATISTICS=");
      n = 0;
      for (i = 0; i < N_REGRESSION_STATS; i++)
	if (selected & (1u << i))
	  {
	    if (n++)
	      g_string_append (string, " ");
	    g_string_append (string, stats[i].name);
	  }
    }

  if (rd->pred || rd->resid)
    {
      g_string_append (string, "\n\t/SAVE=");
      if (rd->pred)
	g_string_append (string, " PRED");
      if (rd->resid)
	g_string_append (string, " RESID");
    }

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}

static void
psppire_dialog_action_regression_class_init (PsppireDialogActionRegressionClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_regression_activate;

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_regression_init (PsppireDialogActionRegression *act)
{
}

