/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009  Free Software Foundation

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

#include <gtk/gtkstock.h>
#include <gtk/gtkmessagedialog.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtkcheckmenuitem.h>
#include <gtk/gtkmain.h>

#include <stdlib.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include "psppire-window.h"
#include "psppire-window-register.h"
#include "psppire-conf.h"

static void psppire_window_base_finalize (PsppireWindowClass *, gpointer);
static void psppire_window_base_init     (PsppireWindowClass *class);
static void psppire_window_class_init    (PsppireWindowClass *class);
static void psppire_window_init          (PsppireWindow      *window);


static PsppireWindowClass *the_class;
static GObjectClass *parent_class;

GType
psppire_window_get_type (void)
{
  static GType psppire_window_type = 0;

  if (!psppire_window_type)
    {
      static const GTypeInfo psppire_window_info =
      {
	sizeof (PsppireWindowClass),
	(GBaseInitFunc) psppire_window_base_init,
        (GBaseFinalizeFunc) psppire_window_base_finalize,
	(GClassInitFunc) psppire_window_class_init,
	(GClassFinalizeFunc) NULL,
	NULL,
        sizeof (PsppireWindow),
	0,
	(GInstanceInitFunc) psppire_window_init,
      };

      psppire_window_type =
	g_type_register_static (GTK_TYPE_WINDOW, "PsppireWindow",
				&psppire_window_info, G_TYPE_FLAG_ABSTRACT);
    }

  return psppire_window_type;
}


/* Properties */
enum
{
  PROP_0,
  PROP_FILENAME,
  PROP_DESCRIPTION
};


gchar *
uniquify (const gchar *str, int *x)
{
  return g_strdup_printf ("%s%d", str, (*x)++);
}

static gchar mdash[6] = {0,0,0,0,0,0};

static void
psppire_window_set_title (PsppireWindow *window)
{
  GString *title = g_string_sized_new (80);

  g_string_printf (title, _("%s %s PSPPIRE %s"),
		   window->basename ? window->basename : "",
		   mdash, window->description);

  if ( window->unsaved)
    g_string_prepend_c (title, '*');

  gtk_window_set_title (GTK_WINDOW (window), title->str);

  g_string_free (title, TRUE);
}

