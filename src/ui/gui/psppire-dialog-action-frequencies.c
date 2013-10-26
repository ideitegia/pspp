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

#include "psppire-dialog-action-frequencies.h"
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


#define FREQUENCY_STATS					\
  FS (MEAN, N_("Mean"))					\
  FS (STDDEV, N_("Standard deviation"))			\
  FS (MINIMUM, N_("Minimum"))				\
  FS (MAXIMUM, N_("Maximum"))				\
  FS (SEMEAN, N_("Standard error of the mean"))		\
  FS (VARIANCE, N_("Variance"))				\
  FS (SKEWNESS, N_("Skewness"))				\
  FS (SESKEW, N_("Standard error of the skewness"))	\
  FS (RANGE, N_("Range"))				\
  FS (MODE, N_("Mode"))					\
  FS (KURTOSIS, N_("Kurtosis"))				\
  FS (SEKURT, N_("Standard error of the kurtosis"))	\
  FS (MEDIAN, N_("Median"))				\
  FS (SUM, N_("Sum"))



enum
{
#define FS(NAME, LABEL) FS_##NAME,
  FREQUENCY_STATS
#undef FS
  N_FREQUENCY_STATS
};

enum
{
#define FS(NAME, LABEL) B_FS_##NAME = 1u << FS_##NAME,
  FREQUENCY_STATS
#undef FS
    B_FS_ALL = (1u << N_FREQUENCY_STATS) - 1,
  B_FS_DEFAULT = B_FS_MEAN | B_FS_STDDEV | B_FS_MINIMUM | B_FS_MAXIMUM
};


static const struct checkbox_entry_item stats[] = {
#define FS(NAME, LABEL) {#NAME, LABEL},
  FREQUENCY_STATS
#undef FS
};


static void
on_tables_clicked (PsppireDialogActionFrequencies * fd)
{
  int ret;

  switch (fd->tables_opts_order)
    {
    case FRQ_AVALUE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->avalue), TRUE);
      break;
    case FRQ_DVALUE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->dvalue), TRUE);
      break;
    case FRQ_ACOUNT:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->afreq), TRUE);
      break;
    case FRQ_DCOUNT:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->dfreq), TRUE);
      break;
    };

  switch (fd->tables_opts_table)
    {
    case FRQ_TABLE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->always), TRUE);
      break;
    case FRQ_NOTABLE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->never), TRUE);
      break;
    case FRQ_LIMIT:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->limit), TRUE);
      break;
    }

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (fd->limit_spinbutton),
                             fd->tables_opts_limit);

  g_signal_emit_by_name (fd->limit, "toggled");

  ret = psppire_dialog_run (PSPPIRE_DIALOG (fd->tables_dialog));

  if (ret == PSPPIRE_RESPONSE_CONTINUE)
    {
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->avalue)))
        fd->tables_opts_order = FRQ_AVALUE;
      else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->dvalue)))
        fd->tables_opts_order = FRQ_DVALUE;
      else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->afreq)))
        fd->tables_opts_order = FRQ_ACOUNT;
      else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->dfreq)))
        fd->tables_opts_order = FRQ_DCOUNT;

      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->always)))
        fd->tables_opts_table = FRQ_TABLE;
      else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->never)))
        fd->tables_opts_table = FRQ_NOTABLE;
      else
        fd->tables_opts_table = FRQ_LIMIT;


      fd->tables_opts_limit =
        gtk_spin_button_get_value (GTK_SPIN_BUTTON (fd->limit_spinbutton));
    }
}


static void
on_charts_clicked (PsppireDialogActionFrequencies *fd)
{
  int ret;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->min), fd->charts_opts_use_min);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (fd->min_spin), fd->charts_opts_min);
  g_signal_emit_by_name (fd->min, "toggled");

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->max), fd->charts_opts_use_max);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (fd->max_spin), fd->charts_opts_max);
  g_signal_emit_by_name (fd->max, "toggled");

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->hist), fd->charts_opts_draw_hist);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->normal), fd->charts_opts_draw_normal);
  g_signal_emit_by_name (fd->hist, "toggled");

  switch (fd->charts_opts_scale)
    {
    case FRQ_FREQ:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->freqs), TRUE);
      break;
    case FRQ_DVALUE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->percents), TRUE);
      break;
    };

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->pie), fd->charts_opts_draw_pie);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->pie_include_missing),
                                fd->charts_opts_pie_include_missing);

  g_signal_emit_by_name (fd->pie, "toggled");

  ret = psppire_dialog_run (PSPPIRE_DIALOG (fd->charts_dialog));

  if ( ret == PSPPIRE_RESPONSE_CONTINUE )
    {
	fd->charts_opts_use_min = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->min));
	fd->charts_opts_min = gtk_spin_button_get_value (GTK_SPIN_BUTTON (fd->min_spin));

	fd->charts_opts_use_max = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->max));
	fd->charts_opts_max = gtk_spin_button_get_value (GTK_SPIN_BUTTON (fd->max_spin));

	fd->charts_opts_draw_hist = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->hist));
	fd->charts_opts_draw_normal = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->normal));
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->freqs)))
	  fd->charts_opts_scale = FRQ_FREQ;
	else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->percents)))
	  fd->charts_opts_scale = FRQ_PERCENT;

	fd->charts_opts_draw_pie = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->pie));
	fd->charts_opts_pie_include_missing
          = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->pie_include_missing));
    }
}


