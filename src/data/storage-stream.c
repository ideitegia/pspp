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

#include <data/storage-stream.h>

#include <assert.h>
#include <stdlib.h>

#include <data/case-sink.h>
#include <data/case-source.h>
#include <data/case.h>
#include <data/casefile.h> 
#include <data/fastfile.h> 

#include "xalloc.h"

/* Storage sink. */

/* Information about storage sink. */
struct storage_sink_info 
  {
    struct casefile *casefile;  /* Storage. */
  };

static struct storage_sink_info *
get_storage_sink_info (struct case_sink *sink) 
{
  assert (sink->class == &storage_sink_class);
  return sink->aux;
}

/* Initializes a storage sink. */
static void
storage_sink_open (struct case_sink *sink)
{
  struct storage_sink_info *info;

  sink->aux = info = xmalloc (sizeof *info);
  info->casefile = fastfile_create (sink->value_cnt);
}

/* Writes case C to the storage sink SINK.
   Returns true if successful, false if an I/O error occurred. */
static bool
storage_sink_write (struct case_sink *sink, const struct ccase *c)
{
  struct storage_sink_info *info = get_storage_sink_info (sink);
  return casefile_append (info->casefile, c);
}

/* Destroys internal data in SINK. */
static void
storage_sink_destroy (struct case_sink *sink)
{
  struct storage_sink_info *info = get_storage_sink_info (sink);
  casefile_destroy (info->casefile);
  free (info); 
}

/* Closes the sink and returns a storage source to read back the
   written data. */
static struct case_source *
storage_sink_make_source (struct case_sink *sink) 
{
  struct storage_sink_info *info = get_storage_sink_info (sink);
  struct case_source *source = storage_source_create (info->casefile);
  info->casefile = NULL;
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

struct storage_source_info 
  {
    struct casefile *casefile;  /* Storage. */
    struct casereader *reader;  /* Reader. */
  };

static struct storage_source_info *
get_storage_source_info (const struct case_source *source) 
{
  assert (source->class == &storage_source_class);
  return source->aux;
}

/* Returns the number of cases that will be read by
   storage_source_read(). */
static int
storage_source_count (const struct case_source *source) 
{
  struct storage_source_info *info = get_storage_source_info (source);
  return casefile_get_case_cnt (info->casefile);
}

/* Reads one case into OUTPUT_CASE.
   Returns true if successful, false at end of file or if an
   I/O error occurred. */
static bool
storage_source_read (struct case_source *source, struct ccase *output_case)
{
  struct storage_source_info *info = get_storage_source_info (source);
  struct ccase casefile_case;

  if (info->reader == NULL)
    info->reader = casefile_get_reader (info->casefile, NULL);

  if (casereader_read (info->reader, &casefile_case))
    {
      case_copy (output_case, 0,
                 &casefile_case, 0,
                 casefile_get_value_cnt (info->casefile));
      return true;
    }
  else
    return false;
}

/* Destroys the source.
   Returns true if successful read, false if an I/O occurred
   during destruction or previously. */
static bool
storage_source_destroy (struct case_source *source)
{
  struct storage_source_info *info = get_storage_source_info (source);
  bool ok = true;
  if (info->casefile)
    {
      ok = !casefile_error (info->casefile);
      casefile_destroy (info->casefile); 
    }
  free (info);
  return ok;
}

/* Returns the casefile encapsulated by SOURCE. */
struct casefile *
storage_source_get_casefile (struct case_source *source) 
{
  struct storage_source_info *info = get_storage_source_info (source);
  return info->casefile;
}

/* Destroys SOURCE and returns the casefile that it
   encapsulated. */
struct casefile *
storage_source_decapsulate (struct case_source *source) 
{
  struct storage_source_info *info = get_storage_source_info (source);
  struct casefile *casefile = info->casefile;
  assert (info->reader == NULL);
  info->casefile = NULL;
  free_case_source (source);
  return casefile;
}

/* Creates and returns a new storage source that encapsulates
   CASEFILE. */
struct case_source *
storage_source_create (struct casefile *casefile)
{
  struct storage_source_info *info;

  info = xmalloc (sizeof *info);
  info->casefile = casefile;
  info->reader = NULL;

  return create_case_source (&storage_source_class, info);
}

/* Storage source. */
const struct case_source_class storage_source_class = 
  {
    "storage",
    storage_source_count,
    storage_source_read,
    storage_source_destroy,
  };
