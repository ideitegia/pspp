/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008-2014 Free Software Foundation.

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

#include "ui/gui/psppire-output-view.h"

#include <errno.h>
#include <stdbool.h>

#include "libpspp/assertion.h"
#include "libpspp/string-map.h"
#include "output/cairo.h"
#include "output/driver-provider.h"
#include "output/driver.h"
#include "output/chart-item.h"
#include "output/message-item.h"
#include "output/output-item.h"
#include "output/table-item.h"
#include "output/text-item.h"

#include "gl/c-xvasprintf.h"
#include "gl/minmax.h"
#include "gl/tmpdir.h"
#include "gl/xalloc.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)

struct output_view_item
  {
    struct output_item *item;
    GtkWidget *drawing_area;
  };

struct psppire_output_view
  {
    struct xr_driver *xr;
    int font_height;

    GtkLayout *output;
    int render_width;
    int max_width;
    int y;

    struct string_map render_opts;
    GtkTreeView *overview;
    GtkTreeIter cur_command;
    bool in_command;

    GtkWidget *toplevel;

    struct output_view_item *items;
    size_t n_items, allocated_items;

    /* Variables pertaining to printing */
    GtkPrintSettings *print_settings;
    struct xr_driver *print_xrd;
    int print_item;
    int print_n_pages;
    gboolean paginated;
  };

enum
  {
    COL_NAME,                   /* Table name. */
    COL_ADDR,                   /* Pointer to the table */
    COL_Y,                      /* Y position of top of name. */
    N_COLS
  };

static void on_dwgarea_realize (GtkWidget *widget, gpointer data);

static gboolean
expose_event_callback (GtkWidget *widget, GdkEventExpose *event, gpointer data)
{
  struct psppire_output_view *view = data;
  struct xr_rendering *r = g_object_get_data (G_OBJECT (widget), "rendering");
  cairo_t *cr = gdk_cairo_create (widget->window);

  const GtkStyle *style = gtk_widget_get_style (GTK_WIDGET (view->output));

  PangoFontDescription *font_desc;
  char *font_name;

  gchar *fgc =
    gdk_color_to_string (&style->text[gtk_widget_get_state (GTK_WIDGET (view->output))]);

  string_map_replace (&view->render_opts, "foreground-color", fgc);

  free (fgc);

  /* Use GTK+ default font as proportional font. */
  font_name = pango_font_description_to_string (style->font_desc);
  string_map_replace (&view->render_opts, "prop-font", font_name);
  g_free (font_name);

  /* Derived emphasized font from proportional font. */
  font_desc = pango_font_description_copy (style->font_desc);
  pango_font_description_set_style (font_desc, PANGO_STYLE_ITALIC);
  font_name = pango_font_description_to_string (font_desc);
  string_map_replace (&view->render_opts, "emph-font", font_name);
  g_free (font_name);
  pango_font_description_free (font_desc);

  xr_rendering_apply_options (r, &view->render_opts);

  xr_rendering_draw (r, cr, event->area.x, event->area.y,
                     event->area.width, event->area.height);
  cairo_destroy (cr);

  return TRUE;
}

static void
free_rendering (gpointer rendering_)
{
  struct xr_rendering *rendering = rendering_;
  xr_rendering_destroy (rendering);
}

