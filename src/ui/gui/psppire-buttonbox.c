/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2007  Free Software Foundation

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
    02110-1301, USA. */


#include <config.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <gtk/gtksignal.h>
#include "psppire-buttonbox.h"
#include "psppire-dialog.h"

#include <gettext.h>

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void psppire_button_box_class_init          (PsppireButtonBoxClass *);
static void psppire_button_box_init                (PsppireButtonBox      *);


GType
psppire_button_box_get_type (void)
{
  static GType button_box_type = 0;

  if (!button_box_type)
    {
      static const GTypeInfo button_box_info =
      {
	sizeof (PsppireButtonBoxClass),
	NULL, /* base_init */
        NULL, /* base_finalize */
	(GClassInitFunc) psppire_button_box_class_init,
        NULL, /* class_finalize */
	NULL, /* class_data */
        sizeof (PsppireButtonBox),
	0,
	(GInstanceInitFunc) psppire_button_box_init,
      };

      button_box_type = g_type_register_static (GTK_TYPE_BUTTON_BOX,
					    "PsppireButtonBox", &button_box_info, 0);
    }

  return button_box_type;
}

static void
psppire_button_box_class_init (PsppireButtonBoxClass *class)
{
}

static void
close_dialog (GtkWidget *w, gpointer data)
{
  PsppireDialog *dialog;

  dialog = PSPPIRE_DIALOG (gtk_widget_get_toplevel (w));

  dialog->response = GTK_RESPONSE_CANCEL;

  psppire_dialog_close (dialog);
}

static void
ok_button_clicked (GtkWidget *w, gpointer data)
{
  PsppireDialog *dialog;

  dialog = PSPPIRE_DIALOG (gtk_widget_get_toplevel (w));

  dialog->response = GTK_RESPONSE_OK;

  psppire_dialog_close (dialog);
}


static void
paste_button_clicked (GtkWidget *w, gpointer data)
{
  PsppireDialog *dialog;

  dialog = PSPPIRE_DIALOG (gtk_widget_get_toplevel (w));

  dialog->response = PSPPIRE_RESPONSE_PASTE;

  psppire_dialog_close (dialog);
}


static void
refresh_clicked (GtkWidget *w, gpointer data)
{
  PsppireDialog *dialog;

  dialog = PSPPIRE_DIALOG (gtk_widget_get_toplevel (w));

  psppire_dialog_reload (dialog);
}


static void
psppire_button_box_init (PsppireButtonBox *button_box)
{
  GtkWidget *button ;

  button = gtk_button_new_from_stock (GTK_STOCK_OK);
  gtk_box_pack_start_defaults (GTK_BOX (button_box), button);
  g_signal_connect (button, "clicked", G_CALLBACK (ok_button_clicked), NULL);
  gtk_widget_show (button);

  button = gtk_button_new_with_mnemonic (_("_Paste"));
  g_signal_connect (button, "clicked", G_CALLBACK (paste_button_clicked),
		    NULL);
  gtk_box_pack_start_defaults (GTK_BOX (button_box), button);
  gtk_widget_show (button);

  button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
  g_signal_connect (button, "clicked", G_CALLBACK (close_dialog), NULL);
  gtk_box_pack_start_defaults (GTK_BOX (button_box), button);
  gtk_widget_show (button);

  button = gtk_button_new_from_stock (GTK_STOCK_REFRESH);
  g_signal_connect (button, "clicked", G_CALLBACK (refresh_clicked), NULL);
  gtk_box_pack_start_defaults (GTK_BOX (button_box), button);
  gtk_widget_show (button);

  button = gtk_button_new_from_stock (GTK_STOCK_HELP);
  gtk_box_pack_start_defaults (GTK_BOX (button_box), button);
  gtk_widget_show (button);

}


/* This function is lifted verbatim from the Gtk2.10.6 library */

void
_psppire_button_box_child_requisition (GtkWidget *widget,
				       int       *nvis_children,
				       int       *nvis_secondaries,
				       int       *width,
				       int       *height)
{
  GtkButtonBox *bbox;
  GtkBoxChild *child;
  GList *children;
  gint nchildren;
  gint nsecondaries;
  gint needed_width;
  gint needed_height;
  GtkRequisition child_requisition;
  gint ipad_w;
  gint ipad_h;
  gint width_default;
  gint height_default;
  gint ipad_x_default;
  gint ipad_y_default;

  gint child_min_width;
  gint child_min_height;
  gint ipad_x;
  gint ipad_y;

  g_return_if_fail (GTK_IS_BUTTON_BOX (widget));

  bbox = GTK_BUTTON_BOX (widget);

  gtk_widget_style_get (widget,
                        "child-min-width", &width_default,
                        "child-min-height", &height_default,
                        "child-internal-pad-x", &ipad_x_default,
                        "child-internal-pad-y", &ipad_y_default,
			NULL);

  child_min_width = bbox->child_min_width   != GTK_BUTTONBOX_DEFAULT
    ? bbox->child_min_width : width_default;
  child_min_height = bbox->child_min_height !=GTK_BUTTONBOX_DEFAULT
    ? bbox->child_min_height : height_default;
  ipad_x = bbox->child_ipad_x != GTK_BUTTONBOX_DEFAULT
    ? bbox->child_ipad_x : ipad_x_default;
  ipad_y = bbox->child_ipad_y != GTK_BUTTONBOX_DEFAULT
    ? bbox->child_ipad_y : ipad_y_default;

  nchildren = 0;
  nsecondaries = 0;
  children = GTK_BOX(bbox)->children;
  needed_width = child_min_width;
  needed_height = child_min_height;
  ipad_w = ipad_x * 2;
  ipad_h = ipad_y * 2;

  while (children)
    {
      child = children->data;
      children = children->next;

      if (GTK_WIDGET_VISIBLE (child->widget))
	{
	  nchildren += 1;
	  gtk_widget_size_request (child->widget, &child_requisition);

	  if (child_requisition.width + ipad_w > needed_width)
	    needed_width = child_requisition.width + ipad_w;
	  if (child_requisition.height + ipad_h > needed_height)
	    needed_height = child_requisition.height + ipad_h;
	  if (child->is_secondary)
	    nsecondaries++;
	}
    }

  if (nvis_children)
    *nvis_children = nchildren;
  if (nvis_secondaries)
    *nvis_secondaries = nsecondaries;
  if (width)
    *width = needed_width;
  if (height)
    *height = needed_height;
}
