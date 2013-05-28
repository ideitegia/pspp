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

#include <errno.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/string-map.h"
#include "output/cairo.h"
#include "output/chart-item.h"
#include "output/driver-provider.h"
#include "output/message-item.h"
#include "output/output-item.h"
#include "output/tab.h"
#include "output/table-item.h"
#include "output/text-item.h"
#include "ui/gui/help-menu.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/psppire-output-window.h"

#include "gl/error.h"
#include "gl/tmpdir.h"
#include "gl/xalloc.h"
#include "gl/c-xvasprintf.h"

#include "helper.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum
  {
    COL_TITLE,                  /* Table title. */
    COL_ADDR,                   /* Pointer to the table */
    COL_Y,                      /* Y position of top of title. */
    N_COLS
  };

static void psppire_output_window_class_init    (PsppireOutputWindowClass *class);
static void psppire_output_window_init          (PsppireOutputWindow      *window);

static void psppire_output_window_style_set (GtkWidget *window, GtkStyle *prev);


GType
psppire_output_window_get_type (void)
{
  static GType psppire_output_window_type = 0;

  if (!psppire_output_window_type)
    {
      static const GTypeInfo psppire_output_window_info =
      {
	sizeof (PsppireOutputWindowClass),
	(GBaseInitFunc) NULL,
        (GBaseFinalizeFunc) NULL,
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
  string_map_destroy (&PSPPIRE_OUTPUT_WINDOW(object)->render_opts);


  if (G_OBJECT_CLASS (parent_class)->finalize)
    (*G_OBJECT_CLASS (parent_class)->finalize) (object);
}


static void
psppire_output_window_dispose (GObject *obj)
{
  PsppireOutputWindow *viewer = PSPPIRE_OUTPUT_WINDOW (obj);
  size_t i;

  if (viewer->dispose_has_run) 
    return;

  viewer->dispose_has_run = TRUE;
  for (i = 0; i < viewer->n_items; i++)
    output_item_unref (viewer->items[i]);
  free (viewer->items);
  viewer->items = NULL;
  viewer->n_items = viewer->allocated_items = 0;

  if (viewer->print_settings != NULL)
    g_object_unref (viewer->print_settings);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
psppire_output_window_class_init (PsppireOutputWindowClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);
  object_class->dispose = psppire_output_window_dispose;
  
  GTK_WIDGET_CLASS (object_class)->style_set = psppire_output_window_style_set;
  object_class->finalize = psppire_output_window_finalize;
}



/* Output driver class. */

struct psppire_output_driver
  {
    struct output_driver driver;
    PsppireOutputWindow *viewer;
    struct xr_driver *xr;
    int font_height;
  };

static struct output_driver_class psppire_output_class;

static struct psppire_output_driver *
psppire_output_cast (struct output_driver *driver)
{
  assert (driver->class == &psppire_output_class);
  return UP_CAST (driver, struct psppire_output_driver, driver);
}

static void on_dwgarea_realize (GtkWidget *widget, gpointer data);

static gboolean
draw_callback (GtkWidget *widget, cairo_t *cr, gpointer data)
{
  PsppireOutputWindow *viewer = PSPPIRE_OUTPUT_WINDOW (data);
  struct xr_rendering *r = g_object_get_data (G_OBJECT (widget), "rendering");
  const GtkStyle *style = gtk_widget_get_style (GTK_WIDGET (viewer));

  PangoFontDescription *font_desc;
  char *font_name;
  
  gchar *fgc =
    gdk_color_to_string (&style->text[gtk_widget_get_state (GTK_WIDGET (widget))]);

  string_map_replace (&viewer->render_opts, "foreground-color", fgc);

  free (fgc);

  /* Use GTK+ default font as proportional font. */
  font_name = pango_font_description_to_string (style->font_desc);
  string_map_replace (&viewer->render_opts, "prop-font", font_name);
  g_free (font_name);

  /* Derived emphasized font from proportional font. */
  font_desc = pango_font_description_copy (style->font_desc);
  pango_font_description_set_style (font_desc, PANGO_STYLE_ITALIC);
  font_name = pango_font_description_to_string (font_desc);
  string_map_replace (&viewer->render_opts, "emph-font", font_name);
  g_free (font_name);
  pango_font_description_free (font_desc);

  xr_rendering_apply_options (r, &viewer->render_opts);
  xr_rendering_draw_all (r, cr);

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

  cr = gdk_cairo_create (gtk_widget_get_window (GTK_WIDGET (pod->viewer)));
  if (pod->xr == NULL)
    {
      const GtkStyle *style = gtk_widget_get_style (GTK_WIDGET (viewer));
      struct text_item *text_item;
      PangoFontDescription *font_desc;
      char *font_name;
      int font_width;
      
      /* Set the widget's text color as the foreground color for the output driver */
      gchar *fgc = gdk_color_to_string (&style->text[gtk_widget_get_state (GTK_WIDGET (viewer))]);

      string_map_insert (&pod->viewer->render_opts, "foreground-color", fgc);
      g_free (fgc);

      /* Use GTK+ default font as proportional font. */
      font_name = pango_font_description_to_string (style->font_desc);
      string_map_insert (&pod->viewer->render_opts, "prop-font", font_name);
      g_free (font_name);

      /* Derived emphasized font from proportional font. */
      font_desc = pango_font_description_copy (style->font_desc);
      pango_font_description_set_style (font_desc, PANGO_STYLE_ITALIC);
      font_name = pango_font_description_to_string (font_desc);
      string_map_insert (&pod->viewer->render_opts, "emph-font", font_name);
      g_free (font_name);
      pango_font_description_free (font_desc);

      /* Pretend that the "page" has a reasonable width and a very big length,
         so that most tables can be conveniently viewed on-screen with vertical
         scrolling only.  (The length should not be increased very much because
         it is already close enough to INT_MAX when expressed as thousands of a
         point.) */
      string_map_insert (&pod->viewer->render_opts, "paper-size", "300x200000mm");
      string_map_insert (&pod->viewer->render_opts, "left-margin", "0");
      string_map_insert (&pod->viewer->render_opts, "right-margin", "0");
      string_map_insert (&pod->viewer->render_opts, "top-margin", "0");
      string_map_insert (&pod->viewer->render_opts, "bottom-margin", "0");

      pod->xr = xr_driver_create (cr, &pod->viewer->render_opts);


      text_item = text_item_create (TEXT_ITEM_PARAGRAPH, "X");
      r = xr_rendering_create (pod->xr, text_item_super (text_item), cr);
      xr_rendering_measure (r, &font_width, &pod->font_height);
      /* xr_rendering_destroy (r); */
      text_item_unref (text_item);
    }
  else
    pod->viewer->y += pod->font_height / 2;

  r = xr_rendering_create (pod->xr, item, cr);
  if (r == NULL)
    goto done;

  xr_rendering_measure (r, &tw, &th);

  drawing_area = gtk_drawing_area_new ();

  g_object_set_data (G_OBJECT (drawing_area), "rendering", r);
  g_signal_connect (drawing_area, "realize",
                     G_CALLBACK (on_dwgarea_realize), pod->viewer);

  g_signal_connect (drawing_area, "draw",
                     G_CALLBACK (draw_callback), pod->viewer);

  gtk_widget_set_size_request (drawing_area, tw, th);
  gtk_layout_put (pod->viewer->output, drawing_area, 0, pod->viewer->y);

  gtk_widget_show (drawing_area);

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
      else if (is_message_item (item))
        {
          const struct message_item *msg_item = to_message_item (item);
          const struct msg *msg = message_item_get_msg (msg_item);
          ds_put_format (&title, "%s: %s", _("Message"),
                         msg_severity_to_string (msg->severity));
        }
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
			  COL_ADDR, item, 
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
  output_driver_init (d, &psppire_output_class, "PSPPIRE",
                      SETTINGS_DEVICE_UNFILTERED);
  output_driver_register (d);
}



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
  min = gtk_adjustment_get_lower (vadj);
  max = gtk_adjustment_get_upper (vadj) - gtk_adjustment_get_page_size (vadj);
  if (y < min)
    y = min;
  else if (y > max)
    y = max;
  gtk_adjustment_set_value (vadj, y);
}

static void psppire_output_window_print (PsppireOutputWindow *window);


static void
export_output (PsppireOutputWindow *window, struct string_map *options,
               const char *format)
{
  struct output_driver *driver;
  size_t i;

  string_map_insert (options, "format", format);
  driver = output_driver_create (options);
  if (driver == NULL)
    return;

  for (i = 0; i < window->n_items; i++)
    driver->class->submit (driver, window->items[i]);
  output_driver_destroy (driver);
}


struct file_types
{
  const gchar *label;
  const gchar *ext;
};

enum 
  {
    FT_AUTO = 0,
    FT_PDF,
    FT_HTML,
    FT_ODT,
    FT_TXT,
    FT_PS,
    FT_CSV,
    n_FT
  };

#define N_EXTENSIONS (n_FT - 1)

struct file_types ft[n_FT] = {
  {N_("Infer file type from extension"),  NULL},
  {N_("PDF (*.pdf)"),                     ".pdf"},
  {N_("HTML (*.html)"),                   ".html"},
  {N_("OpenDocument (*.odt)"),            ".odt"},
  {N_("Text (*.txt)"),                    ".txt"},
  {N_("PostScript (*.ps)"),               ".ps"},
  {N_("Comma-Separated Values (*.csv)"),  ".csv"}
};


static void
on_combo_change (GtkFileChooser *chooser)
{
  gboolean sensitive = FALSE;
  GtkWidget *combo = gtk_file_chooser_get_extra_widget (chooser);

  int x = 0; 
  gchar *fn = gtk_file_chooser_get_filename (chooser);

  if (combo &&  gtk_widget_get_realized (combo))
    x = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));

  if (fn == NULL)
    {
      sensitive = FALSE;
    }
  else
    {
      gint i;
      if ( x != 0 )
	sensitive = TRUE;

      for (i = 1 ; i < N_EXTENSIONS ; ++i)
	{
	  if ( g_str_has_suffix (fn, ft[i].ext))
	    {
	      sensitive = TRUE;
	      break;
	    }
	}
    }

  g_free (fn);

  gtk_dialog_set_response_sensitive (GTK_DIALOG (chooser), GTK_RESPONSE_ACCEPT, sensitive);
}


