/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <stdlib.h>
#include <config.h>

#include "getl.h"

#include <libpspp/str.h>
#include <libpspp/ll.h>
#include <libpspp/version.h>
#include <libpspp/alloc.h>

struct getl_source
  {
    struct getl_source *included_from;	/* File that this is nested inside. */
    struct getl_source *includes;	/* File nested inside this file. */
    
    struct ll  ll;   /* Element in the sources list */

    struct getl_interface *interface;
  };

struct source_stream 
  {
    struct ll_list sources ;  /* List of source files. */

    struct string the_include_path;
  };

const char *
getl_include_path (const struct source_stream *ss)
{
  return ds_cstr (&ss->the_include_path);
}

static struct getl_source *
current_source (const struct source_stream *ss)
{
  const struct ll *ll = ll_head (&ss->sources);
  return ll_data (ll, struct getl_source, ll );
}

/* Initialize getl. */
struct source_stream *
create_source_stream (const char *initial_include_path)
{
  struct source_stream *ss = xzalloc (sizeof (*ss));
  ll_init (&ss->sources);
#if 0
  ds_init_cstr (&ss->the_include_path,
                fn_getenv_default ("STAT_INCLUDE_PATH", include_path));
#endif
  ds_init_cstr (&ss->the_include_path, initial_include_path);

  return ss;
}

/* Delete everything from the include path. */
void
getl_clear_include_path (struct source_stream *ss)
{
  ds_clear (&ss->the_include_path);
}

/* Add to the include path. */
void
getl_add_include_dir (struct source_stream *ss, const char *path)
{
  if (ds_length (&ss->the_include_path))
    ds_put_char (&ss->the_include_path, ':');

  ds_put_cstr (&ss->the_include_path, path);
}

/* Appends source S to the list of source files. */
void
getl_append_source (struct source_stream *ss, struct getl_interface *i) 
{
  struct getl_source *s = xzalloc (sizeof ( struct getl_source ));

  s->interface = i ;

  ll_push_head (&ss->sources, &s->ll);
}

/* Nests source S within the current source file. */
void
getl_include_source (struct source_stream *ss, struct getl_interface *i) 
{
  struct getl_source *current = current_source (ss);
  struct getl_source *s = xzalloc (sizeof ( struct getl_source ));

  s->interface = i;

  s->included_from = current ;
  s->includes  = NULL;
  current->includes = s;

  ll_push_head (&ss->sources, &s->ll);
}

/* Closes the current source, and move  the current source to the 
   next file in the chain. */
static void
close_source (struct source_stream *ss)
{
  struct getl_source *s = current_source (ss);

  if ( s->interface->close ) 
    s->interface->close (s->interface);

  ll_pop_head (&ss->sources);

  if (s->included_from != NULL)
    current_source (ss)->includes = NULL;

  free (s);
}

/* Closes all sources until an interactive source is
   encountered. */
void
getl_abort_noninteractive (struct source_stream *ss) 
{
  while ( ! ll_is_empty (&ss->sources))
    {
      const struct getl_source *s = current_source (ss);
      
      if ( !s->interface->interactive (s->interface) ) 
	close_source (ss);
    }
}

/* Returns true if the current source is interactive,
   false otherwise. */
bool
getl_is_interactive (const struct source_stream *ss) 
{
  const struct getl_source *s = current_source (ss);

  if (ll_is_empty (&ss->sources) ) 
    return false;

  return s->interface->interactive (s->interface);
}

/* Returns the name of the current source, or NULL if there is no 
   current source */
const char *
getl_source_name (const struct source_stream *ss)
{
  const struct getl_source *s = current_source (ss);

  if ( ll_is_empty (&ss->sources) )
    return NULL;

  if ( ! s->interface->name ) 
    return NULL;

  return s->interface->name (s->interface);
}

/* Returns the location within the current source, or -1 if there is
   no current source */
int
getl_source_location (const struct source_stream *ss)
{
  const struct getl_source *s = current_source (ss);

  if ( ll_is_empty (&ss->sources) )
    return -1;

  if ( !s->interface->location )
    return -1;

  return s->interface->location (s->interface);
}


/* Close getl. */
void
destroy_source_stream (struct source_stream *ss)
{
  while ( !ll_is_empty (&ss->sources))
    close_source (ss);
  ds_destroy (&ss->the_include_path);

  free (ss);
}


/* Reads a single line into LINE.
   Returns true when a line has been read, false at end of input.
   On success, sets *SYNTAX to the style of the syntax read. */
bool
getl_read_line (struct source_stream *ss, struct string *line,
		enum getl_syntax *syntax)
{
  while (!ll_is_empty (&ss->sources))
    {
      struct getl_source *s = current_source (ss);

      ds_clear (line);
      if (s->interface->read (s->interface, line, syntax))
        {
          while (s)
	    {
	      if (s->interface->filter)
		s->interface->filter (s->interface, line, *syntax);
	      s = s->included_from;
	    }

          return true;
        }
      close_source (ss);
    }

  return false;
}
