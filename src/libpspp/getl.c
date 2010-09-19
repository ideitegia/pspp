/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010 Free Software Foundation, Inc.

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

#include "libpspp/getl.h"

#include <stdlib.h>

#include "libpspp/ll.h"
#include "libpspp/str.h"
#include "libpspp/string-array.h"

#include "gl/configmake.h"
#include "gl/relocatable.h"
#include "gl/xalloc.h"

struct getl_source
  {
    struct getl_source *included_from;	/* File that this is nested inside. */
    struct getl_source *includes;	/* File nested inside this file. */

    struct ll  ll;   /* Element in the sources list */

    struct getl_interface *interface;
    enum syntax_mode syntax_mode;
    enum error_mode error_mode;
  };

struct source_stream
  {
    struct ll_list sources ;  /* List of source files. */
    struct string_array include_path;
  };

char **
getl_include_path (const struct source_stream *ss_)
{
  struct source_stream *ss = CONST_CAST (struct source_stream *, ss_);
  string_array_terminate_null (&ss->include_path);
  return ss->include_path.strings;
}

static struct getl_source *
current_source (const struct source_stream *ss)
{
  const struct ll *ll = ll_head (&ss->sources);
  return ll_data (ll, struct getl_source, ll );
}

enum syntax_mode
source_stream_current_syntax_mode (const struct source_stream *ss)
{
  struct getl_source *cs = current_source (ss);

  return cs->syntax_mode;
}



enum error_mode
source_stream_current_error_mode (const struct source_stream *ss)
{
  struct getl_source *cs = current_source (ss);

  return cs->error_mode;
}



/* Initialize getl. */
struct source_stream *
create_source_stream (void)
{
  struct source_stream *ss;

  ss = xzalloc (sizeof (*ss));
  ll_init (&ss->sources);

  string_array_init (&ss->include_path);
  string_array_append (&ss->include_path, ".");
  if (getenv ("HOME") != NULL)
    string_array_append_nocopy (&ss->include_path,
                                xasprintf ("%s/.pspp", getenv ("HOME")));
  string_array_append (&ss->include_path, relocate (PKGDATADIR));

  return ss;
}

/* Delete everything from the include path. */
void
getl_clear_include_path (struct source_stream *ss)
{
  string_array_clear (&ss->include_path);
}

/* Add to the include path. */
void
getl_add_include_dir (struct source_stream *ss, const char *path)
{
  string_array_append (&ss->include_path, path);
}

/* Appends source S to the list of source files. */
void
getl_append_source (struct source_stream *ss,
		    struct getl_interface *i,
		    enum syntax_mode syntax_mode,
		    enum error_mode err_mode)
{
  struct getl_source *s = xzalloc (sizeof ( struct getl_source ));

  s->interface = i ;
  s->syntax_mode = syntax_mode;
  s->error_mode = err_mode;

  ll_push_tail (&ss->sources, &s->ll);
}

/* Nests source S within the current source file. */
void
getl_include_source (struct source_stream *ss,
		     struct getl_interface *i,
		     enum syntax_mode syntax_mode,
		     enum error_mode err_mode)
{
  struct getl_source *current = current_source (ss);
  struct getl_source *s = xzalloc (sizeof ( struct getl_source ));

  s->interface = i;

  s->included_from = current ;
  s->includes  = NULL;
  s->syntax_mode  = syntax_mode;
  s->error_mode = err_mode;
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

/* Returns the line number within the current source, or 0 if there is no
   current source. */
int
getl_source_location (const struct source_stream *ss)
{
  const struct getl_source *s = current_source (ss);

  if ( ll_is_empty (&ss->sources) )
    return 0;

  if ( !s->interface->location )
    return 0;

  return s->interface->location (s->interface);
}


/* Close getl. */
void
destroy_source_stream (struct source_stream *ss)
{
  while ( !ll_is_empty (&ss->sources))
    close_source (ss);
  string_array_destroy (&ss->include_path);

  free (ss);
}


/* Reads a single line into LINE.
   Returns true when a line has been read, false at end of input.
*/
bool
getl_read_line (struct source_stream *ss, struct string *line)
{
  assert (ss != NULL);
  while (!ll_is_empty (&ss->sources))
    {
      struct getl_source *s = current_source (ss);

      ds_clear (line);
      if (s->interface->read (s->interface, line))
        {
          while (s)
	    {
	      if (s->interface->filter)
		s->interface->filter (s->interface, line);
	      s = s->included_from;
	    }

          return true;
        }
      close_source (ss);
    }

  return false;
}
