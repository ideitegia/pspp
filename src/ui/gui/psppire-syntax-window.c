/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2012, 2014  Free Software Foundation

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
#include <stdlib.h>

#include <gtksourceview/gtksourcebuffer.h>
#include <gtksourceview/gtksourcelanguage.h>
#include <gtksourceview/gtksourcelanguagemanager.h>
#include <gtksourceview/gtksourceprintcompositor.h>

#include "language/lexer/lexer.h"
#include "libpspp/encoding-guesser.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "ui/gui/executor.h"
#include "ui/gui/help-menu.h"
#include "ui/gui/helper.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-encoding-selector.h"
#include "ui/gui/psppire-lex-reader.h"
#include "ui/gui/psppire-syntax-window.h"
#include "ui/gui/psppire-window-register.h"
#include "ui/gui/psppire.h"

#include "gl/localcharset.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_syntax_window_base_finalize (PsppireSyntaxWindowClass *, gpointer);
static void psppire_syntax_window_base_init     (PsppireSyntaxWindowClass *class);
static void psppire_syntax_window_class_init    (PsppireSyntaxWindowClass *class);
static void psppire_syntax_window_init          (PsppireSyntaxWindow      *syntax_editor);


static void psppire_syntax_window_iface_init (PsppireWindowIface *iface);


/* Properties */
enum
{
  PROP_0,
  PROP_ENCODING
};