static void
create_xr (struct psppire_output_view *view)
{
  const GtkStyle *style = gtk_widget_get_style (GTK_WIDGET (view->output));
  struct text_item *text_item;
  PangoFontDescription *font_desc;
  struct xr_rendering *r;
  char *font_name;
  int font_width;
  cairo_t *cr;
  gchar *fgc;

  cr = gdk_cairo_create (GTK_WIDGET (view->output)->window);

  /* Set the widget's text color as the foreground color for the output driver */
  fgc = gdk_color_to_string (&style->text[gtk_widget_get_state (GTK_WIDGET (view->output))]);

  string_map_insert (&view->render_opts, "foreground-color", fgc);
  g_free (fgc);

  /* Use GTK+ default font as proportional font. */
  font_name = pango_font_description_to_string (style->font_desc);
  string_map_insert (&view->render_opts, "prop-font", font_name);
  g_free (font_name);

  /* Derived emphasized font from proportional font. */
  font_desc = pango_font_description_copy (style->font_desc);
  pango_font_description_set_style (font_desc, PANGO_STYLE_ITALIC);
  font_name = pango_font_description_to_string (font_desc);
  string_map_insert (&view->render_opts, "emph-font", font_name);
  g_free (font_name);
  pango_font_description_free (font_desc);

  /* Pretend that the "page" has a reasonable width and a very big length,
     so that most tables can be conveniently viewed on-screen with vertical
     scrolling only.  (The length should not be increased very much because
     it is already close enough to INT_MAX when expressed as thousands of a
     point.) */
  string_map_insert_nocopy (&view->render_opts, xstrdup ("paper-size"),
                            xasprintf ("%dx1000000pt", view->render_width));
  string_map_insert (&view->render_opts, "left-margin", "0");
  string_map_insert (&view->render_opts, "right-margin", "0");
  string_map_insert (&view->render_opts, "top-margin", "0");
  string_map_insert (&view->render_opts, "bottom-margin", "0");

  view->xr = xr_driver_create (cr, &view->render_opts);

  text_item = text_item_create (TEXT_ITEM_PARAGRAPH, "X");
  r = xr_rendering_create (view->xr, text_item_super (text_item), cr);
  xr_rendering_measure (r, &font_width, &view->font_height);
  /* xr_rendering_destroy (r); */
  text_item_unref (text_item);

  cairo_destroy (cr);
}

static void
rerender (struct psppire_output_view *view)
{
  struct output_view_item *item;
  cairo_t *cr;

  if (!view->n_items || !GTK_WIDGET (view->output)->window)
    return;

  string_map_clear (&view->render_opts);
  xr_driver_destroy (view->xr);
  create_xr (view);

  cr = gdk_cairo_create (GTK_WIDGET (view->output)->window);

  view->y = 0;
  view->max_width = 0;
  for (item = view->items; item < &view->items[view->n_items]; item++)
    {
      struct xr_rendering *r;
      int tw, th;
      bool new;

      if (view->y > 0)
        view->y += view->font_height / 2;

      r = xr_rendering_create (view->xr, item->item, cr);
      if (r == NULL)
        {
          g_warn_if_reached ();
          continue;
        }

      xr_rendering_measure (r, &tw, &th);

      new = !item->drawing_area;
      if (new)
        item->drawing_area = gtk_drawing_area_new ();
      g_object_set_data_full (G_OBJECT (item->drawing_area),
                              "rendering", r, free_rendering);

      gtk_widget_set_size_request (item->drawing_area, tw, th);
      if (new)
        gtk_layout_put (view->output, item->drawing_area, 0, view->y);
      else
        gtk_layout_move (view->output, item->drawing_area, 0, view->y);

      if (view->max_width < tw)
        view->max_width = tw;
      view->y += th;
    }

  gtk_layout_set_size (view->output, view->max_width, view->y);
  cairo_destroy (cr);
}

