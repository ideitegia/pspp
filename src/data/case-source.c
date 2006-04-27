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

#include <config.h>

#include <data/case-source.h>

#include <stdlib.h>

#include "xalloc.h"

/* Creates a case source with class CLASS and auxiliary data AUX
   and based on dictionary DICT. */
struct case_source *
create_case_source (const struct case_source_class *class,
                    void *aux) 
{
  struct case_source *source = xmalloc (sizeof *source);
  source->class = class;
  source->aux = aux;
  return source;
}

/* Destroys case source SOURCE.  It is the caller's responsible to
   call the source's destroy function, if any. */
void
free_case_source (struct case_source *source) 
{
  if (source != NULL) 
    {
      if (source->class->destroy != NULL)
        source->class->destroy (source);
      free (source);
    }
}

/* Returns nonzero if CLASS is the class of SOURCE. */
int
case_source_is_class (const struct case_source *source,
                      const struct case_source_class *class) 
{
  return source != NULL && source->class == class;
}