static void
psppire_syntax_window_set_property (GObject         *object,
                                    guint            prop_id,
                                    const GValue    *value,
                                    GParamSpec      *pspec)
{
  PsppireSyntaxWindow *window = PSPPIRE_SYNTAX_WINDOW (object);

  switch (prop_id)
    {
    case PROP_ENCODING:
      g_free (window->encoding);
      window->encoding = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_syntax_window_get_property (GObject         *object,
                                    guint            prop_id,
                                    GValue          *value,
                                    GParamSpec      *pspec)
{
  PsppireSyntaxWindow *window = PSPPIRE_SYNTAX_WINDOW (object);

  switch (prop_id)
    {
    case PROP_ENCODING:
      g_value_set_string (value, window->encoding);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

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
psppire_syntax_window_dispose (GObject *obj)
{
  PsppireSyntaxWindow *sw = (PsppireSyntaxWindow *)obj;

  GtkClipboard *clip_selection;
  GtkClipboard *clip_primary;

  if (sw->dispose_has_run)
    return;

  g_free (sw->encoding);
  sw->encoding = NULL;

  clip_selection = gtk_widget_get_clipboard (GTK_WIDGET (sw), GDK_SELECTION_CLIPBOARD);
  clip_primary =   gtk_widget_get_clipboard (GTK_WIDGET (sw), GDK_SELECTION_PRIMARY);

  g_signal_handler_disconnect (clip_primary, sw->sel_handler);

  g_signal_handler_disconnect (clip_selection, sw->ps_handler);

  /* Make sure dispose does not run twice. */
  sw->dispose_has_run = TRUE;

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}



static void
psppire_syntax_window_class_init (PsppireSyntaxWindowClass *class)
{
  GParamSpec *encoding_spec;
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);

  GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default ();

  const gchar * const *existing_paths =  gtk_source_language_manager_get_search_path (lm);
  gchar **new_paths = g_strdupv ((gchar **)existing_paths);
  int n = g_strv_length ((gchar **) existing_paths);

  new_paths = g_realloc (new_paths, (n + 2) * sizeof (*new_paths));
  new_paths[n] = g_strdup (relocate (PKGDATADIR));
  new_paths[n+1] = NULL;

  lm = gtk_source_language_manager_new ();
  gtk_source_language_manager_set_search_path (lm, new_paths);

  class->lan = gtk_source_language_manager_get_language (lm, "pspp");

  if (class->lan == NULL)
    g_warning ("pspp.lang file not found.  Syntax highlighting will not be available.");

  parent_class = g_type_class_peek_parent (class);

  g_strfreev (new_paths);

  encoding_spec =
    null_if_empty_param ("encoding",
                         "Character encoding",
                         "IANA character encoding in this syntax file",
			 NULL,
			 G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  parent_class = g_type_class_peek_parent (class);

  gobject_class->set_property = psppire_syntax_window_set_property;
  gobject_class->get_property = psppire_syntax_window_get_property;
  gobject_class->dispose = psppire_syntax_window_dispose;

  g_object_class_install_property (gobject_class,
                                   PROP_ENCODING,
                                   encoding_spec);
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
  struct lex_reader *reader = lex_reader_for_gtk_text_buffer (GTK_TEXT_BUFFER (sw->buffer), start, stop);

  lex_reader_set_file_name (reader, psppire_window_get_filename (win));

  execute_syntax (psppire_default_data_window (), reader);
}

/* Delete the currently selected text */
static void
on_edit_delete (PsppireSyntaxWindow *sw)
{
  GtkTextIter begin, end;
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (sw->buffer);
  
  if ( gtk_text_buffer_get_selection_bounds (buffer, &begin, &end) )
    gtk_text_buffer_delete (buffer, &begin, &end);
}


/* The syntax editor's clipboard deals only with text */
enum {
  SELECT_FMT_NULL,
  SELECT_FMT_TEXT,
};


static void
selection_changed (PsppireSyntaxWindow *sw)
{
  gboolean sel = gtk_text_buffer_get_has_selection (GTK_TEXT_BUFFER (sw->buffer));

  gtk_action_set_sensitive (sw->edit_copy, sel);
  gtk_action_set_sensitive (sw->edit_cut, sel);
  gtk_action_set_sensitive (sw->edit_delete, sel);
}

/* The callback which runs when something request clipboard data */
static void
clipboard_get_cb (GtkClipboard     *clipboard,
		  GtkSelectionData *selection_data,
		  guint             info,
		  gpointer          data)
{
  PsppireSyntaxWindow *sw = data;
  g_assert (info == SELECT_FMT_TEXT);

  gtk_selection_data_set (selection_data, selection_data->target,
			  8,
			  (const guchar *) sw->cliptext, strlen (sw->cliptext));

}

static void
clipboard_clear_cb (GtkClipboard *clipboard,
		    gpointer data)
{
  PsppireSyntaxWindow *sw = data;
  g_free (sw->cliptext);
  sw->cliptext = NULL;
}


static const GtkTargetEntry targets[] = {
  { "UTF8_STRING",   0, SELECT_FMT_TEXT },
  { "STRING",        0, SELECT_FMT_TEXT },
  { "TEXT",          0, SELECT_FMT_TEXT },
  { "COMPOUND_TEXT", 0, SELECT_FMT_TEXT },
  { "text/plain;charset=utf-8", 0, SELECT_FMT_TEXT },
  { "text/plain",    0, SELECT_FMT_TEXT },
};


/*
  Store a clip containing the currently selected text.
  Returns true iff something was set.
  As a side effect, begin and end will be set to indicate
  the limits of the selected text.
*/
static gboolean
set_clip (PsppireSyntaxWindow *sw, GtkTextIter *begin, GtkTextIter *end)
{
  GtkClipboard *clipboard ;
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (sw->buffer);

  if ( ! gtk_text_buffer_get_selection_bounds (buffer, begin, end) )
    return FALSE;

  g_free (sw->cliptext);
  sw->cliptext = gtk_text_buffer_get_text  (buffer, begin, end, FALSE);

  clipboard =
    gtk_widget_get_clipboard (GTK_WIDGET (sw), GDK_SELECTION_CLIPBOARD);

  if (!gtk_clipboard_set_with_owner (clipboard, targets,
				     G_N_ELEMENTS (targets),
				     clipboard_get_cb, clipboard_clear_cb,
				     G_OBJECT (sw)))
    clipboard_clear_cb (clipboard, sw);

  return TRUE;
}

static void
on_edit_cut (PsppireSyntaxWindow *sw)
{
  GtkTextIter begin, end;
  
  if ( set_clip (sw, &begin, &end))
    gtk_text_buffer_delete (GTK_TEXT_BUFFER (sw->buffer), &begin, &end);
}

static void
on_edit_copy (PsppireSyntaxWindow *sw)
{
  GtkTextIter begin, end;

  set_clip (sw, &begin, &end);
}


/* A callback for when the clipboard contents have been received */
static void
contents_received_callback (GtkClipboard *clipboard,
			    GtkSelectionData *sd,
			    gpointer data)
{
  PsppireSyntaxWindow *syntax_window = data;

  if ( sd->length < 0 )
    return;

  if ( sd->type != gdk_atom_intern ("UTF8_STRING", FALSE))
    return;

  gtk_text_buffer_insert_at_cursor (GTK_TEXT_BUFFER (syntax_window->buffer),
				    (gchar *) sd->data,
				    sd->length);

}

static void
on_edit_paste (PsppireSyntaxWindow *sw)
{
  GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (sw));
  GtkClipboard *clipboard =
    gtk_clipboard_get_for_display (display, GDK_SELECTION_CLIPBOARD);

  gtk_clipboard_request_contents (clipboard,
				  gdk_atom_intern ("UTF8_STRING", TRUE),
				  contents_received_callback,
				  sw);
}


/* Check to see if CLIP holds a target which we know how to paste,
   and set the sensitivity of the Paste action accordingly.
 */
static void
set_paste_sensitivity (GtkClipboard *clip, GdkEventOwnerChange *event, gpointer data)
{
  gint i;
  gboolean compatible_target = FALSE;
  PsppireSyntaxWindow *sw = PSPPIRE_SYNTAX_WINDOW (data);

  for (i = 0 ; i < sizeof (targets) / sizeof (targets[0]) ; ++i)
    {
      GdkAtom atom = gdk_atom_intern (targets[i].target, TRUE);
      if ( gtk_clipboard_wait_is_target_available (clip, atom))
	{
	  compatible_target = TRUE;
	  break;
	}
    }

  gtk_action_set_sensitive (sw->edit_paste, compatible_target);
}




/* Parse and execute all the text in the buffer */
static void
on_run_all (GtkMenuItem *menuitem, gpointer user_data)
{
  GtkTextIter begin, end;
  PsppireSyntaxWindow *se = PSPPIRE_SYNTAX_WINDOW (user_data);

  gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (se->buffer), &begin, 0);
  gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (se->buffer), &end, -1);

  editor_execute_syntax (se, begin, end);
}

/* Parse and execute the currently selected text */
static void
on_run_selection (GtkMenuItem *menuitem, gpointer user_data)
{
  GtkTextIter begin, end;
  PsppireSyntaxWindow *se = PSPPIRE_SYNTAX_WINDOW (user_data);

  if ( gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (se->buffer), &begin, &end) )
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
  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (se->buffer),
				    &here,
				    gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (se->buffer))
				    );

  line = gtk_text_iter_get_line (&here) ;

  /* Now set begin and end to the start of this line, and end of buffer
     respectively */
  gtk_text_buffer_get_iter_at_line (GTK_TEXT_BUFFER (se->buffer), &begin, line);
  gtk_text_buffer_get_iter_at_line (GTK_TEXT_BUFFER (se->buffer), &end, -1);

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
  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (se->buffer),
				    &here,
				    gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (se->buffer))
				    );

  line = gtk_text_iter_get_line (&here) ;

  /* Now set begin and end to the start of this line, and start of
     following line respectively */
  gtk_text_buffer_get_iter_at_line (GTK_TEXT_BUFFER (se->buffer), &begin, line);
  gtk_text_buffer_get_iter_at_line (GTK_TEXT_BUFFER (se->buffer), &end, line + 1);

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
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (se->buffer);
  struct substring text_locale;
  gboolean result ;
  GtkTextIter start, stop;
  gchar *text;

  gchar *suffixedname;
  g_assert (filename);

  suffixedname = append_suffix (filename);

  gtk_text_buffer_get_iter_at_line (buffer, &start, 0);
  gtk_text_buffer_get_iter_at_offset (buffer, &stop, -1);

  text = gtk_text_buffer_get_text (buffer, &start, &stop, FALSE);

  text_locale = recode_substring_pool (se->encoding, "UTF-8", ss_cstr (text),
                                       NULL);

  result =  g_file_set_contents (suffixedname, ss_data (text_locale),
                                 ss_length (text_locale), err);

  ss_dealloc (&text_locale);
  g_free (suffixedname);

  if ( result )
    {
      char *fn = g_filename_display_name (filename);
      gchar *msg = g_strdup_printf (_("Saved file `%s'"), fn);
      g_free (fn);
      gtk_statusbar_push (GTK_STATUSBAR (se->sb), se->text_context, msg);
      gtk_text_buffer_set_modified (buffer, FALSE);
      g_free (msg);
    }

  return result;
}


