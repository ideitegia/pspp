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


static void psppire_buttonbox_class_init          (PsppireButtonBoxClass *);
static void psppire_buttonbox_init                (PsppireButtonBox      *);


GType
psppire_button_box_get_type (void)
{
  static GType buttonbox_type = 0;

  if (!buttonbox_type)
    {
      static const GTypeInfo buttonbox_info =
      {
	sizeof (PsppireButtonBoxClass),
	NULL, /* base_init */
        NULL, /* base_finalize */
	(GClassInitFunc) psppire_buttonbox_class_init,
        NULL, /* class_finalize */
	NULL, /* class_data */
        sizeof (PsppireButtonBox),
	0,
	(GInstanceInitFunc) psppire_buttonbox_init,
      };

      buttonbox_type = g_type_register_static (GTK_TYPE_VBUTTON_BOX,
					    "PsppireButtonBox", &buttonbox_info, 0);
    }

  return buttonbox_type;
}

static void
psppire_buttonbox_class_init (PsppireButtonBoxClass *class)
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

  psppire_dialog_reload (dialog, data);
}


static void
psppire_buttonbox_init (PsppireButtonBox *buttonbox)
{
  GtkWidget *button ;

  button = gtk_button_new_from_stock (GTK_STOCK_OK);
  gtk_box_pack_start_defaults (GTK_BOX (buttonbox), button);
  g_signal_connect (button, "clicked", G_CALLBACK (ok_button_clicked), NULL);
  gtk_widget_show (button);

  button = gtk_button_new_with_mnemonic (_("_Paste"));
  g_signal_connect (button, "clicked", G_CALLBACK (paste_button_clicked),
		    NULL);
  gtk_box_pack_start_defaults (GTK_BOX (buttonbox), button);
  gtk_widget_show (button);

  button = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
  g_signal_connect (button, "clicked", G_CALLBACK (close_dialog), NULL);
  gtk_box_pack_start_defaults (GTK_BOX (buttonbox), button);
  gtk_widget_show (button);

  button = gtk_button_new_from_stock (GTK_STOCK_REFRESH);
  g_signal_connect (button, "clicked", G_CALLBACK (refresh_clicked), NULL);
  gtk_box_pack_start_defaults (GTK_BOX (buttonbox), button);
  gtk_widget_show (button);

  button = gtk_button_new_from_stock (GTK_STOCK_HELP);
  gtk_box_pack_start_defaults (GTK_BOX (buttonbox), button);
  gtk_widget_show (button);

  gtk_widget_show (GTK_WIDGET (buttonbox));
}


GtkWidget*
psppire_buttonbox_new (void)
{
  PsppireButtonBox *buttonbox ;

  buttonbox = g_object_new (psppire_button_box_get_type (), NULL);

  return GTK_WIDGET (buttonbox) ;
}

