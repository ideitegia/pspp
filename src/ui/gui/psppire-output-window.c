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

#include <libpspp/cast.h>
#include <libpspp/message.h>
#include <libpspp/string-map.h>
#include <output/cairo.h>
#include <output/chart-item.h>
#include <output/driver-provider.h>
#include <output/output-item.h>
#include <output/table-item.h>
#include <output/text-item.h>
#include <output/tab.h>
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

enum
  {
    COL_TITLE,                  /* Table title. */
    COL_Y,                      /* Y position of top of title. */
    N_COLS
  };

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
psppire_output_window_dispose (GObject *obj)
{
  PsppireOutputWindow *viewer = PSPPIRE_OUTPUT_WINDOW (obj);
  size_t i;

  for (i = 0; i < viewer->n_items; i++)
    output_item_unref (viewer->items[i]);
  free (viewer->items);
  viewer->items = NULL;
  viewer->n_items = viewer->allocated_items = 0;

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
psppire_output_window_class_init (PsppireOutputWindowClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);
  object_class->dispose = psppire_output_window_dispose;
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

struct psppire_output_driver
  {
    struct output_driver driver;
    PsppireOutputWindow *viewer;
    struct xr_driver *xr;
  };

static struct output_driver_class psppire_output_class;

static struct psppire_output_driver *
psppire_output_cast (struct output_driver *driver)
{
  assert (driver->class == &psppire_output_class);
  return UP_CAST (driver, struct psppire_output_driver, driver);
}

static gboolean
expose_event_callback (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
  struct xr_rendering *r = g_object_get_data (G_OBJECT (widget), "rendering");
  cairo_t *cr;

  cr = gdk_cairo_create (widget->window);
  xr_rendering_draw (r, cr);
  cairo_destroy (cr);

  return TRUE;
}

static void
psppire_output_submit (struct output_driver *this,
                       const struct output_item *item)
{
  struct psppire_output_driver *pod = psppire_output_cast (this);
  PsppireOutputWindow *viewer;
  GtkWidget *drawing_area;
  struct xr_rendering *r;
  struct string title;
  GtkTreeStore *store;
  GtkTreePath *path;
  GtkTreeIter iter;
  cairo_t *cr;
  int tw, th;

  if (pod->viewer == NULL)
    {
      pod->viewer = PSPPIRE_OUTPUT_WINDOW (psppire_output_window_new ());
      gtk_widget_show_all (GTK_WIDGET (pod->viewer));
      pod->viewer->driver = pod;
    }
  viewer = pod->viewer;

  if (viewer->n_items >= viewer->allocated_items)
    viewer->items = x2nrealloc (viewer->items, &viewer->allocated_items,
                                sizeof *viewer->items);
  viewer->items[viewer->n_items++] = output_item_ref (item);

  if (is_text_item (item))
    {
      const struct text_item *text_item = to_text_item (item);
      enum text_item_type type = text_item_get_type (text_item);
      const char *text = text_item_get_text (text_item);

      if (type == TEXT_ITEM_COMMAND_CLOSE)
        {
          viewer->in_command = false;
          return;
        }
      else if (text[0] == '\0')
        return;
    }

  cr = gdk_cairo_create (GTK_WIDGET (pod->viewer)->window);
  if (pod->xr == NULL)
    pod->xr = xr_create_driver (cr);

  r = xr_rendering_create (pod->xr, item, cr);
  if (r == NULL)
    goto done;

  xr_rendering_measure (r, &tw, &th);

  drawing_area = gtk_drawing_area_new ();
  gtk_widget_modify_bg (
    GTK_WIDGET (drawing_area), GTK_STATE_NORMAL,
    &gtk_widget_get_style (drawing_area)->base[GTK_STATE_NORMAL]);
  g_object_set_data (G_OBJECT (drawing_area), "rendering", r);
  gtk_widget_set_size_request (drawing_area, tw, th);
  gtk_layout_put (pod->viewer->output, drawing_area, 0, pod->viewer->y);
  gtk_widget_show (drawing_area);
  g_signal_connect (G_OBJECT (drawing_area), "expose_event",
                     G_CALLBACK (expose_event_callback), NULL);

  if (!is_text_item (item)
      || text_item_get_type (to_text_item (item)) != TEXT_ITEM_SYNTAX
      || !viewer->in_command)
    {
      store = GTK_TREE_STORE (gtk_tree_view_get_model (viewer->overview));

      ds_init_empty (&title);
      if (is_text_item (item)
          && text_item_get_type (to_text_item (item)) == TEXT_ITEM_COMMAND_OPEN)
        {
          gtk_tree_store_append (store, &iter, NULL);
          viewer->cur_command = iter; /* XXX shouldn't save a GtkTreeIter */
          viewer->in_command = true;
        }
      else
        {
          GtkTreeIter *p = viewer->in_command ? &viewer->cur_command : NULL;
          gtk_tree_store_append (store, &iter, p);
        }

      ds_clear (&title);
      if (is_text_item (item))
        ds_put_cstr (&title, text_item_get_text (to_text_item (item)));
      else if (is_table_item (item))
        {
          const char *caption = table_item_get_caption (to_table_item (item));
          if (caption != NULL)
            ds_put_format (&title, "Table: %s", caption);
          else
            ds_put_cstr (&title, "Table");
        }
      else if (is_chart_item (item))
        {
          const char *s = chart_item_get_title (to_chart_item (item));
          if (s != NULL)
            ds_put_format (&title, "Chart: %s", s);
          else
            ds_put_cstr (&title, "Chart");
        }
      gtk_tree_store_set (store, &iter,
                          COL_TITLE, ds_cstr (&title),
                          COL_Y, viewer->y,
                          -1);
      ds_destroy (&title);

      path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &iter);
      gtk_tree_view_expand_row (viewer->overview, path, TRUE);
      gtk_tree_path_free (path);
    }

  if (pod->viewer->max_width < tw)
    pod->viewer->max_width = tw;
  pod->viewer->y += th;

  gtk_layout_set_size (pod->viewer->output,
                       pod->viewer->max_width, pod->viewer->y);

  gtk_window_set_urgency_hint (GTK_WINDOW (pod->viewer), TRUE);

done:
  cairo_destroy (cr);
}

