/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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

#include <data/file-name.h>

struct getl_source
  {
    struct getl_source *included_from;	/* File that this is nested inside. */
    struct getl_source *includes;	/* File nested inside this file. */
    
    struct ll  ll;   /* Element in the sources list */

    struct getl_interface *interface;
  };

/* List of source files. */
static struct ll_list sources ;

static struct string the_include_path;

const char *
getl_include_path (void)
{
  return ds_cstr (&the_include_path);
}

static struct getl_source *
current_source (struct ll_list *list)
{
  const struct ll *ll = ll_head (list);
  return ll_data (ll, struct getl_source, ll );
}

/* Initialize getl. */
void
getl_initialize (void)
{
  ll_init (&sources);
  ds_init_cstr (&the_include_path,
                fn_getenv_default ("STAT_INCLUDE_PATH", include_path));
}

/* Delete everything from the include path. */
void
getl_clear_include_path (void)
{
  ds_clear (&the_include_path);
}

/* Add to the include path. */
void
getl_add_include_dir (const char *path)
{
  if (ds_length (&the_include_path))
    ds_put_char (&the_include_path, ':');

  ds_put_cstr (&the_include_path, path);
}

/* Appends source S to the list of source files. */
void
getl_append_source (struct getl_interface *i) 
{
  struct getl_source *s = xzalloc (sizeof ( struct getl_source ));

  s->interface = i ;

  ll_push_head (&sources, &s->ll);
}

/* Nests source S within the current source file. */
void
getl_include_source (struct getl_interface *i) 
{
  struct getl_source *current = current_source (&sources);
  struct getl_source *s = xzalloc (sizeof ( struct getl_source ));

  s->interface = i;

  s->included_from = current ;
  s->includes  = NULL;
  current->includes = s;

  ll_push_head (&sources, &s->ll);
}

/* Closes the current source, and move  the current source to the 
   next file in the chain. */
static void
close_source (void)
{
  struct getl_source *s = current_source (&sources);

  if ( s->interface->close ) 
    s->interface->close (s->interface);

  ll_pop_head (&sources);

  if (s->included_from != NULL)
    current_source (&sources)->includes = NULL;

  free (s);
}

/* Closes all sources until an interactive source is
   encountered. */
void
getl_abort_noninteractive (void) 
{
  while ( ! ll_is_empty (&sources))
    {
      const struct getl_source *s = current_source (&sources);
      
      if ( !s->interface->interactive (s->interface) ) 
	close_source ();
    }
}

/* Returns true if the current source is interactive,
   false otherwise. */
bool
getl_is_interactive (void) 
{
  const struct getl_source *s = current_source (&sources);

  if (ll_is_empty (&sources) ) 
    return false;

  return s->interface->interactive (s->interface);
}

/* Returns the name of the current source, or NULL if there is no 
   current source */
const char *
getl_source_name (void)
{
  const struct getl_source *s = current_source (&sources);

  if ( ll_is_empty (&sources) )
    return NULL;

  if ( ! s->interface->name ) 
    return NULL;

  return s->interface->name (s->interface);
}

/* Returns the location within the current source, or -1 if there is
   no current source */
int
getl_source_location (void)
{
  const struct getl_source *s = current_source (&sources);

  if ( ll_is_empty (&sources) )
    return -1;

  if ( !s->interface->location )
    return -1;

  return s->interface->location (s->interface);
}


/* Close getl. */
void
getl_uninitialize (void)
{
  while ( !ll_is_empty (&sources))
    close_source ();
  ds_destroy (&the_include_path);
}


/* Reads a single line into LINE.
   Returns true when a line has been read, false at end of input.
   On success, sets *SYNTAX to the style of the syntax read. */
bool
do_read_line (struct string *line, enum getl_syntax *syntax)
{
  while (!ll_is_empty (&sources))
    {
      struct getl_source *s = current_source (&sources);

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
      close_source ();
    }

  return false;
}
