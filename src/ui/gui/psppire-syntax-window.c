/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010  Free Software Foundation

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
#include "executor.h"
#include "helper.h"

#include <libpspp/message.h>
#include <stdlib.h>

#include "psppire.h"
#include "psppire-syntax-window.h"

#include "psppire-data-window.h"
#include "psppire-window-register.h"
#include "psppire.h"
#include "about.h"
#include "psppire-syntax-window.h"
#include "syntax-editor-source.h"
#include <language/lexer/lexer.h>

#include "xalloc.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_syntax_window_base_finalize (PsppireSyntaxWindowClass *, gpointer);
static void psppire_syntax_window_base_init     (PsppireSyntaxWindowClass *class);
static void psppire_syntax_window_class_init    (PsppireSyntaxWindowClass *class);
static void psppire_syntax_window_init          (PsppireSyntaxWindow      *syntax_editor);


static void psppire_syntax_window_iface_init (PsppireWindowIface *iface);


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

      static const GInterfaceInfo window_interface_info =
	{
	  (GInterfaceInitFunc) psppire_syntax_window_iface_init,
	  NULL,
	  NULL
	};

      psppire_syntax_window_type =
	g_type_register_static (PSPPIRE_TYPE_WINDOW, "PsppireSyntaxWindow",
				&psppire_syntax_window_info, 0);

      g_type_add_interface_static (psppire_syntax_window_type,
				   PSPPIRE_TYPE_WINDOW_MODEL,
				   &window_interface_info);
    }

  return psppire_syntax_window_type;
}

static GObjectClass *parent_class ;

static void
psppire_syntax_window_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (parent_class)->finalize)
    (*G_OBJECT_CLASS (parent_class)->finalize) (object);
}


static void
psppire_syntax_window_class_init (PsppireSyntaxWindowClass *class)
{
  parent_class = g_type_class_peek_parent (class);
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
editor_execute_syntax (const PsppireSyntaxWindow *sw, GtkTextIter start,
		       GtkTextIter stop)
{
  PsppireWindow *win = PSPPIRE_WINDOW (sw);
  const gchar *name = psppire_window_get_filename (win);
  execute_syntax (create_syntax_editor_source (sw->buffer, start, stop, name));
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

  return xstrdup (filename);
}

/*
  Save BUFFER to the file called FILENAME.
  FILENAME must be encoded in Glib filename encoding.
  If successful, clears the buffer's modified flag.
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
  g_assert (filename);

  suffixedname = append_suffix (filename);

  gtk_text_buffer_get_iter_at_line (buffer, &start, 0);
  gtk_text_buffer_get_iter_at_offset (buffer, &stop, -1);

  text = gtk_text_buffer_get_text (buffer, &start, &stop, FALSE);

  result =  g_file_set_contents (suffixedname, text, -1, err);

  g_free (suffixedname);

  if ( result )
    {
      char *fn = g_filename_display_name (filename);
      gchar *msg = g_strdup_printf (_("Saved file \"%s\""), fn);
      g_free (fn);
      gtk_statusbar_push (GTK_STATUSBAR (se->sb), se->text_context, msg);
      gtk_text_buffer_set_modified (buffer, FALSE);
      g_free (msg);
    }

  return result;
}


/* Callback for the File->SaveAs menuitem */
static void
syntax_save_as (PsppireWindow *se)
{
  GtkFileFilter *filter;
  gint response;

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

      if ( ! save_editor_to_file (PSPPIRE_SYNTAX_WINDOW (se), filename, &err) )
	{
	  msg ( ME, "%s", err->message );
	  g_error_free (err);
	}

      free (filename);
    }

  gtk_widget_destroy (dialog);
}


/* Callback for the File->Save menuitem */
static void
syntax_save (PsppireWindow *se)
{
  const gchar *filename = psppire_window_get_filename (se);

  if ( filename == NULL )
    syntax_save_as (se);
  else
    {
      GError *err = NULL;
      save_editor_to_file (PSPPIRE_SYNTAX_WINDOW (se), filename, &err);
      if ( err )
	{
	  msg (ME, "%s", err->message);
	  g_error_free (err);
	}
    }
}


/* Callback for the File->Quit menuitem */
static gboolean
on_quit (GtkMenuItem *menuitem, gpointer    user_data)
{
  psppire_quit ();

  return FALSE;
}


void
create_syntax_window (void)
{
  GtkWidget *w = psppire_syntax_window_new ();
  gtk_widget_show (w);
}

void
open_syntax_window (const char *file_name)
{
  GtkWidget *se = psppire_syntax_window_new ();

  if ( psppire_window_load (PSPPIRE_WINDOW (se), file_name) )
    gtk_widget_show (se);
  else
    gtk_widget_destroy (se);
}

static void
on_text_changed (GtkTextBuffer *buffer, PsppireSyntaxWindow *window)
{
  gtk_statusbar_pop (GTK_STATUSBAR (window->sb), window->text_context);
}

static void
on_modified_changed (GtkTextBuffer *buffer, PsppireWindow *window)
{
  if (gtk_text_buffer_get_modified (buffer))
    psppire_window_set_unsaved (window);
}

extern struct source_stream *the_source_stream ;

