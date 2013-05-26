/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2011, 2012 Free Software Foundation, Inc.

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

#include "ui/gui/psppire-cell-renderer-button.h"

#include <math.h>
#include <string.h>

#include "ui/gui/psppire-button-editable.h"
#include "ui/gui/pspp-widget-facade.h"

#include "gl/configmake.h"
#include "gl/relocatable.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static void psppire_cell_renderer_button_dispose (GObject *);
static void psppire_cell_renderer_button_finalize (GObject *);

static void update_style_cache (PsppireCellRendererButton *button,
                                GtkWidget                 *widget);

static void psppire_cell_renderer_button_load_gtkrc (void);


G_DEFINE_TYPE_EXTENDED (PsppireCellRendererButton,
                        psppire_cell_renderer_button,
                        GTK_TYPE_CELL_RENDERER,
                        0,
                        psppire_cell_renderer_button_load_gtkrc ());

static void
psppire_cell_renderer_button_load_gtkrc (void)
{
  const char *gtkrc_file;

  gtkrc_file = relocate (PKGDATADIR "/psppire.gtkrc");
  gtk_rc_add_default_file (gtkrc_file);
  gtk_rc_parse (gtkrc_file);
}

enum
  {
    PROP_0,
    PROP_EDITABLE,
    PROP_LABEL,
    PROP_SLASH
  };

