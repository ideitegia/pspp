/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010, 2011, 2012  Free Software Foundation

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

#include "checkbox-treeview.h"
#include "frequencies-dialog.h"
#include "psppire-var-view.h"

#include <gtk/gtk.h>
#include <stdlib.h>

#include <ui/gui/psppire-data-window.h>
#include <ui/gui/dialog-common.h>
#include <ui/gui/dict-display.h>
#include <ui/gui/builder-wrapper.h>
#include <ui/gui/psppire-dialog.h>
#include "executor.h"
#include "helper.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


#define FREQUENCY_STATS                       \
  FS (MEAN, N_("Mean"))                         \
  FS (STDDEV, N_("Standard deviation"))         \
  FS (MINIMUM, N_("Minimum"))                   \
  FS (MAXIMUM, N_("Maximum"))                   \
  FS (SEMEAN, N_("Standard error of the mean")) \
  FS (VARIANCE, N_("Variance"))                 \
  FS (SKEWNESS, N_("Skewness"))                 \
  FS (SESKEW, N_("Standard error of the skewness"))  \
  FS (RANGE, N_("Range"))                       \
  FS (MODE, N_("Mode"))                         \
  FS (KURTOSIS, N_("Kurtosis"))                 \
  FS (SEKURT, N_("Standard error of the kurtosis"))  \
  FS (MEDIAN, N_("Median"))      \
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


static const struct checkbox_entry_item stats[] =
  {
#define FS(NAME, LABEL) {#NAME, LABEL},
    FREQUENCY_STATS
#undef FS
  };



enum frq_order
  {
    FRQ_AVALUE,
    FRQ_DVALUE,
    FRQ_ACOUNT,
    FRQ_DCOUNT
  };

enum frq_table
  {
    FRQ_TABLE,
    FRQ_NOTABLE,
    FRQ_LIMIT
  };

struct tables_options
{
  enum frq_order order;
  enum frq_table table;
  int limit;
};

enum frq_scale
  {
    FRQ_FREQ,
    FRQ_PERCENT
  };

struct charts_options
  {
    bool use_min;
    double min;
    bool use_max;
    double max;
    bool draw_hist;
    bool draw_normal;
    enum frq_scale scale;
    bool draw_pie;
    bool pie_include_missing;
  };

struct frequencies_dialog
{
  /* Main dialog. */
  GtkTreeView *stat_vars;
  PsppireDict *dict;

  GtkWidget *tables_button;
  GtkWidget *charts_button;

  GtkToggleButton *include_missing;

  GtkTreeModel *stats;

  /* Frequency Tables dialog. */
  GtkWidget *tables_dialog;
  struct tables_options tables_opts;

  GtkToggleButton *always;
  GtkToggleButton *never;
  GtkToggleButton *limit;
  GtkSpinButton *limit_spinbutton;

  GtkToggleButton  *avalue;
  GtkToggleButton  *dvalue;
  GtkToggleButton  *afreq;
  GtkToggleButton  *dfreq;

  /* Charts dialog. */
  GtkWidget *charts_dialog;
  struct charts_options charts_opts;

  GtkToggleButton *freqs;
  GtkToggleButton *percents;

  GtkToggleButton *min;
  GtkSpinButton *min_spin;
  GtkToggleButton *max;
  GtkSpinButton *max_spin;

  GtkToggleButton *hist;
  GtkToggleButton *normal;

  GtkToggleButton *pie;
  GtkToggleButton *pie_include_missing;
};

static void
refresh (PsppireDialog *dialog, struct frequencies_dialog *fd)
{
  GtkTreeIter iter;
  size_t i;
  bool ok;

  GtkTreeModel *liststore = gtk_tree_view_get_model (fd->stat_vars);
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  for (i = 0, ok = gtk_tree_model_get_iter_first (fd->stats, &iter); ok;
       i++, ok = gtk_tree_model_iter_next (fd->stats, &iter))
    gtk_list_store_set (GTK_LIST_STORE (fd->stats), &iter,
			CHECKBOX_COLUMN_SELECTED,
                        (B_FS_DEFAULT & (1u << i)) ? true : false, -1);
}