static void
on_file_chooser_change (GObject *w, GParamSpec *pspec, gpointer data)
{

  GtkFileChooser *chooser = data;
  const gchar *name = g_param_spec_get_name (pspec);

  if ( ! gtk_widget_get_realized (GTK_WIDGET (chooser)))
    return;

  /* Ignore this one.  It causes recursion. */
  if ( 0 == strcmp ("tooltip-text", name))
    return;

  on_combo_change (chooser);
}


/* Recursively descend all the children of W, connecting
   to their "notify" signal */
static void
iterate_widgets (GtkWidget *w, gpointer data)
{
  if ( GTK_IS_CONTAINER (w))
    gtk_container_forall (GTK_CONTAINER (w), iterate_widgets, data);
  else
    g_signal_connect (w, "notify",  G_CALLBACK (on_file_chooser_change), data);
}



static GtkListStore *
create_file_type_list (void)
{
  int i;
  GtkTreeIter iter;
  GtkListStore *list = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
  
  for (i = 0 ; i < n_FT ; ++i)
    {
      gtk_list_store_append (list, &iter);
      gtk_list_store_set (list, &iter,
			  0,  gettext (ft[i].label),
			  1,  ft[i].ext,
			  -1);
    }
  
  return list;
}

static void
psppire_output_window_export (PsppireOutputWindow *window)
{
  gint response;
  GtkWidget *combo;
  GtkListStore *list;

  GtkFileChooser *chooser;
  
  GtkWidget *dialog = gtk_file_chooser_dialog_new (_("Export Output"),
                                        GTK_WINDOW (window),
                                        GTK_FILE_CHOOSER_ACTION_SAVE,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_SAVE,   GTK_RESPONSE_ACCEPT,
                                        NULL);

  g_object_set (dialog, "local-only", FALSE, NULL);

  chooser = GTK_FILE_CHOOSER (dialog);

  list = create_file_type_list ();

  combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (list));


  {
    /* Create text cell renderer */
    GtkCellRenderer *cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), cell, FALSE );

    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (combo), cell,  "text", 0);
  }

  g_signal_connect_swapped (combo, "changed", G_CALLBACK (on_combo_change), chooser);

  gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);

  gtk_file_chooser_set_extra_widget (chooser, combo);

  /* This kludge is necessary because there is no signal to tell us
     when the candidate filename of a GtkFileChooser has changed */
  gtk_container_forall (GTK_CONTAINER (dialog), iterate_widgets, dialog);


  gtk_file_chooser_set_do_overwrite_confirmation (chooser, TRUE);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if ( response == GTK_RESPONSE_ACCEPT )
    {
      gint file_type = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));
      gchar *filename = gtk_file_chooser_get_filename (chooser);
      struct string_map options;

      g_return_if_fail (filename);

      if (file_type == FT_AUTO)
	{
          /* If the "Infer file type from extension" option was chosen,
             search for the respective type in the list.
             (It's a O(n) search, but fortunately n is small). */
	  gint i;
	  for (i = 1 ; i < N_EXTENSIONS ; ++i)
	    {
	      if ( g_str_has_suffix (filename, ft[i].ext))
		{
		  file_type = i;
		  break;
		}
	    }
	}
      else if (! g_str_has_suffix (filename, ft[file_type].ext))
        {
          /* If an explicit document format was chosen, and if the chosen
             filename does not already have that particular "extension",
             then append it.
           */

          gchar *of = filename;
          filename = g_strconcat (filename, ft[file_type].ext, NULL);
          g_free (of);
        }
      
      string_map_init (&options);
      string_map_insert (&options, "output-file", filename);

      switch (file_type)
	{
	case FT_PDF:
          export_output (window, &options, "pdf");
	  break;
	case FT_HTML:
          export_output (window, &options, "html");
	  break;
	case FT_ODT:
          export_output (window, &options, "odt");
	  break;
	case FT_PS:
          export_output (window, &options, "ps");
	  break;
	case FT_CSV:
          export_output (window, &options, "csv");
	  break;

	case FT_TXT:
          string_map_insert (&options, "headers", "false");
          string_map_insert (&options, "paginate", "false");
          string_map_insert (&options, "squeeze", "true");
          string_map_insert (&options, "emphasis", "none");
          string_map_insert (&options, "charts", "none");
          string_map_insert (&options, "top-margin", "0");
          string_map_insert (&options, "bottom-margin", "0");
          export_output (window, &options, "txt");
	  break;
	default:
	  g_assert_not_reached ();
	}

      string_map_destroy (&options);

      free (filename);
    }

  gtk_widget_destroy (dialog);
}


