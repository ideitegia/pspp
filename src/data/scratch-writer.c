/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
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
#include "scratch-writer.h"
#include <stdlib.h>
#include "case.h"
#include "casefile.h"
#include "fastfile.h"
#include "dictionary.h"
#include "file-handle-def.h"
#include "scratch-handle.h"
#include "xalloc.h"

/* A scratch file writer. */
struct scratch_writer 
  {
    struct scratch_handle *handle;      /* Underlying scratch handle. */
    struct file_handle *fh;             /* Underlying file handle. */
    struct dict_compactor *compactor;   /* Compacts into handle->dictionary. */
  };

/* Opens FH, which must have referent type FH_REF_SCRATCH, and
   returns a scratch_writer for it, or a null pointer on
   failure.  Cases stored in the scratch_writer will be expected
   to be drawn from DICTIONARY.

   If you use an any_writer instead, then your code can be more
   flexible without being any harder to write. */
struct scratch_writer *
scratch_writer_open (struct file_handle *fh,
                     const struct dictionary *dictionary) 
{
  struct scratch_handle *sh;
  struct scratch_writer *writer;
  struct dictionary *scratch_dict;
  struct dict_compactor *compactor;

  if (!fh_open (fh, FH_REF_SCRATCH, "scratch file", "we"))
    return NULL;

  /* Destroy previous contents of handle. */
  sh = fh_get_scratch_handle (fh);
  if (sh != NULL) 
    scratch_handle_destroy (sh);

  /* Copy the dictionary and compact if needed. */
  scratch_dict = dict_clone (dictionary);
  if (dict_compacting_would_shrink (scratch_dict)) 
    {
      compactor = dict_make_compactor (scratch_dict);
      dict_compact_values (scratch_dict);
    }
  else
    compactor = NULL;

  /* Create new contents. */
  sh = xmalloc (sizeof *sh);
  sh->dictionary = scratch_dict;
  sh->casefile = fastfile_create (dict_get_next_value_idx (sh->dictionary));

  /* Create writer. */
  writer = xmalloc (sizeof *writer);
  writer->handle = sh;
  writer->fh = fh;
  writer->compactor = compactor;

  fh_set_scratch_handle (fh, sh);
  return writer;
}

/* Writes case C to WRITER. */
bool
scratch_writer_write_case (struct scratch_writer *writer,
                           const struct ccase *c) 
{
  struct scratch_handle *handle = writer->handle;
  if (writer->compactor) 
    {
      struct ccase tmp_case;
      case_create (&tmp_case, dict_get_next_value_idx (handle->dictionary));
      dict_compactor_compact (writer->compactor, &tmp_case, c);
      return casefile_append_xfer (handle->casefile, &tmp_case);
    }
  else 
    return casefile_append (handle->casefile, c);
}

/* Returns true if an I/O error occurred on WRITER, false otherwise. */
bool
scratch_writer_error (const struct scratch_writer *writer) 
{
  return casefile_error (writer->handle->casefile);
}

/* Closes WRITER.
   Returns true if successful, false if an I/O error occurred on WRITER. */
bool
scratch_writer_close (struct scratch_writer *writer) 
{
  struct casefile *cf = writer->handle->casefile;
  bool ok = casefile_error (cf);
  fh_close (writer->fh, "scratch file", "we");
  free (writer);
  return ok;
}