static char *
generate_syntax (const struct frequencies_dialog *fd)
{
  GtkTreeIter iter;
  gboolean ok;
  gint i;
  guint selected = 0;

  gchar *text;
  GString *string = g_string_new ("FREQUENCIES");

  g_string_append (string, "\n\t/VARIABLES=");
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (fd->stat_vars), 0, string);

  g_string_append (string, "\n\t/FORMAT=");

  switch (fd->tables_opts.order)
    {
    case FRQ_AVALUE:
      g_string_append (string, "AVALUE");
      break;
    case FRQ_DVALUE:
      g_string_append (string, "DVALUE");
      break;
    case FRQ_ACOUNT:
      g_string_append (string, "AFREQ");
      break;
    case FRQ_DCOUNT:
      g_string_append (string, "DFREQ");
      break;
    default:
      g_assert_not_reached();
    }

  g_string_append (string, " ");

  switch (fd->tables_opts.table)
    {
    case FRQ_TABLE:
      g_string_append (string, "TABLE");
      break;
    case FRQ_NOTABLE:
      g_string_append (string, "NOTABLE");
      break;
    case FRQ_LIMIT:
      g_string_append_printf (string, "LIMIT (%d)", fd->tables_opts.limit);
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
      g_string_append (string, "\n\t/STATISTICS=");
      if (selected == B_FS_ALL)
        g_string_append (string, "ALL");
      else if (selected == 0)
        g_string_append (string, "NONE");
      else
        {
          int n = 0;
          if ((selected & B_FS_DEFAULT) == B_FS_DEFAULT)
            {
              g_string_append (string, "DEFAULT");
              selected &= ~B_FS_DEFAULT;
              n++;
            }
          for (i = 0; i < N_FREQUENCY_STATS; i++)
            if (selected & (1u << i))
              {
                if (n++)
                  g_string_append (string, " ");
                g_string_append (string, stats[i].name);
              }
        }
    }

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->include_missing)))
    g_string_append (string, "\n\t/MISSING=INCLUDE");

  if (fd->charts_opts.draw_hist)
    {
      g_string_append (string, "\n\t/HISTOGRAM=");
      g_string_append (string,
                       fd->charts_opts.draw_normal ? "NORMAL" : "NONORMAL");

      if (fd->charts_opts.scale == FRQ_PERCENT)
        g_string_append (string, " PERCENT");

      if (fd->charts_opts.use_min)
        g_string_append_printf (string, " MIN(%.15g)", fd->charts_opts.min);
      if (fd->charts_opts.use_max)
        g_string_append_printf (string, " MAX(%.15g)", fd->charts_opts.max);
    }

  if (fd->charts_opts.draw_pie)
    {
      g_string_append (string, "\n\t/PIECHART=");

      if (fd->charts_opts.pie_include_missing)
        g_string_append (string, " MISSING");

      if (fd->charts_opts.use_min)
        g_string_append_printf (string, " MIN(%.15g)", fd->charts_opts.min);
      if (fd->charts_opts.use_max)
        g_string_append_printf (string, " MAX(%.15g)", fd->charts_opts.max);
    }

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}

/* Dialog is valid iff at least one variable has been selected */
static gboolean
dialog_state_valid (gpointer data)
{
  struct frequencies_dialog *fd = data;

  GtkTreeModel *vars = gtk_tree_view_get_model (fd->stat_vars);

  GtkTreeIter notused;

  return gtk_tree_model_get_iter_first (vars, &notused);
}


