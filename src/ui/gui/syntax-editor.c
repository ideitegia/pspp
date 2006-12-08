/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2006  Free Software Foundation
    Written by John Darrington

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
#include <stdlib.h>
#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <libpspp/message.h>

#include "helper.h"

extern GladeXML *xml;

struct syntax_editor
{
  GtkWidget *window;      /* The top level window of the editor */
  GtkTextBuffer *buffer;  /* The buffer which contains the text */
  gchar *name;            /* The name of this syntax buffer/editor */
};

static gboolean save_editor_to_file (struct syntax_editor *se,
				     const gchar *filename,
				     GError **err);

/* If the buffer's modified flag is set, then save it, and close the window.
   Otherwise just close the window.
*/
static void
save_if_modified (struct syntax_editor *se)
{
  if ( TRUE == gtk_text_buffer_get_modified (se->buffer))
    {
      gint response;
      GtkWidget *dialog =
	gtk_message_dialog_new (GTK_WINDOW(se->window),
				GTK_DIALOG_MODAL,
				GTK_MESSAGE_QUESTION,
				GTK_BUTTONS_NONE,
				_("Save contents of syntax editor to %s?"),
				se->name ? se->name : _("Untitled")
				);

      gtk_dialog_add_button  (GTK_DIALOG(dialog),
			      GTK_STOCK_YES,
			      GTK_RESPONSE_ACCEPT);
      gtk_dialog_add_button  (GTK_DIALOG(dialog),
			      GTK_STOCK_NO,
			      GTK_RESPONSE_REJECT);
      gtk_dialog_add_button  (GTK_DIALOG(dialog),
			      GTK_STOCK_CANCEL,
			      GTK_RESPONSE_CANCEL);


      response = gtk_dialog_run (GTK_DIALOG(dialog));

      gtk_widget_destroy (dialog);

      if ( response == GTK_RESPONSE_ACCEPT )
	{
	  GError *err = NULL;

	  if ( ! save_editor_to_file (se, se->name ? se->name : _("Untitled"),
				      &err) )
	    {
	      msg (ME, err->message);
	      g_error_free (err);
	    }
	}

      if ( response == GTK_RESPONSE_CANCEL )
	return ;
    }

  gtk_widget_destroy (se->window);
}

/* Callback for the File->SaveAs menuitem */
static void
on_syntax_save_as   (GtkMenuItem     *menuitem,
		  gpointer         user_data)
{
  GtkFileFilter *filter;
  gint response;
  struct syntax_editor *se = user_data;

  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Save Syntax"),
				 GTK_WINDOW(se->window),
				 GTK_FILE_CHOOSER_ACTION_SAVE,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				 GTK_STOCK_SAVE,   GTK_RESPONSE_ACCEPT,
				 NULL);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Syntax Files (*.sps) "));
  gtk_file_filter_add_pattern (filter, "*.sps");
  gtk_file_filter_add_pattern (filter, "*.SPS");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER(dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER(dialog),
						  TRUE);
  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if ( response == GTK_RESPONSE_ACCEPT )
    {
      GError *err = NULL;
      char *filename =
	gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog) );

      if ( save_editor_to_file (se, filename, &err) )
	{
	  g_free (se->name);
	  se->name = g_strdup (filename);
	}
      else
	{
	  msg ( ME, err->message );
	  g_error_free (err);
	}

      free (filename);
    }

  gtk_widget_destroy ( dialog );
}

/* Callback for the File->Save menuitem */
void
on_syntax_save   (GtkMenuItem     *menuitem,
		  gpointer         user_data)
{
  struct syntax_editor *se = user_data;

  if ( se->name == NULL )
    on_syntax_save_as (menuitem, user_data);
  else
    {
      GError *err;
      save_editor_to_file (se, se->name, &err);
      msg (ME, err->message);
      g_error_free (err);
    }
}


/* Callback for the "delete" action (clicking the x on the top right
   hand corner of the window) */
static gboolean
on_delete (GtkWidget *w, GdkEvent *event, gpointer user_data)
{
  struct syntax_editor *se = user_data;
  save_if_modified (se);
  return TRUE;
}


/* Callback for the File->Quit menuitem */
static gboolean
on_quit (GtkMenuItem *menuitem, gpointer    user_data)
{
  struct syntax_editor *se = user_data;
  save_if_modified (se);
  return FALSE;
}

void
new_syntax_window (GtkMenuItem     *menuitem,
		   gpointer         user_data);



static void open_syntax_window (GtkMenuItem *menuitem,
				gpointer user_data);