static void
psppire_syntax_window_init (PsppireSyntaxWindow *window)
{
  GtkBuilder *xml = builder_new ("syntax-editor.ui");
  GtkWidget *box = gtk_vbox_new (FALSE, 0);

  GtkWidget *menubar = get_widget_assert (xml, "menubar2");
  GtkWidget *sw = get_widget_assert (xml, "scrolledwindow8");


  GtkWidget *text_view = get_widget_assert (xml, "syntax_text_view");
  window->buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view));
  window->lexer = lex_create (the_source_stream);

  window->sb = get_widget_assert (xml, "statusbar2");
  window->text_context = gtk_statusbar_get_context_id (GTK_STATUSBAR (window->sb), "Text Context");

  g_signal_connect (window->buffer, "changed", G_CALLBACK (on_text_changed), window);

  g_signal_connect (window->buffer, "modified-changed",
		    G_CALLBACK (on_modified_changed), window);

  connect_help (xml);

  gtk_container_add (GTK_CONTAINER (window), box);

  g_object_ref (menubar);

  g_object_ref (sw);

  g_object_ref (window->sb);


  gtk_box_pack_start (GTK_BOX (box), menubar, FALSE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), sw, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), window->sb, FALSE, TRUE, 0);

  gtk_widget_show_all (box);

  g_signal_connect (get_action_assert (xml,"file_new_syntax"),
		    "activate",
		    G_CALLBACK (create_syntax_window),
		    NULL);

#if 0
  g_signal_connect (get_action_assert (xml,"file_new_data"),
		    "activate",
		    G_CALLBACK (create_data_window),
		    window);
#endif

  {
    GtkAction *abt = get_action_assert (xml, "help_about");
    g_object_set (abt, "stock-id", "gtk-about", NULL);

    g_signal_connect (abt,
		      "activate",
		      G_CALLBACK (about_new),
		      window);
  }

  g_signal_connect (get_action_assert (xml,"help_reference"),
		    "activate",
		    G_CALLBACK (reference_manual),
		    NULL);

  g_signal_connect_swapped (get_action_assert (xml, "file_save"),
		    "activate",
		    G_CALLBACK (syntax_save),
		    window);

  g_signal_connect_swapped (get_action_assert (xml, "file_save_as"),
		    "activate",
		    G_CALLBACK (syntax_save_as),
		    window);

  g_signal_connect (get_action_assert (xml,"file_quit"),
		    "activate",
		    G_CALLBACK (on_quit),
		    window);

  g_signal_connect (get_action_assert (xml,"run_all"),
		    "activate",
		    G_CALLBACK (on_run_all),
		    window);


  g_signal_connect (get_action_assert (xml,"run_selection"),
		    "activate",
		    G_CALLBACK (on_run_selection),
		    window);

  g_signal_connect (get_action_assert (xml,"run_current_line"),
		    "activate",
		    G_CALLBACK (on_run_current_line),
		    window);

  g_signal_connect (get_action_assert (xml,"run_to_end"),
		    "activate",
		    G_CALLBACK (on_run_to_end),
		    window);

  g_signal_connect (get_action_assert (xml,"windows_minimise_all"),
		    "activate",
		    G_CALLBACK (psppire_window_minimise_all), NULL);


  {
  GtkUIManager *uim = GTK_UI_MANAGER (get_object_assert (xml, "uimanager1", GTK_TYPE_UI_MANAGER));

  PSPPIRE_WINDOW (window)->menu =
    GTK_MENU_SHELL (gtk_ui_manager_get_widget (uim,"/ui/menubar2/windows/windows_minimise_all")->parent);
  }

  g_object_unref (xml);
}


GtkWidget*
psppire_syntax_window_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_syntax_window_get_type (),
				   "filename", "Syntax",
				   "description", _("Syntax Editor"),
				   NULL));
}

static void
error_dialog (GtkWindow *w, const gchar *filename,  GError *err)
{
  gchar *fn = g_filename_display_basename (filename);

  GtkWidget *dialog =
    gtk_message_dialog_new (w,
			    GTK_DIALOG_DESTROY_WITH_PARENT,
			    GTK_MESSAGE_ERROR,
			    GTK_BUTTONS_CLOSE,
			    _("Cannot load syntax file '%s'"),
			    fn);

  g_free (fn);

  g_object_set (dialog, "icon-name", "psppicon", NULL);

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
					    "%s", err->message);

  gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);
}

/*
  Loads the buffer from the file called FILENAME
*/
gboolean
syntax_load (PsppireWindow *window, const gchar *filename)
{
  GError *err = NULL;
  gchar *text_locale = NULL;
  gchar *text_utf8 = NULL;
  gsize len_locale = -1;
  gsize len_utf8 = -1;
  GtkTextIter iter;
  PsppireSyntaxWindow *sw = PSPPIRE_SYNTAX_WINDOW (window);

  /* FIXME: What if it's a very big file ? */
  if ( ! g_file_get_contents (filename, &text_locale, &len_locale, &err) )
    {
      error_dialog (GTK_WINDOW (window), filename, err);
      g_clear_error (&err);
      return FALSE;
    }

  text_utf8 = g_locale_to_utf8 (text_locale, len_locale, NULL, &len_utf8, &err);

  free (text_locale);

  if ( text_utf8 == NULL )
    {
      error_dialog (GTK_WINDOW (window), filename, err);
      g_clear_error (&err);
      return FALSE;
    }

  gtk_text_buffer_get_iter_at_line (sw->buffer, &iter, 0);

  gtk_text_buffer_insert (sw->buffer, &iter, text_utf8, len_utf8);

  gtk_text_buffer_set_modified (sw->buffer, FALSE);

  free (text_utf8);

  return TRUE;
}



static void
psppire_syntax_window_iface_init (PsppireWindowIface *iface)
{
  iface->save = syntax_save;
  iface->load = syntax_load;
}
