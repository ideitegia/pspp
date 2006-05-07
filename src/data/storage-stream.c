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

#include <data/storage-stream.h>

#include <stdlib.h>

#include <data/case-sink.h>
#include <data/case-source.h>
#include <data/case.h>
#include <data/casefile.h> 

#include "xalloc.h"

/* Information about storage sink or source. */
struct storage_stream_info 
  {
    struct casefile *casefile;  /* Storage. */
  };

/* Storage sink. */

/* Initializes a storage sink. */
static void
storage_sink_open (struct case_sink *sink)
{
  struct storage_stream_info *info;

  sink->aux = info = xmalloc (sizeof *info);
  info->casefile = casefile_create (sink->value_cnt);
}

/* Destroys storage stream represented by INFO. */
static void
destroy_storage_stream_info (struct storage_stream_info *info) 
{
  if (info != NULL) 
    {
      casefile_destroy (info->casefile);
      free (info); 
    }
}

/* Writes case C to the storage sink SINK.
   Returns true if successful, false if an I/O error occurred. */
static bool
storage_sink_write (struct case_sink *sink, const struct ccase *c)
{
  struct storage_stream_info *info = sink->aux;

  return casefile_append (info->casefile, c);
}

/* Destroys internal data in SINK. */
static void
storage_sink_destroy (struct case_sink *sink)
{
  destroy_storage_stream_info (sink->aux);
}

/* Closes the sink and returns a storage source to read back the
   written data. */
static struct case_source *
storage_sink_make_source (struct case_sink *sink) 
{
  struct case_source *source
    = create_case_source (&storage_source_class, sink->aux);
  sink->aux = NULL;
  return source;
}

/* Storage sink. */
const struct case_sink_class storage_sink_class = 
  {
    "storage",
    storage_sink_open,
    storage_sink_write,
    storage_sink_destroy,
    storage_sink_make_source,
  };

/* Storage source. */

/* Returns the number of cases that will be read by
   storage_source_read(). */
static int
storage_source_count (const struct case_source *source) 
{
  struct storage_stream_info *info = source->aux;

  return casefile_get_case_cnt (info->casefile);
}

/* Reads all cases from the storage source and passes them one by one to
   write_case(). */
static bool
storage_source_read (struct case_source *source,
                     struct ccase *output_case,
                     write_case_func *write_case, write_case_data wc_data)
{
  struct storage_stream_info *info = source->aux;
  struct ccase casefile_case;
  struct casereader *reader;
  bool ok = true;

  for (reader = casefile_get_reader (info->casefile);
       ok && casereader_read (reader, &casefile_case);
       case_destroy (&casefile_case))
    {
      case_copy (output_case, 0,
                 &casefile_case, 0,
                 casefile_get_value_cnt (info->casefile));
      ok = write_case (wc_data);
    }
  casereader_destroy (reader);

  return ok;
}

/* Destroys the source's internal data. */
static void
storage_source_destroy (struct case_source *source)
{
  destroy_storage_stream_info (source->aux);
}

/* Storage source. */
const struct case_source_class storage_source_class = 
  {
    "storage",
    storage_source_count,
    storage_source_read,
    storage_source_destroy,
  };

/* Returns the casefile encapsulated by SOURCE. */
struct casefile *
storage_source_get_casefile (struct case_source *source) 
{
  struct storage_stream_info *info = source->aux;

  assert (source->class == &storage_source_class);
  return info->casefile;
}

/* Destroys SOURCE and returns the casefile that it
   encapsulated. */
struct casefile *
storage_source_decapsulate (struct case_source *source) 
{
  struct storage_stream_info *info = source->aux;
  struct casefile *casefile;

  assert (source->class == &storage_source_class);
  casefile = info->casefile;
  info->casefile = NULL;
  free_case_source (source);
  return casefile;
}

/* Creates and returns a new storage stream that encapsulates
   CASEFILE. */
struct case_source *
storage_source_create (struct casefile *casefile)
{
  struct storage_stream_info *info;

  info = xmalloc (sizeof *info);
  info->casefile = casefile;

  return create_case_source (&storage_source_class, info);
}
