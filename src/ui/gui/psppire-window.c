/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009, 2010, 2011, 2013  Free Software Foundation

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

#include "psppire-window.h"
#include "psppire-window-base.h"

#include <gtk/gtk.h>

#include <stdlib.h>
#include <xalloc.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include "data/any-reader.h"
#include "data/file-name.h"
#include "data/dataset.h"

#include "helper.h"
#include "psppire-data-window.h"
#include "psppire-encoding-selector.h"
#include "psppire-syntax-window.h"
#include "psppire-window-register.h"
#include "psppire.h"

static void psppire_window_base_init     (PsppireWindowClass *class);
static void psppire_window_class_init    (PsppireWindowClass *class);
static void psppire_window_init          (PsppireWindow      *window);


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
        (GBaseFinalizeFunc) NULL,
	(GClassInitFunc) psppire_window_class_init,
	(GClassFinalizeFunc) NULL,
	NULL,
        sizeof (PsppireWindow),
	0,
	(GInstanceInitFunc) psppire_window_init,
      };

      psppire_window_type =
	g_type_register_static (PSPPIRE_TYPE_WINDOW_BASE, "PsppireWindow",
				&psppire_window_info, G_TYPE_FLAG_ABSTRACT);
    }

  return psppire_window_type;
}


/* Properties */
enum
{
  PROP_0,
  PROP_FILENAME,
  PROP_DESCRIPTION,
  PROP_ID
};


static void
psppire_window_set_title (PsppireWindow *window)
{
  GString *title = g_string_sized_new (80);

  if (window->dirty)
    g_string_append_c (title, '*');

  if (window->basename || window->id)
    {
      if (window->basename)
        g_string_append_printf (title, "%s ", window->basename);

      if (window->id != '\0')
        g_string_append_printf (title, "[%s] ", window->id);

      g_string_append_unichar (title, 0x2014); /* em dash */
      g_string_append_c (title, ' '); /* em dash */
    }

  g_string_append_printf (title, "PSPPIRE %s", window->description);

  gtk_window_set_title (GTK_WINDOW (window), title->str);

  g_string_free (title, TRUE);
}

static void
psppire_window_update_list_name (PsppireWindow *window)
{
  PsppireWindowRegister *reg = psppire_window_register_new ();
  GString *candidate = g_string_sized_new (80);
  int n;

  n = 1;
  do
    {
      /* Compose a name. */
      g_string_truncate (candidate, 0);
      if (window->filename)
        {
          gchar *display_filename = g_filename_display_name (window->filename);
          g_string_append (candidate, display_filename);
          g_free (display_filename);

          if (window->id)
            g_string_append_printf (candidate, " [%s]", window->id);
        }
      else if (window->id)
        g_string_append_printf (candidate, "[%s]", window->id);
      else
        g_string_append (candidate, window->description);

      if (n++ > 1)
        g_string_append_printf (candidate, " #%d", n);

      if (window->list_name && !strcmp (candidate->str, window->list_name))
        {
          /* Keep the existing name. */
          g_string_free (candidate, TRUE);
          return;
        }
    }
  while (psppire_window_register_lookup (reg, candidate->str));

  if (window->list_name)
    psppire_window_register_remove (reg, window->list_name);

  g_free (window->list_name);
  window->list_name = g_string_free (candidate, FALSE);

  psppire_window_register_insert (reg, window, window->list_name);
}

