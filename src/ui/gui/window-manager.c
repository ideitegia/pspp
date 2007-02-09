/*
    PSPPIRE --- A Graphical User Interface for PSPP
    Copyright (C) 2006, 2007  Free Software Foundation

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
#include "syntax-editor.h"
#include "data-editor.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


#include "window-manager.h"



/* A list of struct editor_windows */
static GSList *window_list = NULL;


static void
deregister_window (GtkWindow *w, gpointer data)
{
  struct editor_window *e = data;

  window_list = g_slist_remove (window_list, e);

  if ( g_slist_length (window_list) == 0 )
    gtk_main_quit ();
};


static void
register_window (struct editor_window *e)
{
  window_list = g_slist_prepend (window_list, e);
}


static gint
next_window_id (void)
{
  return g_slist_length (window_list);
}

void
minimise_all_windows (void)
{
  const GSList *i = NULL;

  for (i = window_list; i != NULL ; i = i->next)
    {
      struct editor_window *e = i->data;
      gtk_window_iconify (e->window);
    }
}

static void set_window_name (struct editor_window *e, const gchar *name );


struct editor_window *
window_create (enum window_type type, const gchar *name)
{
  struct editor_window *e;
  switch (type)
    {
    case WINDOW_SYNTAX:
      e = (struct editor_window *) new_syntax_editor ();
      break;
    case WINDOW_DATA:
      e = (struct editor_window *) new_data_editor ();
      break;
    default:
      g_assert_not_reached ();
    };

  e->type = type;
  e->name = NULL;

  set_window_name (e, name);


  gtk_window_set_icon_from_file (GTK_WINDOW (e->window),
				 PKGDATADIR "/psppicon.png", 0);

  g_signal_connect (e->window, "destroy",
		    G_CALLBACK (deregister_window), e);

  register_window (e);

  gtk_widget_show (GTK_WIDGET (e->window));

  return e;
}

void
default_window_name (struct editor_window *w)
{
  set_window_name (w, NULL);
}

static void
set_window_name (struct editor_window *e,
		 const gchar *name )
{
  gchar *title ;
  g_free (e->name);

  e->name = NULL;

  if ( name )
    {
      e->name =  g_strdup (name);
      return;
    }

  switch (e->type )
    {
    case WINDOW_SYNTAX:
      e->name = g_strdup_printf (_("Syntax%d"), next_window_id () );
      title = g_strdup_printf (_("%s --- PSPP Syntax Editor"), e->name);
      break;
    case WINDOW_DATA:
      e->name = g_strdup_printf (_("Untitled%d"), next_window_id () );
      title = g_strdup_printf (_("%s --- PSPP Data Editor"), e->name);
      break;
    default:
      g_assert_not_reached ();
    }

  gtk_window_set_title (GTK_WINDOW (e->window), title);

  g_free (title);
}


/* Set the name of this window based on FILENAME.
   FILENAME is in "filename encoding" */
void
window_set_name_from_filename (struct editor_window *e,
			       const gchar *filename)
{
  gchar *title;
  gchar *basename = g_path_get_basename (filename);

  set_window_name (e, filename);

  switch (e->type )
    {
    case WINDOW_SYNTAX:
      title = g_strdup_printf (_("%s --- PSPP Syntax Editor"), basename);
      break;
    case WINDOW_DATA:
      title = g_strdup_printf (_("%s --- PSPP Data Editor"), basename);
      break;
    default:
      g_assert_not_reached ();
    }
  g_free (basename);

  gtk_window_set_title (GTK_WINDOW (e->window), title);

  g_free (title);
}

const gchar *
window_name (const struct editor_window *e)
{
  return e->name;
}
