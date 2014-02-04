/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2008, 2009, 2010, 2011, 2014 Free Software Foundation, Inc.

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

#include "select-cases-dialog.h"
#include <float.h>
#include <gtk/gtk.h>
#include "executor.h"
#include "psppire-dialog.h"
#include "psppire-data-window.h"
#include "psppire-selector.h"
#include "dict-display.h"
#include "dialog-common.h"
#include "widget-io.h"
#include "psppire-scanf.h"
#include "builder-wrapper.h"
#include "helper.h"

#include <xalloc.h>


#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid



/* FIXME: These shouldn't be here */
#include "psppire-data-store.h"


struct select_cases_dialog
{
  /* The XML that created the dialog */
  GtkBuilder *xml;

  GtkWidget *spinbutton ;
  GtkWidget *spinbutton1 ;
  GtkWidget *spinbutton2 ;

  GtkWidget *hbox1;
  GtkWidget *hbox2;

  PsppireDataStore *data_store;
};

static gchar * generate_syntax (const struct select_cases_dialog *scd);


static const gchar label1[]=N_("Approximately %3d%% of all cases.");
static const gchar label2[]=N_("Exactly %3d cases from the first %3d cases.");


static void
sample_subdialog (GtkButton *b, gpointer data)
{
  gint response;
  struct select_cases_dialog *scd = data;

  gint case_count = psppire_data_store_get_case_count (scd->data_store);

  GtkWidget *parent_dialog = get_widget_assert (scd->xml,
						"select-cases-dialog");
  GtkWidget *dialog = get_widget_assert (scd->xml,
					 "select-cases-random-sample-dialog");
  GtkWidget *percent = get_widget_assert (scd->xml,
					  "radiobutton-sample-percent");
  GtkWidget *sample_n_cases = get_widget_assert (scd->xml,
						 "radiobutton-sample-n-cases");
  GtkWidget *table = get_widget_assert (scd->xml,
					"select-cases-random-sample-table");

  if ( ! scd->hbox1 )
    {
      scd->hbox1 = psppire_scanf_new (gettext (label1), &scd->spinbutton);

      gtk_widget_show (scd->hbox1);

      gtk_table_attach_defaults (GTK_TABLE (table),
				 scd->hbox1, 1, 2, 0, 1);

      g_signal_connect (percent, "toggled",
			G_CALLBACK (set_sensitivity_from_toggle), scd->hbox1);

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (percent), TRUE);
    }


  if ( ! scd->hbox2 )
    {
      scd->hbox2 =
	psppire_scanf_new (gettext (label2), &scd->spinbutton1, &scd->spinbutton2);

      gtk_spin_button_set_range (GTK_SPIN_BUTTON (scd->spinbutton1),
				 1, case_count);

      gtk_spin_button_set_range (GTK_SPIN_BUTTON (scd->spinbutton2),
				 1, case_count);

      gtk_widget_show (scd->hbox2);
      gtk_widget_set_sensitive (scd->hbox2, FALSE);

      gtk_table_attach_defaults (GTK_TABLE (table),
				 scd->hbox2, 1, 2, 1, 2);

      g_signal_connect (sample_n_cases, "toggled",
			G_CALLBACK (set_sensitivity_from_toggle), scd->hbox2);

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sample_n_cases), FALSE);
    }


  gtk_window_set_transient_for (GTK_WINDOW (dialog),
				GTK_WINDOW (parent_dialog));

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  if ( response != PSPPIRE_RESPONSE_CONTINUE)
    {
      g_signal_handlers_disconnect_by_func
	(G_OBJECT (percent),
	 G_CALLBACK (set_sensitivity_from_toggle),
	 scd->hbox1);

      g_signal_handlers_disconnect_by_func
	(G_OBJECT (sample_n_cases),
	 G_CALLBACK (set_sensitivity_from_toggle),
	 scd->hbox2);

      gtk_widget_destroy(scd->hbox1);
      gtk_widget_destroy(scd->hbox2);
      scd->hbox1 = scd->hbox2 = NULL;
    }
  else
    {
      gchar *text;
      GtkWidget *l0 = get_widget_assert (scd->xml, "random-sample-label");

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (percent)))
	{
	  text = widget_printf (gettext(label1), scd->spinbutton);
	  gtk_label_set_text (GTK_LABEL (l0), text);
	}
      else
	{
	  text =
	    widget_printf (gettext(label2), scd->spinbutton1, scd->spinbutton2);
	  gtk_label_set_text (GTK_LABEL (l0), text);

	}
      g_free (text);

    }
}

