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

#include "weight-cases-dialog.h"
#include "psppire-selector.h"
#include "psppire-dialog.h"
#include "executor.h"
#include "psppire-data-window.h"
#include "dict-display.h"
#include <language/syntax-string-source.h>
#include "helper.h"

#include <gtk/gtk.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


#include "psppire-var-store.h"

static void
on_select (PsppireSelector *sel, gpointer data)
{
  GtkRadioButton *radiobutton2 = data;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radiobutton2), TRUE);
}

static void
on_deselect (PsppireSelector *sel, gpointer data)
{
  GtkRadioButton *radiobutton1 = data;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radiobutton1), TRUE);
}


static void
on_toggle (GtkToggleButton *button, gpointer data)
{
  GtkEntry *entry = data;
  if ( gtk_toggle_button_get_active (button))
    gtk_entry_set_text (entry, "");
}

struct weight_cases_dialog
{
  PsppireDict *dict;
  GtkEntry *entry;
  GtkLabel *status;
  GtkToggleButton *off;
  GtkToggleButton *on;
};

static void
refresh (PsppireDialog *dialog, const struct weight_cases_dialog *wcd)
{
  const struct variable *var = dict_get_weight (wcd->dict->dict);

  if ( ! var )
    {
      gtk_entry_set_text (wcd->entry, "");
      gtk_label_set_text (wcd->status, _("Do not weight cases"));
      gtk_toggle_button_set_active (wcd->off, TRUE);
    }
  else
    {
      gchar *text =
	g_strdup_printf (_("Weight cases by %s"), var_get_name (var));

      gtk_entry_set_text (wcd->entry, var_get_name (var));
      gtk_label_set_text (wcd->status, text);

      g_free (text);
      gtk_toggle_button_set_active (wcd->on, TRUE);
    }

  g_signal_emit_by_name (wcd->entry, "activate");
}


static gchar * generate_syntax (const struct weight_cases_dialog *wcd);


/* Pops up the Weight Cases dialog box */
void
weight_cases_dialog (GObject *o, gpointer data)
{
  gint response;
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);
  struct weight_cases_dialog wcd;

  GtkBuilder *xml = builder_new ("psppire.ui");

  GtkWidget *dialog = get_widget_assert (xml, "weight-cases-dialog");
  GtkWidget *source = get_widget_assert (xml, "weight-cases-treeview");
  GtkWidget *entry = get_widget_assert (xml, "weight-cases-entry");
  GtkWidget *selector = get_widget_assert (xml, "weight-cases-selector");
  GtkWidget *radiobutton1 = get_widget_assert (xml,
					       "weight-cases-radiobutton1");
  GtkWidget *radiobutton2 = get_widget_assert (xml, "radiobutton2");
  GtkWidget *status  = get_widget_assert (xml, "weight-status-label");

  PsppireVarStore *vs = NULL;

  g_object_get (de->data_editor, "var-store", &vs,  NULL);
  g_object_get (vs, "dictionary", &wcd.dict, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  g_signal_connect (radiobutton1, "toggled", G_CALLBACK (on_toggle), entry);
  g_signal_connect (selector, "selected", G_CALLBACK (on_select),
		    radiobutton2);

  g_signal_connect (selector, "de-selected", G_CALLBACK (on_deselect),
		    radiobutton1);

  
  g_object_set (source, "model", wcd.dict,
				 "selection-mode", GTK_SELECTION_SINGLE,
				 "predicate", var_is_numeric,
				 NULL);

  psppire_selector_set_filter_func (PSPPIRE_SELECTOR (selector),
				    is_currently_in_entry);


  wcd.entry = GTK_ENTRY (entry);
  wcd.status = GTK_LABEL (status);
  wcd.off = GTK_TOGGLE_BUTTON (radiobutton1);
  wcd.on = GTK_TOGGLE_BUTTON (radiobutton2);

  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &wcd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  g_object_unref (xml);

  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (&wcd);
	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (&wcd);
        paste_syntax_in_new_window (syntax);
	g_free (syntax);
      }
      break;
    default:
      break;
    }
}


static gchar *
generate_syntax (const struct weight_cases_dialog *wcd)
{
  gchar *syntax;

  const gchar *text  = gtk_entry_get_text (wcd->entry);

  struct variable *var = psppire_dict_lookup_var (wcd->dict, text);

  if ( var == NULL)
    syntax = g_strdup ("WEIGHT OFF.");
  else
    syntax = g_strdup_printf ("WEIGHT BY %s.\n",
			      var_get_name (var));

  return syntax;
}
