/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011  Free Software Foundation

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
#include "output/output-item.h"
#include "output/tab.h"
#include "output/table-item.h"
#include "output/text-item.h"
#include "ui/gui/help-menu.h"
#include "ui/gui/helper.h"
#include "ui/gui/psppire-output-window.h"

#include "gl/error.h"
#include "gl/tmpdir.h"
#include "gl/xalloc.h"

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
    int font_height;
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
  xr_rendering_draw (r, cr, event->area.x, event->area.y,
                     event->area.width, event->area.height);
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
    {
      const GtkStyle *style = gtk_widget_get_style (GTK_WIDGET (viewer));
      struct string_map options = STRING_MAP_INITIALIZER (options);
      struct text_item *text_item;
      PangoFontDescription *font_desc;
      char *font_name;
      int font_width;

      /* Use GTK+ default font as proportional font. */
      font_name = pango_font_description_to_string (style->font_desc);
      string_map_insert (&options, "prop-font", font_name);
      g_free (font_name);

      /* Derived emphasized font from proportional font. */
      font_desc = pango_font_description_copy (style->font_desc);
      pango_font_description_set_style (font_desc, PANGO_STYLE_ITALIC);
      font_name = pango_font_description_to_string (font_desc);
      string_map_insert (&options, "emph-font", font_name);
      g_free (font_name);
      pango_font_description_free (font_desc);

      /* Pretend that the "page" has a reasonable width and a very big length,
         so that most tables can be conveniently viewed on-screen with vertical
         scrolling only.  (The length should not be increased very much because
         it is already close enough to INT_MAX when expressed as thousands of a
         point.) */
      string_map_insert (&options, "paper-size", "300x200000mm");
      string_map_insert (&options, "left-margin", "0");
      string_map_insert (&options, "right-margin", "0");
      string_map_insert (&options, "top-margin", "0");
      string_map_insert (&options, "bottom-margin", "0");

      pod->xr = xr_driver_create (cr, &options);

      string_map_destroy (&options);

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

#define N_EXTENTIONS (n_FT - 1)

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

      for (i = 1 ; i < N_EXTENTIONS ; ++i)
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
  
  for (i = 0 ; i < 6 ; ++i)
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
      int file_type = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));
      char *filename = gtk_file_chooser_get_filename (chooser);
      struct string_map options;

      g_return_if_fail (filename);

      if (file_type == FT_AUTO)
	{
	  gint i;
	  for (i = 1 ; i < N_EXTENTIONS ; ++i)
	    {
	      if ( g_str_has_suffix (filename, ft[i].ext))
		{
		  file_type = i;
		  break;
		}
	    }
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


static void
insert_glyph (struct string_map *map, const char *opt, gunichar glyph)
{
  char s[6] = {0,0,0,0,0,0};

  g_unichar_to_utf8 (glyph, s);
  string_map_insert (map, opt, s);
}

struct glyph_pair
{
  gunichar glyph;
  char opt[10];
};

/* See the table at 
   http://en.wikipedia.org/wiki/Box-drawing_characters */
struct glyph_pair table[] = {
  {0x2500, "box[1010]"},
  {0x2501, "box[2020]"},
  {0x2502, "box[0101]"},
  {0x2503, "box[0202]"},

  {0x250C, "box[1100]"},
  {0x250D, "box[2100]"},
  {0x250E, "box[1200]"},
  {0x250F, "box[2200]"},
  {0x2510, "box[0110]"},
  {0x2511, "box[0110]"},
  {0x2512, "box[0210]"},
  {0x2513, "box[0220]"},
  {0x2514, "box[1001]"},
  {0x2515, "box[2001]"},
  {0x2516, "box[1002]"},
  {0x2517, "box[2002]"},
  {0x2518, "box[0011]"},
  {0x2519, "box[0021]"},
  {0x251A, "box[0012]"},
  {0x251B, "box[0022]"},
  {0x251C, "box[1101]"},
  {0x251D, "box[2101]"},
  {0x251E, "box[1102]"},
  {0x251F, "box[1201]"},
  {0x2520, "box[1202]"},
  {0x2521, "box[2102]"},
  {0x2522, "box[2201]"},
  {0x2523, "box[2202]"},
  {0x2524, "box[0111]"},
  {0x2525, "box[0121]"},
  {0x2526, "box[0112]"},
  {0x2527, "box[0211]"},
  {0x2528, "box[0212]"},
  {0x2529, "box[0122]"},
  {0x252A, "box[0221]"},
  {0x252B, "box[0222]"},
  {0x252C, "box[1110]"},
  {0x252D, "box[1120]"},
  {0x252E, "box[2110]"},
  {0x252F, "box[2120]"},
  {0x2530, "box[1210]"},
  {0x2531, "box[1220]"},
  {0x2532, "box[2210]"},
  {0x2533, "box[2220]"},
  {0x2534, "box[1011]"},
  {0x2535, "box[1021]"},
  {0x2536, "box[2011]"},
  {0x2537, "box[2021]"},
  {0x2538, "box[1012]"},
  {0x2539, "box[1022]"},
  {0x253A, "box[2012]"},
  {0x253B, "box[2022]"},
  {0x253C, "box[1111]"},
  {0x253D, "box[1121]"},
  {0x253E, "box[2111]"},
  {0x253F, "box[2121]"},
  {0x2540, "box[1112]"},
  {0x2541, "box[1211]"},
  {0x2542, "box[1212]"},
  {0x2543, "box[1122]"},
  {0x2544, "box[2112]"},
  {0x2545, "box[1221]"},
  {0x2546, "box[2211]"},
  {0x2547, "box[2122]"},
  {0x2548, "box[2221]"},
  {0x2549, "box[1222]"},
  {0x254A, "box[2212]"},
  {0x254B, "box[2222]"},

  {0x2574, "box[0010]"},
  {0x2575, "box[0001]"},
  {0x2576, "box[1000]"},
  {0x2577, "box[0100]"},
  {0x2578, "box[0020]"},
  {0x2579, "box[0002]"},
  {0x257A, "box[2000]"},
  {0x257B, "box[0200]"},
  {0x257C, "box[2010]"},
  {0x257D, "box[0201]"},
  {0x257E, "box[1020]"},
  {0x257F, "box[0102]"},
};


static void
utf8_box_chars (struct string_map *map)
{
  int i;
  for (i = 0; i < sizeof (table) / sizeof (table[0]); ++i)
    {
      const struct glyph_pair *p = &table[i];
      insert_glyph (map, p->opt, p->glyph);
    }
}



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
      utf8_box_chars (&options);
      /* fall-through */

    case SELECT_FMT_TEXT:
      string_map_insert (&options, "format", "txt");
      break;

    case SELECT_FMT_HTML:
      string_map_insert (&options, "format", "html");
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
      gtk_selection_data_set (selection_data, selection_data->target,
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
psppire_output_window_init (PsppireOutputWindow *window)
{
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkBuilder *xml;
  GtkAction *copy_action;
  GtkAction *select_all_action;
  GtkTreeSelection *sel;

  xml = builder_new ("output-viewer.ui");

  copy_action = get_action_assert (xml, "edit_copy");
  select_all_action = get_action_assert (xml, "edit_select-all");

  gtk_action_set_sensitive (copy_action, FALSE);

  g_signal_connect_swapped (copy_action, "activate", G_CALLBACK (on_copy), window);

  g_signal_connect_swapped (select_all_action, "activate", G_CALLBACK (on_select_all), window);

  gtk_widget_reparent (get_widget_assert (xml, "vbox1"), GTK_WIDGET (window));

  window->output = GTK_LAYOUT (get_widget_assert (xml, "output"));
  window->y = 0;

  window->overview = GTK_TREE_VIEW (get_widget_assert (xml, "overview"));

  sel = gtk_tree_view_get_selection (window->overview);

  gtk_tree_selection_set_mode (sel, GTK_SELECTION_MULTIPLE);

  g_signal_connect (sel, "changed", G_CALLBACK (on_selection_change), copy_action);

  gtk_tree_view_set_model (window->overview,
                           GTK_TREE_MODEL (gtk_tree_store_new (
                                             N_COLS,
                                             G_TYPE_STRING,  /* COL_TITLE */
					     G_TYPE_POINTER, /* COL_ADDR */
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

  g_signal_connect (get_action_assert (xml,"windows_minimise-all"),
		    "activate",
		    G_CALLBACK (psppire_window_minimise_all),
		    NULL);

  {
    GtkUIManager *uim = GTK_UI_MANAGER (get_object_assert (xml, "uimanager1", GTK_TYPE_UI_MANAGER));
    merge_help_menu (uim);

    PSPPIRE_WINDOW (window)->menu =
      GTK_MENU_SHELL (gtk_ui_manager_get_widget (uim,"/ui/menubar/windows_menuitem/windows_minimise-all")->parent);
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
                            xasprintf("%.2fx%.2fmm", width, height));
  string_map_insert_nocopy (&options, xstrdup ("left-margin"),
                            xasprintf ("%.2fmm", left_margin));
  string_map_insert_nocopy (&options, xstrdup ("right-margin"),
                            xasprintf ("%.2fmm", right_margin));
  string_map_insert_nocopy (&options, xstrdup ("top-margin"),
                            xasprintf ("%.2fmm", top_margin));
  string_map_insert_nocopy (&options, xstrdup ("bottom-margin"),
                            xasprintf ("%.2fmm", bottom_margin));

  window->print_xrd =
    xr_driver_create (gtk_print_context_get_cairo_context (context), &options);

  string_map_destroy (&options);
}

static gboolean
paginate (GtkPrintOperation *operation,
	  GtkPrintContext   *context,
	  PsppireOutputWindow *window)
{
  if ( window->print_item < window->n_items )
    {
      xr_driver_output_item (window->print_xrd, window->items[window->print_item++]);
      if (xr_driver_need_new_page (window->print_xrd))
	{
	  xr_driver_next_page (window->print_xrd, NULL);
	  window->print_n_pages ++;
	}
      return FALSE;
    }
  else
    {
      gtk_print_operation_set_n_pages (operation, window->print_n_pages);
      window->print_item = 0;
      create_xr_print_driver (context, window);
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
  xr_driver_next_page (window->print_xrd, gtk_print_context_get_cairo_context (context));
  while ( window->print_item < window->n_items)
    {
      xr_driver_output_item (window->print_xrd, window->items [window->print_item++]);
      if ( xr_driver_need_new_page (window->print_xrd) )
	  break;	  
    }
}


static void
psppire_output_window_print (PsppireOutputWindow *window)
{
  GtkPrintOperationResult res;

  GtkPrintOperation *print = gtk_print_operation_new ();

  if (window->print_settings != NULL) 
    gtk_print_operation_set_print_settings (print, window->print_settings);

  g_signal_connect (print, "begin_print", G_CALLBACK (begin_print), window);
  g_signal_connect (print, "end_print", G_CALLBACK (end_print),     window);
  g_signal_connect (print, "paginate", G_CALLBACK (paginate),       window);
  g_signal_connect (print, "draw_page", G_CALLBACK (draw_page),     window);

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
