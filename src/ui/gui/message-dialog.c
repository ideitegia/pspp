/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.

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


#include <stdio.h>
#include <stdarg.h>

#include <config.h>
#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include <libpspp/message.h>
#include <libpspp/msg-locator.h>
#include "message-dialog.h"
#include "progname.h"


#include <gtk/gtk.h>
#include <glade/glade.h>
#include <glib.h>

#include "helper.h"

static void enqueue_msg (const struct msg *m);
static gboolean popup_messages (gpointer);

#define MAX_EARLY_MESSAGES 100
static GQueue *early_queue;

static unsigned long dropped_messages;

#define MAX_LATE_MESSAGES 10
static GQueue *late_queue;

static int error_cnt, warning_cnt, note_cnt;

static GladeXML *message_xml;
static GtkDialog *message_dialog;

void
message_dialog_init (struct source_stream *ss)
{
  early_queue = g_queue_new ();
  dropped_messages = 0;
  late_queue = g_queue_new ();
  error_cnt = warning_cnt = note_cnt = 0;
  msg_init (ss, enqueue_msg);
  message_xml = XML_NEW ("message-dialog.glade");
  message_dialog = GTK_DIALOG (get_widget_assert (message_xml,
                                                  "message-dialog"));
}

void
message_dialog_done (void)
{
  msg_done ();
  g_queue_free (early_queue);
  dropped_messages = 0;
  g_queue_free (late_queue);
  gtk_widget_destroy (GTK_WIDGET (message_dialog));
  g_object_unref (message_xml);
}

static void
format_message (struct msg *m, struct string *msg)
{
  const char *label;

  if (m->where.file_name)
    ds_put_format (msg, "%s:", m->where.file_name);
  if (m->where.line_number != -1)
    ds_put_format (msg, "%d:", m->where.line_number);
  if (m->where.file_name || m->where.line_number != -1)
    ds_put_char (msg, ' ');

  switch (m->severity)
    {
    case MSG_ERROR:
      switch (m->category)
	{
	case MSG_SYNTAX:
	  label = _("syntax error");
	  break;

	case MSG_DATA:
	  label = _("data file error");
	  break;

	case MSG_GENERAL:
	default:
	  label = _("PSPP error");
	  break;
	}
      break;
    case MSG_WARNING:
      switch (m->category)
	{
	case MSG_SYNTAX:
	  label = _("syntax warning");
          break;

	case MSG_DATA:
	  label = _("data file warning");
	  break;

	case MSG_GENERAL:
        default:
	  label = _("PSPP warning");
          break;
        }
      break;
    case MSG_NOTE:
    default:
      switch (m->category)
        {
        case MSG_SYNTAX:
	  label = _("syntax information");
          break;

        case MSG_DATA:
	  label = _("data file information");
          break;

        case MSG_GENERAL:
        default:
	  label = _("PSPP information");
	  break;
	}
      break;
    }
  ds_put_format (msg, "%s: %s\n", label, m->text);
  msg_destroy (m);
}

static void
enqueue_msg (const struct msg *msg)
{
  struct msg *m = msg_dup (msg);

  switch (m->severity)
    {
    case MSG_ERROR:
      error_cnt++;
      break;
    case MSG_WARNING:
      warning_cnt++;
      break;
    case MSG_NOTE:
      note_cnt++;
      break;
    }

  if (g_queue_get_length (early_queue) < MAX_EARLY_MESSAGES)
    {
      if (g_queue_is_empty (early_queue))
        g_idle_add (popup_messages, NULL);
      g_queue_push_tail (early_queue, m);
    }
  else
    {
      if (g_queue_get_length (late_queue) >= MAX_LATE_MESSAGES)
        {
          struct msg *m = g_queue_pop_head (late_queue);
          msg_destroy (m);
          dropped_messages++;
        }
      g_queue_push_tail (late_queue, m);
    }
}

gboolean
popup_messages (gpointer unused UNUSED)
{
  GtkTextBuffer *text_buffer;
  GtkTextIter end;
  GtkTextView *text_view;
  GtkLabel *label;
  struct string lead, msg;
  int message_cnt;

  /* If a pointer grab is in effect, then the combination of that, and
     a modal dialog box, will cause an impossible situation.
     So don't pop it up just yet.
  */
  if ( gdk_pointer_is_grabbed ())
    return TRUE;

  /* Compose the lead-in. */
  message_cnt = error_cnt + warning_cnt + note_cnt;
  ds_init_empty (&lead);
  if (dropped_messages == 0)
    ds_put_format (
      &lead,
      ngettext ("The PSPP processing engine reported the following message:",
                "The PSPP processing engine reported the following messages:",
                message_cnt));
  else
    {
      ds_put_format (
        &lead,
        ngettext ("The PSPP processing engine reported %d message.",
                  "The PSPP processing engine reported %d messages.",
                  message_cnt),
        message_cnt);
      ds_put_cstr (&lead, "  ");
      ds_put_format (
        &lead,
        ngettext ("%d of these messages are displayed below.",
                  "%d of these messages are displayed below.",
                  MAX_EARLY_MESSAGES + MAX_LATE_MESSAGES),
        MAX_EARLY_MESSAGES + MAX_LATE_MESSAGES);
    }


  /* Compose the messages. */
  ds_init_empty (&msg);
  while (!g_queue_is_empty (early_queue))
    format_message (g_queue_pop_head (early_queue), &msg);
  if (dropped_messages)
    {
      ds_put_format (&msg, "...\nOmitting %lu messages\n...\n",
                     dropped_messages);
      dropped_messages = 0;
    }
  while (!g_queue_is_empty (late_queue))
    format_message (g_queue_pop_head (late_queue), &msg);

  /* Set up the dialog. */
  if (message_xml == NULL || message_dialog == NULL)
    goto use_fallback;

  text_buffer = gtk_text_buffer_new (NULL);
  gtk_text_buffer_get_end_iter (text_buffer, &end);
  gtk_text_buffer_insert (text_buffer, &end, ds_data (&msg), ds_length (&msg));
  ds_destroy (&msg);

  label = GTK_LABEL (get_widget_assert (message_xml, "lead-in"));
  if (label == NULL)
    goto use_fallback;
  gtk_label_set_text (label, ds_cstr (&lead));

  text_view = GTK_TEXT_VIEW (get_widget_assert (message_xml, "message"));
  if (text_view == NULL)
    goto use_fallback;
  gtk_text_view_set_buffer (text_view, text_buffer);

  gtk_dialog_run (message_dialog);
  gtk_widget_hide (GTK_WIDGET (message_dialog));

  return FALSE;

use_fallback:
  g_warning ("Could not create message dialog.  "
             "Is PSPPIRE properly installed?");
  fputs (ds_cstr (&msg), stderr);
  ds_destroy (&lead);
  ds_destroy (&msg);
  return FALSE;
}