/* PsppireWindow 'pick_Filename' callback. */
static void
syntax_pick_filename (PsppireWindow *window)
{
  PsppireSyntaxWindow *se = PSPPIRE_SYNTAX_WINDOW (window);
  const char *default_encoding;
  GtkFileFilter *filter;
  gint response;

  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Save Syntax"),
				 GTK_WINDOW (se),
				 GTK_FILE_CHOOSER_ACTION_SAVE,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				 GTK_STOCK_SAVE,   GTK_RESPONSE_ACCEPT,
				 NULL);

  g_object_set (dialog, "local-only", FALSE, NULL);

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

  default_encoding = se->encoding != NULL ? se->encoding : locale_charset ();
  gtk_file_chooser_set_extra_widget (
    GTK_FILE_CHOOSER (dialog),
    psppire_encoding_selector_new (default_encoding, false));

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if ( response == GTK_RESPONSE_ACCEPT )
    {
      gchar *encoding;
      char *filename;

      filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog) );
      psppire_window_set_filename (window, filename);
      free (filename);

      encoding = psppire_encoding_selector_get_encoding (
        gtk_file_chooser_get_extra_widget (GTK_FILE_CHOOSER (dialog)));
      if (encoding != NULL)
        {
          g_free (se->encoding);
          se->encoding = encoding;
        }
    }

  gtk_widget_destroy (dialog);
}


