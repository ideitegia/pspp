/*
   PSPPIRE --- A Graphical User Interface for PSPP
   Copyright (C) 2005  Free Software Foundation

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

/*
   This widget is a subclass of GtkEntry.  It's an entry widget with a
   button on the right hand side.

   This code is heavily based upon the GtkSpinButton widget.  Therefore
   the copyright notice of that code is pasted below.

   Please note however,  this code is covered by the GPL, not the LGPL.
*/

/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * GtkSpinButton widget for GTK+
 * Copyright (C) 1998 Lars Hamann and Stefan Jeske
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

#include <config.h>
#include <gettext.h>
#define _(msgid) gettext (msgid)


#include <gtk/gtksignal.h>
#include <gtk/gtkentry.h>
#include "customentry.h"


static void psppire_custom_entry_class_init          (PsppireCustomEntryClass *klass);
static void psppire_custom_entry_init                (PsppireCustomEntry      *ce);

static GtkEntryClass *parent_class = NULL;

/* Signals */
enum
{
  CLICKED,
  n_SIGNALS
};


static guint custom_entry_signals[n_SIGNALS] = {0};


GType
psppire_custom_entry_get_type (void)
{
  static GType ce_type = 0;

  if (!ce_type)
    {
      static const GTypeInfo ce_info =
	{
	  sizeof (PsppireCustomEntryClass),
	  NULL, /* base_init */
	  NULL, /* base_finalize */
	  (GClassInitFunc) psppire_custom_entry_class_init,
	  NULL, /* class_finalize */
	  NULL, /* class_data */
	  sizeof (PsppireCustomEntry),
	  0,
	  (GInstanceInitFunc) psppire_custom_entry_init,
	};

      ce_type = g_type_register_static (GTK_TYPE_ENTRY, "PsppireCustomEntry",
					&ce_info, 0);
    }

  return ce_type;
}


static void
psppire_custom_entry_map (GtkWidget *widget)
{
  if (GTK_WIDGET_REALIZED (widget) && !GTK_WIDGET_MAPPED (widget))
    {
      GTK_WIDGET_CLASS (parent_class)->map (widget);
      gdk_window_show (PSPPIRE_CUSTOM_ENTRY (widget)->panel);
    }
}

static void
psppire_custom_entry_unmap (GtkWidget *widget)
{
  if (GTK_WIDGET_MAPPED (widget))
    {
      gdk_window_hide (PSPPIRE_CUSTOM_ENTRY (widget)->panel);
      GTK_WIDGET_CLASS (parent_class)->unmap (widget);
    }
}

static gint psppire_custom_entry_get_button_width (PsppireCustomEntry *custom_entry);

static void
psppire_custom_entry_realize (GtkWidget *widget)
{
  PsppireCustomEntry *custom_entry;
  GdkWindowAttr attributes;
  gint attributes_mask;
  guint real_width;
  gint button_size ;

  custom_entry = PSPPIRE_CUSTOM_ENTRY (widget);

  button_size = psppire_custom_entry_get_button_width (custom_entry);

  real_width = widget->allocation.width;
  widget->allocation.width -= button_size + 2 * widget->style->xthickness;
  gtk_widget_set_events (widget, gtk_widget_get_events (widget) |
			 GDK_KEY_RELEASE_MASK);
  GTK_WIDGET_CLASS (parent_class)->realize (widget);

  widget->allocation.width = real_width;

  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.colormap = gtk_widget_get_colormap (widget);
  attributes.event_mask = gtk_widget_get_events (widget);
  attributes.event_mask |= GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK
    | GDK_BUTTON_RELEASE_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_ENTER_NOTIFY_MASK
    | GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK;

  attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

  attributes.x = (widget->allocation.x +
		  widget->allocation.width - button_size -
		  2 * widget->style->xthickness);
  attributes.y = widget->allocation.y + (widget->allocation.height -
					 widget->requisition.height) / 2;
  attributes.width = button_size + 2 * widget->style->xthickness;
  attributes.height = widget->requisition.height;

  custom_entry->panel = gdk_window_new (gtk_widget_get_parent_window (widget),
					&attributes, attributes_mask);
  gdk_window_set_user_data (custom_entry->panel, widget);

  gtk_style_set_background (widget->style, custom_entry->panel, GTK_STATE_NORMAL);


  gtk_widget_queue_resize (GTK_WIDGET (custom_entry));
}


