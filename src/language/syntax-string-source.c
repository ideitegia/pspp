/* PSPPIRE - a graphical interface for PSPP.
   Copyright (C) 2007 Free Software Foundation, Inc.

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
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <libpspp/str.h>

#include <stdlib.h>

#include "syntax-string-source.h"

struct syntax_string_source
  {
    struct getl_interface parent;
    struct string buffer;
    size_t posn;
  };


static bool
always_false (const struct getl_interface *i UNUSED)
{
  return false;
}

/* Returns the name of the source */
static const char *
name (const struct getl_interface *i UNUSED)
{
  return NULL;
}


/* Returns the location within the source */
static int
location (const struct getl_interface *i UNUSED)
{
  return -1;
}


static void
do_close (struct getl_interface *i )
{
  struct syntax_string_source *sss = (struct syntax_string_source *) i;

  ds_destroy (&sss->buffer);

  free (sss);
}



static bool
read_single_line (struct getl_interface *i,
		  struct string *line)
{
  struct syntax_string_source *sss = (struct syntax_string_source *) i;

  size_t next;

  if ( sss->posn == -1)
    return false;

  next = ss_find_char (ds_substr (&sss->buffer,
				  sss->posn, -1), '\n');

  ds_assign_substring (line,
		       ds_substr (&sss->buffer,
				  sss->posn,
				  next)
		       );

  if ( next != -1 )
    sss->posn += next + 1; /* + 1 to skip newline */
  else
    sss->posn = -1; /* End of file encountered */

  return true;
}

struct getl_interface *
create_syntax_string_source (const char *format, ...)
{
  va_list args;

  struct syntax_string_source *sss = xzalloc (sizeof *sss);

  sss->posn = 0;

  ds_init_empty (&sss->buffer);

  va_start (args, format);
  ds_put_vformat (&sss->buffer, format, args);
  va_end (args);

  sss->parent.interactive = always_false;
  sss->parent.close = do_close;
  sss->parent.read = read_single_line;

  sss->parent.name = name;
  sss->parent.location = location;


  return (struct getl_interface *) sss;
}

/* Return the syntax currently contained in S.
   Primarily usefull for debugging */
const char *
syntax_string_source_get_syntax (const struct syntax_string_source *s)
{
  return ds_cstr (&s->buffer);
}
