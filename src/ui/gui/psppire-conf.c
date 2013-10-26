/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009, 2010  Free Software Foundation

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

/*
   This module provides an interface for simple user preference config
   parameters.
*/

#include <config.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#include <glib.h>

#include "psppire-conf.h"

static void psppire_conf_init            (PsppireConf      *conf);
static void psppire_conf_class_init      (PsppireConfClass *class);

static void psppire_conf_finalize        (GObject   *object);
static void psppire_conf_dispose        (GObject   *object);

static GObjectClass *parent_class = NULL;


GType
psppire_conf_get_type (void)
{
  static GType conf_type = 0;

  if (!conf_type)
    {
      static const GTypeInfo conf_info =
      {
	sizeof (PsppireConfClass),
	NULL,		/* base_init */
	NULL,		/* base_finalize */
        (GClassInitFunc) psppire_conf_class_init,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
        sizeof (PsppireConf),
	0,
        (GInstanceInitFunc) psppire_conf_init,
      };

      conf_type = g_type_register_static (G_TYPE_OBJECT,
						"PsppireConf",
						&conf_info, 0);
    }

  return conf_type;
}


static void
conf_read (PsppireConf *conf)
{
  g_key_file_load_from_file (conf->keyfile,
			     conf->filename,
			     G_KEY_FILE_KEEP_COMMENTS,
			     NULL);
}

static gboolean
flush_conf (PsppireConf *conf)
{
  gsize length = 0;

  gchar *kf = g_key_file_to_data  (conf->keyfile, &length, NULL);

  if ( ! g_file_set_contents (conf->filename, kf, length, NULL) )
    {
      g_warning ("Cannot open %s for writing", conf->filename);
    }

  g_free (kf);
  conf->idle = 0;
  return FALSE;
}

static void
conf_write (PsppireConf *conf)
{
  if ( conf->idle == 0)
    conf->idle = g_idle_add_full (G_PRIORITY_LOW, 
				  (GSourceFunc) flush_conf, conf, NULL);
}


static void
psppire_conf_dispose  (GObject *object)
{
}

static void
psppire_conf_finalize (GObject *object)
{
  PsppireConf *conf = PSPPIRE_CONF (object);
  g_key_file_free (conf->keyfile);
  g_free (conf->filename);
}


static PsppireConf *the_instance = NULL;

static GObject*
psppire_conf_construct   (GType                  type,
				     guint                  n_construct_params,
				     GObjectConstructParam *construct_params)
{
  GObject *object;

  if (!the_instance)
    {
      object = G_OBJECT_CLASS (parent_class)->constructor (type,
                                                           n_construct_params,
                                                           construct_params);
      the_instance = PSPPIRE_CONF (object);
    }
  else
    object = g_object_ref (G_OBJECT (the_instance));

  return object;
}

static void
psppire_conf_class_init (PsppireConfClass *class)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass*) class;

  object_class->finalize = psppire_conf_finalize;
  object_class->dispose = psppire_conf_dispose;
  object_class->constructor = psppire_conf_construct;

}


static void
psppire_conf_init (PsppireConf *conf)
{
  const gchar *dirname;
  struct stat s;

  /* Get the name of the directory for user configuration files, then, if it
     doesn't already exist, create it, since we might be the first program
     to want to put files there. */
  dirname = g_get_user_config_dir ();
  if (stat (dirname, &s) == -1 && errno == ENOENT)
    mkdir (dirname, 0700);

  conf->filename = g_strdup_printf ("%s/%s", dirname, "psppirerc");

  conf->keyfile = g_key_file_new ();

  conf->dispose_has_run = FALSE;
  conf->idle = 0;
}


PsppireConf *
psppire_conf_new (void)
{
  return g_object_new (psppire_conf_get_type (), NULL);
}



gboolean
psppire_conf_get_int (PsppireConf *conf, const gchar *base,
		      const gchar *name, gint *value)
{
  gboolean ok;
  GError *err = NULL;
  conf_read (conf);
  *value = g_key_file_get_integer (conf->keyfile,
				   base,
				   name, &err);

  ok = (err == NULL);
  if ( err != NULL )
    g_error_free (err);

  return ok;
}

gboolean
psppire_conf_get_boolean (PsppireConf *conf, const gchar *base,
			  const gchar *name, gboolean *value)
{
  gboolean ok;
  gboolean b;
  GError *err = NULL;
  conf_read (conf);
  b = g_key_file_get_boolean (conf->keyfile,
			      base,
			      name, &err);

  ok = (err == NULL);
  if ( err != NULL )
    g_error_free (err);

  if (ok)
    *value = b;

  return ok;
}



gboolean
psppire_conf_get_string (PsppireConf *conf, const gchar *base,
			 const gchar *name, gchar **value)
{
  gboolean ok;
  gchar *b;
  GError *err = NULL;
  conf_read (conf);
  b = g_key_file_get_string (conf->keyfile,
			     base,
			     name, &err);

  ok = (err == NULL);
  if ( err != NULL )
    g_error_free (err);

  if (ok)
    *value = b;

  return ok;
}



void
psppire_conf_set_int (PsppireConf *conf,
		      const gchar *base, const gchar *name,
		      gint value)
{
  g_key_file_set_integer (conf->keyfile, base, name, value);
  conf_write (conf);
}

void
psppire_conf_set_boolean (PsppireConf *conf,
			  const gchar *base, const gchar *name,
			  gboolean value)
{
  g_key_file_set_boolean (conf->keyfile, base, name, value);
  conf_write (conf);
}


void
psppire_conf_set_string (PsppireConf *conf,
			 const gchar *base, const gchar *name,
			 const gchar *value)
{
  g_key_file_set_string (conf->keyfile, base, name, value);
  conf_write (conf);
}



/*
  A convenience function to set the geometry of a
  window from from a saved config
*/
void
psppire_conf_set_window_geometry (PsppireConf *conf,
				  const gchar *base,
				  GtkWindow *window)
{
  gint height, width;
  gint x, y;
  gboolean maximize;

  if (psppire_conf_get_int (conf, base, "height", &height)
      &&
      psppire_conf_get_int (conf, base, "width", &width) )
    {
      gtk_window_set_default_size (window, width, height);
    }

  if ( psppire_conf_get_int (conf, base, "x", &x)
       &&
       psppire_conf_get_int (conf, base, "y", &y) )
    {
      gtk_window_move (window, x, y);
    }

  if ( psppire_conf_get_boolean (conf, base, "maximize", &maximize))
    {
      if (maximize)
	gtk_window_maximize (window);
      else
	gtk_window_unmaximize (window);
    }
}


/*
   A convenience function to save the window geometry.
   This should typically be called from a window's
   "configure-event" and "window-state-event" signal handlers
 */
void
psppire_conf_save_window_geometry (PsppireConf *conf,
				   const gchar *base,
				   GtkWindow *gtk_window)
{
  gboolean maximized;
  GdkWindow *w;

  w = gtk_widget_get_window (GTK_WIDGET (gtk_window));
  if (w == NULL)
    return;

  maximized = (gdk_window_get_state (w) & GDK_WINDOW_STATE_MAXIMIZED) != 0;
  psppire_conf_set_boolean (conf, base, "maximize", maximized);

  if (!maximized)
    {
      gint x, y;

      gint width = gdk_window_get_width (w);
      gint height= gdk_window_get_height (w);

      gdk_window_get_position (w, &x, &y);

      psppire_conf_set_int (conf, base, "height", height);
      psppire_conf_set_int (conf, base, "width", width);
      psppire_conf_set_int (conf, base, "x", x);
      psppire_conf_set_int (conf, base, "y", y);
    }
}