/* PsppireWindow 'save' callback. */
static void
syntax_save (PsppireWindow *se)
{
  const gchar *filename = psppire_window_get_filename (se);
  GError *err = NULL;
  save_editor_to_file (PSPPIRE_SYNTAX_WINDOW (se), filename, &err);
  if ( err )
    {
      msg (ME, "%s", err->message);
      g_error_free (err);
    }
}


/* Callback for the File->Quit menuitem */
static gboolean
on_quit (GtkMenuItem *menuitem, gpointer    user_data)
{
  psppire_quit ();

  return FALSE;
}


static void
load_and_show_syntax_window (GtkWidget *se, const gchar *filename,
                             const gchar *encoding)
{
  gboolean ok;

  gtk_source_buffer_begin_not_undoable_action (PSPPIRE_SYNTAX_WINDOW (se)->buffer);
  ok = psppire_window_load (PSPPIRE_WINDOW (se), filename, encoding, NULL);
  gtk_source_buffer_end_not_undoable_action (PSPPIRE_SYNTAX_WINDOW (se)->buffer);

  if (ok )
    gtk_widget_show (se);
  else
    gtk_widget_destroy (se);
}

void
create_syntax_window (void)
{
  GtkWidget *w = psppire_syntax_window_new (NULL);
  gtk_widget_show (w);
}

