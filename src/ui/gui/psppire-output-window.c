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
#include "helper.h"

#include <libpspp/message.h>
#include <stdlib.h>

#include "about.h"

#include "psppire-output-window.h"


#include "xalloc.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid



static void psppire_output_window_base_finalize (PsppireOutputWindowClass *, gpointer);
static void psppire_output_window_base_init     (PsppireOutputWindowClass *class);
static void psppire_output_window_class_init    (PsppireOutputWindowClass *class);
static void psppire_output_window_init          (PsppireOutputWindow      *window);


GType
psppire_output_window_get_type (void)
{
  static GType psppire_output_window_type = 0;

  if (!psppire_output_window_type)
    {
      static const GTypeInfo psppire_output_window_info =
      {
	sizeof (PsppireOutputWindowClass),
	(GBaseInitFunc) psppire_output_window_base_init,
        (GBaseFinalizeFunc) psppire_output_window_base_finalize,
	(GClassInitFunc)psppire_output_window_class_init,
	(GClassFinalizeFunc) NULL,
	NULL,
        sizeof (PsppireOutputWindow),
	0,
	(GInstanceInitFunc) psppire_output_window_init,
      };

      psppire_output_window_type =
	g_type_register_static (PSPPIRE_TYPE_WINDOW, "PsppireOutputWindow",
				&psppire_output_window_info, 0);
    }

  return psppire_output_window_type;
}

static GObjectClass *parent_class;

static void
psppire_output_window_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (parent_class)->finalize)
    (*G_OBJECT_CLASS (parent_class)->finalize) (object);
}


static void
psppire_output_window_class_init (PsppireOutputWindowClass *class)
{
  parent_class = g_type_class_peek_parent (class);
}


static void
psppire_output_window_base_init (PsppireOutputWindowClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = psppire_output_window_finalize;
}



static void
psppire_output_window_base_finalize (PsppireOutputWindowClass *class,
				     gpointer class_data)
{
}




static PsppireOutputWindow *the_output_viewer = NULL;


int viewer_length = 16;
int viewer_width = 59;

/* Callback for the "delete" action (clicking the x on the top right
   hand corner of the window) */
static gboolean
on_delete (GtkWidget *w, GdkEvent *event, gpointer user_data)
{
  PsppireOutputWindow *ow = PSPPIRE_OUTPUT_WINDOW (user_data);

  gtk_widget_destroy (GTK_WIDGET (ow));

  the_output_viewer = NULL;

  unlink (OUTPUT_FILE_NAME);

  return FALSE;
}



static void
cancel_urgency (GtkWindow *window,  gpointer data)
{
  gtk_window_set_urgency_hint (window, FALSE);
}
/* Sets width and length according to the new size
   of the output window */
static void
on_textview_resize (GtkWidget     *widget,
		    GtkAllocation *allocation,
		    gpointer       user_data)
{
  PangoContext * context ;
  PangoLayout *layout ;
  PangoRectangle logical;
  GtkStyle *style;
  gint right_margin, left_margin;
  GtkTextView *text_view = GTK_TEXT_VIEW (widget);

  context = gtk_widget_create_pango_context (widget);
  layout = pango_layout_new (context);

  style = gtk_widget_get_style (widget);

  pango_layout_set_font_description (layout, style->font_desc);

  /* Find the width of one character.  We can use any character, because
     the textview has a monospaced font */
  pango_layout_set_text (layout, "M", 1);

  pango_layout_get_extents (layout,  NULL, &logical);

  left_margin = gtk_text_view_get_left_margin (text_view);
  right_margin = gtk_text_view_get_right_margin (text_view);

  viewer_length = allocation->height / PANGO_PIXELS (logical.height);
  viewer_width = (allocation->width - right_margin - left_margin)
    / PANGO_PIXELS (logical.width);

  g_object_unref (G_OBJECT (layout));
  g_object_unref (G_OBJECT (context));
}