static void
on_tables_clicked (struct frequencies_dialog *fd)
{
  int ret;

  switch (fd->tables_opts.order)
    {
    case FRQ_AVALUE:
      gtk_toggle_button_set_active (fd->avalue, TRUE);
      break;
    case FRQ_DVALUE:
      gtk_toggle_button_set_active (fd->dvalue, TRUE);
      break;
    case FRQ_ACOUNT:
      gtk_toggle_button_set_active (fd->afreq, TRUE);
      break;
    case FRQ_DCOUNT:
      gtk_toggle_button_set_active (fd->dfreq, TRUE);
      break;
    };

  switch (fd->tables_opts.table)
    {
    case FRQ_TABLE:
      gtk_toggle_button_set_active (fd->always, TRUE);
      break;
    case FRQ_NOTABLE:
      gtk_toggle_button_set_active (fd->never, TRUE);
      break;
    case FRQ_LIMIT:
      gtk_toggle_button_set_active (fd->limit, TRUE);
      break;
    }
  gtk_spin_button_set_value (fd->limit_spinbutton,
			     fd->tables_opts.limit);
  g_signal_emit_by_name (fd->limit, "toggled");

  ret = psppire_dialog_run (PSPPIRE_DIALOG (fd->tables_dialog));

  if ( ret == PSPPIRE_RESPONSE_CONTINUE )
    {
      if (gtk_toggle_button_get_active (fd->avalue))
	fd->tables_opts.order = FRQ_AVALUE;
      else if (gtk_toggle_button_get_active (fd->dvalue))
	fd->tables_opts.order = FRQ_DVALUE;
      else if (gtk_toggle_button_get_active (fd->afreq))
	fd->tables_opts.order = FRQ_ACOUNT;
      else if (gtk_toggle_button_get_active (fd->dfreq))
	fd->tables_opts.order = FRQ_DCOUNT;

      if (gtk_toggle_button_get_active (fd->always))
        fd->tables_opts.table = FRQ_TABLE;
      else if (gtk_toggle_button_get_active (fd->never))
        fd->tables_opts.table = FRQ_NOTABLE;
      else
        fd->tables_opts.table = FRQ_LIMIT;

      fd->tables_opts.limit = gtk_spin_button_get_value (fd->limit_spinbutton);
    }
}

static void
on_charts_clicked (struct frequencies_dialog *fd)
{
  int ret;

  gtk_toggle_button_set_active (fd->min, fd->charts_opts.use_min);
  gtk_spin_button_set_value (fd->min_spin, fd->charts_opts.min);
  g_signal_emit_by_name (fd->min, "toggled");

  gtk_toggle_button_set_active (fd->max, fd->charts_opts.use_max);
  gtk_spin_button_set_value (fd->max_spin, fd->charts_opts.max);
  g_signal_emit_by_name (fd->max, "toggled");

  gtk_toggle_button_set_active (fd->hist, fd->charts_opts.draw_hist);
  gtk_toggle_button_set_active (fd->normal, fd->charts_opts.draw_normal);
  g_signal_emit_by_name (fd->hist, "toggled");

  switch (fd->charts_opts.scale)
    {
    case FRQ_FREQ:
      gtk_toggle_button_set_active (fd->freqs, TRUE);
      break;
    case FRQ_DVALUE:
      gtk_toggle_button_set_active (fd->percents, TRUE);
      break;
    };


  gtk_toggle_button_set_active (fd->pie, fd->charts_opts.draw_pie);
  gtk_toggle_button_set_active (fd->pie_include_missing,
                                fd->charts_opts.pie_include_missing);
  g_signal_emit_by_name (fd->pie, "toggled");

  ret = psppire_dialog_run (PSPPIRE_DIALOG (fd->charts_dialog));

  if ( ret == PSPPIRE_RESPONSE_CONTINUE )
    {
      fd->charts_opts.use_min = gtk_toggle_button_get_active (fd->min);
      fd->charts_opts.min = gtk_spin_button_get_value (fd->min_spin);

      fd->charts_opts.use_max = gtk_toggle_button_get_active (fd->max);
      fd->charts_opts.max = gtk_spin_button_get_value (fd->max_spin);

      fd->charts_opts.draw_hist = gtk_toggle_button_get_active (fd->hist);
      fd->charts_opts.draw_normal = gtk_toggle_button_get_active (fd->normal);
      if (gtk_toggle_button_get_active (fd->freqs))
	fd->charts_opts.scale = FRQ_FREQ;
      else if (gtk_toggle_button_get_active (fd->percents))
	fd->charts_opts.scale = FRQ_PERCENT;

      fd->charts_opts.draw_pie = gtk_toggle_button_get_active (fd->pie);
      fd->charts_opts.pie_include_missing
        = gtk_toggle_button_get_active (fd->pie_include_missing);
    }
}


/* Makes widget W's sensitivity follow the active state of TOGGLE */
static void
sensitive_if_active (GtkToggleButton *toggle, GtkWidget *w)
{
  gboolean active = gtk_toggle_button_get_active (toggle);

  gtk_widget_set_sensitive (w, active);
}

