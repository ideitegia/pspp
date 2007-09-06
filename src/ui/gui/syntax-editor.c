/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006 Free Software Foundation, Inc.

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
#include <stdlib.h>
#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <libpspp/message.h>
#include <libpspp/getl.h>
#include "helper.h"
#include "data-editor.h"
#include "about.h"

#include "window-manager.h"

#include <data/dictionary.h>
#include <language/lexer/lexer.h>
#include <language/command.h>
#include <data/procedure.h>
#include "syntax-editor.h"
#include "syntax-editor-source.h"

extern struct source_stream *the_source_stream ;
extern struct dataset *the_dataset;

static gboolean save_editor_to_file (struct syntax_editor *se,
				     const gchar *filename,
				     GError **err);

/* Append ".sps" to FILENAME if necessary.
   The returned result must be freed when no longer required.
 */
static gchar *
append_suffix (const gchar *filename)
{
  if ( ! g_str_has_suffix (filename, ".sps" ) &&
       ! g_str_has_suffix (filename, ".SPS" ) )
    {
      return g_strdup_printf ("%s.sps", filename);
    }

  return strdup (filename);
}

/* If the buffer's modified flag is set, then save it, and close the window.
   Otherwise just close the window.
*/
static void
save_if_modified (struct syntax_editor *se)
{
  struct editor_window *e = (struct editor_window *) se;
  if ( TRUE == gtk_text_buffer_get_modified (se->buffer))
    {
      gint response;
      GtkWidget *dialog =
	gtk_message_dialog_new (GTK_WINDOW (e->window),
				GTK_DIALOG_MODAL,
				GTK_MESSAGE_QUESTION,
				GTK_BUTTONS_NONE,
				_("Save contents of syntax editor to %s?"),
				e->name
				);

      gtk_dialog_add_button  (GTK_DIALOG (dialog),
			      GTK_STOCK_YES,
			      GTK_RESPONSE_ACCEPT);
      gtk_dialog_add_button  (GTK_DIALOG (dialog),
			      GTK_STOCK_NO,
			      GTK_RESPONSE_REJECT);
      gtk_dialog_add_button  (GTK_DIALOG (dialog),
			      GTK_STOCK_CANCEL,
			      GTK_RESPONSE_CANCEL);


      response = gtk_dialog_run (GTK_DIALOG (dialog));

      gtk_widget_destroy (dialog);

      if ( response == GTK_RESPONSE_ACCEPT )
	{
	  GError *err = NULL;

	  if ( ! save_editor_to_file (se, e->name, &err) )
	    {
	      msg (ME, err->message);
	      g_error_free (err);
	    }
	}

      if ( response == GTK_RESPONSE_CANCEL )
	return ;
    }

  gtk_widget_destroy (GTK_WIDGET (e->window));
}

