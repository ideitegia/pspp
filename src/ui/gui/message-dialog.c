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


static GQueue *message_queue;


void
message_dialog_init (struct source_stream *ss)
{
  message_queue = g_queue_new ();
  msg_init (ss, enqueue_msg);
}

void
message_dialog_done (void)
{
  msg_done ();
  g_queue_free (message_queue);
}

static gboolean
dequeue_message (gpointer data)
{
  struct msg * m ;

  /* If a pointer grab is in effect, then the combination of that, and
     a modal dialog box, will cause an impossible situation.
     So don't pop it up just yet.
  */
  if ( gdk_pointer_is_grabbed ())
    return TRUE;

  m = g_queue_pop_tail (message_queue);

  if ( m )
    {
      popup_message (m);
      msg_destroy (m);
      return TRUE;
    }

  return FALSE;
}

static void
enqueue_msg (const struct msg *msg)
{
  struct msg *m = msg_dup (msg);

  g_queue_push_head (message_queue, m);

  g_idle_add (dequeue_message, 0);
}


void
popup_message (const struct msg *m)
{
  GtkWidget *dialog;
  gchar *location = NULL;

  gint message_type;
  const char *msg;

  switch (m->severity)
    {
    case MSG_ERROR:
      message_type = GTK_MESSAGE_ERROR;
      switch (m->category)
	{
	case MSG_SYNTAX:
	  msg = _("Syntax Error");
	  break;

	case MSG_DATA:
	  msg = _("Data File Error");
	  break;

	case MSG_GENERAL:
	default:
	  msg = _("PSPP Error");
	  break;
	};
      break;
    case MSG_WARNING:
      message_type = GTK_MESSAGE_WARNING;
      switch (m->category)
	{
	case MSG_SYNTAX:
	  msg = _("Syntax Warning");
      break;

	case MSG_DATA:
	  msg = _("Data File Warning");
	  break;

	case MSG_GENERAL:
    default:
	  msg = _("PSPP Warning");
      break;
    };
      break;
    case MSG_NOTE:
    default:
      message_type = GTK_MESSAGE_INFO;
  switch (m->category)
    {
    case MSG_SYNTAX:
	  msg = _("Syntax Information");
      break;

    case MSG_DATA:
	  msg = _("Data File Information");
      break;

    case MSG_GENERAL:
    default:
	  msg = _("PSPP Information");
	  break;
	};
      break;
    };

  dialog = gtk_message_dialog_new ( NULL,
				  GTK_DIALOG_MODAL,
				  message_type,
				  GTK_BUTTONS_CLOSE,
				  msg);
  if ( m->where.line_number != -1)
    {
      location = g_strdup_printf (_("%s (line %d)"),
				  m->where.file_name ? m->where.file_name : "",
				  m->where.line_number);
    }
  else
    {
      location = g_strdup_printf (_("%s"),
				  m->where.file_name ? m->where.file_name : "");    }

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
					    _("%s %s"),
					    location,
					    m->text);
  g_free (location);

  gtk_window_set_keep_above (GTK_WINDOW (dialog), TRUE);

  gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);
}