/* Pops up the Frequencies dialog box */
void
frequencies_dialog (PsppireDataWindow *de)
{
  gint response;

  struct frequencies_dialog fd;

  GtkBuilder *xml = builder_new ("frequencies.ui");

  GtkWidget *dialog = get_widget_assert   (xml, "frequencies-dialog");
  GtkWidget *source = get_widget_assert   (xml, "dict-treeview");
  GtkWidget *dest =   get_widget_assert   (xml, "var-treeview");
  GtkWidget *tables_button = get_widget_assert (xml, "tables-button");
  GtkWidget *charts_button = get_widget_assert (xml, "charts-button");
  GtkWidget *stats_treeview = get_widget_assert (xml, "stats-treeview");

  put_checkbox_items_in_treeview (GTK_TREE_VIEW(stats_treeview),
				  B_FS_DEFAULT,
				  N_FREQUENCY_STATS,
				  stats
				  );


  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  g_object_get (de->data_editor, "dictionary", &fd.dict, NULL);
  g_object_set (source, "model", fd.dict, NULL);

  fd.stat_vars = GTK_TREE_VIEW (dest);
  fd.tables_button = get_widget_assert (xml, "tables-button");
  fd.charts_button = get_widget_assert (xml, "charts-button");

  fd.include_missing = GTK_TOGGLE_BUTTON (
    get_widget_assert (xml, "include_missing"));

  fd.stats = gtk_tree_view_get_model (GTK_TREE_VIEW (stats_treeview));

  /* Frequency Tables dialog. */
  fd.tables_dialog = get_widget_assert (xml, "tables-dialog");
  fd.tables_opts.order = FRQ_AVALUE;
  fd.tables_opts.table = FRQ_TABLE;
  fd.tables_opts.limit = 50;

  fd.always = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "always"));
  fd.never = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "never"));
  fd.limit  = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "limit"));
  fd.limit_spinbutton =
    GTK_SPIN_BUTTON (get_widget_assert (xml, "limit-spin"));
  g_signal_connect (fd.limit, "toggled",
		    G_CALLBACK (sensitive_if_active), fd.limit_spinbutton);

  fd.avalue = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "avalue"));
  fd.dvalue = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "dvalue"));
  fd.afreq  = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "afreq"));
  fd.dfreq  = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "dfreq"));

  gtk_window_set_transient_for (GTK_WINDOW (fd.tables_dialog),
                                GTK_WINDOW (de));

  /* Charts dialog. */
  fd.charts_dialog = get_widget_assert (xml, "charts-dialog");
  fd.charts_opts.use_min = false;
  fd.charts_opts.min = 0;
  fd.charts_opts.use_max = false;
  fd.charts_opts.max = 100;
  fd.charts_opts.draw_hist = false;
  fd.charts_opts.draw_normal = false;
  fd.charts_opts.scale = FRQ_FREQ;
  fd.charts_opts.draw_pie = false;
  fd.charts_opts.pie_include_missing = false;

  fd.freqs = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "freqs"));
  fd.percents = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "percents"));

  fd.min = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "min"));
  fd.min_spin = GTK_SPIN_BUTTON (get_widget_assert (xml, "min-spin"));
  g_signal_connect (fd.min, "toggled",
		    G_CALLBACK (sensitive_if_active), fd.min_spin);
  fd.max = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "max"));
  fd.max_spin = GTK_SPIN_BUTTON (get_widget_assert (xml, "max-spin"));
  g_signal_connect (fd.max, "toggled",
		    G_CALLBACK (sensitive_if_active), fd.max_spin);

  fd.hist = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "hist"));
  fd.normal = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "normal"));
  g_signal_connect (fd.hist, "toggled",
		    G_CALLBACK (sensitive_if_active), fd.normal);

  fd.pie = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "pie"));
  fd.pie_include_missing = GTK_TOGGLE_BUTTON (
    get_widget_assert (xml, "pie-include-missing"));
  g_signal_connect (fd.pie, "toggled",
		    G_CALLBACK (sensitive_if_active), fd.pie_include_missing);

  gtk_window_set_transient_for (GTK_WINDOW (fd.charts_dialog),
                                GTK_WINDOW (de));

  /* Main dialog. */
  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &fd);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &fd);

  g_signal_connect_swapped (tables_button, "clicked",
			    G_CALLBACK (on_tables_clicked),  &fd);
  g_signal_connect_swapped (charts_button, "clicked",
			    G_CALLBACK (on_charts_clicked),  &fd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (de, generate_syntax (&fd)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (&fd)));
      break;
    default:
      break;
    }

  g_object_unref (xml);
}