void
open_syntax_window (const char *file_name, const gchar *encoding)
{
  GtkWidget *se = psppire_syntax_window_new (NULL);

  if ( file_name)
    load_and_show_syntax_window (se, file_name, encoding);
}



static void psppire_syntax_window_print (PsppireSyntaxWindow *window);

static void
on_modified_changed (GtkTextBuffer *buffer, PsppireWindow *window)
{
  if (gtk_text_buffer_get_modified (buffer))
    psppire_window_set_unsaved (window);
}

static void undo_redo_update (PsppireSyntaxWindow *window);
static void undo_last_edit (PsppireSyntaxWindow *window);
static void redo_last_edit (PsppireSyntaxWindow *window);

static void
on_text_changed (GtkTextBuffer *buffer, PsppireSyntaxWindow *window)
{
  gtk_statusbar_pop (GTK_STATUSBAR (window->sb), window->text_context);
  undo_redo_update (window);
}

static void
psppire_syntax_window_init (PsppireSyntaxWindow *window)
{
  GtkBuilder *xml = builder_new ("syntax-editor.ui");
  GtkWidget *box = gtk_vbox_new (FALSE, 0);

  GtkWidget *menubar = get_widget_assert (xml, "menubar");
  GtkWidget *sw = get_widget_assert (xml, "scrolledwindow8");

  GtkWidget *text_view = get_widget_assert (xml, "syntax_text_view");

  PsppireSyntaxWindowClass *class
    = PSPPIRE_SYNTAX_WINDOW_CLASS (G_OBJECT_GET_CLASS (window));

  GtkClipboard *clip_selection = gtk_widget_get_clipboard (GTK_WIDGET (window), GDK_SELECTION_CLIPBOARD);
  GtkClipboard *clip_primary =   gtk_widget_get_clipboard (GTK_WIDGET (window), GDK_SELECTION_PRIMARY);

  window->print_settings = NULL;
  window->undo_menuitem = get_action_assert (xml, "edit_undo");
  window->redo_menuitem = get_action_assert (xml, "edit_redo");

  if (class->lan)
    window->buffer = gtk_source_buffer_new_with_language (class->lan);
  else
    window->buffer = gtk_source_buffer_new (NULL);

  gtk_text_view_set_buffer (GTK_TEXT_VIEW (text_view), GTK_TEXT_BUFFER (window->buffer));

  g_object_set (window->buffer,
		"highlight-matching-brackets", TRUE,
		NULL);

  g_object_set (text_view,
		"show-line-numbers", TRUE,
		"show-line-marks", TRUE,
		"auto-indent", TRUE,
		"indent-width", 4,
		"highlight-current-line", TRUE,
		NULL);

  window->encoding = NULL;

  window->cliptext = NULL;
  window->dispose_has_run = FALSE;

  window->edit_delete = get_action_assert (xml, "edit_delete");
  window->edit_copy = get_action_assert (xml, "edit_copy");
  window->edit_cut = get_action_assert (xml, "edit_cut");
  window->edit_paste = get_action_assert (xml, "edit_paste");

  window->buffer = GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view)));

  window->sb = get_widget_assert (xml, "statusbar2");
  window->text_context = gtk_statusbar_get_context_id (GTK_STATUSBAR (window->sb), "Text Context");

  g_signal_connect (window->buffer, "changed", 
		    G_CALLBACK (on_text_changed), window);

  g_signal_connect (window->buffer, "modified-changed", 
		    G_CALLBACK (on_modified_changed), window);

  g_signal_connect_swapped (get_action_assert (xml, "file_print"), "activate",
                            G_CALLBACK (psppire_syntax_window_print), window);


  g_signal_connect_swapped (window->undo_menuitem,
			    "activate",
                            G_CALLBACK (undo_last_edit),
			    window);

  g_signal_connect_swapped (window->redo_menuitem,
			    "activate",
                            G_CALLBACK (redo_last_edit),
			    window);

  undo_redo_update (window);

  window->sel_handler = g_signal_connect_swapped (clip_primary, "owner-change", 
						   G_CALLBACK (selection_changed), window);

  window->ps_handler = g_signal_connect (clip_selection, "owner-change", 
					  G_CALLBACK (set_paste_sensitivity), window);

  connect_help (xml);

  gtk_container_add (GTK_CONTAINER (window), box);

  g_object_ref (menubar);

  g_object_ref (sw);

  g_object_ref (window->sb);

  gtk_box_pack_start (GTK_BOX (box), menubar, FALSE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), sw, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), window->sb, FALSE, TRUE, 0);

  gtk_widget_show_all (box);

  g_signal_connect_swapped (get_action_assert (xml,"file_new_syntax"), "activate", G_CALLBACK (create_syntax_window), NULL);

  g_signal_connect (get_action_assert (xml,"file_new_data"),
		    "activate",
		    G_CALLBACK (create_data_window),
		    window);

  g_signal_connect_swapped (get_action_assert (xml, "file_open"),
		    "activate",
		    G_CALLBACK (psppire_window_open),
		    window);

  g_signal_connect_swapped (get_action_assert (xml, "file_save"),
		    "activate",
		    G_CALLBACK (psppire_window_save),
		    window);

  g_signal_connect_swapped (get_action_assert (xml, "file_save_as"),
		    "activate",
		    G_CALLBACK (psppire_window_save_as),
		    window);

  g_signal_connect (get_action_assert (xml,"file_quit"),
		    "activate",
		    G_CALLBACK (on_quit),
		    window);

  g_signal_connect_swapped (window->edit_delete,
		    "activate",
		    G_CALLBACK (on_edit_delete),
		    window);

  g_signal_connect_swapped (window->edit_copy,
		    "activate",
		    G_CALLBACK (on_edit_copy),
		    window);

  g_signal_connect_swapped (window->edit_cut,
		    "activate",
		    G_CALLBACK (on_edit_cut),
		    window);

  g_signal_connect_swapped (window->edit_paste,
		    "activate",
		    G_CALLBACK (on_edit_paste),
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

  merge_help_menu (uim);

  PSPPIRE_WINDOW (window)->menu =
    GTK_MENU_SHELL (gtk_ui_manager_get_widget (uim,"/ui/menubar/windows/windows_minimise_all")->parent);
  }

  g_object_unref (xml);
}





