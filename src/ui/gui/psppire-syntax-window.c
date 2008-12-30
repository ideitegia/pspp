/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008  Free Software Foundation

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

#include <gtk/gtksignal.h>
#include <gtk/gtkbox.h>
#include <glade/glade.h>
#include "helper.h"

#include <libpspp/message.h>
#include <stdlib.h>


#include "psppire-syntax-window.h"

#include "data-editor.h"
#include "about.h"
#include "psppire-syntax-window.h"
#include "syntax-editor-source.h"
#include <language/lexer/lexer.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid



static void psppire_syntax_window_base_finalize (PsppireSyntaxWindowClass *, gpointer);
static void psppire_syntax_window_base_init     (PsppireSyntaxWindowClass *class);
static void psppire_syntax_window_class_init    (PsppireSyntaxWindowClass *class);
static void psppire_syntax_window_init          (PsppireSyntaxWindow      *syntax_editor);


GType
psppire_syntax_window_get_type (void)
{
  static GType psppire_syntax_window_type = 0;

  if (!psppire_syntax_window_type)
    {
      static const GTypeInfo psppire_syntax_window_info =
      {
	sizeof (PsppireSyntaxWindowClass),
	(GBaseInitFunc) psppire_syntax_window_base_init,
        (GBaseFinalizeFunc) psppire_syntax_window_base_finalize,
	(GClassInitFunc)psppire_syntax_window_class_init,
	(GClassFinalizeFunc) NULL,
	NULL,
        sizeof (PsppireSyntaxWindow),
	0,
	(GInstanceInitFunc) psppire_syntax_window_init,
      };

      psppire_syntax_window_type =
	g_type_register_static (PSPPIRE_WINDOW_TYPE, "PsppireSyntaxWindow",
				&psppire_syntax_window_info, 0);
    }

  return psppire_syntax_window_type;
}


static void
psppire_syntax_window_finalize (GObject *object)
{
  g_debug ("%s %p", __FUNCTION__, object);

  GObjectClass *class = G_OBJECT_GET_CLASS (object);

  GObjectClass *parent_class = g_type_class_peek_parent (class);


  if (G_OBJECT_CLASS (parent_class)->finalize)
    (*G_OBJECT_CLASS (parent_class)->finalize) (object);

}


static void
psppire_syntax_window_class_init (PsppireSyntaxWindowClass *class)
{
}


static void
psppire_syntax_window_base_init (PsppireSyntaxWindowClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = psppire_syntax_window_finalize;
}



static void
psppire_syntax_window_base_finalize (PsppireSyntaxWindowClass *class,
				     gpointer class_data)
{
}


static void
editor_execute_syntax (const PsppireSyntaxWindow *se, GtkTextIter start,
		GtkTextIter stop)
{
  execute_syntax (create_syntax_editor_source (se->buffer, start, stop));
}


/* Parse and execute all the text in the buffer */
static void
on_run_all (GtkMenuItem *menuitem, gpointer user_data)
{
  GtkTextIter begin, end;
  PsppireSyntaxWindow *se = PSPPIRE_SYNTAX_WINDOW (user_data);

  gtk_text_buffer_get_iter_at_offset (se->buffer, &begin, 0);
  gtk_text_buffer_get_iter_at_offset (se->buffer, &end, -1);

  editor_execute_syntax (se, begin, end);
}

