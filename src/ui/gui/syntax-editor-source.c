/*
  PSPPIRE --- A Graphical User Interface for PSPP
  Copyright (C) 2006  Free Software Foundation

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

#include <libpspp/getl.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <libpspp/str.h>

#include <stdlib.h>

#include <gtk/gtk.h>

#include "syntax-editor-source.h"
#include "syntax-editor.h"

struct syntax_editor_source
  {
    struct getl_interface parent;
    const struct syntax_editor *se;
    GtkTextIter i;
    GtkTextIter end;
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
  const struct syntax_editor_source *ses =
    (const struct syntax_editor_source *) i;

  return window_name ((const struct editor_window *) ses->se);
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
		       struct string *line,
		       enum getl_syntax *syntax_rules)
{
  gchar *text;
  GtkTextIter next_line;

  struct syntax_editor_source *ses = (struct syntax_editor_source *) i;

  if ( gtk_text_iter_compare (&ses->i, &ses->end) >= 0)
    return false;

  next_line = ses->i;
  gtk_text_iter_forward_line (&next_line);

  text = gtk_text_buffer_get_text (ses->se->buffer,
				   &ses->i, &next_line,
				   FALSE);
  g_strchomp (text);

  ds_assign_cstr (line, text);

  g_free (text);

  gtk_text_iter_forward_line (&ses->i);

  return true;
}


static void
close (struct getl_interface *i )
{
  free (i);
}

struct getl_interface *
create_syntax_editor_source (const struct syntax_editor *se,
			     GtkTextIter start,
			     GtkTextIter stop
			     )
{
  struct syntax_editor_source *ses = xzalloc (sizeof *ses);

  ses->se = se;
  ses->i = start;
  ses->end = stop;


  ses->parent.interactive = always_false;
  ses->parent.read = read_line_from_buffer;
  ses->parent.close = close;

  ses->parent.name = name;
  ses->parent.location = location;


  return (struct getl_interface *) ses;
}
