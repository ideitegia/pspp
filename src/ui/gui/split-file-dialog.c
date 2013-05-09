/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010, 2011, 2012  Free Software Foundation

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

#include "split-file-dialog.h"
#include "psppire-selector.h"
#include "psppire-dialog.h"
#include "executor.h"
#include "psppire-data-window.h"
#include "dict-display.h"
#include "builder-wrapper.h"
#include "helper.h"

#include <data/dictionary.h>

#include "psppire-var-view.h"

#include <gtk/gtk.h>


#include "dialog-common.h"


struct split_file_dialog
{
  /* The XML that created the dialog */
  GtkBuilder *xml;

  /* The dictionary to which this dialog pertains */
  PsppireDict *dict;

  /* The treeview widget containing the list of variables
     upon which the file should be split */
  GtkTreeView *tv;

  PsppireSelector *selector;
};


static gchar *
generate_syntax (const struct split_file_dialog *sfd)
{
  gchar *text;
  GtkWidget *off = get_widget_assert (sfd->xml, "split-radiobutton0");

  GString *string = g_string_new ("SPLIT FILE OFF.");

  if ( ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (off)))
    {
      GString * varlist = g_string_sized_new (80);
      GtkWidget *sort = get_widget_assert (sfd->xml, "split-radiobutton3");
      GtkWidget *layered = get_widget_assert (sfd->xml, "split-radiobutton1");
      gint n_vars = psppire_var_view_append_names (PSPPIRE_VAR_VIEW (sfd->tv), 0, varlist);

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
  GtkBuilder *xml = user_data;
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
refresh (PsppireDialog *dialog, struct split_file_dialog *d)
{
  GtkWidget *off = get_widget_assert (d->xml, "split-radiobutton0");
  GtkWidget *on = get_widget_assert (d->xml, "split-radiobutton1");

  GtkTreeModel *liststore = gtk_tree_view_get_model (d->tv);

  gint n_vars = dict_get_split_cnt (d->dict->dict);

  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  if ( n_vars == 0 )
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(off), TRUE);
  else
    {
      GtkTreeIter iter;
      gint i;
      const struct variable *const *vars = dict_get_split_vars (d->dict->dict);

      for (i = 0 ; i < n_vars; ++i )
	{
	  gtk_list_store_append (GTK_LIST_STORE (liststore), &iter);

	  gtk_list_store_set (GTK_LIST_STORE (liststore), &iter,
			      0, vars[i],
			      -1);
	}

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(on), TRUE);
    }

  gtk_toggle_button_toggled (GTK_TOGGLE_BUTTON(off));
}



/* Pops up the Split File dialog box */
void
split_file_dialog (PsppireDataWindow *de)
{
  gint response;
  struct split_file_dialog sfd;

  GtkWidget *dialog   ;
  GtkWidget *source   ;
  GtkWidget *selector ;
  GtkWidget *on_off   ;

  sfd.xml = builder_new ("split-file.ui");

  dialog = get_widget_assert   (sfd.xml, "split-file-dialog");
  source = get_widget_assert   (sfd.xml, "split-file-dict-treeview");
  selector = get_widget_assert (sfd.xml, "split-file-selector");
  on_off = get_widget_assert   (sfd.xml, "split-radiobutton0");

  sfd.tv = GTK_TREE_VIEW (get_widget_assert (sfd.xml, "split-file-grouping-vars"));

  g_object_get (de->data_editor, "dictionary", &sfd.dict, NULL);

  sfd.selector = PSPPIRE_SELECTOR (selector);

  g_object_set (source, "model", sfd.dict, NULL);

  g_signal_connect (on_off, "toggled", G_CALLBACK(on_off_toggled),  sfd.xml);

  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &sfd);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));


  switch (response)
    {
    case GTK_RESPONSE_OK:
      g_free (execute_syntax_string (de, generate_syntax (&sfd)));
      break;
    case PSPPIRE_RESPONSE_PASTE:
      g_free (paste_syntax_to_window (generate_syntax (&sfd)));
      break;
    default:
      break;
    }

  g_object_unref (sfd.xml);
}

