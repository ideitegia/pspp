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

#include "psppire-dialog-action-descriptives.h"

#include "psppire-checkbox-treeview.h"

#include "psppire-var-view.h"
#include "psppire-dict.h"
#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_dialog_action_descriptives_class_init      (PsppireDialogActionDescriptivesClass *class);

G_DEFINE_TYPE (PsppireDialogActionDescriptives, psppire_dialog_action_descriptives, PSPPIRE_TYPE_DIALOG_ACTION);


#define DESCRIPTIVE_STATS                       \
  DS (MEAN, N_("Mean"))                         \
  DS (STDDEV, N_("Standard deviation"))         \
  DS (MINIMUM, N_("Minimum"))                   \
  DS (MAXIMUM, N_("Maximum"))                   \
  DS (RANGE, N_("Range"))                       \
  DS (SUM, N_("Sum"))                           \
  DS (SEMEAN, N_("Standard error"))             \
  DS (VARIANCE, N_("Variance"))                 \
  DS (KURTOSIS, N_("Kurtosis"))                 \
  DS (SKEWNESS, N_("Skewness"))

enum
  {
#define DS(NAME, LABEL) DS_##NAME,
    DESCRIPTIVE_STATS
#undef DS
    N_DESCRIPTIVE_STATS
  };

enum
  {
#define DS(NAME, LABEL) B_DS_##NAME = 1u << DS_##NAME,
    DESCRIPTIVE_STATS
#undef DS
    B_DS_ALL = (1u << N_DESCRIPTIVE_STATS) - 1,
    B_DS_DEFAULT = B_DS_MEAN | B_DS_STDDEV | B_DS_MINIMUM | B_DS_MAXIMUM
  };


static const struct checkbox_entry_item stats[] =
  {
#define DS(NAME, LABEL) {#NAME, LABEL},
    DESCRIPTIVE_STATS
#undef DS
  };


static char *
generate_syntax (PsppireDialogAction *act)
{
  PsppireDialogActionDescriptives *scd = PSPPIRE_DIALOG_ACTION_DESCRIPTIVES (act);
  gchar *text;
  GString *string;
  GtkTreeIter iter;
  unsigned int selected;
  size_t i;
  bool listwise, include;
  bool ok;

  string = g_string_new ("DESCRIPTIVES");
  g_string_append (string, "\n    /VARIABLES=");
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (scd->stat_vars), 0, string);

  listwise = gtk_toggle_button_get_active (scd->exclude_missing_listwise);
  include = gtk_toggle_button_get_active (scd->include_user_missing);
  if (listwise || include)
    {
      g_string_append (string, "\n    /MISSING=");
      if (listwise)
        {
          g_string_append (string, "LISTWISE");
          if (include)
            g_string_append (string, " ");
        }
      if (include)
        g_string_append (string, "INCLUDE");
    }

  selected = 0;
  for (i = 0, ok = gtk_tree_model_get_iter_first (scd->stats, &iter); ok;
       i++, ok = gtk_tree_model_iter_next (scd->stats, &iter))
    {
      gboolean toggled;
      gtk_tree_model_get (scd->stats, &iter,
			  CHECKBOX_COLUMN_SELECTED, &toggled, -1);
      if (toggled)
        selected |= 1u << i;
    }

  if (selected != B_DS_DEFAULT)
    {
      g_string_append (string, "\n    /STATISTICS=");
      if (selected == B_DS_ALL)
        g_string_append (string, "ALL");
      else if (selected == 0)
        g_string_append (string, "NONE");
      else
        {
          int n = 0;
          if ((selected & B_DS_DEFAULT) == B_DS_DEFAULT)
            {
              g_string_append (string, "DEFAULT");
              selected &= ~B_DS_DEFAULT;
              n++;
            }
          for (i = 0; i < N_DESCRIPTIVE_STATS; i++)
            if (selected & (1u << i))
              {
                if (n++)
                  g_string_append (string, " ");
                g_string_append (string, stats[i].name);
              }
        }
    }

  if (gtk_toggle_button_get_active (scd->save_z_scores))
    g_string_append (string, "\n    /SAVE");

  g_string_append (string, ".");

  if (gtk_toggle_button_get_active (scd->save_z_scores))
    g_string_append (string, "\nEXECUTE.");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}

static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionDescriptives *dd = data;

  GtkTreeModel *vars = gtk_tree_view_get_model (dd->stat_vars);

  GtkTreeIter notused;

  return gtk_tree_model_get_iter_first (vars, &notused);
}

static void
dialog_refresh (PsppireDialogAction *scd_)
{
  PsppireDialogActionDescriptives *scd
    = PSPPIRE_DIALOG_ACTION_DESCRIPTIVES (scd_);
  GtkTreeModel *liststore;
  GtkTreeIter iter;
  size_t i;
  bool ok;

  liststore = gtk_tree_view_get_model (scd->stat_vars);
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  for (i = 0, ok = gtk_tree_model_get_iter_first (scd->stats, &iter); ok;
       i++, ok = gtk_tree_model_iter_next (scd->stats, &iter))
    gtk_list_store_set (GTK_LIST_STORE (scd->stats), &iter,
			CHECKBOX_COLUMN_SELECTED,
                        (B_DS_DEFAULT & (1u << i)) ? true : false, -1);

  gtk_toggle_button_set_active (scd->exclude_missing_listwise, false);
  gtk_toggle_button_set_active (scd->include_user_missing, false);
  gtk_toggle_button_set_active (scd->save_z_scores, false);
}

static void
psppire_dialog_action_descriptives_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionDescriptives *act = PSPPIRE_DIALOG_ACTION_DESCRIPTIVES (a);

  GtkBuilder *xml = builder_new ("descriptives.ui");

  GtkWidget *stats_treeview = get_widget_assert    (xml, "statistics");

  pda->dialog = get_widget_assert   (xml, "descriptives-dialog");
  pda->source = get_widget_assert   (xml, "all-variables");
  act->variables =   get_widget_assert   (xml, "stat-variables");

  g_object_set (pda->source, "model", pda->dict,
	"predicate", var_is_numeric, NULL);

  psppire_checkbox_treeview_populate (PSPPIRE_CHECKBOX_TREEVIEW (stats_treeview),
				      B_DS_DEFAULT,
				      N_DESCRIPTIVE_STATS, stats);

  act->stat_vars = GTK_TREE_VIEW (act->variables);
  act->stats = gtk_tree_view_get_model (GTK_TREE_VIEW (stats_treeview));
  
  act->include_user_missing =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "include_user_missing"));
  act->exclude_missing_listwise =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "exclude_missing_listwise"));
  act->save_z_scores =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "save_z_scores"));

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, dialog_refresh);

  PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_descriptives_parent_class)->activate (pda);

  g_object_unref (xml);
}

static void
psppire_dialog_action_descriptives_class_init (PsppireDialogActionDescriptivesClass *class)
{
  GTK_ACTION_CLASS (class)->activate = psppire_dialog_action_descriptives_activate;

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}

static void
psppire_dialog_action_descriptives_init (PsppireDialogActionDescriptives *act)
{
}
