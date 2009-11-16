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

#include <config.h>

#include "checkbox-treeview.h"
#include "frequencies-dialog.h"

#include <gtk/gtk.h>
#include <stdlib.h>

#include <language/syntax-string-source.h>
#include <ui/gui/psppire-data-window.h>
#include <ui/gui/dialog-common.h>
#include <ui/gui/dict-display.h>
#include <ui/gui/helper.h>
#include <ui/gui/psppire-dialog.h>
#include <ui/gui/psppire-var-store.h>
#include "executor.h"

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

struct format_options
{
  enum frq_order order;
  gboolean use_limits;
  int limit;
};

struct frequencies_dialog
{
  GtkTreeView *stat_vars;
  PsppireDict *dict;

  GtkWidget *table_button;

  GtkWidget *format_dialog;
  GtkWidget *maximum_cats;
  GtkWidget *limit_toggle_button;
  GtkSpinButton *limit_spinbutton;

  GtkToggleButton  *avalue;
  GtkToggleButton  *dvalue;
  GtkToggleButton  *afreq;
  GtkToggleButton  *dfreq;

  struct format_options current_opts;

  GtkTreeModel *stats;
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

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->table_button), TRUE);
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
  append_variable_names (string, fd->dict, GTK_TREE_VIEW (fd->stat_vars), 0);

  g_string_append (string, "\n\t/FORMAT=");

  switch (fd->current_opts.order)
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

  if ( fd->current_opts.use_limits )
    {
      g_string_append_printf (string, "LIMIT (%d)", fd->current_opts.limit);
    }
  else
    {
      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd->table_button)))
	g_string_append (string, "TABLE");
      else
	g_string_append (string, "NOTABLE");
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
on_format_clicked (struct frequencies_dialog *fd)
{
  int ret;
  g_signal_emit_by_name (fd->limit_toggle_button, "toggled");

  switch (fd->current_opts.order)
    {
    case FRQ_AVALUE:
      gtk_toggle_button_set_active (fd->avalue, TRUE);
      break;
    case FRQ_DVALUE:
      gtk_toggle_button_set_active (fd->dvalue, TRUE);
      break;
    case FRQ_ACOUNT:
      gtk_toggle_button_set_active (fd->dfreq, TRUE);
      break;
    case FRQ_DCOUNT:
      gtk_toggle_button_set_active (fd->dfreq, TRUE);
      break;
    };

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd->limit_toggle_button),
				fd->current_opts.use_limits);

  gtk_spin_button_set_value (fd->limit_spinbutton,
			     fd->current_opts.limit);

  ret = psppire_dialog_run (PSPPIRE_DIALOG (fd->format_dialog));

  if ( ret == PSPPIRE_RESPONSE_CONTINUE )
    {
      if (gtk_toggle_button_get_active (fd->avalue))
	fd->current_opts.order = FRQ_AVALUE;
      else if (gtk_toggle_button_get_active (fd->dvalue))
	fd->current_opts.order = FRQ_DVALUE;
      else if (gtk_toggle_button_get_active (fd->afreq))
	fd->current_opts.order = FRQ_ACOUNT;
      else if (gtk_toggle_button_get_active (fd->dfreq))
	fd->current_opts.order = FRQ_DCOUNT;

      fd->current_opts.use_limits = gtk_toggle_button_get_active
	(GTK_TOGGLE_BUTTON (fd->limit_toggle_button));

      fd->current_opts.limit =
	gtk_spin_button_get_value (fd->limit_spinbutton);
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
frequencies_dialog (GObject *o, gpointer data)
{
  gint response;
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);

  struct frequencies_dialog fd;

  GtkBuilder *xml = builder_new ("frequencies.ui");

  GtkWidget *dialog = get_widget_assert   (xml, "frequencies-dialog");
  GtkWidget *source = get_widget_assert   (xml, "dict-treeview");
  GtkWidget *dest =   get_widget_assert   (xml, "var-treeview");
  GtkWidget *selector = get_widget_assert (xml, "selector1");
  GtkWidget *format_button = get_widget_assert (xml, "button1");
  GtkWidget *stats_treeview = get_widget_assert (xml, "stats-treeview");

  PsppireVarStore *vs = NULL;

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  put_checkbox_items_in_treeview (GTK_TREE_VIEW(stats_treeview),
				  B_FS_DEFAULT,
				  N_FREQUENCY_STATS,
				  stats
				  );


  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  g_object_get (vs, "dictionary", &fd.dict, NULL);
  g_object_set (source, "model", fd.dict, NULL);


  set_dest_model (GTK_TREE_VIEW (dest), fd.dict);


  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector),
				 insert_source_row_into_tree_view,
				 NULL,
				 NULL);


  fd.stat_vars = GTK_TREE_VIEW (dest);
  fd.table_button = get_widget_assert (xml, "checkbutton1");
  fd.format_dialog = get_widget_assert (xml, "format-dialog");
  fd.maximum_cats = get_widget_assert (xml, "hbox5");
  fd.limit_toggle_button = get_widget_assert (xml, "checkbutton2");
  fd.limit_spinbutton =
    GTK_SPIN_BUTTON (get_widget_assert (xml, "spinbutton1"));

  fd.stats = gtk_tree_view_get_model (GTK_TREE_VIEW (stats_treeview));

  fd.avalue = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "radiobutton1"));
  fd.dvalue = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "radiobutton2"));
  fd.afreq  = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "radiobutton3"));
  fd.dfreq  = GTK_TOGGLE_BUTTON (get_widget_assert (xml, "radiobutton4"));

  fd.current_opts.order = FRQ_AVALUE;
  fd.current_opts.use_limits = FALSE;
  fd.current_opts.limit = 50;


  gtk_window_set_transient_for (GTK_WINDOW (fd.format_dialog), GTK_WINDOW (de));


  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &fd);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      dialog_state_valid, &fd);


  g_signal_connect_swapped (format_button, "clicked",
			    G_CALLBACK (on_format_clicked),  &fd);

  g_signal_connect (fd.limit_toggle_button, "toggled",
		    G_CALLBACK (sensitive_if_active), fd.maximum_cats);


  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (&fd);

	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (&fd);
	paste_syntax_in_new_window (syntax);
	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (xml);
}