static struct output_driver_class psppire_output_class =
  {
    "PSPPIRE",                  /* name */
    NULL,                       /* create */
    NULL,                       /* destroy */
    psppire_output_submit,      /* submit */
    NULL,                       /* flush */
  };

void
psppire_output_window_setup (void)
{
  struct psppire_output_driver *pod;
  struct output_driver *d;

  pod = xzalloc (sizeof *pod);
  d = &pod->driver;
  output_driver_init (d, &psppire_output_class, "PSPPIRE", 0);
  output_driver_register (d);
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

  ow->driver->viewer = NULL;

  return FALSE;
}



static void
cancel_urgency (GtkWindow *window,  gpointer data)
{
  gtk_window_set_urgency_hint (window, FALSE);
}

static void
on_row_activate (GtkTreeView *overview,
                 GtkTreePath *path,
                 GtkTreeViewColumn *column,
                 PsppireOutputWindow *window)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkAdjustment *vadj;
  GValue value = {0};
  double y, min, max;

  model = gtk_tree_view_get_model (overview);
  if (!gtk_tree_model_get_iter (model, &iter, path))
    return;

  gtk_tree_model_get_value (model, &iter, COL_Y, &value);
  y = g_value_get_long (&value);
  g_value_unset (&value);

  vadj = gtk_layout_get_vadjustment (window->output);
  min = vadj->lower;
  max = vadj->upper - vadj->page_size;
  if (y < min)
    y = min;
  else if (y > max)
    y = max;
  gtk_adjustment_set_value (vadj, y);
}

static GtkFileFilter *
add_filter (GtkFileChooser *chooser, const char *name, const char *pattern)
{
  GtkFileFilter *filter = gtk_file_filter_new ();
  g_object_ref_sink (G_OBJECT (filter));
  gtk_file_filter_set_name (filter, name);
  gtk_file_filter_add_pattern (filter, pattern);
  gtk_file_chooser_add_filter (chooser, filter);
  return filter;
}

static void
export_output (PsppireOutputWindow *window, struct string_map *options,
               const char *class_name)
{
  struct output_driver *driver;
  size_t i;

  driver = output_driver_create (class_name, options);
  if (driver == NULL)
    return;

  for (i = 0; i < window->n_items; i++)
    driver->class->submit (driver, window->items[i]);
  output_driver_destroy (driver);
}

