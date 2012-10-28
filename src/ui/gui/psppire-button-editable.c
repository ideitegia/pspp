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

#include "ui/gui/psppire-button-editable.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* GtkCellEditable interface. */
static void gtk_cell_editable_interface_init (GtkCellEditableIface *iface);
static void button_editable_editing_done (GtkCellEditable *cell_editable);
static void button_editable_remove_widget (GtkCellEditable *cell_editable);
static void button_editable_start_editing (GtkCellEditable *cell_editable,
                                           GdkEvent        *event);

G_DEFINE_TYPE_EXTENDED (PsppireButtonEditable,
                        psppire_button_editable,
                        GTK_TYPE_BUTTON,
                        0,
                        G_IMPLEMENT_INTERFACE (
                          GTK_TYPE_CELL_EDITABLE,
                          gtk_cell_editable_interface_init));

enum
  {
    PROP_0,
    PROP_PATH,
    PROP_SLASH,
    PROP_EDITING_CANCELED
  };

static void
psppire_button_editable_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  PsppireButtonEditable *obj = PSPPIRE_BUTTON_EDITABLE (object);

  switch (prop_id)
    {
    case PROP_PATH:
      g_free (obj->path);
      obj->path = g_value_dup_string (value);
      break;

    case PROP_SLASH:
      psppire_button_editable_set_slash (obj, g_value_get_boolean (value));
      break;

    case PROP_EDITING_CANCELED:
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_button_editable_get_property (GObject      *object,
                                      guint         prop_id,
                                      GValue       *value,
                                      GParamSpec   *pspec)
{
  PsppireButtonEditable *obj = PSPPIRE_BUTTON_EDITABLE (object);

  switch (prop_id)
    {
    case PROP_PATH:
      g_value_set_string (value, obj->path);
      break;

    case PROP_SLASH:
      g_value_set_boolean (value, psppire_button_editable_get_slash (obj));
      break;

    case PROP_EDITING_CANCELED:
      g_value_set_boolean (value, FALSE);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_button_editable_finalize (GObject *gobject)
{
  PsppireButtonEditable *obj = PSPPIRE_BUTTON_EDITABLE (gobject);

  g_free (obj->path);

  G_OBJECT_CLASS (psppire_button_editable_parent_class)->finalize (gobject);
}

static gboolean
psppire_button_editable_button_release (GtkWidget      *widget,
                                        GdkEventButton *event)
{
  if (event->button == 1)
    {
      g_signal_emit_by_name (widget, "button-release-event", event, NULL);
    }

  return TRUE;
}

static gboolean
psppire_button_editable_expose_event (GtkWidget      *widget,
                                      GdkEventExpose *event)
{
  GtkWidgetClass *widget_class;
  GtkStyle *style = gtk_widget_get_style (widget);
  GtkAllocation allocation;
  gboolean retval;

  widget_class = GTK_WIDGET_CLASS (psppire_button_editable_parent_class);
  retval = widget_class->expose_event (widget, event);

  gtk_widget_get_allocation (widget, &allocation);
  if (PSPPIRE_BUTTON_EDITABLE (widget)->slash)
    gdk_draw_line (gtk_widget_get_window (widget), style->black_gc,
                   allocation.x,
                   allocation.y + allocation.height,
                   allocation.x + allocation.width,
                   allocation.y);
  return retval;
}

static void
psppire_button_editable_class_init (PsppireButtonEditableClass *class)
{
  GObjectClass *gobject_class;
  GtkWidgetClass *widget_class;

  gobject_class = G_OBJECT_CLASS (class);
  widget_class = GTK_WIDGET_CLASS (class);

  gobject_class->set_property = psppire_button_editable_set_property;
  gobject_class->get_property = psppire_button_editable_get_property;
  gobject_class->finalize = psppire_button_editable_finalize;

  widget_class->button_release_event = psppire_button_editable_button_release;
  widget_class->expose_event = psppire_button_editable_expose_event;

  g_object_class_install_property (gobject_class,
                                   PROP_PATH,
                                   g_param_spec_string ("path",
							_("TreeView path"),
							_("The path to the row in the GtkTreeView, as a string"),
							"",
							G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
                                   PROP_SLASH,
                                   g_param_spec_boolean ("slash",
                                                         _("Diagonal slash"),
                                                         _("Whether to draw a diagonal slash across the button."),
                                                         FALSE,
                                                         G_PARAM_READWRITE));

  g_object_class_override_property (gobject_class,
                                   PROP_EDITING_CANCELED,
                                    "editing-canceled");
}

static void
psppire_button_editable_init (PsppireButtonEditable *obj)
{
  obj->path = g_strdup ("");
  obj->slash = FALSE;
}

PsppireButtonEditable *
psppire_button_editable_new (void)
{
  return PSPPIRE_BUTTON_EDITABLE (g_object_new (PSPPIRE_TYPE_BUTTON_EDITABLE, NULL));
}

void
psppire_button_editable_set_slash (PsppireButtonEditable *button,
                                   gboolean slash)
{
  g_return_if_fail (button != NULL);
  if ((button->slash != 0) != (slash != 0))
    {
      button->slash = slash;
      gtk_widget_queue_draw (GTK_WIDGET (button));
    }
}

gboolean
psppire_button_editable_get_slash (const PsppireButtonEditable *button)
{
  g_return_val_if_fail (button != NULL, FALSE);
  return button->slash;
}

/* GtkCellEditable interface. */

static void
gtk_cell_editable_interface_init (GtkCellEditableIface *iface)
{
  g_return_if_fail (iface != NULL);

  iface->editing_done = button_editable_editing_done;
  iface->remove_widget = button_editable_remove_widget;
  iface->start_editing = button_editable_start_editing;
}

static void
button_editable_editing_done (GtkCellEditable *cell_editable)
{
}

static void
button_editable_remove_widget (GtkCellEditable *cell_editable)
{
}

static void
button_editable_start_editing (GtkCellEditable *cell_editable,
                               GdkEvent        *event)
{
}
