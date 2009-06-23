/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009  Free Software Foundation

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
#include <output/cairo.h>
#include <output/manager.h>
#include <output/output.h>
#include <output/table.h>
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

/* Output driver class. */

static PsppireOutputWindow *the_output_viewer = NULL;

static gboolean
expose_event_callback (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
  struct som_entity *entity = g_object_get_data (G_OBJECT (widget), "entity");
  GdkWindow *window = widget->window;
  cairo_t *cairo = gdk_cairo_create (GDK_DRAWABLE (window));
  struct outp_driver *driver = xr_create_driver (cairo); /* XXX can fail */
  struct tab_table *t = entity->ext;
  void *rendering;

  rendering = entity->class->render_init (entity, driver, tab_l (t),
                                          tab_r (t), tab_t (t), tab_b (t));

  entity->class->title (rendering, 0, 0,
                        entity->table_num, entity->subtable_num,
                        entity->command_name);
  entity->class->render (rendering, tab_l (t), tab_t (t),
                         tab_nc (t) - tab_r (t),
                         tab_nr (t) - tab_b (t));

  entity->class->render_free (rendering);
  driver->class->close_driver (driver);
  outp_free_driver (driver);
  return TRUE;
}

static void
psppire_output_submit (struct outp_driver *this, struct som_entity *entity)
{
  if (the_output_viewer == NULL)
    {
      the_output_viewer = PSPPIRE_OUTPUT_WINDOW (psppire_output_window_new ());
      gtk_widget_show_all (GTK_WIDGET (the_output_viewer));
    }

  if (entity->type == SOM_TABLE)
    {
      GdkWindow *window = GTK_WIDGET (the_output_viewer)->window;
      cairo_t *cairo = gdk_cairo_create (GDK_DRAWABLE (window));
      struct outp_driver *driver = xr_create_driver (cairo); /* XXX can fail */
      struct tab_table *t = entity->ext;
      GtkWidget *drawing_area;
      void *rendering;
      int tw, th;

      tab_ref (t);
      rendering = entity->class->render_init (entity, driver, tab_l (t),
                                              tab_r (t), tab_t (t), tab_b (t));
      entity->class->area (rendering, &tw, &th);

      drawing_area = gtk_drawing_area_new ();
      gtk_widget_modify_bg (GTK_WIDGET (drawing_area), GTK_STATE_NORMAL,
                            &gtk_widget_get_style (drawing_area)->base[GTK_STATE_NORMAL]);
      g_object_set_data (G_OBJECT (drawing_area),
                         "entity", som_entity_clone (entity));
      gtk_widget_set_size_request (drawing_area, tw / 1024, th / 1024);
      gtk_layout_put (the_output_viewer->output, drawing_area,
                      0, the_output_viewer->y);
      gtk_widget_show (drawing_area);
      g_signal_connect (G_OBJECT (drawing_area), "expose_event",
                        G_CALLBACK (expose_event_callback), NULL);

      entity->class->render_free (rendering);
      driver->class->close_driver (driver);
      outp_free_driver (driver);

      if (tw / 1024 > the_output_viewer->max_width)
        the_output_viewer->max_width = tw / 1024;
      the_output_viewer->y += th / 1024;

      gtk_layout_set_size (the_output_viewer->output,
                           the_output_viewer->max_width, the_output_viewer->y);
    }

  gtk_window_set_urgency_hint (GTK_WINDOW (the_output_viewer), TRUE);
}

static struct outp_class psppire_output_class =
  {
    "PSPPIRE",                  /* name */
    true,                       /* special */
    NULL,                       /* open_driver */
    NULL,                       /* close_driver */
    NULL,                       /* open_page */
    NULL,                       /* close_page */
    NULL,                       /* flush */
    psppire_output_submit,      /* submit */
    NULL,                       /* line */
    NULL,                       /* text_metrics */
    NULL,                       /* text_draw */
    NULL,                       /* initialise_chart */
    NULL,                       /* finalise_chart */
  };

void
psppire_output_window_setup (void)
{
  outp_register_driver (outp_allocate_driver (&psppire_output_class,
                                              "PSPPIRE", 0));
}

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

  return FALSE;
}



static void
cancel_urgency (GtkWindow *window,  gpointer data)
{
  gtk_window_set_urgency_hint (window, FALSE);
}


static void
psppire_output_window_init (PsppireOutputWindow *window)
{
  GtkBuilder *xml = builder_new ("output-viewer.ui");

  gtk_widget_reparent (get_widget_assert (xml, "vbox1"), GTK_WIDGET (window));

  window->output = GTK_LAYOUT (get_widget_assert (xml, "output"));
  window->y = 0;

  gtk_widget_modify_bg (GTK_WIDGET (window->output), GTK_STATE_NORMAL,
                        &gtk_widget_get_style (GTK_WIDGET (window->output))->base[GTK_STATE_NORMAL]);

  connect_help (xml);

  g_signal_connect (window,
		    "focus-in-event",
		    G_CALLBACK (cancel_urgency),
		    NULL);

  g_signal_connect (get_action_assert (xml,"help_about"),
		    "activate",
		    G_CALLBACK (about_new),
		    window);

  g_signal_connect (get_action_assert (xml,"help_reference"),
		    "activate",
		    G_CALLBACK (reference_manual),
		    NULL);

  g_signal_connect (get_action_assert (xml,"windows_minimise-all"),
		    "activate",
		    G_CALLBACK (psppire_window_minimise_all),
		    NULL);

  {
    GtkUIManager *uim = GTK_UI_MANAGER (get_object_assert (xml, "uimanager1", GTK_TYPE_UI_MANAGER));

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