enum {
  SELECT_FMT_NULL,
  SELECT_FMT_TEXT,
  SELECT_FMT_UTF8,
  SELECT_FMT_HTML,
  SELECT_FMT_ODT
};

/* GNU Hurd doesn't have PATH_MAX.  Use a fallback.
   Temporary directory names are usually not that long.  */
#ifndef PATH_MAX
# define PATH_MAX 1024
#endif

static void
clipboard_get_cb (GtkClipboard     *clipboard,
		  GtkSelectionData *selection_data,
		  guint             info,
		  gpointer          data)
{
  PsppireOutputWindow *window = data;

  gsize length;
  gchar *text = NULL;
  struct output_driver *driver = NULL;
  char dirname[PATH_MAX], *filename;
  struct string_map options;

  GtkTreeSelection *sel = gtk_tree_view_get_selection (window->overview);
  GtkTreeModel *model = gtk_tree_view_get_model (window->overview);

  GList *rows = gtk_tree_selection_get_selected_rows (sel, &model);
  GList *n = rows;

  if ( n == NULL)
    return;

  if (path_search (dirname, sizeof dirname, NULL, NULL, true)
      || mkdtemp (dirname) == NULL)
    {
      error (0, errno, _("failed to create temporary directory"));
      return;
    }
  filename = xasprintf ("%s/clip.tmp", dirname);

  string_map_init (&options);
  string_map_insert (&options, "output-file", filename);

  switch (info)
    {
    case SELECT_FMT_UTF8:
      string_map_insert (&options, "box", "unicode");
      /* fall-through */

    case SELECT_FMT_TEXT:
      string_map_insert (&options, "format", "txt");
      break;

    case SELECT_FMT_HTML:
      string_map_insert (&options, "format", "html");
      string_map_insert (&options, "borders", "false");
      string_map_insert (&options, "css", "false");
      break;

    case SELECT_FMT_ODT:
      string_map_insert (&options, "format", "odt");
      break;

    default:
      g_warning ("unsupported clip target\n");
      goto finish;
      break;
    }

  driver = output_driver_create (&options);
  if (driver == NULL)
    goto finish;

  while (n)
    {
      GtkTreePath *path = n->data ; 
      GtkTreeIter iter;
      struct output_item *item ;

      gtk_tree_model_get_iter (model, &iter, path);
      gtk_tree_model_get (model, &iter, COL_ADDR, &item, -1);

      driver->class->submit (driver, item);

      n = n->next;
    }

  if ( driver->class->flush)
    driver->class->flush (driver);


  /* Some drivers (eg: the odt one) don't write anything until they
     are closed */
  output_driver_destroy (driver);
  driver = NULL;

  if ( g_file_get_contents (filename, &text, &length, NULL) )
    {
      gtk_selection_data_set (selection_data, gtk_selection_data_get_target (selection_data),
			      8,
			      (const guchar *) text, length);
    }

 finish:

  if (driver != NULL)
    output_driver_destroy (driver);

  g_free (text);

  unlink (filename);
  free (filename);
  rmdir (dirname);

  g_list_free (rows);
}