static void
psppire_window_set_property (GObject         *object,
			     guint            prop_id,
			     const GValue    *value,
			     GParamSpec      *pspec)
{
  PsppireWindow *window = PSPPIRE_WINDOW (object);

  switch (prop_id)
    {
    case PROP_DESCRIPTION:
      window->description = g_value_dup_string (value);
      psppire_window_set_title (window);
      break;
    case PROP_FILENAME:
      {
	PsppireWindowRegister *reg = psppire_window_register_new ();

	gchar *candidate_name ;

	{
	  const gchar *name = g_value_get_string (value);
	  int x = 0;
	  GValue def = {0};
	  g_value_init (&def, pspec->value_type);

	  if ( NULL == name)
	    {
	      g_param_value_set_default (pspec, &def);
	      name = g_value_get_string (&def);
	    }

	  candidate_name = strdup (name);

	  while ( psppire_window_register_lookup (reg, candidate_name))
	    {
	      free (candidate_name);
	      candidate_name = uniquify (name, &x);
	    }

	  window->basename = g_path_get_basename (candidate_name);

	  g_value_unset (&def);
	}

	psppire_window_set_title (window);

	if ( window->name)
	  psppire_window_register_remove (reg, window->name);

	free (window->name);
	window->name = candidate_name;

	psppire_window_register_insert (reg, window, window->name);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_window_get_property (GObject         *object,
			     guint            prop_id,
			     GValue          *value,
			     GParamSpec      *pspec)
{
  PsppireWindow *window = PSPPIRE_WINDOW (object);

  switch (prop_id)
    {
    case PROP_FILENAME:
      g_value_set_string (value, window->name);
      break;
    case PROP_DESCRIPTION:
      g_value_set_string (value, window->description);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
on_realize (GtkWindow *window, gpointer data)
{
  PsppireConf *conf = psppire_conf_new ();

  const gchar *base = G_OBJECT_TYPE_NAME (window);

  psppire_conf_set_window_geometry (conf, base, window);
}


static gboolean
on_configure (GtkWidget *window, GdkEventConfigure *event, gpointer data)
{
  const gchar *base = G_OBJECT_TYPE_NAME (window);

  PsppireConf *conf = psppire_conf_new ();

  psppire_conf_save_window_geometry (conf, base, event);

  return FALSE;
}


static void
psppire_window_finalize (GObject *object)
{
  PsppireWindow *window = PSPPIRE_WINDOW (object);

  PsppireWindowRegister *reg = psppire_window_register_new ();

  psppire_window_register_remove (reg, window->name);
  free (window->name);
  free (window->description);

  g_signal_handler_disconnect (psppire_window_register_new (),
			       window->remove_handler);

  g_signal_handler_disconnect (psppire_window_register_new (),
			       window->insert_handler);

  g_hash_table_destroy (window->menuitem_table);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
psppire_window_class_init (PsppireWindowClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  GParamSpec *description_spec =
    g_param_spec_string ("description",
		       "Description",
		       "A string describing the usage of the window",
			 "??????", /*Should be overridden by derived classes */
		       G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  GParamSpec *filename_spec =
    g_param_spec_string ("filename",
		       "File name",
		       "The name of the file associated with this window, if any",
			 "Untitled",
			 G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  g_unichar_to_utf8 (0x2014, mdash);

  object_class->set_property = psppire_window_set_property;
  object_class->get_property = psppire_window_get_property;

  g_object_class_install_property (object_class,
                                   PROP_DESCRIPTION,
                                   description_spec);

  g_object_class_install_property (object_class,
                                   PROP_FILENAME,
                                   filename_spec);

  the_class = class;
  parent_class = g_type_class_peek_parent (class);
}


static void
psppire_window_base_init (PsppireWindowClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = psppire_window_finalize;
}



static void
psppire_window_base_finalize (PsppireWindowClass *class,
				gpointer class_data)
{
}

static void
menu_toggled (GtkCheckMenuItem *mi, gpointer data)
{
  /* Prohibit changes to the state */
  mi->active = !mi->active;
}


/* Look up the window associated with this menuitem and present it to the user */
static void
menu_activate (GtkMenuItem *mi, gpointer data)
{
  const gchar *key = data;

  PsppireWindowRegister *reg = psppire_window_register_new ();

  PsppireWindow *window = psppire_window_register_lookup (reg, key);

  gtk_window_present (GTK_WINDOW (window));
}

static void
insert_menuitem_into_menu (PsppireWindow *window, gpointer key)
{
  GtkWidget *item = gtk_check_menu_item_new_with_label (key);

  g_signal_connect (item, "toggled", G_CALLBACK (menu_toggled), NULL);
  g_signal_connect (item, "activate", G_CALLBACK (menu_activate), key);

  gtk_widget_show (item);

  gtk_menu_shell_append (window->menu, item);

  /* Set the state without emitting a signal */
  GTK_CHECK_MENU_ITEM (item)->active =
   (psppire_window_register_lookup (psppire_window_register_new (), key) == window);

  g_hash_table_insert (window->menuitem_table, key, item);
}

static void
insert_item (gpointer key, gpointer value, gpointer data)
{
  PsppireWindow *window = PSPPIRE_WINDOW (data);

  if ( NULL != g_hash_table_lookup (window->menuitem_table, key))
    return;

  insert_menuitem_into_menu (window, key);
}

/* Insert a new item into the window menu */
static void
insert_menuitem (GObject *reg, const gchar *key, gpointer data)
{
  PsppireWindow *window = PSPPIRE_WINDOW (data);
  
  insert_menuitem_into_menu (window, (gpointer) key);
}


static void
remove_menuitem (PsppireWindowRegister *reg, const gchar *key, gpointer data)
{
  PsppireWindow *window = PSPPIRE_WINDOW (data);
  GtkWidget *item ;

  item = g_hash_table_lookup (window->menuitem_table, key);

  g_hash_table_remove (window->menuitem_table, key);

  if (GTK_IS_CONTAINER (window->menu))
    gtk_container_remove (GTK_CONTAINER (window->menu), item);
}

static void
insert_existing_items (PsppireWindow *window)
{
  psppire_window_register_foreach (psppire_window_register_new (), insert_item, window);
}


static gboolean
on_delete (PsppireWindow *w, GdkEvent *event, gpointer user_data)
{
  PsppireWindowRegister *reg = psppire_window_register_new ();

  if ( w->unsaved )
    {
      gint response = psppire_window_query_save (w);

      if ( response == GTK_RESPONSE_CANCEL)
	return TRUE;

      if ( response == GTK_RESPONSE_ACCEPT)
	{
	  psppire_window_save (w);
	}
    }

  if ( 1 == psppire_window_register_n_items (reg))
    gtk_main_quit ();

  return FALSE;
}


static void
psppire_window_init (PsppireWindow *window)
{
  window->name = NULL;
  window->menu = NULL;

  window->menuitem_table  = g_hash_table_new (g_str_hash, g_str_equal);


  g_signal_connect (window,  "realize", G_CALLBACK (insert_existing_items), NULL);

  window->insert_handler = g_signal_connect (psppire_window_register_new (),
					     "inserted",
					     G_CALLBACK (insert_menuitem),
					     window);

  window->remove_handler = g_signal_connect (psppire_window_register_new (),
					     "removed",
					     G_CALLBACK (remove_menuitem),
					     window);

  window->unsaved = FALSE;

  g_signal_connect_swapped (window, "delete-event", G_CALLBACK (on_delete), window);

  g_object_set (window, "icon-name", "psppicon", NULL);

  g_signal_connect (window, "configure-event",
		    G_CALLBACK (on_configure), window);

  g_signal_connect (window, "realize",
		    G_CALLBACK (on_realize), window);

}


/* 
   Ask the user if the buffer should be saved.
   Return the response.
*/
gint
psppire_window_query_save (PsppireWindow *se)
{
  gint response;
  GtkWidget *dialog;

  const gchar *description;
  const gchar *filename = psppire_window_get_filename (se);

  g_object_get (se, "description", &description, NULL);

  g_return_val_if_fail (filename != NULL, GTK_RESPONSE_NONE);

  dialog =
    gtk_message_dialog_new (GTK_WINDOW (se),
			    GTK_DIALOG_MODAL,
			    GTK_MESSAGE_QUESTION,
			    GTK_BUTTONS_NONE,
			    _("Save contents of %s to \"%s\"?"),
			    description,
			    filename);

  gtk_dialog_add_button  (GTK_DIALOG (dialog),
			  GTK_STOCK_YES,
			  GTK_RESPONSE_ACCEPT);

  gtk_dialog_add_button  (GTK_DIALOG (dialog),
			  GTK_STOCK_NO,
			  GTK_RESPONSE_REJECT);

  gtk_dialog_add_button  (GTK_DIALOG (dialog),
			  GTK_STOCK_CANCEL,
			  GTK_RESPONSE_CANCEL);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);

  return response;
}


const gchar *
psppire_window_get_filename (PsppireWindow *w)
{
  const gchar *name = NULL;
  g_object_get (w, "filename", &name, NULL);
  return name;
}


void
psppire_window_set_filename (PsppireWindow *w, const gchar *filename)
{
  g_object_set (w, "filename", filename, NULL);
}

void
psppire_window_set_unsaved (PsppireWindow *w, gboolean unsaved)
{
  w->unsaved = unsaved;

  psppire_window_set_title (w);
}

gboolean
psppire_window_get_unsaved (PsppireWindow *w)
{
  return w->unsaved;
}





static void
minimise_window (gpointer key, gpointer value, gpointer data)
{
  gtk_window_iconify (GTK_WINDOW (value));
}


void
psppire_window_minimise_all (void)
{
  PsppireWindowRegister *reg = psppire_window_register_new ();

  g_hash_table_foreach (reg->name_table, minimise_window, NULL);
}




GType
psppire_window_model_get_type (void)
{
  static GType window_model_type = 0;

  if (! window_model_type)
    {
      static const GTypeInfo window_model_info =
      {
        sizeof (PsppireWindowIface), /* class_size */
	NULL,           /* base_init */
	NULL,		/* base_finalize */
	NULL,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	0,
	0,              /* n_preallocs */
	NULL
      };

      window_model_type =
	g_type_register_static (G_TYPE_INTERFACE, "PsppireWindowModel",
				&window_model_info, 0);

      g_type_interface_add_prerequisite (window_model_type, G_TYPE_OBJECT);
    }

  return window_model_type;
}


void
psppire_window_save (PsppireWindow *w)
{
  PsppireWindowIface *i = PSPPIRE_WINDOW_MODEL_GET_IFACE (w);

  g_assert (PSPPIRE_IS_WINDOW_MODEL (w));

  g_assert (i);

  g_return_if_fail (i->save);

  i->save (w);
}


/* Puts FILE_NAME into the recent list.
   If it's already in the list, it moves it to the top
*/
void
add_most_recent (const char *file_name, GtkRecentManager *rm)
{
  gchar *uri = g_filename_to_uri  (file_name, NULL, NULL);

  if ( uri )
    gtk_recent_manager_add_item (rm, uri);

  g_free (uri);
}



/* 
   If FILE_NAME exists in the recent list, then  delete it.
 */
void
delete_recent (const char *file_name, GtkRecentManager *rm)
{
  gchar *uri = g_filename_to_uri  (file_name, NULL, NULL);

  if ( uri )
    gtk_recent_manager_remove_item (rm, uri, NULL);

  g_free (uri);
  
}

