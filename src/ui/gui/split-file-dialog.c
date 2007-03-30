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

#include "split-file-dialog.h"
#include "psppire-selector.h"
#include "psppire-dialog.h"
#include "helper.h"
#include "data-editor.h"
#include "dict-display.h"
#include <language/syntax-string-source.h>
#include "syntax-editor.h"

#include <gtk/gtk.h>
#include <glade/glade.h>


#include "dialog-common.h"

/* FIXME: These shouldn't be here */
#include <gtksheet/gtksheet.h>
#include "psppire-var-store.h"



static gchar *
generate_syntax (GladeXML *xml, PsppireDict *dict)
{
  gchar *text;
  GtkWidget *off = get_widget_assert (xml, "split-radiobutton0");

  GtkWidget *vars =
    get_widget_assert (xml, "split-file-grouping-vars");

  GString *string = g_string_new ("SPLIT FILE OFF.");

  if ( ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (off)))
    {
      GString * varlist = g_string_sized_new (80);
      GtkWidget *sort = get_widget_assert (xml, "split-radiobutton3");
      GtkWidget *layered = get_widget_assert (xml, "split-radiobutton1");
      gint n_vars = append_variable_names (varlist,
					   dict, GTK_TREE_VIEW (vars));

      if ( n_vars > 0 )
	{
	  g_string_assign (string, "");

	  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(sort)))
	    {
	      g_string_append (string, "SORT CASES BY");
	      g_string_append (string, varlist->str);
	      g_string_append (string, ".\n");
	    }

	  g_string_append (string, "SPLIT FILE ");

	  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (layered)))
	    g_string_append (string, "LAYERED ");
	  else
	    g_string_append (string, "SEPARATE ");

	  g_string_append (string, "BY ");
	  g_string_append (string, varlist->str);
	  g_string_append (string, ".");
	}
      g_string_free (varlist, TRUE);
    }

  text =  string->str;

  g_string_free (string, FALSE);

  return text;
};


static void
on_off_toggled (GtkToggleButton *togglebutton,
		gpointer         user_data)
{
  GladeXML *xml = user_data;
  GtkWidget *dest =   get_widget_assert (xml, "split-file-grouping-vars");
  GtkWidget *selector = get_widget_assert (xml, "split-file-selector");
  GtkWidget *source = get_widget_assert (xml, "split-file-dict-treeview");
  GtkWidget *button3 = get_widget_assert (xml, "split-radiobutton3");
  GtkWidget *button4 = get_widget_assert (xml, "split-radiobutton4");

  gboolean state = !gtk_toggle_button_get_active (togglebutton);

  gtk_widget_set_sensitive (dest, state);
  gtk_widget_set_sensitive (selector, state);
  gtk_widget_set_sensitive (source, state);
  gtk_widget_set_sensitive (button3, state);
  gtk_widget_set_sensitive (button4, state);
}

static void
refresh (GladeXML *xml)
{
  GtkWidget *off = get_widget_assert (xml, "split-radiobutton0");

  g_print ("%s\n", __FUNCTION__);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(off), TRUE);
  gtk_toggle_button_toggled (GTK_TOGGLE_BUTTON(off));
}



/* Pops up the Weight Cases dialog box */
void
split_file_dialog (GObject *o, gpointer data)
{
  gint response;
  struct data_editor *de = data;
  PsppireDict *dict;

  GladeXML *xml = XML_NEW ("psppire.glade");

  GtkWidget *dialog = get_widget_assert   (xml, "split-file-dialog");
  GtkWidget *source = get_widget_assert   (xml, "split-file-dict-treeview");
  GtkWidget *dest =   get_widget_assert   (xml, "split-file-grouping-vars");
  GtkWidget *selector = get_widget_assert (xml, "split-file-selector");
  GtkWidget *on_off = get_widget_assert   (xml, "split-radiobutton0");

  GtkSheet *var_sheet =
    GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

  PsppireVarStore *vs = PSPPIRE_VAR_STORE (gtk_sheet_get_model (var_sheet));

  dict = vs->dict;

  attach_dictionary_to_treeview (GTK_TREE_VIEW (source),
				 vs->dict,
				 GTK_SELECTION_MULTIPLE, NULL);


  g_signal_connect (on_off, "toggled", G_CALLBACK(on_off_toggled),  xml);


  set_dest_model (GTK_TREE_VIEW (dest), vs->dict);

  psppire_selector_set_subjects (PSPPIRE_SELECTOR (selector),
				 source,
				 dest,
				 insert_source_row_into_tree_view,
				 NULL);

  refresh (xml);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (xml, dict);
	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (xml, dict);

	struct syntax_editor *se =
	  (struct syntax_editor *) window_create (WINDOW_SYNTAX, NULL);

	gtk_text_buffer_insert_at_cursor (se->buffer, syntax, -1);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

  g_object_unref (xml);
}

