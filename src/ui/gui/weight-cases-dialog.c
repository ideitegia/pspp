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

#include "weight-cases-dialog.h"
#include "psppire-selector.h"
#include "psppire-dialog.h"
#include "helper.h"
#include "data-editor.h"
#include "dict-display.h"
#include <language/syntax-string-source.h>
#include "syntax-editor.h"

#include <gtk/gtk.h>
#include <glade/glade.h>

/* FIXME: These shouldn't be here */
#include <gtksheet/gtksheet.h>
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




static gchar * generate_syntax (PsppireDict *, GtkEntry *);


/* Pops up the Weight Cases dialog box */
void
weight_cases_dialog (GObject *o, gpointer data)
{
  gint response;
  struct data_editor *de = data;
  PsppireDict *dict;
  struct variable *var;

  GladeXML *xml = glade_xml_new (PKGDATADIR "/psppire.glade",
				 "weight-cases-dialog", NULL);

  GtkWidget *dialog = get_widget_assert (xml, "weight-cases-dialog");
  GtkWidget *source = get_widget_assert (xml, "weight-cases-treeview");
  GtkWidget *entry = get_widget_assert (xml, "weight-cases-entry");
  GtkWidget *selector = get_widget_assert (xml, "weight-cases-selector");
  GtkWidget *radiobutton1 = get_widget_assert (xml,
					       "weight-cases-radiobutton1");
  GtkWidget *radiobutton2 = get_widget_assert (xml, "radiobutton2");

  GtkSheet *var_sheet =
    GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

  PsppireVarStore *vs = PSPPIRE_VAR_STORE (gtk_sheet_get_model (var_sheet));

  dict = vs->dict;

  g_signal_connect (radiobutton1, "toggled", G_CALLBACK (on_toggle), entry);
  g_signal_connect (selector, "selected", G_CALLBACK (on_select),
		    radiobutton2);

  g_signal_connect (selector, "de-selected", G_CALLBACK (on_deselect),
		    radiobutton1);

  attach_dictionary_to_treeview (GTK_TREE_VIEW (source),
				 dict,
				 GTK_SELECTION_SINGLE,
				 var_is_numeric
				 );

  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector),
				 source,
				 entry,
				 insert_source_row_into_entry,
				 is_currently_in_entry
				 );

  var = dict_get_weight (dict->dict);
  if ( ! var )
    gtk_entry_set_text (GTK_ENTRY (entry), "");
  else
    gtk_entry_set_text (GTK_ENTRY (entry), var_get_name (var));

  g_signal_emit_by_name (entry, "activate");

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  g_object_unref (xml);

  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (dict, GTK_ENTRY (entry));
	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (dict, GTK_ENTRY (entry));

	struct syntax_editor *se =
	  (struct syntax_editor *) window_create (WINDOW_SYNTAX, NULL);

	gtk_text_buffer_insert_at_cursor (se->buffer, syntax, -1);

	g_free (syntax);
      }
      break;
    default:
      break;
    }
}


static gchar *
generate_syntax (PsppireDict *dict, GtkEntry *entry)
{
  gchar *syntax;

  const gchar *text  = gtk_entry_get_text (entry);

  struct variable *var = psppire_dict_lookup_var (dict, text);

  if ( var == NULL)
    syntax = g_strdup ("WEIGHT OFF.");
  else
    syntax = g_strdup_printf ("WEIGHT BY %s.\n",
			      var_get_name (var));

  return syntax;
}
