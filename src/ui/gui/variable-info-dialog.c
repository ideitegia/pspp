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
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <gtksheet/gtksheet.h>

#include "dict-display.h"
#include "var-display.h"
#include <data/variable.h>
#include <data/value-labels.h>
#include "data-editor.h"
#include "psppire-dialog.h"
#include "psppire-var-store.h"
#include "helper.h"
#include <gtksheet/gtksheet.h>

#include <language/syntax-string-source.h>
#include "syntax-editor.h"


#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static struct variable *
get_selected_variable (GtkTreeView *treeview)
{
  struct variable *var;
  GtkTreeModel *top_model;
  GtkTreeIter top_iter;

  GtkTreeModel *model;
  GtkTreeIter iter;

  GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);

  if (! gtk_tree_selection_get_selected (selection,
					 &top_model, &top_iter))
    {
      return NULL;
    }

  get_base_model (top_model, &top_iter, &model, &iter);

  g_assert (PSPPIRE_IS_DICT (model));

  gtk_tree_model_get (model,
		      &iter, DICT_TVM_COL_VAR, &var, -1);

  return var;
}



static void
populate_text (GtkTreeView *treeview, gpointer data)
{
  gchar *text = 0;
  GString *gstring;

  GtkTextBuffer *textbuffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW(data));
  const struct variable *var = get_selected_variable (treeview);

  gstring = g_string_sized_new (200);
  text = name_to_string (var, NULL);
  g_string_assign (gstring, text);
  g_free (text);
  g_string_append (gstring, "\n");


  text = label_to_string (var, NULL);
  g_string_append_printf (gstring, _("Label: %s\n"), text);
  g_free (text);

  {
    const struct fmt_spec *fmt = var_get_print_format (var);
    char buffer[FMT_STRING_LEN_MAX + 1];

    fmt_to_string (fmt, buffer);
    /* No conversion necessary.  buffer will always be ascii */
    g_string_append_printf (gstring, _("Type: %s\n"), buffer);
  }

  text = missing_values_to_string (var, NULL);
  g_string_append_printf (gstring, _("Missing Values: %s\n"),
			  text);
  g_free (text);

  text = measure_to_string (var, NULL);
  g_string_append_printf (gstring, _("Measurement Level: %s\n"),
			  text);
  g_free (text);



  /* Value Labels */
  if ( var_has_value_labels (var))
    {
      struct val_labs_iterator *vli = 0;
      struct val_lab *vl;
      const struct val_labs *labs = var_get_value_labels (var);

      g_string_append (gstring, "\n");
      g_string_append (gstring, _("Value Labels:\n"));

#if 1
      for (vl = val_labs_first_sorted (labs, &vli);
	   vl;
	   vl = val_labs_next (labs, &vli))
	{
	  gchar *const vstr  =
	    value_to_text (vl->value,  *var_get_print_format (var));

	  text = pspp_locale_to_utf8 (vl->label, -1, NULL);

	  g_string_append_printf (gstring, _("%s %s\n"), vstr, text);

	  g_free (text);
	  g_free (vstr);
	}
#endif
    }

  gtk_text_buffer_set_text (textbuffer, gstring->str, gstring->len);

  g_string_free (gstring, TRUE);
}

static gchar * generate_syntax (GtkTreeView *treeview);


void
variable_info_dialog (GObject *o, gpointer data)
{
  struct data_editor *de = data;

  gint response ;

  GladeXML *xml = XML_NEW ("psppire.glade");

  GtkWidget *dialog = get_widget_assert (xml, "variable-info-dialog");
  GtkWidget *treeview = get_widget_assert (xml, "treeview2");
  GtkWidget *textview = get_widget_assert (xml, "textview1");

  GtkSheet *var_sheet =
    GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));


  PsppireVarStore *vs = PSPPIRE_VAR_STORE (gtk_sheet_get_model (var_sheet));

  gtk_window_set_transient_for (GTK_WINDOW (dialog), de->parent.window);

  attach_dictionary_to_treeview (GTK_TREE_VIEW (treeview),
				 vs->dict,
				 GTK_SELECTION_SINGLE,
				 NULL );


  g_signal_connect (treeview, "cursor-changed", G_CALLBACK (populate_text),
		    textview);


  gtk_text_view_set_indent (GTK_TEXT_VIEW (textview), -5);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  switch (response)
    {
    case PSPPIRE_RESPONSE_GOTO:
      {
	gint row, col;
	GtkNotebook *notebook = GTK_NOTEBOOK
	  (get_widget_assert (de->xml,"notebook"));

	const struct variable *var =
	  get_selected_variable (GTK_TREE_VIEW (treeview));


	if (PAGE_VAR_SHEET == gtk_notebook_get_current_page (notebook))
	  {
	    GtkSheet *var_sheet =
	      GTK_SHEET (get_widget_assert (de->xml, "variable_sheet"));

	    gtk_sheet_get_active_cell (var_sheet, &row, &col);
	    row = var_get_dict_index (var);
	    gtk_sheet_set_active_cell (var_sheet, row, col);
	  }
	else
	  {
	    GtkSheet *data_sheet =
	      GTK_SHEET (get_widget_assert (de->xml, "data_sheet"));

	    gtk_sheet_get_active_cell (data_sheet, &row, &col);
	    col = var_get_dict_index (var);
	    gtk_sheet_set_active_cell (data_sheet, row, col);
	  }
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (GTK_TREE_VIEW (treeview));

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

static gchar *
generate_syntax (GtkTreeView *treeview)
{
  const struct variable *var = get_selected_variable (treeview);

  if ( NULL == var)
    return g_strdup ("");

  return g_strdup (var_get_name (var));
}