static void
clipboard_clear_cb (GtkClipboard *clipboard,
		    gpointer data)
{
}

static const GtkTargetEntry targets[] = {

  { "STRING",        0, SELECT_FMT_TEXT },
  { "TEXT",          0, SELECT_FMT_TEXT },
  { "COMPOUND_TEXT", 0, SELECT_FMT_TEXT },
  { "text/plain",    0, SELECT_FMT_TEXT },

  { "UTF8_STRING",   0, SELECT_FMT_UTF8 },
  { "text/plain;charset=utf-8", 0, SELECT_FMT_UTF8 },

  { "text/html",     0, SELECT_FMT_HTML },

  { "application/vnd.oasis.opendocument.text", 0, SELECT_FMT_ODT }
};

static void
on_copy (PsppireOutputWindow *window)
{
  {
    GtkClipboard *clipboard =
      gtk_widget_get_clipboard (GTK_WIDGET (window),
				GDK_SELECTION_CLIPBOARD);

    if (!gtk_clipboard_set_with_data (clipboard, targets,
				       G_N_ELEMENTS (targets),
				       clipboard_get_cb, clipboard_clear_cb,
				      window))

      clipboard_clear_cb (clipboard, window);
  }
}

static void
on_selection_change (GtkTreeSelection *sel, GtkAction *copy_action)
{
  /* The Copy action is available only if there is something selected */
  gtk_action_set_sensitive (copy_action, gtk_tree_selection_count_selected_rows (sel) > 0);
}