/* Parse and execute the currently selected text */
static void
on_run_selection (GtkMenuItem *menuitem, gpointer user_data)
{
  GtkTextIter begin, end;
  PsppireSyntaxWindow *se = PSPPIRE_SYNTAX_WINDOW (user_data);

  if ( gtk_text_buffer_get_selection_bounds (se->buffer, &begin, &end) )
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

  PsppireSyntaxWindow *se = PSPPIRE_SYNTAX_WINDOW (user_data);

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



/* Parse and execute the current line */
static void
on_run_current_line (GtkMenuItem *menuitem, gpointer user_data)
{
  GtkTextIter begin, end;
  GtkTextIter here;
  gint line;

  PsppireSyntaxWindow *se = PSPPIRE_SYNTAX_WINDOW (user_data);

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

/*
  Save BUFFER to the file called FILENAME.
  If successful, clears the buffer's modified flag
*/
static gboolean
save_editor_to_file (PsppireSyntaxWindow *se,
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
      psppire_window_set_filename (PSPPIRE_WINDOW (se), filename);
      gtk_text_buffer_set_modified (buffer, FALSE);
    }

  return result;
}

/* If the buffer's modified flag is set, then save it, and close the window.
   Otherwise just close the window.
*/
static void
save_if_modified (PsppireSyntaxWindow *se)
{

  if ( TRUE == gtk_text_buffer_get_modified (se->buffer))
    {
      gint response;
      GtkWidget *dialog;

      const gchar *filename = psppire_window_get_filename (PSPPIRE_WINDOW (se));

      g_return_if_fail (filename != NULL);

      dialog =
	gtk_message_dialog_new (GTK_WINDOW (se),
				GTK_DIALOG_MODAL,
				GTK_MESSAGE_QUESTION,
				GTK_BUTTONS_NONE,
				_("Save contents of syntax editor to %s?"),
				filename);

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

	  if ( ! save_editor_to_file (se, filename, &err) )
	    {
	      msg (ME, err->message);
	      g_error_free (err);
	    }
	}

      if ( response == GTK_RESPONSE_CANCEL )
	return ;
    }

  gtk_widget_destroy (GTK_WIDGET (se));
}

/* Callback for the File->SaveAs menuitem */
static void
on_syntax_save_as (GtkMenuItem *menuitem, gpointer user_data)
{
  GtkFileFilter *filter;
  gint response;

  PsppireSyntaxWindow *se = PSPPIRE_SYNTAX_WINDOW (user_data);

  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Save Syntax"),
				 GTK_WINDOW (se),
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

      if ( ! save_editor_to_file (se, filename, &err) )
	{
	  msg ( ME, err->message );
	  g_error_free (err);
	}

      free (filename);
    }

  gtk_widget_destroy (dialog);
}


/* Callback for the File->Save menuitem */
static void
on_syntax_save (GtkMenuItem *menuitem, gpointer user_data)
{
  PsppireSyntaxWindow *se = PSPPIRE_SYNTAX_WINDOW (user_data);
  const gchar *filename = psppire_window_get_filename (PSPPIRE_WINDOW (se));


  if ( filename == NULL )
    on_syntax_save_as (menuitem, se);
  else
    {
      GError *err = NULL;
      save_editor_to_file (se, filename, &err);
      if ( err )
	{
	  msg (ME, err->message);
	  g_error_free (err);
	}
    }
}


/* Callback for the File->Quit menuitem */
static gboolean
on_quit (GtkMenuItem *menuitem, gpointer    user_data)
{
  PsppireSyntaxWindow *se = PSPPIRE_SYNTAX_WINDOW (user_data);
  save_if_modified (se);
  return FALSE;
}


/* Callback for the "delete" action (clicking the x on the top right
   hand corner of the window) */
static gboolean
on_delete (GtkWidget *w, GdkEvent *event, gpointer user_data)
{
  PsppireSyntaxWindow *se = PSPPIRE_SYNTAX_WINDOW (user_data);

  save_if_modified (se);
  return TRUE;
}


void
create_syntax_window (void)
{
  GtkWidget *w = psppire_syntax_window_new ();
  gtk_widget_show (w);
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

      GtkWidget *se = psppire_syntax_window_new ();

      if ( psppire_syntax_window_load_from_file (PSPPIRE_SYNTAX_WINDOW (se), file_name, NULL) )
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
      gtk_widget_show (se);
    }

  gtk_widget_destroy (dialog);
}