GtkWidget*
psppire_syntax_window_new (const char *encoding)
{
  return GTK_WIDGET (g_object_new (psppire_syntax_window_get_type (),
				   "description", _("Syntax Editor"),
                                   "encoding", encoding,
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
			    _("Cannot load syntax file `%s'"),
			    fn);

  g_free (fn);

  g_object_set (dialog, "icon-name", "pspp", NULL);

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
					    "%s", err->message);

  gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);
}

/*
  Loads the buffer from the file called FILENAME
*/
gboolean
syntax_load (PsppireWindow *window, const gchar *filename,
             const gchar *encoding, gpointer not_used)
{
  GError *err = NULL;
  gchar *text_locale = NULL;
  gchar *text_utf8 = NULL;
  gsize len_locale = -1;
  gsize len_utf8 = -1;
  GtkTextIter iter;
  PsppireSyntaxWindow *sw = PSPPIRE_SYNTAX_WINDOW (window);
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (sw->buffer);

  /* FIXME: What if it's a very big file ? */
  if ( ! g_file_get_contents (filename, &text_locale, &len_locale, &err) )
    {
      error_dialog (GTK_WINDOW (window), filename, err);
      g_clear_error (&err);
      return FALSE;
    }

  if (!encoding || !encoding[0])
    {
      /* Determine the file's encoding and update sw->encoding.  (The ordering
         is important here because encoding_guess_whole_file() often returns
         its argument instead of a copy of it.) */
      char *guessed_encoding;

      guessed_encoding = g_strdup (encoding_guess_whole_file (sw->encoding,
                                                              text_locale,
                                                              len_locale));
      g_free (sw->encoding);
      sw->encoding = guessed_encoding;
    }
  else
    {
      g_free (sw->encoding);
      sw->encoding = g_strdup (encoding);
    }

  text_utf8 = recode_substring_pool ("UTF-8", sw->encoding,
                                     ss_buffer (text_locale, len_locale),
                                     NULL).string;
  free (text_locale);

  if ( text_utf8 == NULL )
    {
      error_dialog (GTK_WINDOW (window), filename, err);
      g_clear_error (&err);
      return FALSE;
    }

  gtk_text_buffer_get_iter_at_line (buffer, &iter, 0);

  gtk_text_buffer_insert (buffer, &iter, text_utf8, len_utf8);

  gtk_text_buffer_set_modified (buffer, FALSE);

  free (text_utf8);

  add_most_recent (filename, "text/x-spss-syntax", sw->encoding);

  return TRUE;
}