void
psppire_output_view_put (struct psppire_output_view *view,
                         const struct output_item *item)
{
  struct output_view_item *view_item;
  GtkWidget *drawing_area;
  struct xr_rendering *r;
  struct string name;
  GtkTreeStore *store;
  cairo_t *cr = NULL;
  GtkTreePath *path;
  GtkTreeIter iter;
  int tw, th;

  if (is_text_item (item))
    {
      const struct text_item *text_item = to_text_item (item);
      enum text_item_type type = text_item_get_type (text_item);
      const char *text = text_item_get_text (text_item);

      if (type == TEXT_ITEM_COMMAND_CLOSE)
        {
          view->in_command = false;
          return;
        }
      else if (text[0] == '\0')
        return;
    }

  if (view->n_items >= view->allocated_items)
    view->items = x2nrealloc (view->items, &view->allocated_items,
                                sizeof *view->items);
  view_item = &view->items[view->n_items++];
  view_item->item = output_item_ref (item);
  view_item->drawing_area = NULL;

  if (GTK_WIDGET (view->output)->window)
    {
      view_item->drawing_area = drawing_area = gtk_drawing_area_new ();

      cr = gdk_cairo_create (GTK_WIDGET (view->output)->window);
      if (view->xr == NULL)
        create_xr (view);

      if (view->y > 0)
        view->y += view->font_height / 2;

      r = xr_rendering_create (view->xr, item, cr);
      if (r == NULL)
        goto done;

      xr_rendering_measure (r, &tw, &th);

      g_object_set_data_full (G_OBJECT (drawing_area), "rendering", r, free_rendering);
      g_signal_connect (drawing_area, "realize",
                        G_CALLBACK (on_dwgarea_realize), view);
      g_signal_connect (drawing_area, "expose_event",
                        G_CALLBACK (expose_event_callback), view);

      gtk_widget_set_size_request (drawing_area, tw, th);
      gtk_layout_put (view->output, drawing_area, 0, view->y);

      gtk_widget_show (drawing_area);
    }

  if (view->overview
      && (!is_text_item (item)
          || text_item_get_type (to_text_item (item)) != TEXT_ITEM_SYNTAX
          || !view->in_command))
    {
      store = GTK_TREE_STORE (gtk_tree_view_get_model (view->overview));

      ds_init_empty (&name);
      if (is_text_item (item)
          && text_item_get_type (to_text_item (item)) == TEXT_ITEM_COMMAND_OPEN)
        {
          gtk_tree_store_append (store, &iter, NULL);
          view->cur_command = iter; /* XXX shouldn't save a GtkTreeIter */
          view->in_command = true;
        }
      else
        {
          GtkTreeIter *p = view->in_command ? &view->cur_command : NULL;
          gtk_tree_store_append (store, &iter, p);
        }

      ds_clear (&name);
      if (is_text_item (item))
        ds_put_cstr (&name, text_item_get_text (to_text_item (item)));
      else if (is_message_item (item))
        {
          const struct message_item *msg_item = to_message_item (item);
          const struct msg *msg = message_item_get_msg (msg_item);
          ds_put_format (&name, "%s: %s", _("Message"),
                         msg_severity_to_string (msg->severity));
        }
      else if (is_table_item (item))
        {
          const char *title = table_item_get_title (to_table_item (item));
          if (title != NULL)
            ds_put_format (&name, "Table: %s", title);
          else
            ds_put_cstr (&name, "Table");
        }
      else if (is_chart_item (item))
        {
          const char *s = chart_item_get_title (to_chart_item (item));
          if (s != NULL)
            ds_put_format (&name, "Chart: %s", s);
          else
            ds_put_cstr (&name, "Chart");
        }
      gtk_tree_store_set (store, &iter,
                          COL_NAME, ds_cstr (&name),
			  COL_ADDR, item,
                          COL_Y, view->y,
                          -1);
      ds_destroy (&name);

      path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &iter);
      gtk_tree_view_expand_row (view->overview, path, TRUE);
      gtk_tree_path_free (path);
    }

  if (view->max_width < tw)
    view->max_width = tw;
  view->y += th;

  gtk_layout_set_size (view->output, view->max_width, view->y);

done:
  cairo_destroy (cr);
}

static void
on_row_activate (GtkTreeView *overview,
                 GtkTreePath *path,
                 GtkTreeViewColumn *column,
                 struct psppire_output_view *view)
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

  vadj = gtk_layout_get_vadjustment (view->output);
  min = vadj->lower;
  max = vadj->upper - vadj->page_size;
  if (y < min)
    y = min;
  else if (y > max)
    y = max;
  gtk_adjustment_set_value (vadj, y);
}

