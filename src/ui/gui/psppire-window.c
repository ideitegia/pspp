/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008  Free Software Foundation

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

#include <gtk/gtksignal.h>
#include <gtk/gtkwindow.h>

#include <stdlib.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#include "psppire-window.h"


static void psppire_window_base_finalize (PsppireWindowClass *, gpointer);
static void psppire_window_base_init     (PsppireWindowClass *class);
static void psppire_window_class_init    (PsppireWindowClass *class);
static void psppire_window_init          (PsppireWindow      *window);


static PsppireWindowClass *the_class;


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
	(GClassInitFunc)psppire_window_class_init,
	(GClassFinalizeFunc) NULL,
	NULL,
        sizeof (PsppireWindow),
	0,
	(GInstanceInitFunc) psppire_window_init,
      };

      psppire_window_type =
	g_type_register_static (GTK_TYPE_WINDOW, "PsppireWindow",
				&psppire_window_info, 0);
    }

  return psppire_window_type;
}


/* Properties */
enum
{
  PROP_0,
  PROP_FILENAME,
  PROP_USAGE
};


gchar *
uniquify (const gchar *str, int *x)
{
  return g_strdup_printf ("%s%d", str, (*x)++);
}



static void
psppire_window_set_property (GObject         *object,
			     guint            prop_id,
			     const GValue    *value,
			     GParamSpec      *pspec)
{
  PsppireWindow *window = PSPPIRE_WINDOW (object);

  PsppireWindowClass *class = PSPPIRE_WINDOW_CLASS (G_OBJECT_GET_CLASS (object));
  switch (prop_id)
    {
    case PROP_USAGE:
      window->usage = g_value_get_enum (value);
      break;
    case PROP_FILENAME:
      {
	gchar mdash[6] = {0,0,0,0,0,0};
	gchar *basename, *title;
	const gchar *name = g_value_get_string (value);
	gchar *candidate_name = strdup (name);
	int x = 0;

	while ( g_hash_table_lookup (class->name_table, candidate_name))
	  {
	    free (candidate_name);
	    candidate_name = uniquify (name, &x);
	  }

	basename = g_path_get_basename (candidate_name);
	g_unichar_to_utf8 (0x2014, mdash);

	switch (window->usage)
	  {
	  case PSPPIRE_WINDOW_USAGE_SYNTAX:
	    title = g_strdup_printf ( _("%s %s PSPPIRE Syntax Editor"),
				      basename, mdash);
	    break;
	  case PSPPIRE_WINDOW_USAGE_OUTPUT:
	    title = g_strdup_printf ( _("%s %s PSPPIRE Output"),
				      basename, mdash);
	  case PSPPIRE_WINDOW_USAGE_DATA:
	    title = g_strdup_printf ( _("%s %s PSPPIRE Data Editor"),
				      basename, mdash);
	    break;
	  default:
	    g_assert_not_reached ();
	    break;
	  }

	gtk_window_set_title (GTK_WINDOW (window), title);

	free (window->name);
	window->name = candidate_name;


	g_hash_table_insert (class->name_table, window->name, window);

	free (basename);
	free (title);
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
    case PROP_USAGE:
      g_value_set_enum (value, window->usage);
      break;
    case PROP_FILENAME:
      g_value_set_string (value, window->name);
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
  PsppireWindowClass *class = PSPPIRE_WINDOW_CLASS (G_OBJECT_GET_CLASS (object));

  GtkWindowClass *parent_class = g_type_class_peek_parent (class);

  if ( window->finalized )
    return;

  window->finalized = TRUE;

  g_debug ("%s %p", __FUNCTION__, object);

  g_hash_table_remove (class->name_table, window->name);
  free (window->name);

  if (G_OBJECT_CLASS (parent_class)->finalize)
     (*G_OBJECT_CLASS (parent_class)->finalize) (object);
}


static void
psppire_window_class_init (PsppireWindowClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  GParamSpec *use_class_spec =
    g_param_spec_enum ("usage",
		       "Usage",
		       "What the window is used for",
		       G_TYPE_PSPPIRE_WINDOW_USAGE,
		       PSPPIRE_WINDOW_USAGE_SYNTAX /* default value */,
		       G_PARAM_CONSTRUCT_ONLY |G_PARAM_READABLE | G_PARAM_WRITABLE);


  GParamSpec *filename_spec =
    g_param_spec_string ("filename",
		       "File name",
		       "The name of the file associated with this window, if any",
			 "Untitled",
			 G_PARAM_READABLE | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT
			 );


  object_class->set_property = psppire_window_set_property;
  object_class->get_property = psppire_window_get_property;

  g_object_class_install_property (object_class,
                                   PROP_FILENAME,
                                   filename_spec);

  g_object_class_install_property (object_class,
                                   PROP_USAGE,
                                   use_class_spec);


  class->name_table = g_hash_table_new (g_str_hash, g_str_equal);

  g_hash_table_insert (class->name_table, "Untitled", NULL);

  the_class = class;
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
  g_hash_table_destroy (class->name_table);
}



static void
psppire_window_init (PsppireWindow *window)
{
  window->name = NULL;
  window->finalized = FALSE;
}


GtkWidget*
psppire_window_new (PsppireWindowUsage usage)
{
  return GTK_WIDGET (g_object_new (psppire_window_get_type (),
				   "type", GTK_WINDOW_TOPLEVEL,
				   "usage", usage,
				   NULL));
}


const gchar *
psppire_window_get_filename (PsppireWindow *w)
{
  const gchar *name = NULL;
  g_object_get (w, "filename", name, NULL);
  return name;
}


void
psppire_window_set_filename (PsppireWindow *w, const gchar *filename)
{
  g_object_set (w, "filename", filename, NULL);
}


static void
minimise_all  (gpointer key,
	       gpointer value,
	       gpointer user_data)
{
  PsppireWindow *w = PSPPIRE_WINDOW (value);

  gtk_window_iconify (w);
}



void
psppire_window_minimise_all (void)
{
  g_hash_table_foreach (the_class->name_table, minimise_all, NULL);
}





GType
psppire_window_usage_get_type (void)
{
  static GType etype = 0;
  if (etype == 0)
    {
      static const GEnumValue values[] = {
	{ PSPPIRE_WINDOW_USAGE_SYNTAX, "PSPPIRE_WINDOW_USAGE_SYNTAX",
	  "Syntax" },

	{ PSPPIRE_WINDOW_USAGE_OUTPUT, "PSPPIRE_WINDOW_USAGE_OUTPUT",
	  "Output" },

	{ PSPPIRE_WINDOW_USAGE_DATA,   "PSPPIRE_WINDOW_USAGE_DATA",
	  "Data" },

	{ 0, NULL, NULL }
      };

      etype = g_enum_register_static
	(g_intern_static_string ("PsppireWindowUsage"), values);
    }

  return etype;
}