static void
psppire_syntax_window_iface_init (PsppireWindowIface *iface)
{
  iface->save = syntax_save;
  iface->pick_filename = syntax_pick_filename;
  iface->load = syntax_load;
}




static void
undo_redo_update (PsppireSyntaxWindow *window)
{
  gtk_action_set_sensitive (window->undo_menuitem,
			    gtk_source_buffer_can_undo (window->buffer));

  gtk_action_set_sensitive (window->redo_menuitem,
			    gtk_source_buffer_can_redo (window->buffer));
}

static void
undo_last_edit (PsppireSyntaxWindow *window)
{
  gtk_source_buffer_undo (window->buffer);
  undo_redo_update (window);
}

static void
redo_last_edit (PsppireSyntaxWindow *window)
{
  gtk_source_buffer_redo (window->buffer);
  undo_redo_update (window);
}



/* Printing related stuff */


static void
begin_print (GtkPrintOperation *operation,
          GtkPrintContext   *context,
          PsppireSyntaxWindow *window)
{
  window->compositor =
    gtk_source_print_compositor_new (window->buffer);
}


static void
end_print (GtkPrintOperation *operation,
          GtkPrintContext   *context,
          PsppireSyntaxWindow *window)
{
  g_object_unref (window->compositor);
  window->compositor = NULL;
}



static gboolean
paginate (GtkPrintOperation *operation,
          GtkPrintContext   *context,
          PsppireSyntaxWindow *window)
{
  if (gtk_source_print_compositor_paginate (window->compositor, context))
    {
      gint n_pages = gtk_source_print_compositor_get_n_pages (window->compositor);
      gtk_print_operation_set_n_pages (operation, n_pages);
        
      return TRUE;
    }

  return FALSE;
}

static void
draw_page (GtkPrintOperation *operation,
           GtkPrintContext   *context,
           gint               page_nr,
          PsppireSyntaxWindow *window)
{
  gtk_source_print_compositor_draw_page (window->compositor, 
					 context,
					 page_nr);
}



static void
psppire_syntax_window_print (PsppireSyntaxWindow *window)
{
  GtkPrintOperationResult res;

  GtkPrintOperation *print = gtk_print_operation_new ();

  if (window->print_settings != NULL) 
    gtk_print_operation_set_print_settings (print, window->print_settings);


  g_signal_connect (print, "begin_print", G_CALLBACK (begin_print), window);
  g_signal_connect (print, "end_print", G_CALLBACK (end_print),     window);
  g_signal_connect (print, "draw_page", G_CALLBACK (draw_page),     window);
  g_signal_connect (print, "paginate", G_CALLBACK (paginate),       window);

  res = gtk_print_operation_run (print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                                 GTK_WINDOW (window), NULL);

  if (res == GTK_PRINT_OPERATION_RESULT_APPLY)
    {
      if (window->print_settings != NULL)
        g_object_unref (window->print_settings);
      window->print_settings = g_object_ref (gtk_print_operation_get_print_settings (print));
    }

  g_object_unref (print);
}
