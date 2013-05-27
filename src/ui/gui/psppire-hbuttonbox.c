/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010, 2011  Free Software Foundation

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

#include <glib.h>
#include <gtk/gtk.h>
#include "psppire-hbuttonbox.h"
#include "psppire-dialog.h"

#include <gettext.h>

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void psppire_hbuttonbox_class_init          (PsppireHButtonBoxClass *);
static void psppire_hbuttonbox_init                (PsppireHButtonBox      *);

static void gtk_hbutton_box_size_request  (GtkWidget      *widget,
					   GtkRequisition *requisition);
static void gtk_hbutton_box_size_allocate (GtkWidget      *widget,
					   GtkAllocation  *allocation);


static void
psppire_hbutton_box_get_preferred_height (GtkWidget *widget,
                                gint      *minimal_height,
                                gint      *natural_height)
{
  GtkRequisition requisition;

  gtk_hbutton_box_size_request (widget, &requisition);

  *minimal_height = *natural_height = requisition.height;
}


static void
psppire_hbutton_box_get_preferred_width (GtkWidget *widget,
                                gint      *minimal_width,
                                gint      *natural_width)
{
  GtkRequisition requisition;

  gtk_hbutton_box_size_request (widget, &requisition);

  *minimal_width = *natural_width = requisition.width;
}




GType
psppire_hbutton_box_get_type (void)
{
  static GType hbuttonbox_type = 0;

  if (!hbuttonbox_type)
    {
      static const GTypeInfo hbuttonbox_info =
      {
	sizeof (PsppireHButtonBoxClass),
	NULL, /* base_init */
        NULL, /* base_finalize */
	(GClassInitFunc) psppire_hbuttonbox_class_init,
        NULL, /* class_finalize */
	NULL, /* class_data */
        sizeof (PsppireHButtonBox),
	0,
	(GInstanceInitFunc) psppire_hbuttonbox_init,
      };

      hbuttonbox_type = g_type_register_static (PSPPIRE_BUTTONBOX_TYPE,
					    "PsppireHButtonBox", &hbuttonbox_info, 0);
    }

  return hbuttonbox_type;
}

static void
psppire_hbuttonbox_class_init (PsppireHButtonBoxClass *class)
{
  GtkWidgetClass *widget_class;

  widget_class = (GtkWidgetClass*) class;

  widget_class->get_preferred_width = psppire_hbutton_box_get_preferred_width;
  widget_class->get_preferred_height = psppire_hbutton_box_get_preferred_height;
  widget_class->size_allocate = gtk_hbutton_box_size_allocate;
}


static void
psppire_hbuttonbox_init (PsppireHButtonBox *hbuttonbox)
{
}


GtkWidget*
psppire_hbuttonbox_new (void)
{
  PsppireHButtonBox *hbuttonbox ;

  hbuttonbox = g_object_new (psppire_hbutton_box_get_type (), NULL);

  return GTK_WIDGET (hbuttonbox) ;
}



/* The following two functions are lifted verbatim from
   the Gtk2.10.6 library */

static void
gtk_hbutton_box_size_request (GtkWidget      *widget,
			      GtkRequisition *requisition)
{
  GtkBox *box;
  GtkButtonBox *bbox;
  gint nvis_children;
  gint child_width;
  gint child_height;
  gint spacing;
  GtkButtonBoxStyle layout;

  box = GTK_BOX (widget);
  bbox = GTK_BUTTON_BOX (widget);

  spacing = gtk_box_get_spacing (box);
  layout = gtk_button_box_get_layout (bbox) ;

  _psppire_button_box_child_requisition (widget,
                                     &nvis_children,
				     NULL,
                                     &child_width,
                                     &child_height);

  if (nvis_children == 0)
  {
    requisition->width = 0;
    requisition->height = 0;
  }
  else
  {
    switch (layout)
    {
    case GTK_BUTTONBOX_SPREAD:
      requisition->width =
	      nvis_children*child_width + ((nvis_children+1)*spacing);
      break;
    default:
    case GTK_BUTTONBOX_EDGE:
    case GTK_BUTTONBOX_START:
    case GTK_BUTTONBOX_END:
      requisition->width = nvis_children*child_width + ((nvis_children-1)*spacing);
      break;
    }

    requisition->height = child_height;
  }

  requisition->width += gtk_container_get_border_width (GTK_CONTAINER (box)) * 2;
  requisition->height += gtk_container_get_border_width (GTK_CONTAINER (box)) * 2;
}