static void
psppire_window_name_changed (PsppireWindow *window)
{
  psppire_window_set_title (window);
  psppire_window_update_list_name (window);
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
      g_free (window->description);
      window->description = g_value_dup_string (value);
      psppire_window_set_title (window);
      break;
    case PROP_FILENAME:
      g_free (window->filename);
      window->filename = g_value_dup_string (value);
      g_free (window->basename);
      window->basename = (window->filename
                          ? g_filename_display_basename (window->filename)
                          : NULL);
      psppire_window_name_changed (window);
      break;
      break;
    case PROP_ID:
      g_free (window->id);
      window->id = g_value_dup_string (value);
      psppire_window_name_changed (window);
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
      g_value_set_string (value, window->filename);
      break;
    case PROP_DESCRIPTION:
      g_value_set_string (value, window->description);
      break;
    case PROP_ID:
      g_value_set_string (value, window->id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_window_finalize (GObject *object)
{
  PsppireWindow *window = PSPPIRE_WINDOW (object);

  PsppireWindowRegister *reg = psppire_window_register_new ();

  g_signal_handler_disconnect (reg, window->remove_handler);
  g_signal_handler_disconnect (reg, window->insert_handler);
  psppire_window_register_remove (reg, window->list_name);
  g_free (window->filename);
  g_free (window->basename);
  g_free (window->id);
  g_free (window->description);
  g_free (window->list_name);

  g_hash_table_destroy (window->menuitem_table);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
psppire_window_class_init (PsppireWindowClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  GParamSpec *description_spec =
    null_if_empty_param ("description",
		       "Description",
		       "A string describing the usage of the window",
			 NULL, /*Should be overridden by derived classes */
		       G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

  GParamSpec *filename_spec =
    null_if_empty_param ("filename",
		       "File name",
		       "The name of the file associated with this window, if any",
			 NULL,
			 G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  GParamSpec *id_spec =
    null_if_empty_param ("id",
                         "Identifier",
                         "The PSPP language identifier for the data associated "
                         "with this window (e.g. dataset name)",
			 NULL,
			 G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  object_class->set_property = psppire_window_set_property;
  object_class->get_property = psppire_window_get_property;

  g_object_class_install_property (object_class,
                                   PROP_DESCRIPTION,
                                   description_spec);

  g_object_class_install_property (object_class,
                                   PROP_FILENAME,
                                   filename_spec);

  g_object_class_install_property (object_class,
                                   PROP_ID,
                                   id_spec);

  parent_class = g_type_class_peek_parent (class);
}


static void
psppire_window_base_init (PsppireWindowClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = psppire_window_finalize;
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
  gchar *filename;
  GtkWidget *item;

  /* Add a separator before adding the first real item.  If we add a separator
     at any other time, sometimes GtkUIManager removes it. */
  if (!window->added_separator)
    {
      GtkWidget *separator = gtk_separator_menu_item_new ();
      gtk_widget_show (separator);
      gtk_menu_shell_append (window->menu, separator);
      window->added_separator = TRUE;
    }

  filename = g_filename_display_name (key);
  item = gtk_check_menu_item_new_with_label (filename);
  g_free (filename);

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

  if ( w->dirty )
    {
      gint response = psppire_window_query_save (w);

      switch (response)
	{
	default:
	case GTK_RESPONSE_CANCEL:
	  return TRUE;
	  break;
	case GTK_RESPONSE_APPLY:
	  psppire_window_save (w);
          if (w->dirty)
            {
              /* Save failed, or user exited Save As dialog with Cancel. */
              return TRUE;
            }
	  break;
	case GTK_RESPONSE_REJECT:
	  break;
	}
    }

  if ( 1 == psppire_window_register_n_items (reg))
    gtk_main_quit ();

  return FALSE;
}


static void
psppire_window_init (PsppireWindow *window)
{
  window->menu = NULL;
  window->filename = NULL;
  window->basename = NULL;
  window->id = NULL;
  window->description = NULL;
  window->list_name = NULL;

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

  window->added_separator = FALSE;
  window->dirty = FALSE;

  g_signal_connect_swapped (window, "delete-event", G_CALLBACK (on_delete), window);

  g_object_set (window, "icon-name", "pspp", NULL);
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
  GtkWidget *cancel_button;

  gchar *description;

  GTimeVal time;

  g_get_current_time (&time);

  if (se->filename)
    description = g_filename_display_basename (se->filename);
  else if (se->id)
    description = g_strdup (se->id);
  else
    description = g_strdup (se->description);
  dialog =
    gtk_message_dialog_new (GTK_WINDOW (se),
			    GTK_DIALOG_MODAL,
			    GTK_MESSAGE_WARNING,
			    GTK_BUTTONS_NONE,
			    _("Save the changes to `%s' before closing?"),
			    description);
  g_free (description);

  g_object_set (dialog, "icon-name", "pspp", NULL);

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
					    _("If you don't save, changes from the last %ld seconds will be permanently lost."),
					    time.tv_sec - se->savetime.tv_sec);

  gtk_dialog_add_button  (GTK_DIALOG (dialog),
			  _("Close _without saving"),
			  GTK_RESPONSE_REJECT);

  cancel_button = gtk_dialog_add_button  (GTK_DIALOG (dialog),
					  GTK_STOCK_CANCEL,
					  GTK_RESPONSE_CANCEL);

  gtk_dialog_add_button  (GTK_DIALOG (dialog),
			  GTK_STOCK_SAVE,
			  GTK_RESPONSE_APPLY);

  gtk_widget_grab_focus (cancel_button);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);

  return response;
}


/* The return value is encoded in the glib filename encoding. */
const gchar *
psppire_window_get_filename (PsppireWindow *w)
{
  return w->filename;
}


/* FILENAME must be encoded in the glib filename encoding. */
void
psppire_window_set_filename (PsppireWindow *w, const gchar *filename)
{
  g_object_set (w, "filename", filename, NULL);
}

void
psppire_window_set_unsaved (PsppireWindow *w)
{
  if ( w->dirty == FALSE)
    g_get_current_time (&w->savetime);

  w->dirty = TRUE;

  psppire_window_set_title (w);
}

gboolean
psppire_window_get_unsaved (PsppireWindow *w)
{
  return w->dirty;
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

  g_assert (i);
  g_return_if_fail (i->save);

  if (w->filename == NULL)
    psppire_window_save_as (w);
  else
    {
      i->save (w);
      w->dirty = FALSE;
      psppire_window_set_title (w);
    }
}

void
psppire_window_save_as (PsppireWindow *w)
{
  PsppireWindowIface *i = PSPPIRE_WINDOW_MODEL_GET_IFACE (w);
  gchar *old_filename;

  g_assert (i);
  g_return_if_fail (i->pick_filename);

  old_filename = w->filename;
  w->filename = NULL;

  i->pick_filename (w);
  if (w->filename == NULL)
    w->filename = old_filename;
  else
    {
      g_free (old_filename);
      psppire_window_save (w);
    }
}

static void delete_recent (const char *file_name);

gboolean
psppire_window_load (PsppireWindow *w, const gchar *file, gpointer hint)
{
  gboolean ok;
  PsppireWindowIface *i = PSPPIRE_WINDOW_MODEL_GET_IFACE (w);

  g_assert (PSPPIRE_IS_WINDOW_MODEL (w));

  g_assert (i);

  g_return_val_if_fail (i->load, FALSE);

  ok = i->load (w, file, hint);

  if ( ok )
    {
      psppire_window_set_filename (w, file);
      w->dirty = FALSE;
    }
  else
    delete_recent (file);

  return ok;
}


static void
on_selection_changed (GtkFileChooser *chooser, GtkWidget *encoding_selector)
{
  const gchar *sysname;

  const gchar *name = gtk_file_chooser_get_filename (chooser);

  if ( NULL == name )
    return;

  sysname = convert_glib_filename_to_system_filename (name, NULL);

  if ( ! fn_exists (sysname))
    {
      gtk_widget_set_sensitive (encoding_selector, FALSE);
      return;
    }

  gtk_widget_set_sensitive (encoding_selector, ANY_NO == any_reader_may_open (sysname));
}

GtkWidget *
psppire_window_file_chooser_dialog (PsppireWindow *toplevel)
{
  GtkFileFilter *filter = gtk_file_filter_new ();
  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Open"),
				 GTK_WINDOW (toplevel),
				 GTK_FILE_CHOOSER_ACTION_OPEN,
				 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				 GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				 NULL);

  g_object_set (dialog, "local-only", FALSE, NULL);

  gtk_file_filter_set_name (filter, _("Data and Syntax Files"));
  gtk_file_filter_add_mime_type (filter, "application/x-spss-sav");
  gtk_file_filter_add_mime_type (filter, "application/x-spss-por");
  gtk_file_filter_add_pattern (filter, "*.zsav");
  gtk_file_filter_add_pattern (filter, "*.sps");
  gtk_file_filter_add_pattern (filter, "*.SPS");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("System Files (*.sav, *.zsav)"));
  gtk_file_filter_add_mime_type (filter, "application/x-spss-sav");
  gtk_file_filter_add_pattern (filter, "*.zsav");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Portable Files (*.por) "));
  gtk_file_filter_add_mime_type (filter, "application/x-spss-por");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Syntax Files (*.sps) "));
  gtk_file_filter_add_pattern (filter, "*.sps");
  gtk_file_filter_add_pattern (filter, "*.SPS");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  if (toplevel->filename)
    {
      const gchar *filename = toplevel->filename;
      gchar *dir_name;

      if ( ! g_path_is_absolute (filename))
        {
          gchar *path =
            g_build_filename (g_get_current_dir (), filename, NULL);
          dir_name = g_path_get_dirname (path);
          g_free (path);
        }
      else
        {
          dir_name = g_path_get_dirname (filename);
        }
      gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog),
                                           dir_name);
      free (dir_name);
    }


  {
    GtkWidget *encoding_selector =  psppire_encoding_selector_new ("Auto", true);

    gtk_widget_set_sensitive (encoding_selector, FALSE);

    g_signal_connect (dialog, "selection-changed", G_CALLBACK (on_selection_changed),
		      encoding_selector);

    gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (dialog), encoding_selector);
  }

  return dialog;
}