static void
on_select_all (PsppireOutputWindow *window)
{
  GtkTreeSelection *sel = gtk_tree_view_get_selection (window->overview);
  gtk_tree_view_expand_all (window->overview);
  gtk_tree_selection_select_all (sel);
}


static void
copy_base_to_bg (GtkWidget *dest, GtkWidget *src)
{
  int i;
  for (i = 0; i < 5; ++i)
    {
      GdkColor *col = &gtk_widget_get_style (src)->base[i];
      gtk_widget_modify_bg (dest, i, col);

      col = &gtk_widget_get_style (src)->text[i];
      gtk_widget_modify_fg (dest, i, col);
    }
}

static void 
on_dwgarea_realize (GtkWidget *dwg_area, gpointer data)
{
  GtkWidget *viewer = GTK_WIDGET (data);

  copy_base_to_bg (dwg_area, viewer);
}


static void
psppire_output_window_style_set (GtkWidget *w, GtkStyle *prev)
{
  GtkWidget *op = GTK_WIDGET (PSPPIRE_OUTPUT_WINDOW (w)->output);

  /* Copy the base style from the parent widget to the container and 
     all its children.
     We do this, because the container's primary purpose is to 
     display text.  This way psppire appears to follow the chosen
     gnome theme.
   */
  copy_base_to_bg (op, w);
  gtk_container_foreach (GTK_CONTAINER (op), (GtkCallback) copy_base_to_bg,
			 PSPPIRE_OUTPUT_WINDOW (w)->output);

    /* Chain up to the parent class */
  GTK_WIDGET_CLASS (parent_class)->style_set (w, prev);
}

