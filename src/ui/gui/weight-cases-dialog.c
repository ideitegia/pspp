/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2007  Free Software Foundation

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA. */


#include <config.h>

#include "psppire-var-select.h"
#include <glade/glade.h>
#include "helper.h"
#include <gettext.h>

#include "psppire-var-store.h"
#include "weight-cases-dialog.h"

#include "psppire-dialog.h"

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void
refresh_var_select (PsppireVarSelect *vs)
{
  struct variable *weight;

  psppire_var_select_deselect_all (vs);

  weight = psppire_dict_get_weight_variable (vs->dict);

  if ( weight )
    psppire_var_select_set_variable (vs, weight);
}


static void
on_refresh (GtkWidget *dialog, gpointer data)
{
  refresh_var_select (data);
}


static void
on_radiobutton_toggle (GtkToggleButton *button, gpointer data)
{
  PsppireVarSelect *vs = data;
  if ( gtk_toggle_button_get_active (button) )
    {
      psppire_var_select_deselect_all (vs);
    }
}


/* Callback for when new variable is selected.
   IDX is the dict index of the variable selected.
   Updates the label and toggle buttons in the dialog box
   to reflect this new selection. */
static void
select_var_callback (PsppireVarSelect *vs, gint idx, gpointer data)
{
  GladeXML * xml  = data;

  GtkWidget *label =  get_widget_assert (xml, "weight-status-label");

  GtkWidget *radiobutton2 =  get_widget_assert (xml, "radiobutton2");

  struct variable *var = psppire_dict_get_variable (vs->dict, idx);

  gtk_label_set_text (GTK_LABEL (label), var_get_name(var));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radiobutton2), TRUE);
}



static void
deselect_all (PsppireVarSelect *vs, gpointer data)
{
  GladeXML * xml  = data;

  GtkWidget *label =  get_widget_assert (xml, "weight-status-label");

  GtkWidget *radiobutton1 =  get_widget_assert (xml, "radiobutton1");

  gtk_label_set_text (GTK_LABEL (label), _("Do not weight cases"));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radiobutton1), TRUE);
}



PsppireDialog *
create_weight_dialog (PsppireVarSelect *select, GladeXML *xml)
{
  GtkWidget *dialog =  get_widget_assert (xml, "weight-cases-dialog");
  GtkWidget *radiobutton1 =  get_widget_assert (xml, "radiobutton1");

  g_signal_connect (dialog, "refresh", G_CALLBACK (on_refresh), select);

  g_signal_connect (select, "variable-selected",
		    G_CALLBACK (select_var_callback), xml);

  g_signal_connect (select, "deselect-all",
		    G_CALLBACK (deselect_all), xml);

  g_signal_connect (radiobutton1, "toggled",
		    G_CALLBACK (on_radiobutton_toggle),
		    select);

  refresh_var_select (select);

  return PSPPIRE_DIALOG (dialog);
}
