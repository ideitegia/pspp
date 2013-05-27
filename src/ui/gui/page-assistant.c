/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2012, 2013  Free Software Foundation

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

#include "ui/gui/text-data-import-dialog.h"

#include <errno.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "data/data-in.h"
#include "data/data-out.h"
#include "data/format-guesser.h"
#include "data/value-labels.h"
#include "language/data-io/data-parser.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/line-reader.h"
#include "libpspp/message.h"
#include "ui/gui/checkbox-treeview.h"
#include "ui/gui/dialog-common.h"
#include "ui/gui/executor.h"
#include "ui/gui/helper.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dialog.h"
#include "ui/gui/psppire-encoding-selector.h"
#include "ui/gui/psppire-empty-list-store.h"
#include "ui/gui/psppire-var-sheet.h"

#include "gl/error.h"
#include "gl/intprops.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Assistant. */

static void close_assistant (struct import_assistant *, int response);
static void on_prepare (GtkAssistant *assistant, GtkWidget *page,
                        struct import_assistant *);
static void on_cancel (GtkAssistant *assistant, struct import_assistant *);
static void on_close (GtkAssistant *assistant, struct import_assistant *);
static void on_paste (GtkButton *button, struct import_assistant *);
static void on_reset (GtkButton *button, struct import_assistant *);

/* Initializes IA's asst substructure.  PARENT_WINDOW must be the
   window to use as the assistant window's parent.  */
struct import_assistant *
init_assistant (GtkWindow *parent_window)
{
  struct import_assistant *ia = xzalloc (sizeof *ia);
  struct assistant *a = &ia->asst;

  a->builder = builder_new ("text-data-import.ui");
  a->assistant = GTK_ASSISTANT (gtk_assistant_new ());

  a->prop_renderer = gtk_cell_renderer_text_new ();
  g_object_ref_sink (a->prop_renderer);
  a->fixed_renderer = gtk_cell_renderer_text_new ();
  g_object_ref_sink (a->fixed_renderer);
  g_object_set (G_OBJECT (a->fixed_renderer),
                "family", "Monospace",
                (void *) NULL);

  g_signal_connect (a->assistant, "prepare", G_CALLBACK (on_prepare), ia);
  g_signal_connect (a->assistant, "cancel", G_CALLBACK (on_cancel), ia);
  g_signal_connect (a->assistant, "close", G_CALLBACK (on_close), ia);
  a->paste_button = gtk_button_new_from_stock (GTK_STOCK_PASTE);
  gtk_assistant_add_action_widget (a->assistant, a->paste_button);
  g_signal_connect (a->paste_button, "clicked", G_CALLBACK (on_paste), ia);
  a->reset_button = gtk_button_new_from_stock ("pspp-stock-reset");
  gtk_assistant_add_action_widget (a->assistant, a->reset_button);
  g_signal_connect (a->reset_button, "clicked", G_CALLBACK (on_reset), ia);
  gtk_window_set_title (GTK_WINDOW (a->assistant),
                        _("Importing Delimited Text Data"));
  gtk_window_set_transient_for (GTK_WINDOW (a->assistant), parent_window);
  gtk_window_set_icon_name (GTK_WINDOW (a->assistant), "pspp");


  return ia;
}

/* Frees IA's asst substructure. */
void
destroy_assistant (struct import_assistant *ia)
{
  struct assistant *a = &ia->asst;

  g_object_unref (a->prop_renderer);
  g_object_unref (a->fixed_renderer);
  g_object_unref (a->builder);
}

/* Appends a page of the given TYPE, with PAGE as its content, to
   the GtkAssistant encapsulated by IA.  Returns the GtkWidget
   that represents the page. */