static void
gtk_hbutton_box_size_allocate (GtkWidget     *widget,
			       GtkAllocation *allocation)
{
  GtkBox *base_box;
  GtkButtonBox *box;
  GList *children;
  GtkAllocation child_allocation;
  gint nvis_children;
  gint n_secondaries;
  gint child_width;
  gint child_height;
  gint x = 0;
  gint secondary_x = 0;
  gint y = 0;
  gint width;
  gint childspace;
  gint childspacing = 0;
  GtkButtonBoxStyle layout;
  gint spacing;

  base_box = GTK_BOX (widget);
  box = GTK_BUTTON_BOX (widget);
  spacing = gtk_box_get_spacing (base_box);
  layout = gtk_button_box_get_layout (box) ;
  _psppire_button_box_child_requisition (widget,
                                     &nvis_children,
				     &n_secondaries,
                                     &child_width,
                                     &child_height);
  gtk_widget_set_allocation (widget, allocation);
  width = allocation->width - gtk_container_get_border_width (GTK_CONTAINER (box))*2;
  switch (layout)
  {
  case GTK_BUTTONBOX_SPREAD:
    childspacing = (width - (nvis_children * child_width)) / (nvis_children + 1);
    x = allocation->x + gtk_container_get_border_width (GTK_CONTAINER (box)) + childspacing;
    secondary_x = x + ((nvis_children - n_secondaries) * (child_width + childspacing));
    break;
  default:
  case GTK_BUTTONBOX_EDGE:
    if (nvis_children >= 2)
      {
	childspacing = (width - (nvis_children * child_width)) / (nvis_children - 1);
	x = allocation->x + gtk_container_get_border_width (GTK_CONTAINER (box));
	secondary_x = x + ((nvis_children - n_secondaries) * (child_width + childspacing));
      }
    else
      {
	/* one or zero children, just center */
        childspacing = width;
	x = secondary_x = allocation->x + (allocation->width - child_width) / 2;
      }
    break;
  case GTK_BUTTONBOX_START:
    childspacing = spacing;
    x = allocation->x + gtk_container_get_border_width (GTK_CONTAINER (box));
    secondary_x = allocation->x + allocation->width
      - child_width * n_secondaries
      - spacing * (n_secondaries - 1)
      - gtk_container_get_border_width (GTK_CONTAINER (box));
    break;
  case GTK_BUTTONBOX_END:
    childspacing = spacing;
    x = allocation->x + allocation->width
      - child_width * (nvis_children - n_secondaries)
      - spacing * (nvis_children - n_secondaries - 1)
      - gtk_container_get_border_width (GTK_CONTAINER (box));
    secondary_x = allocation->x + gtk_container_get_border_width (GTK_CONTAINER (box));
    break;
  }


  y = allocation->y + (allocation->height - child_height) / 2;
  childspace = child_width + childspacing;

  children = gtk_container_get_children (GTK_CONTAINER (box));

  while (children)
    {
      GtkWidget *child = children->data;
      children = children->next;

      if (gtk_widget_get_visible (child))
	{
          gboolean is_secondary = FALSE;
          gtk_container_child_get (GTK_CONTAINER (box), child, "secondary", &is_secondary, NULL);


	  child_allocation.width = child_width;
	  child_allocation.height = child_height;
	  child_allocation.y = y;

          if (is_secondary)
            {
	      child_allocation.x = secondary_x;
	      secondary_x += childspace;
	    }
          else
	    {
	      child_allocation.x = x;
	      x += childspace;
	    }

	  if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL)
	    child_allocation.x = (allocation->x + allocation->width) - (child_allocation.x + child_width - allocation->x);

	  gtk_widget_size_allocate (child, &child_allocation);
	}
    }
}
