/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2009, 2010, 2011, 2012, 2013  Free Software Foundation


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

#include "ui/gui/psppire-dialog-action-var-info.h"

#include <gtk/gtk.h>

#include "data/format.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "libpspp/i18n.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/helper.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dialog.h"
#include "ui/gui/psppire-dictview.h"
#include "ui/gui/var-display.h"

static void psppire_dialog_action_var_info_init            (PsppireDialogActionVarInfo      *act);
static void psppire_dialog_action_var_info_class_init      (PsppireDialogActionVarInfoClass *class);

G_DEFINE_TYPE (PsppireDialogActionVarInfo, psppire_dialog_action_var_info, PSPPIRE_TYPE_DIALOG_ACTION);

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


static gboolean
treeview_item_selected (gpointer data)
{
  PsppireDialogAction *pda = data;
  GtkTreeView *tv = GTK_TREE_VIEW (pda->source);
  GtkTreeSelection *selection = gtk_tree_view_get_selection (tv);

  return gtk_tree_selection_count_selected_rows (selection) > 0;
}

static gchar *
generate_syntax (PsppireDialogAction *act)

{
  struct variable **vars;
  size_t n_vars;
  size_t line_len;
  GString *s;
  char *str;
  size_t i;

  psppire_dict_view_get_selected_variables (PSPPIRE_DICT_VIEW (act->source),
                                            &vars, &n_vars);

  s = g_string_new ("");
  line_len = 0;
  for (i = 0; i < n_vars; i++)
    {
      const char *name = var_get_name (vars[i]);
      size_t name_len = strlen (name);

      if (line_len > 0)
        {
          if (line_len + 1 + name_len > 69)
            {
              g_string_append_c (s, '\n');
              line_len = 0;
            }
          else
            {
              g_string_append_c (s, ' ');
              line_len++;
            }
        }

      g_string_append (s, name);
      line_len += name_len;
    }

  g_free (vars);

  str = s->str;
  g_string_free (s, FALSE);
  return str;
}

static void
populate_text_var (GString *gstring, const struct variable *var)
{
  gchar *text = NULL;

  g_string_append (gstring, var_get_name (var));
  g_string_append (gstring, "\n");


  g_string_append_printf (gstring, _("Label: %s\n"), label_to_string (var));
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

  g_string_append_printf (gstring, _("Measurement Level: %s\n"),
			  measure_to_string (var_get_measure (var)));


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
	  gchar *const vstr  = value_to_text (vl->value,  var);

	  g_string_append_printf (gstring, _("%s %s\n"),
                                  vstr, val_lab_get_escaped_label (vl));

	  g_free (vstr);
	}
      free (labels);
    }
}


static void
populate_text (GtkTreeSelection *selection, gpointer data)
{
  GtkTreeView *treeview = gtk_tree_selection_get_tree_view (selection);
  GString *gstring;
  PsppireDict *dict;
  size_t n_vars;
  size_t i;

  GtkTextBuffer *textbuffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (data));
  struct variable **vars;

  g_object_get (treeview, "model", &dict,
		NULL);

  gstring = g_string_sized_new (200);

  psppire_dict_view_get_selected_variables (PSPPIRE_DICT_VIEW (treeview),
                                            &vars, &n_vars);
  for (i = 0; i < n_vars; i++)
    {
      if (i > 0)
        g_string_append_c (gstring, '\n');
      populate_text_var (gstring, vars[i]);
    }
  g_free (vars);

  gtk_text_buffer_set_text (textbuffer, gstring->str, gstring->len);

  g_string_free (gstring, TRUE);
}


static void
jump_to (PsppireDialog *d, gint response, gpointer data)
{
  PsppireDataWindow *dw;
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (data);
  struct variable **vars;
  size_t n_vars;

  if (response !=  PSPPIRE_RESPONSE_GOTO)
    return;

  psppire_dict_view_get_selected_variables (PSPPIRE_DICT_VIEW (pda->source),
                                            &vars, &n_vars);
  if (n_vars > 0)
    {
      g_object_get (pda, "top-level", &dw, NULL);

      psppire_data_editor_goto_variable (dw->data_editor,
                                         var_get_dict_index (vars[0]));
    }
  g_free (vars);
}

static void
psppire_dialog_action_var_info_activate (GtkAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);

  GtkBuilder *xml = builder_new ("variable-info.ui");
  GtkWidget *textview = get_widget_assert (xml, "textview1");  

  pda->dialog = get_widget_assert (xml, "variable-info-dialog");
  pda->source = get_widget_assert (xml, "treeview2");

  g_object_set (pda->source,
		"selection-mode", GTK_SELECTION_MULTIPLE,
		NULL);

  g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (pda->source)),
                    "changed", G_CALLBACK (populate_text),
		    textview);


  g_signal_connect (pda->dialog, "response", G_CALLBACK (jump_to),
		    pda);

  psppire_dialog_action_set_valid_predicate (pda,
					     treeview_item_selected);

  g_object_unref (xml);

  if (PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_var_info_parent_class)->activate)
    PSPPIRE_DIALOG_ACTION_CLASS (psppire_dialog_action_var_info_parent_class)->activate (pda);
}

static void
psppire_dialog_action_var_info_class_init (PsppireDialogActionVarInfoClass *class)
{
  GtkActionClass *action_class = GTK_ACTION_CLASS (class);

  action_class->activate = psppire_dialog_action_var_info_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_var_info_init (PsppireDialogActionVarInfo *act)
{
}