GtkWidget *
add_page_to_assistant (struct import_assistant *ia,
                       GtkWidget *page, GtkAssistantPageType type)
{
  const char *title;
  char *title_copy;
  GtkWidget *content;

  title = gtk_window_get_title (GTK_WINDOW (page));
  title_copy = xstrdup (title ? title : "");

  content = gtk_bin_get_child (GTK_BIN (page));
  assert (content);
  g_object_ref (content);
  gtk_container_remove (GTK_CONTAINER (page), content);

  gtk_widget_destroy (page);

  gtk_assistant_append_page (ia->asst.assistant, content);
  gtk_assistant_set_page_type (ia->asst.assistant, content, type);
  gtk_assistant_set_page_title (ia->asst.assistant, content, title_copy);
  gtk_assistant_set_page_complete (ia->asst.assistant, content, true);

  free (title_copy);

  return content;
}

/* Called just before PAGE is displayed as the current page of
   ASSISTANT, this updates IA content according to the new
   page. */
static void
on_prepare (GtkAssistant *assistant, GtkWidget *page,
            struct import_assistant *ia)
{
  int pn = gtk_assistant_get_current_page (assistant);

  gtk_widget_show (ia->asst.reset_button);
  gtk_widget_hide (ia->asst.paste_button);

  if ( ia->spreadsheet) 
    {
      if (pn == 0)
	{
	  prepare_sheet_spec_page (ia);
        }
      else if (pn == 1)
	{
	  post_sheet_spec_page (ia);
	  prepare_formats_page (ia);
	}
    }
  else
    {
      switch (pn)
	{
	case 0:
	  reset_intro_page (ia);
	  break;
	case 1:
	  reset_first_line_page (ia);
	  break;
	case 2:
	  prepare_separators_page (ia);
	  reset_separators_page (ia);
	  break;
	case 3:
	  prepare_formats_page (ia);
	  reset_formats_page (ia);
	  break;
	}
    }


#if GTK3_TRANSITION
  if (gtk_assistant_get_page_type (assistant, page)
      == GTK_ASSISTANT_PAGE_CONFIRM)
    gtk_widget_grab_focus (assistant->apply);
  else
    gtk_widget_grab_focus (assistant->forward);
#endif
}

/* Called when the Cancel button in the assistant is clicked. */
static void
on_cancel (GtkAssistant *assistant, struct import_assistant *ia)
{
  close_assistant (ia, GTK_RESPONSE_CANCEL);
}

/* Called when the Apply button on the last page of the assistant
   is clicked. */
static void
on_close (GtkAssistant *assistant, struct import_assistant *ia)
{
  close_assistant (ia, GTK_RESPONSE_APPLY);
}

/* Called when the Paste button on the last page of the assistant
   is clicked. */
static void
on_paste (GtkButton *button, struct import_assistant *ia)
{
  close_assistant (ia, PSPPIRE_RESPONSE_PASTE);
}

/* Called when the Reset button is clicked. */
static void
on_reset (GtkButton *button, struct import_assistant *ia)
{
  gint pn = gtk_assistant_get_current_page (ia->asst.assistant);
  
  if ( ia->spreadsheet) 
    {
      switch (pn)
	{
	case 0:
	  reset_sheet_spec_page (ia);
	 break;
	case 1:
	  reset_formats_page (ia);
	  break;
	}
    }
  else
    {
      switch (pn)
	{
	case 0:
	  reset_intro_page (ia);
	  break;
	case 1:
	  reset_first_line_page (ia);
	  break;
	case 2:
	  reset_separators_page (ia);
	  break;
	case 3:
	  reset_formats_page (ia);
	  break;
	}
    }
}

/* Causes the assistant to close, returning RESPONSE for
   interpretation by text_data_import_assistant. */
static void
close_assistant (struct import_assistant *ia, int response)
{
  ia->asst.response = response;
  /*  Use our loop_done variable until we find out
      why      g_main_loop_quit (ia->asst.main_loop); doesn't work.
  */
  ia->asst.loop_done = true;
  gtk_widget_hide (GTK_WIDGET (ia->asst.assistant));
}