static void
psppire_output_window_export (PsppireOutputWindow *window)
{
  gint response;

  GtkFileFilter *pdf_filter;
  GtkFileFilter *html_filter;
  GtkFileFilter *odt_filter;
  GtkFileFilter *txt_filter;
  GtkFileFilter *ps_filter;
  GtkFileFilter *csv_filter;
  GtkFileChooser *chooser;
  GtkWidget *dialog;

  dialog = gtk_file_chooser_dialog_new (_("Export Output"),
                                        GTK_WINDOW (window),
                                        GTK_FILE_CHOOSER_ACTION_SAVE,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_SAVE,   GTK_RESPONSE_ACCEPT,
                                        NULL);
  chooser = GTK_FILE_CHOOSER (dialog);

  pdf_filter = add_filter (chooser, _("PDF Files (*.pdf)"), "*.pdf");
  html_filter = add_filter (chooser, _("HTML Files (*.html)"), "*.html");
  odt_filter = add_filter (chooser, _("OpenDocument Files (*.odt)"), "*.odt");
  txt_filter = add_filter (chooser, _("Text Files (*.txt)"), "*.txt");
  ps_filter = add_filter (chooser, _("PostScript Files (*.ps)"), "*.ps");
  csv_filter = add_filter (chooser, _("Comma-Separated Value Files (*.csv)"),
                           "*.csv");

  gtk_file_chooser_set_do_overwrite_confirmation (chooser, TRUE);
  gtk_file_chooser_set_filter (chooser, pdf_filter);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if ( response == GTK_RESPONSE_ACCEPT )
    {
      char *filename = gtk_file_chooser_get_filename (chooser);
      GtkFileFilter *filter = gtk_file_chooser_get_filter (chooser);
      struct string_map options;

      g_return_if_fail (filename);
      g_return_if_fail (filter);

      string_map_init (&options);
      string_map_insert (&options, "output-file", filename);
      if (filter == pdf_filter)
        {
          string_map_insert (&options, "output-type", "pdf");
          export_output (window, &options, "cairo");
        }
      else if (filter == html_filter)
        export_output (window, &options, "html");
      else if (filter == odt_filter)
        export_output (window, &options, "odf");
      else if (filter == txt_filter)
        {
          string_map_insert (&options, "headers", "false");
          string_map_insert (&options, "paginate", "false");
          string_map_insert (&options, "squeeze", "true");
          string_map_insert (&options, "emphasis", "none");
          string_map_insert (&options, "chart-type", "none");
          string_map_insert (&options, "top-margin", "0");
          string_map_insert (&options, "bottom-margin", "0");
          export_output (window, &options, "ascii");
        }
      else if (filter == ps_filter)
        {
          string_map_insert (&options, "output-type", "ps");
          export_output (window, &options, "cairo");
        }
      else if (filter == csv_filter)
        export_output (window, &options, "csv");
      else
        g_return_if_reached ();

      free (filename);
    }

  g_object_unref (G_OBJECT (pdf_filter));
  g_object_unref (G_OBJECT (html_filter));
  g_object_unref (G_OBJECT (txt_filter));
  g_object_unref (G_OBJECT (ps_filter));
  g_object_unref (G_OBJECT (csv_filter));

  gtk_widget_destroy (dialog);
}

static void
psppire_output_window_init (PsppireOutputWindow *window)
{
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkBuilder *xml;

  xml = builder_new ("output-viewer.ui");

  gtk_widget_reparent (get_widget_assert (xml, "vbox1"), GTK_WIDGET (window));

  window->output = GTK_LAYOUT (get_widget_assert (xml, "output"));
  window->y = 0;

  window->overview = GTK_TREE_VIEW (get_widget_assert (xml, "overview"));
  gtk_tree_view_set_model (window->overview,
                           GTK_TREE_MODEL (gtk_tree_store_new (
                                             N_COLS,
                                             G_TYPE_STRING, /* COL_TITLE */
                                             G_TYPE_LONG))); /* COL_Y */

  window->in_command = false;

  window->items = NULL;
  window->n_items = window->allocated_items = 0;

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (window->overview), column);
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, TRUE);
  gtk_tree_view_column_add_attribute (column, renderer, "text", COL_TITLE);

  g_signal_connect (GTK_TREE_VIEW (window->overview),
                    "row-activated", G_CALLBACK (on_row_activate), window);

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

  g_signal_connect_swapped (get_action_assert (xml, "file_export"), "activate",
                            G_CALLBACK (psppire_output_window_export), window);

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
