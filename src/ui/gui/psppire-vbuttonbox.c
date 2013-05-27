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
#include "psppire-vbuttonbox.h"
#include "psppire-dialog.h"

#include <gettext.h>

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void psppire_vbuttonbox_class_init          (PsppireVButtonBoxClass *);
static void psppire_vbuttonbox_init                (PsppireVButtonBox      *);

static void gtk_vbutton_box_size_request  (GtkWidget      *widget,
					   GtkRequisition *requisition);
static void gtk_vbutton_box_size_allocate (GtkWidget      *widget,
					   GtkAllocation  *allocation);


static void
psppire_vbutton_box_get_preferred_height (GtkWidget *widget,
                                gint      *minimal_height,
                                gint      *natural_height)
{
  GtkRequisition requisition;

  gtk_vbutton_box_size_request (widget, &requisition);

  *minimal_height = *natural_height = requisition.height;
}


static void
psppire_vbutton_box_get_preferred_width (GtkWidget *widget,
                                gint      *minimal_width,
                                gint      *natural_width)
{
  GtkRequisition requisition;

  gtk_vbutton_box_size_request (widget, &requisition);

  *minimal_width = *natural_width = requisition.width;
}


GType
psppire_vbutton_box_get_type (void)
{
  static GType vbuttonbox_type = 0;

  if (!vbuttonbox_type)
    {
      static const GTypeInfo vbuttonbox_info =
      {
	sizeof (PsppireVButtonBoxClass),
	NULL, /* base_init */
        NULL, /* base_finalize */
	(GClassInitFunc) psppire_vbuttonbox_class_init,
        NULL, /* class_finalize */
	NULL, /* class_data */
        sizeof (PsppireVButtonBox),
	0,
	(GInstanceInitFunc) psppire_vbuttonbox_init,
      };

      vbuttonbox_type = g_type_register_static (PSPPIRE_BUTTONBOX_TYPE,
					    "PsppireVButtonBox", &vbuttonbox_info, 0);
    }

  return vbuttonbox_type;
}

static void
psppire_vbuttonbox_class_init (PsppireVButtonBoxClass *class)
{
  GtkWidgetClass *widget_class;

  widget_class = (GtkWidgetClass*) class;

  widget_class->get_preferred_width = psppire_vbutton_box_get_preferred_width;
  widget_class->get_preferred_height = psppire_vbutton_box_get_preferred_height;
  widget_class->size_allocate = gtk_vbutton_box_size_allocate;
}


static void
psppire_vbuttonbox_init (PsppireVButtonBox *vbuttonbox)
{
}


GtkWidget*
psppire_vbuttonbox_new (void)
{
  PsppireVButtonBox *vbuttonbox ;

  vbuttonbox = g_object_new (psppire_vbutton_box_get_type (), NULL);

  return GTK_WIDGET (vbuttonbox) ;
}



/* The following two functions are lifted verbatim from
   the Gtk2.10.6 library */

static void
gtk_vbutton_box_size_request (GtkWidget      *widget,
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
  layout = gtk_button_box_get_layout (bbox);

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
        requisition->height =
		nvis_children*child_height + ((nvis_children+1)*spacing);
	break;
      case GTK_BUTTONBOX_EDGE:
      case GTK_BUTTONBOX_START:
      case GTK_BUTTONBOX_END:
      default:
        requisition->height =
		nvis_children*child_height + ((nvis_children-1)*spacing);
	break;
      }
      requisition->width = child_width;
    }

  requisition->width += gtk_container_get_border_width (GTK_CONTAINER (box)) * 2;
  requisition->height += gtk_container_get_border_width (GTK_CONTAINER (box)) * 2;
}



static void
gtk_vbutton_box_size_allocate (GtkWidget     *widget,
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
  gint y = 0;
  gint secondary_y = 0;
  gint height;
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
  height = allocation->height - gtk_container_get_border_width (GTK_CONTAINER (box))*2;
  switch (layout)
  {
  case GTK_BUTTONBOX_SPREAD:
    childspacing = (height - (nvis_children * child_height)) / (nvis_children + 1);
    y = allocation->y + gtk_container_get_border_width (GTK_CONTAINER (box)) + childspacing;
    secondary_y = y + ((nvis_children - n_secondaries) * (child_height + childspacing));
    break;
  case GTK_BUTTONBOX_EDGE:
  default:
    if (nvis_children >= 2)
      {
        childspacing = (height - (nvis_children*child_height)) / (nvis_children-1);
	y = allocation->y + gtk_container_get_border_width (GTK_CONTAINER (box));
	secondary_y = y + ((nvis_children - n_secondaries) * (child_height + childspacing));
      }
    else
      {
	/* one or zero children, just center */
	childspacing = height;
	y = secondary_y = allocation->y + (allocation->height - child_height) / 2;
      }
    break;
  case GTK_BUTTONBOX_START:
    childspacing = spacing;
    y = allocation->y + gtk_container_get_border_width (GTK_CONTAINER (box));
    secondary_y = allocation->y + allocation->height
      - child_height * n_secondaries
      - spacing * (n_secondaries - 1)
      - gtk_container_get_border_width (GTK_CONTAINER (box));
    break;
  case GTK_BUTTONBOX_END:
    childspacing = spacing;
    y = allocation->y + allocation->height
      - child_height * (nvis_children - n_secondaries)
      - spacing * (nvis_children - n_secondaries - 1)
      - gtk_container_get_border_width (GTK_CONTAINER (box));
    secondary_y = allocation->y + gtk_container_get_border_width (GTK_CONTAINER (box));
    break;
  }

  x = allocation->x + (allocation->width - child_width) / 2;
  childspace = child_height + childspacing;

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
	  child_allocation.x = x;

	  if (is_secondary)
	    {
	      child_allocation.y = secondary_y;
	      secondary_y += childspace;
	    }
	  else
	    {
	      child_allocation.y = y;
	      y += childspace;
	    }

	  gtk_widget_size_allocate (child, &child_allocation);
	}
    }
}