extern struct source_stream *the_source_stream ;

static void
psppire_syntax_window_init (PsppireSyntaxWindow *window)
{
  GladeXML *xml = XML_NEW ("syntax-editor.glade");
  GtkWidget *box = gtk_vbox_new (FALSE, 0);
  
  GtkWidget *menubar = get_widget_assert (xml, "menubar2");
  GtkWidget *sw = get_widget_assert (xml, "scrolledwindow8");
  GtkWidget *sb = get_widget_assert (xml, "statusbar2");

  GtkWidget *text_view = get_widget_assert (xml, "syntax_text_view");
  window->buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
  window->lexer = lex_create (the_source_stream);

  connect_help (xml);

  gtk_container_add (GTK_CONTAINER (window), box);

  g_object_ref (menubar);
  gtk_widget_unparent (menubar);

  g_object_ref (sw);
  gtk_widget_unparent (sw);

  g_object_ref (sb);
  gtk_widget_unparent (sb);


  gtk_box_pack_start (GTK_BOX (box), menubar, FALSE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), sw, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), sb, FALSE, TRUE, 0);

  gtk_widget_show_all (box);

  g_signal_connect (get_widget_assert (xml,"file_new_syntax"),
		    "activate",
		    G_CALLBACK (create_syntax_window),
		    NULL);

  g_signal_connect (get_widget_assert (xml,"file_open_syntax"),
		    "activate",
		    G_CALLBACK (open_syntax_window),
		    window);


  g_signal_connect (get_widget_assert (xml,"file_new_data"),
		    "activate",
		    G_CALLBACK (new_data_window),
		    window);

  g_signal_connect (get_widget_assert (xml,"help_about"),
		    "activate",
		    G_CALLBACK (about_new),
		    window);

  g_signal_connect (get_widget_assert (xml,"help_reference"),
		    "activate",
		    G_CALLBACK (reference_manual),
		    NULL);

  g_signal_connect (get_widget_assert (xml, "file_save"),
		    "activate",
		    G_CALLBACK (on_syntax_save),
		    window);

  g_signal_connect (get_widget_assert (xml, "file_save_as"),
		    "activate",
		    G_CALLBACK (on_syntax_save_as),
		    window);

  g_signal_connect (get_widget_assert (xml,"file_quit"),
		    "activate",
		    G_CALLBACK (on_quit),
		    window);

  g_signal_connect (get_widget_assert (xml,"run_all"),
		    "activate",
		    G_CALLBACK (on_run_all),
		    window);


  g_signal_connect (get_widget_assert (xml,"run_selection"),
		    "activate",
		    G_CALLBACK (on_run_selection),
		    window);

  g_signal_connect (get_widget_assert (xml,"run_current_line"),
		    "activate",
		    G_CALLBACK (on_run_current_line),
		    window);

  g_signal_connect (get_widget_assert (xml,"run_to_end"),
		    "activate",
		    G_CALLBACK (on_run_to_end),
		    window);

#if 0
  g_signal_connect (get_widget_assert (xml,"windows_minimise_all"),
		    "activate",
		    G_CALLBACK (minimise_all_windows),
		    NULL);
#endif

  g_object_unref (xml);

  g_signal_connect (window, "delete-event",
		    G_CALLBACK (on_delete), window);
}


GtkWidget*
psppire_syntax_window_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_syntax_window_get_type (), NULL));
}


/*
  Loads the buffer from the file called FILENAME
*/
gboolean
psppire_syntax_window_load_from_file (PsppireSyntaxWindow *se,
				      const gchar *filename,
				      GError **err)
{
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

  gtk_text_buffer_get_iter_at_line (se->buffer, &iter, 0);

  gtk_text_buffer_insert (se->buffer, &iter, text, -1);

  psppire_window_set_filename (PSPPIRE_WINDOW (se), filename);

  gtk_text_buffer_set_modified (se->buffer, FALSE);

  return TRUE;
}