static void
psppire_output_window_init (PsppireOutputWindow *window)
{
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkBuilder *xml;
  GtkAction *copy_action;
  GtkAction *select_all_action;
  GtkTreeSelection *sel;
  GtkTreeModel *model;

  string_map_init (&window->render_opts);

  xml = builder_new ("output-viewer.ui");

  copy_action = get_action_assert (xml, "edit_copy");
  select_all_action = get_action_assert (xml, "edit_select-all");

  gtk_action_set_sensitive (copy_action, FALSE);

  g_signal_connect_swapped (copy_action, "activate", G_CALLBACK (on_copy), window);

  g_signal_connect_swapped (select_all_action, "activate", G_CALLBACK (on_select_all), window);

  gtk_widget_reparent (get_widget_assert (xml, "vbox1"), GTK_WIDGET (window));

  window->output = GTK_LAYOUT (get_widget_assert (xml, "output"));
  window->y = 0;
  window->print_settings = NULL;
  window->dispose_has_run = FALSE;

  window->overview = GTK_TREE_VIEW (get_widget_assert (xml, "overview"));

  sel = gtk_tree_view_get_selection (window->overview);

  gtk_tree_selection_set_mode (sel, GTK_SELECTION_MULTIPLE);

  g_signal_connect (sel, "changed", G_CALLBACK (on_selection_change), copy_action);

  model = GTK_TREE_MODEL (gtk_tree_store_new (
                                             N_COLS,
                                             G_TYPE_STRING,  /* COL_TITLE */
					     G_TYPE_POINTER, /* COL_ADDR */
                                             G_TYPE_LONG));  /* COL_Y */
  gtk_tree_view_set_model (window->overview, model);
  g_object_unref (model);

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

  connect_help (xml);

  g_signal_connect (window,
		    "focus-in-event",
		    G_CALLBACK (cancel_urgency),
		    NULL);

  g_signal_connect (get_action_assert (xml,"windows_minimise-all"),
		    "activate",
		    G_CALLBACK (psppire_window_minimise_all),
		    NULL);

  {
    GtkWidget *w;
    GtkUIManager *uim = GTK_UI_MANAGER (get_object_assert (xml, "uimanager1", GTK_TYPE_UI_MANAGER));
    merge_help_menu (uim);

    w = gtk_ui_manager_get_widget (uim,"/ui/menubar/windows_menuitem/windows_minimise-all");

    PSPPIRE_WINDOW (window)->menu =
      GTK_MENU_SHELL (gtk_widget_get_parent (w));
  }

  g_signal_connect_swapped (get_action_assert (xml, "file_export"), "activate",
                            G_CALLBACK (psppire_output_window_export), window);


  g_signal_connect_swapped (get_action_assert (xml, "file_print"), "activate",
                            G_CALLBACK (psppire_output_window_print), window);

  g_object_unref (xml);

  g_signal_connect (window, "delete-event",
		    G_CALLBACK (on_delete), window);
}


GtkWidget*
psppire_output_window_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_output_window_get_type (),
				   /* TRANSLATORS: This will form a filename.  Please avoid whitespace. */
				   "filename", _("Output"),
				   "description", _("Output Viewer"),
				   NULL));
}



static cairo_t *
get_cairo_context_from_print_context (GtkPrintContext *context)
{
  cairo_t *cr = gtk_print_context_get_cairo_context (context);
  
  /*
    For all platforms except windows, gtk_print_context_get_dpi_[xy] returns 72.
    Windows returns 600.
  */
  double xres = gtk_print_context_get_dpi_x (context);
  double yres = gtk_print_context_get_dpi_y (context);
  
  /* This means that the cairo context now has its dimensions in Points */
  cairo_scale (cr, xres / 72.0, yres / 72.0);
  
  return cr;
}


