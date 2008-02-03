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
#include "psppire-buttonbox.h"
#include "psppire-selector.h"

static void psppire_dialog_class_init          (PsppireDialogClass *);
static void psppire_dialog_init                (PsppireDialog      *);


enum  {DIALOG_REFRESH,
       VALIDITY_CHANGED,
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
	else if ( GTK_IS_TABLE (dialog->box))
	  g_value_set_enum (value, PSPPIRE_TABULAR);
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

  switch ( orientation )
    {
    case PSPPIRE_HORIZONTAL:
      dialog->box = gtk_hbox_new (FALSE, 5);
      break;
    case PSPPIRE_VERTICAL:
      dialog->box = gtk_vbox_new (FALSE, 5);
      break;
    case PSPPIRE_TABULAR:
      dialog->box = gtk_table_new (2, 3, FALSE);
      g_object_set (dialog->box,
		    "row-spacing", 5,
		    "column-spacing", 5,
		    NULL);
      break;
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


  signals [VALIDITY_CHANGED] =
    g_signal_new ("validity-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__BOOLEAN,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_BOOLEAN);


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
  dialog->contents_are_valid = NULL;
  dialog->validity_data = NULL;

  g_value_init (&value, orientation_spec->value_type);
  g_param_value_set_default (orientation_spec, &value);

  gtk_window_set_type_hint (GTK_WINDOW (dialog),
	GDK_WINDOW_TYPE_HINT_DIALOG);

  dialog_set_orientation (dialog, &value);

  g_value_unset (&value);

  g_signal_connect (G_OBJECT (dialog), "delete-event",
		    G_CALLBACK (delete_event_callback),
		    dialog);

  gtk_window_set_type_hint (GTK_WINDOW (dialog),
	GDK_WINDOW_TYPE_HINT_DIALOG);

  gtk_widget_show_all (dialog->box);
}


GtkWidget*
psppire_dialog_new (void)
{
  PsppireDialog *dialog ;

  dialog = g_object_new (psppire_dialog_get_type (), NULL);

  return GTK_WIDGET (dialog) ;
}


void
psppire_dialog_notify_change (PsppireDialog *dialog)
{
  if ( dialog->contents_are_valid )
    {
      gboolean valid = dialog->contents_are_valid (dialog->validity_data);

      g_signal_emit (dialog, signals [VALIDITY_CHANGED], 0, valid);
    }
}


/* Descend the widget tree, connecting appropriate signals to the
   psppire_dialog_notify_change callback */
static void
connect_notify_signal (GtkWidget *w, gpointer data)
{
  PsppireDialog *dialog = data;

  if ( PSPPIRE_IS_BUTTONBOX (w))
    return;



  if ( GTK_IS_CONTAINER (w))
    {
      gtk_container_foreach (GTK_CONTAINER (w),
			     connect_notify_signal,
			     dialog);
    }


  /* It's unfortunate that GTK+ doesn't have a generic
     "user-modified-state-changed" signal.  Instead, we have to try and
     predict what widgets and signals are likely to exist in our dialogs. */

  if ( GTK_IS_TOGGLE_BUTTON (w))
    {
      g_signal_connect_swapped (w, "toggled",
				G_CALLBACK (psppire_dialog_notify_change),
				dialog);
    }

  if ( PSPPIRE_IS_SELECTOR (w))
    {
      g_signal_connect_swapped (w, "selected",
				G_CALLBACK (psppire_dialog_notify_change),
				dialog);

      g_signal_connect_swapped (w, "de-selected",
				G_CALLBACK (psppire_dialog_notify_change),
				dialog);
    }

  if ( GTK_IS_EDITABLE (w))
    {
      g_signal_connect_swapped (w, "changed",
				G_CALLBACK (psppire_dialog_notify_change),
				dialog);
    }

  if ( GTK_IS_CELL_EDITABLE (w))
    {
      g_signal_connect_swapped (w, "editing-done",
				G_CALLBACK (psppire_dialog_notify_change),
				dialog);
    }

  if ( GTK_IS_TEXT_VIEW (w))
    {
      GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (w));

      g_signal_connect_swapped (buffer, "changed",
				G_CALLBACK (psppire_dialog_notify_change),
				dialog);
    }

  if ( GTK_IS_TREE_VIEW (w))
    {
      gint i = 0;
      GtkTreeView *tv = GTK_TREE_VIEW (w);
      GtkTreeSelection *selection =
	gtk_tree_view_get_selection (tv);
      GtkTreeViewColumn *col;
      GtkTreeModel *model = gtk_tree_view_get_model (tv);

      if ( model)
	{
      g_signal_connect_swapped (model, "row-changed",
				G_CALLBACK (psppire_dialog_notify_change),
				dialog);

      g_signal_connect_swapped (model, "row-deleted",
				G_CALLBACK (psppire_dialog_notify_change),
				dialog);

      g_signal_connect_swapped (model, "row-inserted",
				G_CALLBACK (psppire_dialog_notify_change),
				dialog);
	}

      g_signal_connect_swapped (selection, "changed",
				G_CALLBACK (psppire_dialog_notify_change),
				dialog);

      while ((col = gtk_tree_view_get_column (tv, i++)))
	{
	  GList *renderers = gtk_tree_view_column_get_cell_renderers (col);
	  GList *start = renderers;
	  while (renderers)
	    {
	      if ( GTK_IS_CELL_RENDERER_TOGGLE (renderers->data))
		g_signal_connect_swapped (renderers->data, "toggled",
					  G_CALLBACK (psppire_dialog_notify_change), dialog);
	      renderers = renderers->next;
	    }
	  g_list_free (start);
	}
    }
}


gint
psppire_dialog_run (PsppireDialog *dialog)
{
  if ( dialog->contents_are_valid != NULL )
    gtk_container_foreach (GTK_CONTAINER (dialog->box),
			   connect_notify_signal,
			   dialog);

  dialog->loop = g_main_loop_new (NULL, FALSE);

  gtk_widget_show (GTK_WIDGET (dialog));

  if ( dialog->contents_are_valid != NULL)
    g_signal_emit (dialog, signals [VALIDITY_CHANGED], 0, FALSE);

  g_signal_emit (dialog, signals [DIALOG_REFRESH], 0);

  g_main_loop_run (dialog->loop);

  g_main_loop_unref (dialog->loop);

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
	  { PSPPIRE_TABULAR,   "PSPPIRE_TABULAR",   "Tabular" },
	  { 0, NULL, NULL }
	};

      etype = g_enum_register_static
	(g_intern_static_string ("PsppireOrientation"), values);

    }
  return etype;
}


void
psppire_dialog_set_valid_predicate (PsppireDialog *dialog,
				    ContentsAreValid contents_are_valid,
				    gpointer data)
{
  dialog->contents_are_valid = contents_are_valid;
  dialog->validity_data = data;
}


