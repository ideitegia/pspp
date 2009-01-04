/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007  Free Software Foundation

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

#include "psppire-dialog.h"
#include "helper.h"
#include "data-editor.h"
#include <language/syntax-string-source.h>
#include "syntax-editor.h"
#include "psppire-var-store.h"
#include <ui/syntax-gen.h>

#include "comments-dialog.h"

#include "dialog-common.h"

#include <gtk/gtk.h>

#include <gettext.h>

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


struct comment_dialog
{
  GtkBuilder *xml;
  PsppireDict *dict;
};

static void refresh (PsppireDialog *dialog, const struct comment_dialog *);
static char *generate_syntax (const struct comment_dialog *);

static void
set_column_number (GtkTextBuffer *textbuffer,
     GtkTextIter   *iter,
     GtkTextMark   *mark,
     gpointer       data)
{
  GtkLabel *label = data;
  gchar *text ;

  text = g_strdup_printf ( _("Column Number: %d"),
			   1 + gtk_text_iter_get_line_offset (iter));

  gtk_label_set_text (label, text);

  g_free (text);
}

static void
wrap_line (GtkTextBuffer *buffer,
     GtkTextIter   *iter,
     gchar         *text,
     gint           count,
     gpointer       data)
{
  gint chars = gtk_text_iter_get_chars_in_line (iter);

  if ( chars > DOC_LINE_LENGTH )
    {
      GtkTextIter line_fold = *iter;
      GtkTextIter cursor;

      gtk_text_iter_set_line_offset (&line_fold, DOC_LINE_LENGTH);

      gtk_text_buffer_insert (buffer, &line_fold, "\r\n", 2);

      cursor = line_fold;
      gtk_text_iter_forward_to_line_end (&cursor);

      gtk_text_buffer_place_cursor (buffer, &cursor);
    }

}


void
comments_dialog (GObject *o, gpointer data)
{
  GtkTextIter iter;
  gint response ;
  struct data_editor *de = data;
  struct comment_dialog cd;

  GtkBuilder *xml = builder_new ("psppire.ui");

  GtkWidget *dialog = get_widget_assert (xml, "comments-dialog");
  GtkWidget *textview = get_widget_assert (xml, "comments-textview1");
  GtkWidget *label = get_widget_assert (xml, "column-number-label");
  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));

  PsppireVarStore *vs = NULL;

  g_object_get (de->data_editor, "var-store", &vs, NULL);

  gtk_window_set_transient_for (GTK_WINDOW (dialog), de->parent.window);

  {
    PangoContext * context ;
    PangoLayout *  layout ;
    PangoRectangle rect;

    /* Since we're going to truncate lines to 80 chars,
       we need a monospaced font otherwise it'll look silly */
    PangoFontDescription *font_desc =
      pango_font_description_from_string ("monospace");

    gtk_widget_modify_font (textview, font_desc);


    /* and let's just make sure that a complete line fits into the
       widget's width */
    context = gtk_widget_create_pango_context (textview);
    layout = pango_layout_new (context);

    pango_layout_set_text (layout, "M", 1);

    pango_layout_set_font_description (layout, font_desc);

    pango_layout_get_extents (layout, NULL, &rect);

    g_object_set (textview, "width-request",
		  PANGO_PIXELS (rect.width) * DOC_LINE_LENGTH + 20, NULL);

    g_object_unref (G_OBJECT (layout));
    g_object_unref (G_OBJECT (context));

    pango_font_description_free (font_desc);
  }

  cd.xml = xml;
  cd.dict = vs->dict;

  g_signal_connect (buffer, "mark-set",
		    G_CALLBACK (set_column_number), label);

  g_signal_connect_after (buffer, "insert-text",
			  G_CALLBACK (wrap_line), NULL);

  gtk_text_buffer_get_iter_at_offset (buffer, &iter, 0);
  gtk_text_buffer_place_cursor (buffer, &iter);


  g_signal_connect (dialog, "refresh", G_CALLBACK (refresh),  &cd);


  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  switch (response)
    {
    case GTK_RESPONSE_OK:
      {
	gchar *syntax = generate_syntax (&cd);
	struct getl_interface *sss = create_syntax_string_source (syntax);
	execute_syntax (sss);

	g_free (syntax);
      }
      break;
    case PSPPIRE_RESPONSE_PASTE:
      {
	gchar *syntax = generate_syntax (&cd);

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


static void
add_line_to_buffer (GtkTextBuffer *buffer, const char *line)
{
  gtk_text_buffer_insert_at_cursor (buffer, line, -1);

  gtk_text_buffer_insert_at_cursor (buffer, "\n", 1);
}

static void
refresh (PsppireDialog *dialog, const struct comment_dialog *cd)
{
  gint i;
  GtkWidget *tv = get_widget_assert (cd->xml, "comments-textview1");
  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (tv));

  gtk_text_buffer_set_text (buffer, "", 0);

  for ( i = 0 ; i < dict_get_document_line_cnt (cd->dict->dict); ++i )
    {
      struct string str;
      ds_init_empty (&str);
      dict_get_document_line (cd->dict->dict, i, &str);
      add_line_to_buffer (buffer, ds_cstr (&str));
      ds_destroy (&str);
    }
}



static char *
generate_syntax (const struct comment_dialog *cd)
{
  gint i;

  GString *str;
  gchar *text;
  GtkWidget *tv = get_widget_assert (cd->xml, "comments-textview1");
  GtkWidget *check = get_widget_assert (cd->xml, "comments-checkbutton1");
  GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (tv));
  const char *existing_docs = dict_get_documents (cd->dict->dict);

  str = g_string_new ("\n* Data File Comments.\n\n");

  if ( NULL != existing_docs)
    g_string_append (str, "DROP DOCUMENTS.\n");

  g_string_append (str, "ADD DOCUMENT\n");

  for (i = 0 ; i < gtk_text_buffer_get_line_count (buffer) ; ++i )
    {
      struct string tmp;
      GtkTextIter start;
      char *line;

      gtk_text_buffer_get_iter_at_line (buffer, &start, i);
      if (gtk_text_iter_ends_line (&start))
	line = g_strdup ("");
      else
        {
          GtkTextIter end = start;
          gtk_text_iter_forward_to_line_end (&end);
          line = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
        }

      ds_init_empty (&tmp);
      syntax_gen_string (&tmp, ss_cstr (line));
      g_free (line);

      g_string_append_printf (str, " %s\n", ds_cstr (&tmp));

      ds_destroy (&tmp);
    }
  g_string_append (str, " .\n");



  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check)))
    g_string_append (str, "DISPLAY DOCUMENTS.\n");

  text = str->str;

  g_string_free (str, FALSE);

  return text;
}
