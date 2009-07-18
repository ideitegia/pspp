/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2009  Free Software Foundation

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
#include <gtk/gtk.h>

#include "var-display.h"
#include <data/variable.h>
#include <data/format.h>
#include <data/value-labels.h>
#include "psppire-data-window.h"
#include "psppire-dialog.h"
#include "psppire-var-store.h"
#include "psppire-dictview.h"
#include "helper.h"

#include <language/syntax-string-source.h>
#include <libpspp/i18n.h>
#include "helper.h"


#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static const gchar none[] = N_("None");


static const gchar *
label_to_string (const struct variable *var)
{
  const char *label = var_get_label (var);

  if (NULL == label) return g_strdup (none);

  return label;
}


static void
populate_text (PsppireDictView *treeview, gpointer data)
{
  gchar *text = 0;
  GString *gstring;
  PsppireDict *dict;

  GtkTextBuffer *textbuffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data));
  const struct variable *var =
    psppire_dict_view_get_selected_variable (treeview);

  if ( var == NULL)
    return;

  g_object_get (treeview,
		"dictionary", &dict,
		NULL);

  gstring = g_string_sized_new (200);
  g_string_assign (gstring, var_get_name (var));
  g_string_append (gstring, "\n");


  g_string_append_printf (gstring, _("Label: %s\n"), label_to_string (var));
  {
    const struct fmt_spec *fmt = var_get_print_format (var);
    char buffer[FMT_STRING_LEN_MAX + 1];

    fmt_to_string (fmt, buffer);
    /* No conversion necessary.  buffer will always be ascii */
    g_string_append_printf (gstring, _("Type: %s\n"), buffer);
  }

  text = missing_values_to_string (dict, var, NULL);
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
      const struct val_labs *vls = var_get_value_labels (var);
      const struct val_lab **labels;
      size_t n_labels;
      size_t i;

      g_string_append (gstring, "\n");
      g_string_append (gstring, _("Value Labels:\n"));

      labels = val_labs_sorted (vls);
      n_labels = val_labs_count (vls);
      for (i = 0; i < n_labels; i++)
        {
          const struct val_lab *vl = labels[i];
	  gchar *const vstr  =
	    value_to_text (vl->value,  dict, *var_get_print_format (var));

	  g_string_append_printf (gstring, _("%s %s\n"), vstr, val_lab_get_label (vl));

	  g_free (vstr);
	}
      free (labels);
    }

  gtk_text_buffer_set_text (textbuffer, gstring->str, gstring->len);

  g_string_free (gstring, TRUE);
}

static gboolean
treeview_item_selected (gpointer data)
{
  GtkTreeView *tv = GTK_TREE_VIEW (data);
  GtkTreeModel *model = gtk_tree_view_get_model (tv);

  gint n_rows = gtk_tree_model_iter_n_children  (model, NULL);

  if ( n_rows == 0 )
    return FALSE;

  return TRUE;
}


static gchar * generate_syntax (PsppireDictView *treeview);


void
variable_info_dialog (GObject *o, gpointer data)
{
  PsppireDataWindow *de = PSPPIRE_DATA_WINDOW (data);

  gint response ;

  GtkBuilder *xml = builder_new ("variable-info-dialog.ui");

  GtkWidget *dialog = get_widget_assert (xml, "variable-info-dialog");
  GtkWidget *treeview = get_widget_assert (xml, "treeview2");
  GtkWidget *textview = get_widget_assert (xml, "textview1");

  PsppireVarStore *vs = NULL;

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  g_object_set (treeview,
		"dictionary", vs->dict,
		"selection-mode", GTK_SELECTION_SINGLE,
		NULL);

  g_signal_connect (treeview, "cursor-changed", G_CALLBACK (populate_text),
		    textview);


  gtk_text_view_set_indent (GTK_TEXT_VIEW (textview), -5);

  psppire_dialog_set_valid_predicate (PSPPIRE_DIALOG (dialog),
				      treeview_item_selected, treeview);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  switch (response)
    {
    case PSPPIRE_RESPONSE_GOTO:
      {
	const struct variable *var =
	  psppire_dict_view_get_selected_variable (PSPPIRE_DICT_VIEW (treeview));

	if ( NULL == var)
	  goto done;

	g_object_set (de->data_editor,
		      "current-variable", var_get_dict_index (var),
		      NULL);
      }

      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (PSPPIRE_DICT_VIEW (treeview));
        paste_syntax_in_new_window (syntax);

	g_free (syntax);
      }
      break;
    default:
      break;
    }

 done:
  g_object_unref (xml);
}

static gchar *
generate_syntax (PsppireDictView *treeview)
{
  const struct variable *var =
    psppire_dict_view_get_selected_variable (treeview);

  if ( NULL == var)
    return g_strdup ("");

  return g_strdup (var_get_name (var));
}