/* Create a new syntax editor with NAME.
   If NAME is NULL, a name will be automatically assigned
*/
static struct syntax_editor *
new_syntax_editor (const gchar *name)
{
  GladeXML *new_xml ;
  GtkWidget *text_view;
  struct syntax_editor *se ;

  new_xml = glade_xml_new  (xml->filename, "syntax_editor", NULL);

  se = g_malloc (sizeof (*se));

  se->window = get_widget_assert (new_xml, "syntax_editor");
  text_view = get_widget_assert (new_xml, "syntax_text_view");
  se->buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW(text_view));
  if ( name )
    se->name = g_strdup (name);
  else
    se->name = NULL;

  g_signal_connect (get_widget_assert (new_xml,"file_new_syntax"),
		    "activate",
		    G_CALLBACK(new_syntax_window),
		    se->window);

  g_signal_connect (get_widget_assert (new_xml,"file_open_syntax"),
		    "activate",
		    G_CALLBACK(open_syntax_window),
		    se->window);

  g_signal_connect (get_widget_assert (new_xml,"file_quit"),
		    "activate",
		    G_CALLBACK(on_quit),
		    se);

  g_signal_connect (get_widget_assert (new_xml,"file_save"),
		    "activate",
		    G_CALLBACK(on_syntax_save),
		    se);

  g_signal_connect (get_widget_assert (new_xml,"file_save_as"),
		    "activate",
		    G_CALLBACK(on_syntax_save_as),
		    se);

  g_object_unref (new_xml);

  g_signal_connect (se->window, "delete-event",
		    G_CALLBACK(on_delete), se);

  return se;
}

/* Callback for the File->New->Syntax menuitem */
void
new_syntax_window (GtkMenuItem     *menuitem,
		   gpointer         user_data)
{
  struct syntax_editor *se =   new_syntax_editor (NULL);
  gtk_widget_show (se->window);
}


static void
set_window_title_from_filename (struct syntax_editor *se,
				const gchar *filename)
{
  gchar *title;
  gchar *basename ;
  g_free (se->name);
  se->name = strdup (filename);
  basename = g_path_get_basename (filename);
  title =
    g_strdup_printf (_("%s --- PSPP Syntax Editor"), basename);
  g_free (basename);
  gtk_window_set_title (GTK_WINDOW(se->window), title);
  g_free (title);
}


/* Save BUFFER to the file called FILENAME.
   If successful, clears the buffer's modified flag */
static gboolean
save_editor_to_file (struct syntax_editor *se,
		     const gchar *filename,
		     GError **err)
{
  GtkTextBuffer *buffer = se->buffer;
  gboolean result ;
  GtkTextIter start, stop;
  gchar *text;

  gchar *glibfilename;
  g_assert (filename);

  glibfilename = g_filename_from_utf8 (filename, -1, 0, 0, err);

  if ( ! glibfilename )
    return FALSE;

  gtk_text_buffer_get_iter_at_line (buffer, &start, 0);
  gtk_text_buffer_get_iter_at_offset (buffer, &stop, -1);

  text = gtk_text_buffer_get_text (buffer, &start, &stop, FALSE);

  result =  g_file_set_contents (glibfilename, text, -1, err);

  if ( result )
    {
      set_window_title_from_filename (se, filename);
      gtk_text_buffer_set_modified (buffer, FALSE);
    }

  return result;
}


/* Loads the buffer from the file called FILENAME
*/
static gboolean
load_editor_from_file (struct syntax_editor *se,
		       const gchar *filename,
		       GError **err)
{
  GtkTextBuffer *buffer = se->buffer;
  gchar *text;
  GtkTextIter iter;

  gchar *glibfilename = g_filename_from_utf8 (filename, -1, 0, 0, err);

  if ( ! glibfilename )
    return FALSE;

  /* FIXME: What if it's a very big file ? */
  if ( ! g_file_get_contents (glibfilename, &text, NULL, err) )
    {
      g_free (glibfilename);
      return FALSE;
    }
  g_free (glibfilename);

  gtk_text_buffer_get_iter_at_line (buffer, &iter, 0);

  gtk_text_buffer_insert (buffer, &iter, text, -1);

  set_window_title_from_filename (se, filename);
  gtk_text_buffer_set_modified (buffer, FALSE);

  return TRUE;
}


/* Callback for the File->Open->Syntax menuitem */
static void
open_syntax_window (GtkMenuItem *menuitem, gpointer parent)
{
  GtkFileFilter *filter;
  gint response;

  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Open Syntax"),
				 GTK_WINDOW(parent),
				 GTK_FILE_CHOOSER_ACTION_OPEN,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				 GTK_STOCK_OPEN,   GTK_RESPONSE_ACCEPT,
				 NULL);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Syntax Files (*.sps) "));
  gtk_file_filter_add_pattern (filter, "*.sps");
  gtk_file_filter_add_pattern (filter, "*.SPS");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER(dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response == GTK_RESPONSE_ACCEPT)
    {
      const char *file_name = gtk_file_chooser_get_filename
	(GTK_FILE_CHOOSER (dialog));

      struct syntax_editor *se = new_syntax_editor (file_name);

      load_editor_from_file (se, file_name, NULL);

      gtk_widget_show (se->window);
    }

  gtk_widget_destroy (dialog);
}


#if 1
/* FIXME: get rid of these functions */
void
on_syntax4_activate   (GtkMenuItem     *menuitem,
		       gpointer         user_data)
{
  g_print ("%s\n", __FUNCTION__);
}



void
on_syntax2_activate   (GtkMenuItem     *menuitem,
		       gpointer         user_data)
{
  g_print ("%s\n", __FUNCTION__);
}

void
on_syntax1_activate   (GtkMenuItem     *menuitem,
		       gpointer         user_data)
{
  g_print ("%s\n", __FUNCTION__);
  new_syntax_window (menuitem, user_data);
}
#endif