static void
psppire_cell_renderer_button_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  PsppireCellRendererButton *obj = PSPPIRE_CELL_RENDERER_BUTTON (object);
  switch (prop_id)
    {
    case PROP_EDITABLE:
      obj->editable = g_value_get_boolean (value);
      if (obj->editable)
	g_object_set (obj, "mode", GTK_CELL_RENDERER_MODE_EDITABLE, NULL);
      else
	g_object_set (obj, "mode", GTK_CELL_RENDERER_MODE_INERT, NULL);
      break;

    case PROP_LABEL:
      g_free (obj->label);
      obj->label = g_value_dup_string (value);
      break;

    case PROP_SLASH:
      psppire_cell_renderer_button_set_slash (obj,
                                              g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_cell_renderer_button_get_property (GObject      *object,
                                           guint         prop_id,
                                           GValue       *value,
                                           GParamSpec   *pspec)
{
  PsppireCellRendererButton *obj = PSPPIRE_CELL_RENDERER_BUTTON (object);

  switch (prop_id)
    {
    case PROP_EDITABLE:
      g_value_set_boolean (value, obj->editable);
      break;

    case PROP_LABEL:
      g_value_set_string (value, obj->label);
      break;

    case PROP_SLASH:
      g_value_set_boolean (value,
                           psppire_cell_renderer_button_get_slash (obj));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
on_style_set (GtkWidget                 *base,
              GtkStyle                  *previous_style,
              PsppireCellRendererButton *button)
{
  update_style_cache (button, NULL);
}

static void
update_style_cache (PsppireCellRendererButton *button,
                    GtkWidget                 *widget)
{
  if (button->base == widget)
    return;

  /* Clear old cache. */
  if (button->button_style)
    {
      g_object_unref (button->button_style);
      button->button_style = NULL;
    }
  if (button->label_style)
    {
      g_object_unref (button->label_style);
      button->label_style = NULL;
    }
  if (button->base != NULL)
    {
      if (button->style_set_handler)
        {
          g_signal_handler_disconnect (button->base,
                                       button->style_set_handler);
          button->style_set_handler = 0;
        }
      g_object_unref (button->base);
      button->base = NULL;
    }

  /* Populate cache. */
  if (widget)
    {
      button->button_style = facade_get_style (widget, GTK_TYPE_BUTTON, 0);
      button->label_style = facade_get_style (widget, GTK_TYPE_BUTTON,
                                              GTK_TYPE_LABEL, 0);
      button->base = widget;
      button->style_set_handler = g_signal_connect (widget, "style-set",
                                                    G_CALLBACK (on_style_set),
                                                    button);
      g_object_ref (widget);
      g_object_ref (button->button_style);
      g_object_ref (button->label_style);
    }
}

static void
psppire_cell_renderer_button_render (GtkCellRenderer      *cell,
                                     GdkDrawable          *window,
                                     GtkWidget            *widget,
                                     GdkRectangle         *background_area,
                                     GdkRectangle         *cell_area,
                                     GdkRectangle         *expose_area,
                                     GtkCellRendererState  flags)
{
  GtkStateType state_type;
  PsppireCellRendererButton *button = PSPPIRE_CELL_RENDERER_BUTTON (cell);
  gfloat xalign, yalign;
  
  if (!button->editable || ! gtk_cell_renderer_get_sensitive (cell))
    state_type = GTK_STATE_INSENSITIVE;
  else if (flags & GTK_CELL_RENDERER_SELECTED)
    {
      if (gtk_widget_has_focus (widget))
        state_type = GTK_STATE_SELECTED;
      else
        state_type = GTK_STATE_ACTIVE;
    }
  else if (flags & GTK_CELL_RENDERER_PRELIT)
    state_type = GTK_STATE_PRELIGHT;
  else
    {
      if (gtk_widget_get_state (widget) == GTK_STATE_INSENSITIVE)
        state_type = GTK_STATE_INSENSITIVE;
      else
        state_type = GTK_STATE_NORMAL;
    }

  gtk_cell_renderer_get_alignment (cell, &xalign, &yalign);

  update_style_cache (button, widget);
  facade_button_render (widget, window, expose_area,
                        cell_area, button->border_width, button->button_style,
                        state_type,
                        button->label_style, button->label, button->xpad,
                        button->ypad, xalign, yalign);

  if (button->slash)
    {
      cairo_t *cr = gdk_cairo_create (window);

      cairo_set_line_width (cr, 1.0);
      cairo_set_line_cap (cr, CAIRO_LINE_CAP_SQUARE);
      cairo_move_to (cr, 
		     cell_area->x,
		     cell_area->y + cell_area->height);

      cairo_line_to (cr,
		     cell_area->x + cell_area->width,
		     cell_area->y);
      cairo_stroke (cr);
    }
}

static void
psppire_cell_renderer_button_get_size (GtkCellRenderer      *cell,
                                       GtkWidget            *widget,
                                       GdkRectangle         *cell_area,
                                       gint                 *x_offset,
                                       gint                 *y_offset,
                                       gint                 *width,
                                       gint                 *height)
{
  PsppireCellRendererButton *button = PSPPIRE_CELL_RENDERER_BUTTON (cell);

  update_style_cache (button, widget);
  if (cell_area != NULL)
    {
      /* The caller is really asking for the placement of the focus rectangle.
         The focus rectangle should surround the whole label area, so calculate
         that area. */
      GtkBorder inset;

      facade_button_get_focus_inset (button->border_width, widget,
                                     button->button_style, &inset);

      if (x_offset)
        *x_offset = inset.left;
      if (y_offset)
        *y_offset = inset.top;
      if (width)
        *width = MAX (1, cell_area->width - inset.left - inset.right);
      if (height)
        *height = MAX (1, cell_area->height - inset.top - inset.bottom);
    }
  else
    {
      /* The caller is asking for the preferred size of the cell. */
      GtkRequisition label_req;
      GtkRequisition request;

      facade_label_get_size_request (button->xpad, button->ypad,
                                     widget, button->label, &label_req);
      facade_button_get_size_request (button->border_width, widget,
                                      button->button_style, &label_req,
                                      &request);

      if (x_offset)
        *x_offset = 0;
      if (y_offset)
        *y_offset = 0;
      if (width)
        *width = request.width;
      if (height)
        *height = request.height;
    }
}

static void
psppire_cell_renderer_button_clicked (GtkButton *button,
                                      gpointer   data)
{
  PsppireCellRendererButton *cell_button = data;
  gchar *path;

  g_object_get (button, "path", &path, NULL);
  g_signal_emit_by_name (cell_button, "clicked", path);
  g_free (path);
}

#define IDLE_ID_STRING "psppire-cell-renderer-button-idle-id"

static gboolean
psppire_cell_renderer_button_initial_click (gpointer data)
{
  GtkButton *button = data;

  g_object_steal_data (G_OBJECT (button), IDLE_ID_STRING);
  gtk_button_clicked (button);
  return FALSE;
}

static void
psppire_cell_renderer_button_on_destroy (GObject *object, gpointer user_data)
{
  guint idle_id;

  idle_id = GPOINTER_TO_INT (g_object_steal_data (object, IDLE_ID_STRING));
  if (idle_id != 0)
    g_source_remove (idle_id);
}

static void
psppire_cell_renderer_button_double_click (GtkButton *button,
                                           PsppireCellRendererButton *cell_button)
{
  gchar *path;

  if (g_object_get_data (G_OBJECT (button), IDLE_ID_STRING))
    psppire_cell_renderer_button_initial_click (button);

  g_object_get (button, "path", &path, NULL);
  g_signal_emit_by_name (cell_button, "double-clicked", path);
  g_free (path);
}

static gboolean
psppire_cell_renderer_button_press_event (GtkButton      *button,
                                          GdkEventButton *event,
                                          gpointer        data)
{
  PsppireCellRendererButton *cell_button = data;

  if (event->button == 3)
    {
      /* Allow right-click events to propagate upward in the widget hierarchy.
         Otherwise right-click menus, that trigger on a button-press-event on
         the containing PsppSheetView, will pop up if the button is rendered as
         a facade but not if the button widget exists.

         We have to translate the event's data by hand to be relative to the
         parent window, because the normal GObject signal propagation mechanism
         won't do it for us.  (This might be a hint that we're doing this
         wrong.) */
      gint x, y;

      gdk_window_get_position (event->window, &x, &y);
      event->x += x;
      event->y += y;
      g_signal_stop_emission_by_name (button, "button-press-event");
      return FALSE;
    }

  if (cell_button->click_time != 0)
    {
      GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (button));
      GtkSettings *settings = gtk_settings_get_for_screen (screen);
      gint double_click_distance;
      gint double_click_time;

      g_object_get (settings,
                    "gtk-double-click-time", &double_click_time,
                    "gtk-double-click-distance", &double_click_distance,
                    NULL);

      if (event->type == GDK_BUTTON_PRESS
          && event->button == 1
          && event->time <= cell_button->click_time + double_click_time
          && ABS (event->x_root - cell_button->click_x) <= double_click_distance
          && ABS (event->y_root - cell_button->click_y) <= double_click_distance)
        {
          psppire_cell_renderer_button_double_click (button, cell_button);
          return TRUE;
        }

      cell_button->click_time = 0;
    }

  if (event->type == GDK_2BUTTON_PRESS)
    {
      psppire_cell_renderer_button_double_click (button, cell_button);
      return TRUE;
    }

  return FALSE;
}

static GtkCellEditable *
psppire_cell_renderer_button_start_editing (GtkCellRenderer      *cell,
                                            GdkEvent             *event,
                                            GtkWidget            *widget,
                                            const gchar          *path,
                                            GdkRectangle         *background_area,
                                            GdkRectangle         *cell_area,
                                            GtkCellRendererState  flags)
{
  PsppireCellRendererButton *cell_button = PSPPIRE_CELL_RENDERER_BUTTON (cell);
  gfloat xalign, yalign;

  gtk_cell_renderer_get_alignment (cell, &xalign, &yalign);
  cell_button->button = g_object_new (PSPPIRE_TYPE_BUTTON_EDITABLE,
                                      "label", cell_button->label,
                                      "xalign", xalign,
                                      "yalign", yalign,
                                      "path", path,
                                      NULL);

  g_signal_connect (G_OBJECT (cell_button->button), "clicked",
                    G_CALLBACK (psppire_cell_renderer_button_clicked),
                    cell);
  g_signal_connect (G_OBJECT (cell_button->button), "button-press-event",
                    G_CALLBACK (psppire_cell_renderer_button_press_event),
                    cell);

  gtk_widget_show (cell_button->button);

  if (event != NULL && event->any.type == GDK_BUTTON_RELEASE)
    {
      guint idle_id;

      cell_button->click_time = event->button.time;
      cell_button->click_x = event->button.x_root;
      cell_button->click_y = event->button.y_root;
      idle_id = g_idle_add (psppire_cell_renderer_button_initial_click,
                            cell_button->button);
      g_object_set_data (G_OBJECT (cell_button->button), IDLE_ID_STRING,
                         GINT_TO_POINTER (idle_id));
      g_signal_connect (G_OBJECT (cell_button->button), "destroy",
                        G_CALLBACK (psppire_cell_renderer_button_on_destroy),
                        NULL);
    }
  else
    {
      cell_button->click_time = 0;
      cell_button->click_x = 0;
      cell_button->click_y = 0;
    }

  return GTK_CELL_EDITABLE (cell_button->button);
}

static void
psppire_cell_renderer_button_class_init (PsppireCellRendererButtonClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);
  GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS (class);

  gobject_class->set_property = psppire_cell_renderer_button_set_property;
  gobject_class->get_property = psppire_cell_renderer_button_get_property;
  gobject_class->finalize = psppire_cell_renderer_button_finalize;
  gobject_class->dispose = psppire_cell_renderer_button_dispose;

  cell_class->get_size = psppire_cell_renderer_button_get_size;
  cell_class->render = psppire_cell_renderer_button_render;
  cell_class->start_editing = psppire_cell_renderer_button_start_editing;

  g_signal_new ("clicked",
                G_TYPE_FROM_CLASS (gobject_class),
                G_SIGNAL_RUN_LAST,
                0,
                NULL, NULL,
                g_cclosure_marshal_VOID__STRING,
                G_TYPE_NONE,
                1, G_TYPE_STRING);

  g_signal_new ("double-clicked",
                G_TYPE_FROM_CLASS (gobject_class),
                G_SIGNAL_RUN_LAST,
                0,
                NULL, NULL,
                g_cclosure_marshal_VOID__STRING,
                G_TYPE_NONE,
                1, G_TYPE_STRING);

  g_object_class_install_property (gobject_class,
                                   PROP_EDITABLE,
                                   g_param_spec_boolean ("editable",
                                                         "Editable",
                                                         "Whether the button may be clicked.",
                                                         FALSE,
                                                         G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_LABEL,
                                   g_param_spec_string ("label",
                                                        "Label",
                                                        "Text to appear in button.",
                                                        "",
                                                        G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_SLASH,
                                   g_param_spec_boolean ("slash",
                                                         _("Diagonal slash"),
                                                         _("Whether to draw a diagonal slash across the button."),
                                                         FALSE,
                                                         G_PARAM_READWRITE));

}

static void
psppire_cell_renderer_button_init (PsppireCellRendererButton *obj)
{
  obj->editable = FALSE;
  obj->label = g_strdup ("");
  obj->border_width = 0;
  obj->xpad = 0;
  obj->ypad = 0;

  obj->slash = FALSE;

  obj->button = NULL;

  obj->button_style = NULL;
  obj->label_style = NULL;
  obj->base = NULL;
  obj->style_set_handler = 0;
  obj->dispose_has_run = FALSE;
}

static void
psppire_cell_renderer_button_finalize (GObject *obj)
{
  PsppireCellRendererButton *button = PSPPIRE_CELL_RENDERER_BUTTON (obj);

  g_free (button->label);
}

static void
psppire_cell_renderer_button_dispose (GObject *obj)
{
  PsppireCellRendererButton *button = PSPPIRE_CELL_RENDERER_BUTTON (obj);

  if (button->dispose_has_run)
    return;
  
  button->dispose_has_run = TRUE;

  /* When called with NULL, as we are doing here, update_style_cache
     does nothing more than to drop references */
  update_style_cache (button, NULL);

  G_OBJECT_CLASS (psppire_cell_renderer_button_parent_class)->dispose (obj);
}

GtkCellRenderer *
psppire_cell_renderer_button_new (void)
{
  return GTK_CELL_RENDERER (g_object_new (PSPPIRE_TYPE_CELL_RENDERER_BUTTON, NULL));
}

void
psppire_cell_renderer_button_set_slash (PsppireCellRendererButton *button,
                                        gboolean slash)
{
  g_return_if_fail (button != NULL);
  button->slash = slash;
}

gboolean
psppire_cell_renderer_button_get_slash (const PsppireCellRendererButton *button)
{
  g_return_val_if_fail (button != NULL, FALSE);
  return button->slash;
}