/* Callback for the File->SaveAs menuitem */
static void
on_syntax_save_as (GtkMenuItem *menuitem, gpointer user_data)
{
  GtkFileFilter *filter;
  gint response;
  struct syntax_editor *se = user_data;
  struct editor_window *e = user_data;

  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Save Syntax"),
				 GTK_WINDOW (e->window),
				 GTK_FILE_CHOOSER_ACTION_SAVE,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				 GTK_STOCK_SAVE,   GTK_RESPONSE_ACCEPT,
				 NULL);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Syntax Files (*.sps) "));
  gtk_file_filter_add_pattern (filter, "*.sps");
  gtk_file_filter_add_pattern (filter, "*.SPS");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog),
						  TRUE);
  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if ( response == GTK_RESPONSE_ACCEPT )
    {
      GError *err = NULL;
      char *filename =
	gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog) );

      if ( save_editor_to_file (se, filename, &err) )
	{
	  g_free (e->name);
	  e->name = g_strdup (filename);
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
static void
on_syntax_save (GtkMenuItem *menuitem, gpointer user_data)
{
  struct syntax_editor *se = user_data;
  struct editor_window *e = user_data;

  if ( e->name == NULL )
    on_syntax_save_as (menuitem, user_data);
  else
    {
      GError *err = NULL;
      save_editor_to_file (se, e->name, &err);
      if ( err )
	{
	  msg (ME, err->message);
	  g_error_free (err);
	}
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

static void
editor_execute_syntax (const struct syntax_editor *se, GtkTextIter start,
		GtkTextIter stop)
{
  execute_syntax (create_syntax_editor_source (se, start, stop));
}

/* Parse and execute all the text in the buffer */
static void
on_run_all (GtkMenuItem *menuitem, gpointer user_data)
{
  GtkTextIter begin, end;
  struct syntax_editor *se = user_data;

  gtk_text_buffer_get_iter_at_offset (se->buffer, &begin, 0);
  gtk_text_buffer_get_iter_at_offset (se->buffer, &end, -1);

  editor_execute_syntax (se, begin, end);
}

/* Parse and execute the currently selected text */
static void
on_run_selection (GtkMenuItem *menuitem, gpointer user_data)
{
  GtkTextIter begin, end;
  struct syntax_editor *se = user_data;

  if ( gtk_text_buffer_get_selection_bounds (se->buffer, &begin, &end) )
    editor_execute_syntax (se, begin, end);
}


/* Parse and execute the current line */
static void
on_run_current_line (GtkMenuItem *menuitem, gpointer user_data)
{
  GtkTextIter begin, end;
  GtkTextIter here;
  gint line;

  struct syntax_editor *se = user_data;

  /* Get the current line */
  gtk_text_buffer_get_iter_at_mark (se->buffer,
				    &here,
				    gtk_text_buffer_get_insert (se->buffer)
				    );

  line = gtk_text_iter_get_line (&here) ;

  /* Now set begin and end to the start of this line, and start of
     following line respectively */
  gtk_text_buffer_get_iter_at_line (se->buffer, &begin, line);
  gtk_text_buffer_get_iter_at_line (se->buffer, &end, line + 1);

  editor_execute_syntax (se, begin, end);
}



/* Parse and execute the from the current line, to the end of the
   buffer */
static void
on_run_to_end (GtkMenuItem *menuitem, gpointer user_data)
{
  GtkTextIter begin, end;
  GtkTextIter here;
  gint line;

  struct syntax_editor *se = user_data;

  /* Get the current line */
  gtk_text_buffer_get_iter_at_mark (se->buffer,
				    &here,
				    gtk_text_buffer_get_insert (se->buffer)
				    );

  line = gtk_text_iter_get_line (&here) ;

  /* Now set begin and end to the start of this line, and end of buffer
     respectively */
  gtk_text_buffer_get_iter_at_line (se->buffer, &begin, line);
  gtk_text_buffer_get_iter_at_line (se->buffer, &end, -1);

  editor_execute_syntax (se, begin, end);
}




/*
  Create a new syntax editor with NAME.
  If NAME is NULL, a name will be automatically assigned
*/
struct syntax_editor *
new_syntax_editor (void)
{
  GladeXML *xml = XML_NEW ("syntax-editor.glade");

  GtkWidget *text_view;
  struct syntax_editor *se ;
  struct editor_window *e;

  connect_help (xml);

  se = g_malloc (sizeof (*se));

  e = (struct editor_window *)se;

  e->window = GTK_WINDOW (get_widget_assert (xml, "syntax_editor"));
  text_view = get_widget_assert (xml, "syntax_text_view");
  se->buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
  se->lexer = lex_create (the_source_stream);

  g_signal_connect (get_widget_assert (xml,"file_new_syntax"),
		    "activate",
		    G_CALLBACK (new_syntax_window),
		    e->window);

  g_signal_connect (get_widget_assert (xml,"file_open_syntax"),
		    "activate",
		    G_CALLBACK (open_syntax_window),
		    e->window);

  g_signal_connect (get_widget_assert (xml,"file_new_data"),
		    "activate",
		    G_CALLBACK (new_data_window),
		    e->window);

  g_signal_connect (get_widget_assert (xml,"help_about"),
		    "activate",
		    G_CALLBACK (about_new),
		    e->window);

  g_signal_connect (get_widget_assert (xml,"help_reference"),
		    "activate",
		    G_CALLBACK (reference_manual),
		    NULL);


  g_signal_connect (get_widget_assert (xml, "file_save"),
		    "activate",
		    G_CALLBACK (on_syntax_save),
		    se);

  g_signal_connect (get_widget_assert (xml, "file_save_as"),
		    "activate",
		    G_CALLBACK (on_syntax_save_as),
		    se);


  g_signal_connect (get_widget_assert (xml,"file_quit"),
		    "activate",
		    G_CALLBACK (on_quit),
		    se);


  g_signal_connect (get_widget_assert (xml,"run_all"),
		    "activate",
		    G_CALLBACK (on_run_all),
		    se);


  g_signal_connect (get_widget_assert (xml,"run_selection"),
		    "activate",
		    G_CALLBACK (on_run_selection),
		    se);

  g_signal_connect (get_widget_assert (xml,"run_current_line"),
		    "activate",
		    G_CALLBACK (on_run_current_line),
		    se);


  g_signal_connect (get_widget_assert (xml,"run_to_end"),
		    "activate",
		    G_CALLBACK (on_run_to_end),
		    se);


  g_signal_connect (get_widget_assert (xml,"windows_minimise_all"),
		    "activate",
		    G_CALLBACK (minimise_all_windows),
		    NULL);



  g_object_unref (xml);

  g_signal_connect (e->window, "delete-event",
		    G_CALLBACK (on_delete), se);



  return se;
}

/*
   Callback for the File->New->Syntax menuitem
*/
void
new_syntax_window (GtkMenuItem     *menuitem,
		   gpointer         user_data)
{
  window_create (WINDOW_SYNTAX, NULL);
}


/*
  Save BUFFER to the file called FILENAME.
  If successful, clears the buffer's modified flag
*/
static gboolean
save_editor_to_file (struct syntax_editor *se,
		     const gchar *filename,
		     GError **err)
{
  GtkTextBuffer *buffer = se->buffer;
  gboolean result ;
  GtkTextIter start, stop;
  gchar *text;

  gchar *suffixedname;
  gchar *glibfilename;
  g_assert (filename);

  suffixedname = append_suffix (filename);

  glibfilename = g_filename_from_utf8 (suffixedname, -1, 0, 0, err);

  g_free ( suffixedname);

  if ( ! glibfilename )
    return FALSE;

  gtk_text_buffer_get_iter_at_line (buffer, &start, 0);
  gtk_text_buffer_get_iter_at_offset (buffer, &stop, -1);

  text = gtk_text_buffer_get_text (buffer, &start, &stop, FALSE);

  result =  g_file_set_contents (glibfilename, text, -1, err);

  if ( result )
    {
      window_set_name_from_filename ((struct editor_window *) se, filename);
      gtk_text_buffer_set_modified (buffer, FALSE);
    }

  return result;
}


/*
  Loads the buffer from the file called FILENAME
*/
gboolean
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


  window_set_name_from_filename ((struct editor_window *)se, filename);
  gtk_text_buffer_set_modified (buffer, FALSE);

  return TRUE;
}


/* Callback for the File->Open->Syntax menuitem */
void
open_syntax_window (GtkMenuItem *menuitem, gpointer parent)
{
  GtkFileFilter *filter;
  gint response;

  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Open Syntax"),
				 GTK_WINDOW (parent),
				 GTK_FILE_CHOOSER_ACTION_OPEN,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				 GTK_STOCK_OPEN,   GTK_RESPONSE_ACCEPT,
				 NULL);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Syntax Files (*.sps) "));
  gtk_file_filter_add_pattern (filter, "*.sps");
  gtk_file_filter_add_pattern (filter, "*.SPS");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response == GTK_RESPONSE_ACCEPT)
    {
      const char *file_name = gtk_file_chooser_get_filename
	(GTK_FILE_CHOOSER (dialog));

      struct syntax_editor *se = (struct syntax_editor *)
	window_create (WINDOW_SYNTAX, file_name);

      if ( load_editor_from_file (se, file_name, NULL) )
#if RECENT_LISTS_AVAILABLE
      {
	GtkRecentManager *manager = gtk_recent_manager_get_default();
	gchar *uri = g_filename_to_uri (file_name, NULL, NULL);

	gtk_recent_manager_remove_item (manager, uri, NULL);
	if ( ! gtk_recent_manager_add_item (manager, uri))
	  g_warning ("Could not add item %s to recent list\n",uri);

	g_free (uri);
      }
#else
      ;
#endif

    }

  gtk_widget_destroy (dialog);
}