static void
copy_base_to_bg (GtkWidget *dest, GtkWidget *src)
{
  int i;
  for (i = 0; i < 5; ++i)
    {
      gtk_widget_modify_bg (dest, i, &gtk_widget_get_style (src)->base[i]);
      gtk_widget_modify_fg (dest, i, &gtk_widget_get_style (src)->text[i]);
    }
}

/* Copy the base style from the parent widget to the container and all its
   children.  We do this because the container's primary purpose is to display
   text.  This way psppire appears to follow the chosen gnome theme. */
static void
on_style_set (GtkWidget *toplevel, GtkStyle *prev,
              struct psppire_output_view *view)
{
  copy_base_to_bg (GTK_WIDGET (view->output), toplevel);
  gtk_container_foreach (GTK_CONTAINER (view->output),
                         (GtkCallback) copy_base_to_bg, view->output);
}

static void
on_dwgarea_realize (GtkWidget *dwg_area, gpointer data)
{
  copy_base_to_bg (dwg_area, gtk_widget_get_toplevel (dwg_area));
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
  struct psppire_output_view *view = data;

  gsize length;
  gchar *text = NULL;
  struct output_driver *driver = NULL;
  char dirname[PATH_MAX], *filename;
  struct string_map options;

  GtkTreeSelection *sel = gtk_tree_view_get_selection (view->overview);
  GtkTreeModel *model = gtk_tree_view_get_model (view->overview);

  GList *rows = gtk_tree_selection_get_selected_rows (sel, &model);
  GList *n = rows;

  if ( n == NULL)
    return;

  if (path_search (dirname, sizeof dirname, NULL, NULL, true)
      || mkdtemp (dirname) == NULL)
    {
      msg_error (errno, _("failed to create temporary directory during clipboard operation"));
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
on_copy (struct psppire_output_view *view)
{
  GtkWidget *widget = GTK_WIDGET (view->overview);
  GtkClipboard *cb = gtk_widget_get_clipboard (widget, GDK_SELECTION_CLIPBOARD);

  if (!gtk_clipboard_set_with_data (cb, targets, G_N_ELEMENTS (targets),
                                    clipboard_get_cb, clipboard_clear_cb,
                                    view))
    clipboard_clear_cb (cb, view);
}

static void
on_selection_change (GtkTreeSelection *sel, GtkAction *copy_action)
{
  /* The Copy action is available only if there is something selected */
  gtk_action_set_sensitive (copy_action, gtk_tree_selection_count_selected_rows (sel) > 0);
}

static void
on_select_all (struct psppire_output_view *view)
{
  GtkTreeSelection *sel = gtk_tree_view_get_selection (view->overview);
  gtk_tree_view_expand_all (view->overview);
  gtk_tree_selection_select_all (sel);
}

static void
on_size_allocate (GtkWidget    *widget,
                  GdkRectangle *allocation,
                  struct psppire_output_view *view)
{
  int new_render_width = MAX (300, allocation->width);
  if (view->render_width != new_render_width)
    {
      view->render_width = new_render_width;
      rerender (view);
    }
}

struct psppire_output_view *
psppire_output_view_new (GtkLayout *output, GtkTreeView *overview,
                         GtkAction *copy_action, GtkAction *select_all_action)
{
  struct psppire_output_view *view;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkTreeSelection *sel;
  GtkTreeModel *model;

  view = xmalloc (sizeof *view);
  view->xr = NULL;
  view->font_height = 0;
  view->output = output;
  view->render_width = 0;
  view->max_width = 0;
  view->y = 0;
  string_map_init (&view->render_opts);
  view->overview = overview;
  memset (&view->cur_command, 0, sizeof view->cur_command);
  view->in_command = false;
  view->toplevel = gtk_widget_get_toplevel (GTK_WIDGET (output));
  view->items = NULL;
  view->n_items = view->allocated_items = 0;
  view->print_settings = NULL;
  view->print_xrd = NULL;
  view->print_item = 0;
  view->print_n_pages = 0;
  view->paginated = FALSE;

  g_signal_connect (view->toplevel, "style-set", G_CALLBACK (on_style_set), view);

  g_signal_connect (output, "size-allocate", G_CALLBACK (on_size_allocate), view);

  if (overview)
    {
      model = GTK_TREE_MODEL (gtk_tree_store_new (
                                N_COLS,
                                G_TYPE_STRING,  /* COL_NAME */
                                G_TYPE_POINTER, /* COL_ADDR */
                                G_TYPE_LONG));  /* COL_Y */
      gtk_tree_view_set_model (overview, model);
      g_object_unref (model);

      sel = gtk_tree_view_get_selection (overview);
      gtk_tree_selection_set_mode (sel, GTK_SELECTION_MULTIPLE);
      g_signal_connect (sel, "changed", G_CALLBACK (on_selection_change),
                        copy_action);

      column = gtk_tree_view_column_new ();
      gtk_tree_view_append_column (GTK_TREE_VIEW (overview), column);
      renderer = gtk_cell_renderer_text_new ();
      gtk_tree_view_column_pack_start (column, renderer, TRUE);
      gtk_tree_view_column_add_attribute (column, renderer, "text", COL_NAME);

      g_signal_connect (GTK_TREE_VIEW (overview),
                        "row-activated", G_CALLBACK (on_row_activate), view);

      gtk_action_set_sensitive (copy_action, FALSE);
      g_signal_connect_swapped (copy_action, "activate",
                                G_CALLBACK (on_copy), view);
      g_signal_connect_swapped (select_all_action, "activate",
                                G_CALLBACK (on_select_all), view);
    }

  return view;
}

void
psppire_output_view_destroy (struct psppire_output_view *view)
{
  size_t i;

  if (!view)
    return;

  g_signal_handlers_disconnect_by_func (view->toplevel,
                                        G_CALLBACK (on_style_set), view);

  string_map_destroy (&view->render_opts);

  for (i = 0; i < view->n_items; i++)
    output_item_unref (view->items[i].item);
  free (view->items);
  view->items = NULL;
  view->n_items = view->allocated_items = 0;

  if (view->print_settings != NULL)
    g_object_unref (view->print_settings);

  xr_driver_destroy (view->xr);

  free (view);
}

void
psppire_output_view_clear (struct psppire_output_view *view)
{
  size_t i;

  view->max_width = 0;
  view->y = 0;

  for (i = 0; i < view->n_items; i++)
    {
      gtk_container_remove (GTK_CONTAINER (view->output),
                            view->items[i].drawing_area);
      output_item_unref (view->items[i].item);
    }
  free (view->items);
  view->items = NULL;
  view->n_items = view->allocated_items = 0;
}

/* Export. */

void
psppire_output_view_export (struct psppire_output_view *view,
                            struct string_map *options)
{
  struct output_driver *driver;

  driver = output_driver_create (options);
  if (driver)
    {
      size_t i;

      for (i = 0; i < view->n_items; i++)
        driver->class->submit (driver, view->items[i].item);
      output_driver_destroy (driver);
    }
}

/* Print. */

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
create_xr_print_driver (GtkPrintContext *context, struct psppire_output_view *view)
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

  view->print_xrd = xr_driver_create (get_cairo_context_from_print_context (context), &options);

  string_map_destroy (&options);
}

static gboolean
paginate (GtkPrintOperation *operation,
	  GtkPrintContext   *context,
	  struct psppire_output_view *view)
{
  if (view->paginated)
    {
      /* Sometimes GTK+ emits this signal again even after pagination is
         complete.  Don't let that screw up printing. */
      return TRUE;
    }
  else if ( view->print_item < view->n_items )
    {
      xr_driver_output_item (view->print_xrd,
                             view->items[view->print_item++].item);
      while (xr_driver_need_new_page (view->print_xrd))
	{
	  xr_driver_next_page (view->print_xrd, NULL);
	  view->print_n_pages ++;
	}
      return FALSE;
    }
  else
    {
      gtk_print_operation_set_n_pages (operation, view->print_n_pages);

      /* Re-create the driver to do the real printing. */
      xr_driver_destroy (view->print_xrd);
      create_xr_print_driver (context, view);
      view->print_item = 0;
      view->paginated = TRUE;

      return TRUE;
    }
}

static void
begin_print (GtkPrintOperation *operation,
	     GtkPrintContext   *context,
	     struct psppire_output_view *view)
{
  create_xr_print_driver (context, view);

  view->print_item = 0;
  view->print_n_pages = 1;
  view->paginated = FALSE;
}

static void
end_print (GtkPrintOperation *operation,
	   GtkPrintContext   *context,
	   struct psppire_output_view *view)
{
  xr_driver_destroy (view->print_xrd);
}


static void
draw_page (GtkPrintOperation *operation,
	   GtkPrintContext   *context,
	   gint               page_number,
	   struct psppire_output_view *view)
{
  xr_driver_next_page (view->print_xrd, get_cairo_context_from_print_context (context));
  while (!xr_driver_need_new_page (view->print_xrd)
         && view->print_item < view->n_items)
    xr_driver_output_item (view->print_xrd, view->items [view->print_item++].item);
}


void
psppire_output_view_print (struct psppire_output_view *view,
                           GtkWindow *parent_window)
{
  GtkPrintOperationResult res;

  GtkPrintOperation *print = gtk_print_operation_new ();

  if (view->print_settings != NULL) 
    gtk_print_operation_set_print_settings (print, view->print_settings);

  g_signal_connect (print, "begin_print", G_CALLBACK (begin_print), view);
  g_signal_connect (print, "end_print",   G_CALLBACK (end_print),   view);
  g_signal_connect (print, "paginate",    G_CALLBACK (paginate),    view);
  g_signal_connect (print, "draw_page",   G_CALLBACK (draw_page),   view);

  res = gtk_print_operation_run (print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                                 parent_window, NULL);

  if (res == GTK_PRINT_OPERATION_RESULT_APPLY)
    {
      if (view->print_settings != NULL)
        g_object_unref (view->print_settings);
      view->print_settings = g_object_ref (gtk_print_operation_get_print_settings (print));
    }

  g_object_unref (print);
}

struct psppire_output_view_driver
  {
    struct output_driver driver;
    struct psppire_output_view *view;
  };

static struct psppire_output_view_driver *
psppire_output_view_driver_cast (struct output_driver *driver)
{
  return UP_CAST (driver, struct psppire_output_view_driver, driver);
}

static void
psppire_output_view_submit (struct output_driver *this,
                            const struct output_item *item)
{
  struct psppire_output_view_driver *povd = psppire_output_view_driver_cast (this);

  if (is_table_item (item))
    psppire_output_view_put (povd->view, item);
}

static struct output_driver_class psppire_output_view_driver_class =
  {
    "PSPPIRE Output View",      /* name */
    NULL,                       /* destroy */
    psppire_output_view_submit, /* submit */
    NULL,                       /* flush */
  };

void
psppire_output_view_register_driver (struct psppire_output_view *view)
{
  struct psppire_output_view_driver *povd;
  struct output_driver *d;

  povd = xzalloc (sizeof *povd);
  povd->view = view;
  d = &povd->driver;
  output_driver_init (d, &psppire_output_view_driver_class, "PSPPIRE Output View",
                      SETTINGS_DEVICE_UNFILTERED);
  output_driver_register (d);
}
