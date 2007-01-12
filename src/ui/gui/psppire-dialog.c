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


#include <gtk/gtk.h>
#include <gtk/gtksignal.h>
#include "psppire-dialog.h"

static void psppire_dialog_class_init          (PsppireDialogClass *);
static void psppire_dialog_init                (PsppireDialog      *);


enum  {DIALOG_REFRESH,
       n_SIGNALS};

static guint signals [n_SIGNALS];


GType
psppire_dialog_get_type (void)
{
  static GType dialog_type = 0;

  if (!dialog_type)
    {
      static const GTypeInfo dialog_info =
      {
	sizeof (PsppireDialogClass),
	NULL, /* base_init */
        NULL, /* base_finalize */
	(GClassInitFunc) psppire_dialog_class_init,
        NULL, /* class_finalize */
	NULL, /* class_data */
        sizeof (PsppireDialog),
	0,
	(GInstanceInitFunc) psppire_dialog_init,
      };

      dialog_type = g_type_register_static (GTK_TYPE_WINDOW,
					    "PsppireDialog", &dialog_info, 0);
    }

  return dialog_type;
}



static GObjectClass     *parent_class = NULL;


static void
psppire_dialog_finalize (GObject *object)
{
  PsppireDialog *dialog ;

  g_return_if_fail (object != NULL);
  g_return_if_fail (PSPPIRE_IS_DIALOG (object));

  dialog = PSPPIRE_DIALOG (object);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
psppire_dialog_class_init (PsppireDialogClass *class)
{
  GObjectClass *object_class = (GObjectClass *) class;

  signals [DIALOG_REFRESH] =
    g_signal_new ("refresh",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE,
		  0);


  object_class->finalize = psppire_dialog_finalize;
}




static void
close_dialog (GtkWidget *w, gpointer data)
{
  PsppireDialog *dialog = data;

  psppire_dialog_close (dialog);
}

void
psppire_dialog_close (PsppireDialog *dialog)
{
  g_main_loop_quit (dialog->loop);
  gtk_widget_hide (GTK_WIDGET (dialog));
}


static void
delete_event_callback (GtkWidget *w, GdkEvent *e, gpointer data)
{
  close_dialog (w, data);
}


static void
psppire_dialog_init (PsppireDialog *dialog)
{
  dialog->box = gtk_hbox_new (FALSE, 5);


  gtk_container_add (GTK_CONTAINER (dialog), dialog->box);


  g_signal_connect (G_OBJECT (dialog), "delete-event",
		    G_CALLBACK (delete_event_callback),
		    dialog);

  gtk_widget_show_all (dialog->box);
}


GtkWidget*
psppire_dialog_new (void)
{
  PsppireDialog *dialog ;

  dialog = g_object_new (psppire_dialog_get_type (), NULL);

  return GTK_WIDGET (dialog) ;
}

gint
psppire_dialog_run (PsppireDialog *dialog)
{
  dialog->loop = g_main_loop_new (NULL, FALSE);

  gtk_widget_show (GTK_WIDGET (dialog));

  g_main_loop_run (dialog->loop);

  return dialog->response;
}


void
psppire_dialog_reload (PsppireDialog *dialog, gpointer data)
{
  g_signal_emit (dialog, signals [DIALOG_REFRESH], 0, data);
}