#define MIN_BUTTON_WIDTH  6

static gint
psppire_custom_entry_get_button_width (PsppireCustomEntry *custom_entry)
{
  const gint size = pango_font_description_get_size
    (GTK_WIDGET (custom_entry)->style->font_desc);

  gint button_width = MAX (PANGO_PIXELS (size), MIN_BUTTON_WIDTH);

  return button_width - button_width % 2; /* force even */
}

/**
 * custom_entry_get_shadow_type:
 * @custom_entry: a #PsppireCustomEntry
 *
 * Convenience function to Get the shadow type from the underlying widget's
 * style.
 *
 * Return value: the #GtkShadowType
 **/
static gint
psppire_custom_entry_get_shadow_type (PsppireCustomEntry *custom_entry)
{
  GtkShadowType rc_shadow_type;

  gtk_widget_style_get (GTK_WIDGET (custom_entry), "shadow_type", &rc_shadow_type, NULL);

  return rc_shadow_type;
}


static void
psppire_custom_entry_unrealize (GtkWidget *widget)
{
  PsppireCustomEntry *ce = PSPPIRE_CUSTOM_ENTRY (widget);

  GTK_WIDGET_CLASS (parent_class)->unrealize (widget);

  if (ce->panel)
    {
      gdk_window_set_user_data (ce->panel, NULL);
      gdk_window_destroy (ce->panel);
      ce->panel = NULL;
    }
}


static void
psppire_custom_entry_redraw (PsppireCustomEntry *custom_entry)
{
  GtkWidget *widget;

  widget = GTK_WIDGET (custom_entry);

  if (GTK_WIDGET_DRAWABLE (widget))
    {
      gtk_widget_queue_draw (widget);

      /* We must invalidate the panel window ourselves, because it
       * is not a child of widget->window
       */
      gdk_window_invalidate_rect (custom_entry->panel, NULL, TRUE);
    }
}