static void
range_subdialog (GtkButton *b, gpointer data)
{
  gint response;
  struct select_cases_dialog *scd = data;

  gint n_cases = psppire_data_store_get_case_count (scd->data_store);

  GtkWidget *parent_dialog = get_widget_assert (scd->xml,
						"select-cases-dialog");

  GtkWidget *dialog = get_widget_assert (scd->xml,
					 "select-cases-range-dialog");

  GtkWidget *first = get_widget_assert (scd->xml,
					"range-dialog-first");

  GtkWidget *last = get_widget_assert (scd->xml,
					"range-dialog-last");


  gtk_spin_button_set_range (GTK_SPIN_BUTTON (last),  1,  n_cases);
  gtk_spin_button_set_range (GTK_SPIN_BUTTON (first), 1,  n_cases);

  gtk_window_set_transient_for (GTK_WINDOW (dialog),
				GTK_WINDOW (parent_dialog));


  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));
  if ( response == PSPPIRE_RESPONSE_CONTINUE)
    {
      GtkWidget *first = get_widget_assert (scd->xml, "range-dialog-first");
      GtkWidget *last = get_widget_assert (scd->xml, "range-dialog-last");
      GtkWidget *l1 = get_widget_assert (scd->xml, "range-sample-label");
      gchar *text = widget_printf (_("%d thru %d"), first, last);

      gtk_label_set_text (GTK_LABEL (l1), text);

      g_free (text);
    }
}

static void
set_radiobutton (GtkWidget *button, gpointer data)
{
  GtkToggleButton *toggle = data;
  gtk_toggle_button_set_active (toggle, TRUE);
}