static void
psppire_output_window_init (PsppireOutputWindow *window)
{
  GtkBuilder *xml = builder_new ("output-viewer.ui");

  GtkWidget *box = gtk_vbox_new (FALSE, 0);

  GtkWidget *sw = get_widget_assert (xml, "scrolledwindow1");

  GtkWidget *menubar = get_widget_assert (xml, "menubar1");

  window->textview = get_widget_assert (xml, "output-viewer-textview");


  gtk_container_add (GTK_CONTAINER (window), box);


  g_object_ref (menubar);
  gtk_widget_unparent (menubar);

  g_object_ref (sw);
  gtk_widget_unparent (sw);


  gtk_box_pack_start (GTK_BOX (box), menubar, FALSE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), sw, TRUE, TRUE, 0);


  gtk_widget_show_all (box);

  connect_help (xml);

  window->buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (window->textview));

  g_signal_connect (window,
		    "focus-in-event",
		    G_CALLBACK (cancel_urgency),
		    NULL);

  {
    /* Output uses ascii characters for tabular material.
       So we need a monospaced font otherwise it'll look silly */
    PangoFontDescription *font_desc =
      pango_font_description_from_string ("monospace");

    gtk_widget_modify_font (window->textview, font_desc);
    pango_font_description_free (font_desc);
  }

  g_signal_connect (window->textview, "size-allocate",
		    G_CALLBACK (on_textview_resize), NULL);

  window->fp = NULL;

  g_signal_connect (get_object_assert (xml,"help_about"),
		    "activate",
		    G_CALLBACK (about_new),
		    window);

  g_signal_connect (get_object_assert (xml,"help_reference"),
		    "activate",
		    G_CALLBACK (reference_manual),
		    NULL);

  g_signal_connect (get_object_assert (xml,"windows_minimise-all"),
		    "activate",
		    G_CALLBACK (psppire_window_minimise_all),
		    NULL);

  {
    GtkUIManager *uim = GTK_UI_MANAGER (get_object_assert (xml, "uimanager1"));

    PSPPIRE_WINDOW (window)->menu =
      GTK_MENU_SHELL (gtk_ui_manager_get_widget (uim,"/ui/menubar1/windows_menuitem/windows_minimise-all")->parent);
  }

  g_object_unref (xml);

  g_signal_connect (window, "delete-event",
		    G_CALLBACK (on_delete), window);
}


GtkWidget*
psppire_output_window_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_output_window_get_type (),
				   "filename", "Output",
				   "description", _("Output Viewer"),
				   NULL));
}

static void reload_viewer (PsppireOutputWindow *ow);

void
psppire_output_window_reload (void)
{
  struct stat buf;

  /* If there is no output, then don't do anything */
  if (0 != stat (OUTPUT_FILE_NAME, &buf))
    return ;

  if ( NULL == the_output_viewer )
    {
      the_output_viewer = PSPPIRE_OUTPUT_WINDOW (psppire_output_window_new ());
      gtk_widget_show (GTK_WIDGET (the_output_viewer));
    }

  reload_viewer (the_output_viewer);

}


static void
reload_viewer (PsppireOutputWindow *ow)
{
  GtkTextIter end_iter;
  GtkTextMark *mark ;

  static char *line = NULL;

  gboolean chars_inserted = FALSE;

  gtk_text_buffer_get_end_iter (ow->buffer, &end_iter);

  line = xrealloc (line, sizeof (char) * (viewer_width + 1));

  mark = gtk_text_buffer_create_mark (ow->buffer, NULL, &end_iter, TRUE);

#ifdef __CYGWIN__
  /*
    Apparently Windoze is not capabale of writing to a file whilst
    another (or the same) process is reading from it.   Therefore, we
    must close the file after reading it, and clear the entire buffer
    before writing to it.
    This will be slower for large buffers, but should work
    (in so far as anything ever works on windows).
  */
  {
    GtkTextIter start_iter;
    FILE *fp = fopen (OUTPUT_FILE_NAME, "r");
    if ( !fp)
      {
	g_print ("Cannot open %s\n", OUTPUT_FILE_NAME);
	return;
      }

    /* Delete all the entire buffer */
    gtk_text_buffer_get_start_iter (ov->buffer, &start_iter);
    gtk_text_buffer_delete (ov->buffer, &start_iter, &end_iter);


    gtk_text_buffer_get_start_iter (ov->buffer, &start_iter);
    /* Read in the next lot of text */
    while (fgets (line, viewer_width + 1, fp) != NULL)
      {
	chars_inserted = TRUE;
	gtk_text_buffer_insert (ov->buffer, &start_iter, line, -1);
      }

    fclose (fp);
  }
#else
  {
    if ( ow->fp == NULL)
      {
	ow->fp = fopen (OUTPUT_FILE_NAME, "r");
	if ( ow->fp == NULL)
	  {
	    g_print ("Cannot open %s\n", OUTPUT_FILE_NAME);
	    return;
	  }
      }

    /* Read in the next lot of text */
    while (fgets (line, viewer_width + 1, ow->fp) != NULL)
      {
	chars_inserted = TRUE;
	gtk_text_buffer_insert (ow->buffer, &end_iter, line, -1);
      }
  }
#endif

  /* Scroll to where the start of this lot of text begins */
  gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW (ow->textview),
				mark,
				0.1, TRUE, 0.0, 0.0);


  if ( chars_inserted )
    gtk_window_set_urgency_hint ( GTK_WINDOW (ow), TRUE);
}



