/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010, 2011, 2012  Free Software Foundation

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
#include "psppire-dialog.h"
#include "psppire-buttonbox.h"
#include "psppire-selector.h"
#include <string.h>
#include "builder-wrapper.h"
#include "help-menu.h"

#include "psppire-window-base.h"

static void psppire_dialog_class_init          (PsppireDialogClass *);
static void psppire_dialog_init                (PsppireDialog      *);


enum  {DIALOG_REFRESH,
       RESPONSE,
       VALIDITY_CHANGED,
       DIALOG_HELP,
       n_SIGNALS};

static guint signals [n_SIGNALS];


static void psppire_dialog_buildable_init (GtkBuildableIface *iface);


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

      static const GInterfaceInfo buildable_info =
      {
	(GInterfaceInitFunc) psppire_dialog_buildable_init,
	NULL,
	NULL
      };

      dialog_type = g_type_register_static (PSPPIRE_TYPE_WINDOW_BASE,
					    "PsppireDialog", &dialog_info, 0);

      g_type_add_interface_static (dialog_type,
				   GTK_TYPE_BUILDABLE,
				   &buildable_info);
    }

  return dialog_type;
}



static GObjectClass     *parent_class = NULL;


/* Properties */
enum
{
  PROP_0,
  PROP_ORIENTATION,
  PROP_SLIDING
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
	if ( GTK_IS_VBOX (dialog->box) || GTK_VPANED (dialog->box))
	  g_value_set_enum (value, PSPPIRE_VERTICAL);
	else if ( GTK_IS_HBOX (dialog->box) || GTK_HPANED (dialog->box))
	  g_value_set_enum (value, PSPPIRE_HORIZONTAL);
	else if ( GTK_IS_TABLE (dialog->box))
	  g_value_set_enum (value, PSPPIRE_TABULAR);
      }
      break;
    case PROP_SLIDING:
      g_value_set_boolean (value, dialog->slidable);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
dialog_set_container (PsppireDialog *dialog)
{
  if ( dialog->box != NULL)
    {
      gtk_container_remove (GTK_CONTAINER (dialog), dialog->box);
    }

  switch (dialog->orientation)
    {
    case PSPPIRE_HORIZONTAL:
      if ( dialog->slidable)
	dialog->box = gtk_hpaned_new();
      else
	dialog->box = gtk_hbox_new (FALSE, 5);
      break;
    case PSPPIRE_VERTICAL:
      if ( dialog->slidable)
	dialog->box = gtk_vpaned_new();
      else
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

  gtk_widget_show_all (dialog->box);
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
    case PROP_SLIDING:
      dialog->slidable = g_value_get_boolean (value);
      break;
    case PROP_ORIENTATION:
      dialog->orientation = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };

  dialog_set_container (dialog);
}


static GParamSpec *orientation_spec ;

static void
psppire_dialog_class_init (PsppireDialogClass *class)
{
  GObjectClass *object_class = (GObjectClass *) class;

  GParamSpec *sliding_spec ;

  orientation_spec =
    g_param_spec_enum ("orientation",
		       "Orientation",
		       "Which way widgets are packed",
		       PSPPIRE_TYPE_ORIENTATION,
		       PSPPIRE_HORIZONTAL /* default value */,
		       G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE);

  sliding_spec =
    g_param_spec_boolean ("slidable",
			  "Slidable",
			  "Can the container be sized by the user",
			  FALSE,
			  G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE);

  object_class->set_property = psppire_dialog_set_property;
  object_class->get_property = psppire_dialog_get_property;

  g_object_class_install_property (object_class,
                                   PROP_ORIENTATION,
                                   orientation_spec);


  g_object_class_install_property (object_class,
                                   PROP_SLIDING,
                                   sliding_spec);

  signals [DIALOG_REFRESH] =
    g_signal_new ("refresh",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE,
		  0);


  signals [RESPONSE] =
    g_signal_new ("response",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);


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


  signals [DIALOG_HELP] =
    g_signal_new ("help",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__STRING,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_STRING);


  parent_class = g_type_class_peek_parent (class);
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
  dialog->contents_are_acceptable = NULL;
  dialog->acceptable_data = NULL;
  dialog->slidable = FALSE;

  g_value_init (&value, orientation_spec->value_type);
  g_param_value_set_default (orientation_spec, &value);

  gtk_window_set_type_hint (GTK_WINDOW (dialog),
	GDK_WINDOW_TYPE_HINT_DIALOG);

  g_value_unset (&value);

  g_signal_connect (dialog, "delete-event",
		    G_CALLBACK (delete_event_callback),
		    dialog);

  gtk_window_set_type_hint (GTK_WINDOW (dialog),
	GDK_WINDOW_TYPE_HINT_DIALOG);

  g_object_set (dialog, "icon-name", "pspp", NULL);
}


