/* PSPP - a program for statistical analysis.
   Copyright (C) 2011 Free Software Foundation, Inc.

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

/* A lex_reader object to read characters directly from a GtkTextBuffer */

#include <config.h>

#include "psppire-lex-reader.h"
#include "src/language/lexer/lexer.h"

#include <gtk/gtk.h>


#include "libpspp/cast.h"

static const struct lex_reader_class lex_gtk_text_buffer_reader_class ;


struct lex_gtk_text_buffer_reader
{
  struct lex_reader reader;
  GtkTextBuffer *buffer;
  GtkTextIter start;
  GtkTextIter stop;
};

static struct lex_gtk_text_buffer_reader *
lex_gtk_text_buffer_reader_cast (struct lex_reader *r)
{
  return UP_CAST (r, struct lex_gtk_text_buffer_reader, reader);
}


struct lex_reader *
lex_reader_for_gtk_text_buffer (GtkTextBuffer *buffer, GtkTextIter start, GtkTextIter stop)
{
  struct lex_gtk_text_buffer_reader *r = xmalloc (sizeof *r);

  lex_reader_init (&r->reader, &lex_gtk_text_buffer_reader_class);

  r->buffer = buffer;
  g_object_ref (buffer);

  r->start = start;
  r->stop = stop;

  return &r->reader;
}


static size_t
lex_gtk_text_buffer_read (struct lex_reader *r_, char *buf, size_t n,
                 enum prompt_style prompt_style UNUSED)
{
  struct lex_gtk_text_buffer_reader *r = lex_gtk_text_buffer_reader_cast (r_);
  int n_chars = n;
  char *s;

  GtkTextIter iter = r->start ;
  
  int offset = gtk_text_iter_get_offset (&iter);
  int end_offset = gtk_text_iter_get_offset (&r->stop);

  if ( end_offset - offset < n)
    n_chars = end_offset - offset;
  
  gtk_text_iter_set_offset (&iter, offset + n_chars);

  s = gtk_text_iter_get_text (&r->start, &iter);

  strcpy (buf, s);

  r->start = iter;

  return strlen (s);
}



static void
lex_gtk_text_buffer_close (struct lex_reader *r_)
{
  struct lex_gtk_text_buffer_reader *r = lex_gtk_text_buffer_reader_cast (r_);

  g_object_unref (r->buffer);
}


static const struct lex_reader_class lex_gtk_text_buffer_reader_class =
  {
    lex_gtk_text_buffer_read,
    lex_gtk_text_buffer_close
  };