static void
psppire_dialog_action_frequencies_class_init
(PsppireDialogActionFrequenciesClass * class);

G_DEFINE_TYPE (PsppireDialogActionFrequencies,
               psppire_dialog_action_frequencies, PSPPIRE_TYPE_DIALOG_ACTION);

static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionFrequencies *fd =
    PSPPIRE_DIALOG_ACTION_FREQUENCIES (data);

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (fd->stat_vars));

  GtkTreeIter notused;

  return gtk_tree_model_get_iter_first (vars, &notused);
}

static void
refresh (PsppireDialogAction * fdx)
{
  PsppireDialogActionFrequencies *fd =
    PSPPIRE_DIALOG_ACTION_FREQUENCIES (fdx);

  GtkTreeIter iter;
  size_t i;
  bool ok;

  GtkTreeModel *liststore =
    gtk_tree_view_get_model (GTK_TREE_VIEW (fd->stat_vars));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  for (i = 0, ok = gtk_tree_model_get_iter_first (fd->stats, &iter); ok;
       i++, ok = gtk_tree_model_iter_next (fd->stats, &iter))
    gtk_list_store_set (GTK_LIST_STORE (fd->stats), &iter,
                        CHECKBOX_COLUMN_SELECTED,
                        (B_FS_DEFAULT & (1u << i)) ? true : false, -1);
}



