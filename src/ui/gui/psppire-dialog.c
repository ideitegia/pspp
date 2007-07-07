/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007  Free Software Foundation

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



/* Properties */
enum
{
  PROP_0,
  PROP_ORIENTATION
};


static void
psppire_dialog_get_property (GObject         *object,
			     guint            prop_id,
			     GValue          *value,
			     GParamSpec      *pspec)
{
  PsppireDialog *dialog = PSPPIRE_DIALOG (object);

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      {
	if ( GTK_IS_VBOX (dialog->box) )
	  g_value_set_enum (value, PSPPIRE_VERTICAL);
	else if ( GTK_IS_HBOX (dialog->box))
	  g_value_set_enum (value, PSPPIRE_HORIZONTAL);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
dialog_set_orientation (PsppireDialog *dialog, const GValue *orval)
{
  PsppireOrientation orientation = g_value_get_enum (orval);

  if ( dialog->box != NULL)
    {
      gtk_container_remove (GTK_CONTAINER (dialog), dialog->box);
    }

  if ( orientation == PSPPIRE_HORIZONTAL)
    {
      dialog->box = gtk_hbox_new (FALSE, 5);
    }
  else
    {
      dialog->box = gtk_vbox_new (FALSE, 5);
    }

  gtk_container_add (GTK_CONTAINER (dialog), dialog->box);
}


static void
psppire_dialog_set_property (GObject         *object,
			     guint            prop_id,
			     const GValue    *value,
			     GParamSpec      *pspec)

{
  PsppireDialog *dialog = PSPPIRE_DIALOG (object);

  switch (prop_id)
    {
    case PROP_ORIENTATION:
      dialog_set_orientation (dialog, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static GParamSpec *orientation_spec ;

static void
psppire_dialog_class_init (PsppireDialogClass *class)
{
  GObjectClass *object_class = (GObjectClass *) class;


  orientation_spec =
    g_param_spec_enum ("orientation",
		       "Orientation",
		       "Which way widgets are packed",
		       G_TYPE_PSPPIRE_ORIENTATION,
		       PSPPIRE_HORIZONTAL /* default value */,
		       G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE);


  object_class->set_property = psppire_dialog_set_property;
  object_class->get_property = psppire_dialog_get_property;

  g_object_class_install_property (object_class,
                                   PROP_ORIENTATION,
                                   orientation_spec);



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
  GValue value = {0};
  dialog->box = NULL;

  g_value_init (&value, orientation_spec->value_type);
  g_param_value_set_default (orientation_spec, &value);

  dialog_set_orientation (dialog, &value);

  g_value_unset (&value);

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

  g_signal_emit (dialog, signals [DIALOG_REFRESH], 0);

  g_main_loop_run (dialog->loop);

  return dialog->response;
}


void
psppire_dialog_reload (PsppireDialog *dialog)
{
  g_signal_emit (dialog, signals [DIALOG_REFRESH], 0);
}




GType
psppire_orientation_get_type (void)
{
  static GType etype = 0;
  if (etype == 0)
    {
      static const GEnumValue values[] =
	{
	  { PSPPIRE_HORIZONTAL, "PSPPIRE_HORIZONTAL", "Horizontal" },
	  { PSPPIRE_VERTICAL,   "PSPPIRE_VERTICAL",   "Vertical" },
	  { 0, NULL, NULL }
	};

      etype = g_enum_register_static
	(g_intern_static_string ("PsppireOrientation"), values);

    }
  return etype;
}