/* Pops up the Select Cases dialog box */
void
select_cases_dialog (PsppireDataWindow *de)
{
  gint response;
  struct select_cases_dialog scd = {0,0,0,0,0,0};
  GtkWidget *dialog   ;
  GtkWidget *entry = NULL;
  GtkWidget *selector ;
  GtkWidget *button_range;
  GtkWidget *button_sample;

  scd.xml = builder_new ("select-cases.ui");

  g_object_get (de->data_editor, "data-store", &scd.data_store, NULL);

  button_range = get_widget_assert (scd.xml, "button-range");
  button_sample = get_widget_assert (scd.xml, "button-sample");
  entry = get_widget_assert (scd.xml, "filter-variable-entry");
  selector = get_widget_assert (scd.xml, "psppire-selector-filter");

  {
    GtkWidget *button_if =
      get_widget_assert (scd.xml, "button-if");

    GtkWidget *radiobutton_if =
      get_widget_assert (scd.xml, "radiobutton-if");

    GtkWidget *radiobutton_all =
      get_widget_assert (scd.xml, "radiobutton-all");

    GtkWidget *radiobutton_sample =
      get_widget_assert (scd.xml, "radiobutton-sample");

    GtkWidget *radiobutton_range =
      get_widget_assert (scd.xml, "radiobutton-range");

    GtkWidget *radiobutton_filter =
      get_widget_assert (scd.xml, "radiobutton-filter-variable");

    GtkWidget *range_label =
      get_widget_assert (scd.xml, "range-sample-label");

    GtkWidget *sample_label =
      get_widget_assert (scd.xml, "random-sample-label");

    g_signal_connect (radiobutton_all, "toggled",
		      G_CALLBACK (set_sensitivity_from_toggle_invert),
		      get_widget_assert (scd.xml, "filter-delete-button-box")
		      );

    g_signal_connect (button_if, "clicked",
		      G_CALLBACK (set_radiobutton), radiobutton_if);

    g_signal_connect (button_sample, "clicked",
		      G_CALLBACK (set_radiobutton), radiobutton_sample);

    g_signal_connect (button_range,  "clicked",
		      G_CALLBACK (set_radiobutton), radiobutton_range);

    g_signal_connect (selector, "clicked",
		      G_CALLBACK (set_radiobutton), radiobutton_filter);

    g_signal_connect (selector, "selected",
		      G_CALLBACK (set_radiobutton), radiobutton_filter);

    g_signal_connect (radiobutton_range, "toggled",
		      G_CALLBACK (set_sensitivity_from_toggle),
		      range_label
		      );

    g_signal_connect (radiobutton_sample, "toggled",
		      G_CALLBACK (set_sensitivity_from_toggle),
		      sample_label
		      );

    g_signal_connect (radiobutton_filter, "toggled",
		      G_CALLBACK (set_sensitivity_from_toggle),
		      entry
		      );
  }



  dialog = get_widget_assert (scd.xml, "select-cases-dialog");
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  {
    GtkWidget *source = get_widget_assert   (scd.xml, "select-cases-treeview");

    g_object_set (source, "model",
		  scd.data_store->dict,
		  "selection-mode",
		  GTK_SELECTION_SINGLE, NULL);

    psppire_selector_set_filter_func (PSPPIRE_SELECTOR (selector),
				   is_currently_in_entry);
  }



  g_signal_connect (button_range,
		    "clicked", G_CALLBACK (range_subdialog), &scd);


  g_signal_connect (button_sample,
		    "clicked", G_CALLBACK (sample_subdialog), &scd);


  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (de, generate_syntax (&scd)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (&scd)));
      break;
    default:
      break;
    }

  g_object_unref (scd.xml);
}


static gchar *
generate_syntax_filter (const struct select_cases_dialog *scd)
{
  gchar *text = NULL;
  struct string dss;

  const gchar *filter = "filter_$";
  const gchar key[]="case_$";

  ds_init_empty (&dss);

  if (gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (get_widget_assert (scd->xml,
					      "radiobutton-range"))))
    {
      GtkSpinButton *first =
	GTK_SPIN_BUTTON (get_widget_assert (scd->xml,
					   "range-dialog-first"));

      GtkSpinButton *last =
	GTK_SPIN_BUTTON (get_widget_assert (scd->xml,
					   "range-dialog-last"));

      ds_put_c_format (&dss,
			      "COMPUTE filter_$ = ($CASENUM >= %ld "
			       "AND $CASENUM <= %ld).\n",
			      (long) gtk_spin_button_get_value (first),
			      (long) gtk_spin_button_get_value (last)
			      );

      ds_put_cstr (&dss, "EXECUTE.\n");
    }
  else if ( gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (get_widget_assert (scd->xml,
					      "radiobutton-sample"))))
    {
      GtkWidget *random_sample =
	get_widget_assert (scd->xml,
			   "radiobutton-sample-percent");

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (random_sample)))
	{
	  const double percentage =
	    gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->spinbutton));

	  ds_put_c_format (&dss,
				  "COMPUTE %s = RV.UNIFORM (0,1) < %.*g.\n",
				  filter,
                                  DBL_DIG + 1, percentage / 100.0 );
	}
      else
	{
	  const gint n_cases =
	    gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->spinbutton1));
	  const gint from_n_cases =
	    gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->spinbutton2));


	  const gchar ranvar[]="rv_$";

	  ds_put_c_format (&dss,
				  "COMPUTE %s = $CASENUM.\n", key);

	  ds_put_c_format (&dss,
				  "COMPUTE %s = %s > %d.\n",
				  filter, key, from_n_cases);

	  ds_put_c_format (&dss,
				  "COMPUTE %s = RV.UNIFORM (0, 1).\n",
				  ranvar);

	  ds_put_c_format (&dss,
				  "SORT BY %s, %s.\n",
				  filter, ranvar);

	  ds_put_cstr (&dss, "EXECUTE.\n");
				  

	  ds_put_c_format (&dss,
				  "COMPUTE %s = $CASENUM.\n",
				  filter );

	  ds_put_c_format (&dss,
				  "COMPUTE %s = %s <= %d\n",
				  filter,
				  filter,
				  n_cases );

	  ds_put_cstr (&dss, "EXECUTE.\n");


	  ds_put_c_format (&dss,
				  "SORT BY %s.\n",
				  key);

	  ds_put_c_format (&dss,
				  "DELETE VARIABLES %s, %s.\n",
				  key, ranvar);
	}

      ds_put_cstr (&dss, "EXECUTE.\n");
    }
  else
    {
      GtkEntry *entry =
	GTK_ENTRY (get_widget_assert (scd->xml,
				      "filter-variable-entry"));
      filter = gtk_entry_get_text (entry);
    }

  ds_put_c_format (&dss, "FILTER BY %s.\n", filter);

  text  = ds_steal_cstr (&dss);

  ds_destroy (&dss);

  return text;
}

