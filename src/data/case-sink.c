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

#include <config.h>

#include <data/case-sink.h>

#include <stdlib.h>

#include <data/dictionary.h>

#include "xalloc.h"

/* Creates a case sink to accept cases from the given DICT with
   class CLASS and auxiliary data AUX. */
struct case_sink *
create_case_sink (const struct case_sink_class *class,
                  const struct dictionary *dict, struct casefile_factory *f,
                  void *aux) 
{
  struct case_sink *sink = xmalloc (sizeof *sink);
  sink->class = class;
  sink->value_cnt = dict_get_compacted_value_cnt (dict);
  sink->aux = aux;
  sink->factory = f;
  return sink;
}

/* Destroys case sink SINK.  */
void
free_case_sink (struct case_sink *sink) 
{
  if (sink != NULL) 
    {
      if (sink->class->destroy != NULL)
        sink->class->destroy (sink);
      free (sink); 
    }
}
/* Null sink.  Used by a few procedures that keep track of output
   themselves and would throw away anything that the sink
   contained anyway. */

const struct case_sink_class null_sink_class = 
  {
    "null",
    NULL,
    NULL,
    NULL,
    NULL,
  };
