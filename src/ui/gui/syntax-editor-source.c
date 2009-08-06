/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2009  Free Software Foundation

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

#include <libpspp/getl.h>
#include <libpspp/compiler.h>
#include <libpspp/str.h>

#include <stdlib.h>

#include <gtk/gtk.h>

#include "syntax-editor-source.h"
#include "psppire-syntax-window.h"

#include "xalloc.h"

struct syntax_editor_source
  {
    struct getl_interface parent;
    GtkTextBuffer *buffer;
    GtkTextIter i;
    GtkTextIter end;
    const gchar *name;
  };


static bool
always_false (const struct getl_interface *i UNUSED)
{
  return false;
}

/* Returns the name of the source */
static const char *
name (const struct getl_interface *i)
{
  const struct syntax_editor_source *ses = (const struct syntax_editor_source *) i;
  return ses->name;
}


/* Returns the location within the source */
static int
location (const struct getl_interface *i)
{
  const struct syntax_editor_source *ses = (const struct syntax_editor_source *) i;

  return gtk_text_iter_get_line (&ses->i);
}


static bool
read_line_from_buffer (struct getl_interface *i,
		       struct string *line)
{
  gchar *text;
  GtkTextIter next_line;

  struct syntax_editor_source *ses = (struct syntax_editor_source *) i;

  if ( gtk_text_iter_compare (&ses->i, &ses->end) >= 0)
    return false;

  next_line = ses->i;
  gtk_text_iter_forward_line (&next_line);

  text = gtk_text_buffer_get_text (ses->buffer,
				   &ses->i, &next_line,
				   FALSE);
  g_strchomp (text);

  ds_assign_cstr (line, text);

  g_free (text);

  gtk_text_iter_forward_line (&ses->i);

  return true;
}


static void
do_close (struct getl_interface *i )
{
  free (i);
}

struct getl_interface *
create_syntax_editor_source (GtkTextBuffer *buffer,
			     GtkTextIter start,
			     GtkTextIter stop,
			     const gchar *nm
			     )
{
  struct syntax_editor_source *ses = xzalloc (sizeof *ses);

  ses->buffer = buffer;
  ses->i = start;
  ses->end = stop;
  ses->name = nm;


  ses->parent.interactive = always_false;
  ses->parent.read = read_line_from_buffer;
  ses->parent.close = do_close;

  ses->parent.name = name;
  ses->parent.location = location;


  return &ses->parent;
}
