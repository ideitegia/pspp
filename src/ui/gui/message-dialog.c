/* 
   PSPPIRE --- A Graphical User Interface for PSPP
   Copyright (C) 2004,2005  Free Software Foundation
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
   02110-1301, USA. 
*/


#include <stdio.h>
#include <stdarg.h>

#include <config.h>
#include <libpspp/message.h>
#include "message-dialog.h"
#include "progname.h"


#include <gtk/gtk.h>
#include <glade/glade.h>

#include "helper.h"

extern GladeXML *xml;

#define _(A) A

static void handle_msg(const struct msg *);

void
message_dialog_init (void) 
{
  msg_init(handle_msg);
}

static void
handle_msg(const struct msg *m)
{
  GtkWindow *parent;
  GtkWidget *dialog;

  gint message_type;
  const char *msg;

  switch (m->severity)
    {
    case MSG_ERROR:
      message_type = GTK_MESSAGE_ERROR;
      break;
    case MSG_WARNING:
      message_type = GTK_MESSAGE_WARNING;
      break;
    case MSG_NOTE:
    default:
      message_type = GTK_MESSAGE_INFO;
      break;
    };
  
  switch (m->category) 
    {
    case MSG_SYNTAX:
      msg = _("Script Error");
      break;

    case MSG_DATA:
      msg = _("Data File Error");
      break;

    case MSG_GENERAL:
    default:
      msg = _("PSPP Error");
      break;
    };
  
  parent = GTK_WINDOW(get_widget_assert(xml, "data_editor"));

  dialog = gtk_message_dialog_new(parent,
				  GTK_DIALOG_MODAL,
				  message_type,
				  GTK_BUTTONS_CLOSE,
				  msg);
  
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                           "%s", m->text);
    
  gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);

  gtk_dialog_run(GTK_DIALOG(dialog));

  gtk_widget_destroy (dialog);
}

/* FIXME: This is a stub .
 * A temporary workaround until getl.c is rearranged
 */
void
msg_location (struct msg_locator *loc)
{
	loc->file_name = 0;
	loc->line_number = -1;
}