/* Callback for the file_open action.
   Prompts for a filename and opens it */
void
psppire_window_open (PsppireWindow *de)
{
  GtkWidget *dialog = psppire_window_file_chooser_dialog (de);

  switch (gtk_dialog_run (GTK_DIALOG (dialog)))
    {
    case GTK_RESPONSE_ACCEPT:
      {
	gchar *name =
	  gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));

	gchar *sysname = convert_glib_filename_to_system_filename (name, NULL);

        gchar *encoding = psppire_encoding_selector_get_encoding (
          gtk_file_chooser_get_extra_widget (GTK_FILE_CHOOSER (dialog)));

	enum detect_result res = any_reader_may_open (sysname);
	if (ANY_YES == res)
          open_data_window (de, name, NULL);
	else if (ANY_NO == res)
	  open_syntax_window (name, encoding);

        g_free (encoding);
	g_free (sysname);
	g_free (name);
      }
      break;
    default:
      break;
    }

  gtk_widget_destroy (dialog);
}


/* Puts FILE_NAME (encoded in the glib file name encoding) into the recent list
   with associated MIME_TYPE.  If it's already in the list, it moves it to the
   top. */
void
add_most_recent (const char *file_name, const char *mime_type)
{
  gchar *uri = g_filename_to_uri  (file_name, NULL, NULL);

  if ( uri )
    {
      GtkRecentData recent_data;

      recent_data.display_name = NULL;
      recent_data.description = NULL;
      recent_data.mime_type = CONST_CAST (gchar *, mime_type);
      recent_data.app_name = CONST_CAST (gchar *, g_get_application_name ());
      recent_data.app_exec = g_strjoin (" ", g_get_prgname (), "%u", NULL);
      recent_data.groups = NULL;
      recent_data.is_private = FALSE;

      gtk_recent_manager_add_full (gtk_recent_manager_get_default (),
                                   uri, &recent_data);

      g_free (recent_data.app_exec);
    }

  g_free (uri);
}



/*
   If FILE_NAME exists in the recent list, then  delete it.
 */
static void
delete_recent (const char *file_name)
{
  gchar *uri = g_filename_to_uri  (file_name, NULL, NULL);

  if ( uri )
    gtk_recent_manager_remove_item (gtk_recent_manager_get_default (), uri, NULL);

  g_free (uri);
}

