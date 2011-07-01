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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <config.h>
#include <gtk/gtk.h>

#include "psppire-dialog.h"
#include <gl/xalloc.h>
#include "helper.h"
#include "t-test-options.h"

#include "widget-io.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


enum exclude_mode
  {
    EXCL_ANALYSIS,
    EXCL_LISTWISE
  };

struct tt_options_dialog
{
  GtkWidget *dialog;
  GtkWidget *box;
  GtkWidget *confidence;
  GtkSpinButton *conf_percent;
  GtkToggleButton *analysis;
  GtkToggleButton *listwise;

  gdouble confidence_interval;
  gboolean non_default_options;
  enum exclude_mode excl;
  GtkBuilder *xml;
};

struct tt_options_dialog *
tt_options_dialog_create (GtkWindow *parent)
{
  struct tt_options_dialog *tto = xmalloc (sizeof (*tto));

  tto->xml = builder_new ("t-test.ui");

  tto->confidence =
    widget_scanf (_("Confidence Interval: %2d %%"),
		  &tto->conf_percent);

  tto->dialog = get_widget_assert (tto->xml, "options-dialog");

  tto->box =   get_widget_assert (tto->xml, "vbox1");

  tto->analysis = GTK_TOGGLE_BUTTON (get_widget_assert (tto->xml, "radiobutton1"));
  tto->listwise = GTK_TOGGLE_BUTTON (get_widget_assert (tto->xml, "radiobutton2"));

  gtk_widget_show (tto->confidence);

  psppire_box_pack_start_defaults (GTK_BOX (tto->box), tto->confidence);

  gtk_window_set_transient_for (GTK_WINDOW (tto->dialog), parent);

  tto->confidence_interval = 95;
  tto->excl = EXCL_ANALYSIS;

  return tto;
}


void
tt_options_dialog_destroy (struct tt_options_dialog *tto)
{
  gtk_container_remove (GTK_CONTAINER (tto->box), tto->confidence);
  g_object_unref (tto->xml);
  g_free (tto);
}


void
tt_options_dialog_run (struct tt_options_dialog *tto)
{
  gint response;

  if ( tto->excl == EXCL_ANALYSIS)
    gtk_toggle_button_set_active (tto->analysis, TRUE);
  else
    gtk_toggle_button_set_active (tto->listwise, TRUE);

  gtk_spin_button_set_value (tto->conf_percent, tto->confidence_interval);

  response = psppire_dialog_run (PSPPIRE_DIALOG (tto->dialog));

  if ( response == PSPPIRE_RESPONSE_CONTINUE)
    {
      tto->non_default_options = TRUE;

      tto->confidence_interval = gtk_spin_button_get_value (tto->conf_percent);
      if ( gtk_toggle_button_get_active (tto->analysis) )
	tto->excl = EXCL_ANALYSIS;
      else
	tto->excl = EXCL_LISTWISE;
    }
}

void
tt_options_dialog_append_syntax (const struct tt_options_dialog *tto, GString *str)
{
  g_string_append (str, "\t/MISSING=");

  if ( tto->excl == EXCL_ANALYSIS )
    g_string_append (str, "ANALYSIS");
  else
    g_string_append (str, "LISTWISE");


  g_string_append_printf (str, "\n\t/CRITERIA=CIN(%g)",
			  tto->confidence_interval/100.0);
}