static gchar *
generate_syntax_delete (const struct select_cases_dialog *scd)
{
  gchar *text = NULL;
  struct string dss;

  if ( gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (get_widget_assert (scd->xml,
					      "radiobutton-all"))))
    {
      return xstrdup ("\n");
    }

  ds_init_empty (&dss);

  if ( gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (get_widget_assert (scd->xml,
					      "radiobutton-sample"))))
  {
    GtkWidget *random_sample =
      get_widget_assert (scd->xml,
			 "radiobutton-sample-percent");

    ds_put_cstr (&dss, "SAMPLE ");

    if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (random_sample)))
      {
	const double percentage =
	  gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->spinbutton));
	ds_put_c_format (&dss, "%g.", percentage / 100.0);
      }
    else
      {
	const gint n_cases =
	  gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->spinbutton1));
	const gint from_n_cases =
	  gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->spinbutton2));

      	ds_put_c_format (&dss, "%d FROM %d .", n_cases, from_n_cases);
      }

  }
  else if ( gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (get_widget_assert (scd->xml,
						   "radiobutton-range"))))
    {
      GtkSpinButton *first =
	GTK_SPIN_BUTTON (get_widget_assert (scd->xml,
					   "range-dialog-first"));

      GtkSpinButton *last =
	GTK_SPIN_BUTTON (get_widget_assert (scd->xml,
					   "range-dialog-last"));

      ds_put_c_format (&dss,
			      "COMPUTE filter_$ = ($CASENUM >= %ld "
			       "AND $CASENUM <= %ld).\n",
			      (long) gtk_spin_button_get_value (first),
			      (long) gtk_spin_button_get_value (last)
			      );
      ds_put_cstr (&dss, "EXECUTE.\n");
      ds_put_c_format (&dss, "SELECT IF filter_$.\n");

    }
  else if ( gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON
	     (get_widget_assert (scd->xml,
				 "radiobutton-filter-variable"))))
    {
      GtkEntry *entry =
	GTK_ENTRY (get_widget_assert (scd->xml,
				      "filter-variable-entry"));

      ds_put_c_format (&dss, "SELECT IF (%s <> 0).",
			      gtk_entry_get_text (entry));
    }


  ds_put_cstr (&dss, "\n");

  text = ds_steal_cstr (&dss);

  ds_destroy (&dss);

  return text;
}


static gchar *
generate_syntax (const struct select_cases_dialog *scd)
{
  /* In the simple case, all we need to do is cancel any existing filter */
  if ( gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (get_widget_assert (scd->xml,
					      "radiobutton-all"))))
    {
      return g_strdup ("FILTER OFF.\n");
    }


  /* Are we filtering or deleting ? */
  if ( gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (get_widget_assert (scd->xml,
					      "radiobutton-delete"))))
    {
      return generate_syntax_delete (scd);
    }
  else
    {
      return generate_syntax_filter (scd);
    }

}