GtkWidget*
psppire_dialog_new (void)
{
  PsppireDialog *dialog ;

  dialog = g_object_new (psppire_dialog_get_type (),
			 NULL);

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


static void
remove_notify_handlers (PsppireDialog *dialog, GObject *sel)
{
  g_signal_handlers_disconnect_by_data (sel, dialog);
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

	  g_signal_connect (dialog, "destroy", G_CALLBACK (remove_notify_handlers),
			    model);
	}
      
      g_signal_connect_swapped (selection, "changed",
				G_CALLBACK (psppire_dialog_notify_change),
				dialog);

      while ((col = gtk_tree_view_get_column (tv, i++)))
	{
	  GList *renderers = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (col));
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
  gchar *title = NULL;
  g_object_get (dialog, "title", &title, NULL);

  if (title == NULL)
    g_warning ("PsppireDialog %s has no title", gtk_widget_get_name (GTK_WIDGET (dialog)));
  
  if ( dialog->contents_are_valid != NULL )
    gtk_container_foreach (GTK_CONTAINER (dialog->box),
			   connect_notify_signal,
			   dialog);

  dialog->loop = g_main_loop_new (NULL, FALSE);

  gtk_widget_show (GTK_WIDGET (dialog));

  if ( dialog->contents_are_valid != NULL)
    g_signal_emit (dialog, signals [VALIDITY_CHANGED], 0, FALSE);

  g_signal_emit (dialog, signals [DIALOG_REFRESH], 0);

  gdk_threads_leave ();
  g_main_loop_run (dialog->loop);
  gdk_threads_enter ();

  g_main_loop_unref (dialog->loop);

  g_signal_emit (dialog, signals [RESPONSE], 0, dialog->response);

  return dialog->response;
}


void
psppire_dialog_reload (PsppireDialog *dialog)
{
  g_signal_emit (dialog, signals [DIALOG_REFRESH], 0);
}


void
psppire_dialog_help (PsppireDialog *dialog)
{
  char *name = NULL;
  g_object_get (dialog, "name", &name, NULL);

  online_help (name);

  g_signal_emit (dialog, signals [DIALOG_HELP], 0, name);
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


/* Sets a predicate function that is checked after each change that the user
   makes to the dialog's state.  If the predicate function returns false, then
   "OK" and other buttons that accept the dialog's settings will be
   disabled. */
void
psppire_dialog_set_valid_predicate (PsppireDialog *dialog,
				    ContentsAreValid contents_are_valid,
				    gpointer data)
{
  dialog->contents_are_valid = contents_are_valid;
  dialog->validity_data = data;
}

/* Sets a predicate function that is called after "OK" or another button that
   accepts the dialog's settings is pushed.  If the predicate function returns
   false, then the button push is ignored.  (If the predicate function returns
   false, then it should take some action to notify the user why the contents
   are unacceptable, e.g. pop up a dialog box.)

   An accept predicate is preferred over a validity predicate when the reason
   why the dialog settings are unacceptable may not be obvious to the user, so
   that the user needs a helpful message to explain. */
void
psppire_dialog_set_accept_predicate (PsppireDialog *dialog,
                                     ContentsAreValid contents_are_acceptable,
                                     gpointer data)
{
  dialog->contents_are_acceptable = contents_are_acceptable;
  dialog->acceptable_data = data;
}

gboolean
psppire_dialog_is_acceptable (const PsppireDialog *dialog)
{
  return (dialog->contents_are_acceptable == NULL
          || dialog->contents_are_acceptable (dialog->acceptable_data));
}




static GObject *
get_internal_child    (GtkBuildable *buildable,
		       GtkBuilder *builder,
		       const gchar *childname)
{
  PsppireDialog *dialog = PSPPIRE_DIALOG (buildable);

  if ( 0 == strcmp (childname, "hbox"))
    return G_OBJECT (dialog->box);

  return NULL;
}



static void
psppire_dialog_buildable_init (GtkBuildableIface *iface)
{
  iface->get_internal_child = get_internal_child;
}