static void
create_xr_print_driver (GtkPrintContext *context, PsppireOutputWindow *window)
{
  struct string_map options;
  GtkPageSetup *page_setup;
  double width, height;
  double left_margin;
  double right_margin;
  double top_margin;
  double bottom_margin;

  page_setup = gtk_print_context_get_page_setup (context);
  width = gtk_page_setup_get_paper_width (page_setup, GTK_UNIT_MM);
  height = gtk_page_setup_get_paper_height (page_setup, GTK_UNIT_MM);
  left_margin = gtk_page_setup_get_left_margin (page_setup, GTK_UNIT_MM);
  right_margin = gtk_page_setup_get_right_margin (page_setup, GTK_UNIT_MM);
  top_margin = gtk_page_setup_get_top_margin (page_setup, GTK_UNIT_MM);
  bottom_margin = gtk_page_setup_get_bottom_margin (page_setup, GTK_UNIT_MM);

  string_map_init (&options);
  string_map_insert_nocopy (&options, xstrdup ("paper-size"),
                            c_xasprintf("%.2fx%.2fmm", width, height));
  string_map_insert_nocopy (&options, xstrdup ("left-margin"),
                            c_xasprintf ("%.2fmm", left_margin));
  string_map_insert_nocopy (&options, xstrdup ("right-margin"),
                            c_xasprintf ("%.2fmm", right_margin));
  string_map_insert_nocopy (&options, xstrdup ("top-margin"),
                            c_xasprintf ("%.2fmm", top_margin));
  string_map_insert_nocopy (&options, xstrdup ("bottom-margin"),
                            c_xasprintf ("%.2fmm", bottom_margin));

  window->print_xrd = xr_driver_create (get_cairo_context_from_print_context (context), &options);

  string_map_destroy (&options);
}

static gboolean
paginate (GtkPrintOperation *operation,
	  GtkPrintContext   *context,
	  PsppireOutputWindow *window)
{
  if (window->paginated)
    {
      /* Sometimes GTK+ emits this signal again even after pagination is
         complete.  Don't let that screw up printing. */
      return TRUE;
    }
  else if ( window->print_item < window->n_items )
    {
      xr_driver_output_item (window->print_xrd, window->items[window->print_item++]);
      while (xr_driver_need_new_page (window->print_xrd))
	{
	  xr_driver_next_page (window->print_xrd, NULL);
	  window->print_n_pages ++;
	}
      return FALSE;
    }
  else
    {
      gtk_print_operation_set_n_pages (operation, window->print_n_pages);

      /* Re-create the driver to do the real printing. */
      xr_driver_destroy (window->print_xrd);
      create_xr_print_driver (context, window);
      window->print_item = 0;
      window->paginated = TRUE;

      return TRUE;
    }
}

static void
begin_print (GtkPrintOperation *operation,
	     GtkPrintContext   *context,
	     PsppireOutputWindow *window)
{
  create_xr_print_driver (context, window);

  window->print_item = 0;
  window->print_n_pages = 1;
  window->paginated = FALSE;
}

static void
end_print (GtkPrintOperation *operation,
	   GtkPrintContext   *context,
	   PsppireOutputWindow *window)
{
  xr_driver_destroy (window->print_xrd);
}


static void
draw_page (GtkPrintOperation *operation,
	   GtkPrintContext   *context,
	   gint               page_number,
	   PsppireOutputWindow *window)
{
  xr_driver_next_page (window->print_xrd, get_cairo_context_from_print_context (context));
  while (!xr_driver_need_new_page (window->print_xrd)
         && window->print_item < window->n_items)
    xr_driver_output_item (window->print_xrd, window->items [window->print_item++]);
}


static void
psppire_output_window_print (PsppireOutputWindow *window)
{
  GtkPrintOperationResult res;

  GtkPrintOperation *print = gtk_print_operation_new ();

  if (window->print_settings != NULL) 
    gtk_print_operation_set_print_settings (print, window->print_settings);

  g_signal_connect (print, "begin_print", G_CALLBACK (begin_print), window);
  g_signal_connect (print, "end_print",   G_CALLBACK (end_print),   window);
  g_signal_connect (print, "paginate",    G_CALLBACK (paginate),    window);
  g_signal_connect (print, "draw_page",   G_CALLBACK (draw_page),   window);

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
