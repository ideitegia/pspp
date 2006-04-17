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


void 
vmsg(int klass, const char *fmt, va_list args)
{
  gchar *msg = 0;
  gchar *text = g_strdup_vprintf (fmt, args);

  GtkWindow *parent ;
  GtkWidget *dialog ;
		    
  gint message_type;

  switch (klass)
    {
    case SE:
    case DE:
    case ME:
      message_type = GTK_MESSAGE_ERROR;
      break;
    case SW:
    case DW:
    case MW:
      message_type = GTK_MESSAGE_WARNING;
      break;
    case SM:
    case MM:
    default:
      message_type = GTK_MESSAGE_INFO;
      break;
    };
  
  switch (klass) 
    {
    case SE:
    case SW:
    case SM:
      msg = g_strdup(_("Script Error"));
      break;

    case DE:
    case DW:
      msg = g_strdup(_("Data File Error"));
      break;

    case ME:
    case MW:
    case MM:
    default:
      msg = g_strdup(_("PSPP Error"));
      break;
    };
  
  parent = GTK_WINDOW(get_widget_assert(xml, "data_editor"));

  dialog = gtk_message_dialog_new(parent,
				  GTK_DIALOG_MODAL,
				  message_type,
				  GTK_BUTTONS_CLOSE,
				  msg);
  
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), text);

  g_free(text);
  g_free(msg);
    
  gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);

  gtk_dialog_run(GTK_DIALOG(dialog));

  gtk_widget_destroy (dialog);

}


void 
msg(int klass, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vmsg(klass, fmt, ap);
  va_end(ap);
}


void
err_vmsg (const struct error *e, const char *format, va_list args)
{
  vmsg(e->class, format, args);
}


void 
err_assert_fail(const char *expr, const char *file, int line)
{
  msg(ME, "Assertion failed: %s:%d; (%s)\n",file,line,expr);
}

/* Writes MESSAGE formatted with printf, to stderr, if the
   verbosity level is at least LEVEL. */
void
verbose_msg (int level, const char *format, ...)
{
  /* Do nothing for now. */
}

/* FIXME: This is a stub .
 * A temporary workaround until getl.c is rearranged
 */
void
err_location (struct file_locator *f)
{
	f->file_name = 0;
	f->line_number = -1;
}