static gint
psppire_custom_entry_expose (GtkWidget      *widget,
		     GdkEventExpose *event)
{
  PsppireCustomEntry *ce = PSPPIRE_CUSTOM_ENTRY(widget);

  g_return_val_if_fail (PSPPIRE_IS_CUSTOM_ENTRY (widget), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  if (GTK_WIDGET_DRAWABLE (widget))
    {
      GtkShadowType shadow_type;
      GdkRectangle rect;

      rect.x = 0;
      rect.y = 0;

      if (event->window != ce->panel)
	GTK_WIDGET_CLASS (parent_class)->expose_event (widget, event);

      gdk_drawable_get_size (ce->panel, &rect.width, &rect.height);

      gdk_window_begin_paint_rect (ce->panel, &rect);


      shadow_type = psppire_custom_entry_get_shadow_type (ce);

      if (shadow_type != GTK_SHADOW_NONE)
	{
	  gtk_paint_box (widget->style, ce->panel,
			 GTK_STATE_NORMAL, shadow_type,
			 NULL, widget, "customentry",
			 rect.x, rect.y, rect.width, rect.height);

	}

      gdk_window_end_paint (ce->panel);
    }

  return FALSE;
}


static gint
psppire_custom_entry_button_press (GtkWidget      *widget,
			   GdkEventButton *event);

static void
psppire_custom_entry_size_allocate (GtkWidget     *widget,
			    GtkAllocation *allocation);



static void
psppire_custom_entry_class_init (PsppireCustomEntryClass *klass)
{
  GObjectClass     *gobject_class = G_OBJECT_CLASS (klass);

  GtkWidgetClass   *widget_class;
  GtkEntryClass   *entry_class;

  parent_class = g_type_class_peek_parent (klass);

  widget_class   = (GtkWidgetClass*)   klass;
  entry_class   = (GtkEntryClass*)   klass;

  widget_class->map = psppire_custom_entry_map;
  widget_class->unmap = psppire_custom_entry_unmap;

  widget_class->realize = psppire_custom_entry_realize;
  widget_class->unrealize = psppire_custom_entry_unrealize;

  widget_class->expose_event = psppire_custom_entry_expose;
  widget_class->button_press_event = psppire_custom_entry_button_press;

  widget_class->size_allocate = psppire_custom_entry_size_allocate;


  gtk_widget_class_install_style_property_parser
    (widget_class,
     g_param_spec_enum ("shadow_type",
			"Shadow Type",
			_("Style of bevel around the custom entry button"),
			GTK_TYPE_SHADOW_TYPE,
			GTK_SHADOW_ETCHED_IN,
			G_PARAM_READABLE),
     gtk_rc_property_parse_enum);

  custom_entry_signals[CLICKED] =
    g_signal_new ("clicked",
		  G_TYPE_FROM_CLASS(gobject_class),
		  G_SIGNAL_RUN_LAST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE,
		  0);


}

static void
psppire_custom_entry_init (PsppireCustomEntry *ce)
{
}

GtkWidget*
psppire_custom_entry_new ()
{
  return GTK_WIDGET (g_object_new (psppire_custom_entry_get_type (), NULL));
}



static gint
psppire_custom_entry_button_press (GtkWidget *widget,
				   GdkEventButton *event)
{
  PsppireCustomEntry *ce = PSPPIRE_CUSTOM_ENTRY (widget);

  if (event->window == ce->panel)
    {
      if (!GTK_WIDGET_HAS_FOCUS (widget))
	gtk_widget_grab_focus (widget);

      if ( event->button == 1)
	g_signal_emit (widget, custom_entry_signals[CLICKED], 0);

    }
  else
    return GTK_WIDGET_CLASS (parent_class)->button_press_event (widget, event);

  return FALSE;
}



static void
psppire_custom_entry_size_allocate (GtkWidget     *widget,
			    GtkAllocation *allocation)
{
  PsppireCustomEntry *ce;
  GtkAllocation entry_allocation;
  GtkAllocation panel_allocation;
  gint button_width;
  gint panel_width;

  g_return_if_fail (PSPPIRE_IS_CUSTOM_ENTRY (widget));
  g_return_if_fail (allocation != NULL);

  ce = PSPPIRE_CUSTOM_ENTRY (widget);
  button_width = psppire_custom_entry_get_button_width(ce);
  panel_width = button_width + 2 * widget->style->xthickness;

  widget->allocation = *allocation;

  entry_allocation = *allocation;
  entry_allocation.width -= panel_width;

  if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL)
    {
      entry_allocation.x += panel_width;
      panel_allocation.x = allocation->x;
    }
  else
    {
      panel_allocation.x = allocation->x + allocation->width - panel_width;
    }

  panel_allocation.width = panel_width;
  panel_allocation.height = MIN (widget->requisition.height, allocation->height);

  panel_allocation.y = allocation->y + (allocation->height -
					panel_allocation.height) / 2;

  GTK_WIDGET_CLASS (parent_class)->size_allocate (widget, &entry_allocation);

  if (GTK_WIDGET_REALIZED (widget))
    {
      gdk_window_move_resize (PSPPIRE_CUSTOM_ENTRY (widget)->panel,
			      panel_allocation.x,
			      panel_allocation.y,
			      panel_allocation.width,
			      panel_allocation.height);
    }

  psppire_custom_entry_redraw (ce);
}