static void
psppire_dialog_action_frequencies_activate (GtkAction * a)
{
  PsppireDialogActionFrequencies *act = PSPPIRE_DIALOG_ACTION_FREQUENCIES (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);

  GtkBuilder *xml = builder_new ("frequencies.ui");

  GtkWidget *stats_treeview = get_widget_assert (xml, "stats-treeview");
  GtkWidget *tables_button = get_widget_assert (xml, "tables-button");
  GtkWidget *charts_button = get_widget_assert (xml, "charts-button");

  pda->dialog = get_widget_assert (xml, "frequencies-dialog");
  pda->source = get_widget_assert (xml, "dict-treeview");

  act->stat_vars = get_widget_assert (xml, "var-treeview");

  psppire_checkbox_treeview_populate (PSPPIRE_CHECKBOX_TREEVIEW (stats_treeview),
                                  B_FS_DEFAULT, N_FREQUENCY_STATS, stats);

  act->stats = gtk_tree_view_get_model (GTK_TREE_VIEW (stats_treeview));

  act->include_missing = get_widget_assert (xml, "include_missing");


  act->tables_dialog = get_widget_assert (xml, "tables-dialog");
  act->charts_dialog = get_widget_assert (xml, "charts-dialog");
  act->always = get_widget_assert (xml, "always");
  act->never = get_widget_assert (xml, "never");
  act->limit = get_widget_assert (xml, "limit");
  act->limit_spinbutton = get_widget_assert (xml, "limit-spin");

  g_signal_connect (act->limit, "toggled",
                    G_CALLBACK (set_sensitivity_from_toggle),
                    act->limit_spinbutton);

  act->avalue = get_widget_assert (xml, "avalue");
  act->dvalue = get_widget_assert (xml, "dvalue");
  act->afreq = get_widget_assert (xml, "afreq");
  act->dfreq = get_widget_assert (xml, "dfreq");

  act->charts_opts_use_min = false;
  act->charts_opts_min = 0;
  act->charts_opts_use_max = false;
  act->charts_opts_max = 100;
  act->charts_opts_draw_hist = false;
  act->charts_opts_draw_normal = false;
  act->charts_opts_scale = FRQ_FREQ;
  act->charts_opts_draw_pie = false;
  act->charts_opts_pie_include_missing = false;

  act->freqs = get_widget_assert (xml, "freqs");
  act->percents = get_widget_assert (xml, "percents");

  act->min = get_widget_assert (xml, "min");
  act->min_spin = get_widget_assert (xml, "min-spin");
  g_signal_connect (act->min, "toggled",
		    G_CALLBACK (set_sensitivity_from_toggle), act->min_spin);
  act->max = get_widget_assert (xml, "max");
  act->max_spin = get_widget_assert (xml, "max-spin");
  g_signal_connect (act->max, "toggled",
		    G_CALLBACK (set_sensitivity_from_toggle), act->max_spin);

  act->hist = get_widget_assert (xml, "hist");
  act->normal = get_widget_assert (xml, "normal");
  g_signal_connect (act->hist, "toggled",
		    G_CALLBACK (set_sensitivity_from_toggle), act->normal);

  act->pie =  (get_widget_assert (xml, "pie"));
  act->pie_include_missing = get_widget_assert (xml, "pie-include-missing");


  g_object_unref (xml);


  act->tables_opts_order = FRQ_AVALUE;
  act->tables_opts_table = FRQ_TABLE;
  act->tables_opts_limit = 50;

  g_signal_connect_swapped (tables_button, "clicked",
			    G_CALLBACK (on_tables_clicked),  act);

  g_signal_connect_swapped (charts_button, "clicked",
			    G_CALLBACK (on_charts_clicked),  act);

  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);

  if (PSPPIRE_DIALOG_ACTION_CLASS
      (psppire_dialog_action_frequencies_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS
      (psppire_dialog_action_frequencies_parent_class)->activate (pda);
}

static char *
generate_syntax (PsppireDialogAction * a)
{
  PsppireDialogActionFrequencies *fd = PSPPIRE_DIALOG_ACTION_FREQUENCIES (a);
  gchar *text = NULL;
  gint i;
  gboolean ok;
  GtkTreeIter iter;
  guint selected = 0;
  struct string str;

  ds_init_cstr (&str, "FREQUENCIES");

  ds_put_cstr (&str, "\n\t/VARIABLES=");
  psppire_var_view_append_names_str (PSPPIRE_VAR_VIEW (fd->stat_vars), 0, &str);

  ds_put_cstr (&str, "\n\t/FORMAT=");

  switch (fd->tables_opts_order)
    {
    case FRQ_AVALUE:
      ds_put_cstr (&str, "AVALUE");
      break;
    case FRQ_DVALUE:
      ds_put_cstr (&str, "DVALUE");
      break;
    case FRQ_ACOUNT:
      ds_put_cstr (&str, "AFREQ");
      break;
    case FRQ_DCOUNT:
      ds_put_cstr (&str, "DFREQ");
      break;
    default:
      g_assert_not_reached ();
    }

  ds_put_cstr (&str, " ");

  switch (fd->tables_opts_table)
    {
    case FRQ_TABLE:
      ds_put_cstr (&str, "TABLE");
      break;
    case FRQ_NOTABLE:
      ds_put_cstr (&str, "NOTABLE");
      break;
    case FRQ_LIMIT:
      ds_put_c_format (&str, "LIMIT (%d)", fd->tables_opts_limit);
      break;
    }


  for (i = 0, ok = gtk_tree_model_get_iter_first (fd->stats, &iter); ok;
       i++, ok = gtk_tree_model_iter_next (fd->stats, &iter))
    {
      gboolean toggled;
      gtk_tree_model_get (fd->stats, &iter,
                          CHECKBOX_COLUMN_SELECTED, &toggled, -1);
      if (toggled)
        selected |= 1u << i;
    }

  if (selected != B_FS_DEFAULT)
    {
      ds_put_cstr (&str, "\n\t/STATISTICS=");
      if (selected == B_FS_ALL)
        ds_put_cstr (&str, "ALL");
      else if (selected == 0)
        ds_put_cstr (&str, "NONE");
      else
        {
          int n = 0;
          if ((selected & B_FS_DEFAULT) == B_FS_DEFAULT)
            {
              ds_put_cstr (&str, "DEFAULT");
              selected &= ~B_FS_DEFAULT;
              n++;
            }
          for (i = 0; i < N_FREQUENCY_STATS; i++)
            if (selected & (1u << i))
              {
                if (n++)
                  ds_put_cstr (&str, " ");
                ds_put_cstr (&str, stats[i].name);
              }
        }
    }

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->include_missing)))
    ds_put_cstr (&str, "\n\t/MISSING=INCLUDE");


  if (fd->charts_opts_draw_hist)
    {
      ds_put_cstr (&str, "\n\t/HISTOGRAM=");
      ds_put_cstr (&str,
                       fd->charts_opts_draw_normal ? "NORMAL" : "NONORMAL");

      if (fd->charts_opts_scale == FRQ_PERCENT)
        ds_put_cstr (&str, " PERCENT");

      if (fd->charts_opts_use_min)
        ds_put_c_format (&str, " MIN(%.15g)", fd->charts_opts_min);
      if (fd->charts_opts_use_max)
        ds_put_c_format (&str, " MAX(%.15g)", fd->charts_opts_max);
    }

  if (fd->charts_opts_draw_pie)
    {
      ds_put_cstr (&str, "\n\t/PIECHART=");

      if (fd->charts_opts_pie_include_missing)
        ds_put_cstr (&str, " MISSING");

      if (fd->charts_opts_use_min)
        ds_put_c_format (&str, " MIN(%.15g)", fd->charts_opts_min);
      if (fd->charts_opts_use_max)
        ds_put_c_format (&str, " MAX(%.15g)", fd->charts_opts_max);
    }

  ds_put_cstr (&str, ".\n");

  text = ds_steal_cstr (&str);

  ds_destroy (&str);

  return text;
}

static void
psppire_dialog_action_frequencies_class_init (PsppireDialogActionFrequenciesClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_frequencies_activate;

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_frequencies_init (PsppireDialogActionFrequencies * act)
{
}
