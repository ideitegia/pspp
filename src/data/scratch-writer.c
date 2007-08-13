/* PSPP - a program for statistical analysis.
   Copyright (C) 2006 Free Software Foundation, Inc.

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

#include "scratch-writer.h"

#include <stdlib.h>

#include <data/case.h>
#include <data/casereader.h>
#include <data/casewriter-provider.h>
#include <data/casewriter.h>
#include <data/dictionary.h>
#include <data/file-handle-def.h>
#include <data/scratch-handle.h>
#include <data/variable.h>
#include <libpspp/compiler.h>
#include <libpspp/taint.h>

#include "xalloc.h"

/* A scratch file writer. */
struct scratch_writer
  {
    struct scratch_handle *handle;      /* Underlying scratch handle. */
    struct file_handle *fh;             /* Underlying file handle. */
    struct dict_compactor *compactor;   /* Compacts into handle->dictionary. */
    struct casewriter *subwriter;       /* Data output. */
  };

static struct casewriter_class scratch_writer_casewriter_class;

/* Opens FH, which must have referent type FH_REF_SCRATCH, and
   returns a scratch_writer for it, or a null pointer on
   failure.  Cases stored in the scratch_writer will be expected
   to be drawn from DICTIONARY. */
struct casewriter *
scratch_writer_open (struct file_handle *fh,
                     const struct dictionary *dictionary)
{
  struct scratch_handle *sh;
  struct scratch_writer *writer;
  struct dictionary *scratch_dict;
  struct dict_compactor *compactor;
  struct casewriter *casewriter;
  size_t dict_value_cnt;

  if (!fh_open (fh, FH_REF_SCRATCH, "scratch file", "we"))
    return NULL;

  /* Destroy previous contents of handle. */
  sh = fh_get_scratch_handle (fh);
  if (sh != NULL)
    scratch_handle_destroy (sh);

  /* Copy the dictionary and compact if needed. */
  scratch_dict = dict_clone (dictionary);
  dict_delete_scratch_vars (scratch_dict);
  if (dict_count_values (scratch_dict, 0)
      < dict_get_next_value_idx (scratch_dict))
    {
      compactor = dict_make_compactor (scratch_dict, 0);
      dict_compact_values (scratch_dict);
    }
  else
    compactor = NULL;
  dict_value_cnt = dict_get_next_value_idx (scratch_dict);

  /* Create new contents. */
  sh = xmalloc (sizeof *sh);
  sh->dictionary = scratch_dict;
  sh->casereader = NULL;

  /* Create writer. */
  writer = xmalloc (sizeof *writer);
  writer->handle = sh;
  writer->fh = fh;
  writer->compactor = compactor;
  writer->subwriter = autopaging_writer_create (dict_value_cnt);

  fh_set_scratch_handle (fh, sh);
  casewriter = casewriter_create (dict_value_cnt,
                                  &scratch_writer_casewriter_class, writer);
  taint_propagate (casewriter_get_taint (writer->subwriter),
                   casewriter_get_taint (casewriter));
  return casewriter;
}

/* Writes case C to WRITER. */
static void
scratch_writer_casewriter_write (struct casewriter *w UNUSED, void *writer_,
                                 struct ccase *c)
{
  struct scratch_writer *writer = writer_;
  struct scratch_handle *handle = writer->handle;
  struct ccase tmp;
  if (writer->compactor)
    {
      case_create (&tmp, dict_get_next_value_idx (handle->dictionary));
      dict_compactor_compact (writer->compactor, &tmp, c);
      case_destroy (c);
    }
  else
    case_move (&tmp, c);
  casewriter_write (writer->subwriter, &tmp);
}

/* Closes WRITER. */
static void
scratch_writer_casewriter_destroy (struct casewriter *w UNUSED, void *writer_)
{
  struct scratch_writer *writer = writer_;
  struct casereader *reader = casewriter_make_reader (writer->subwriter);
  if (!casereader_error (reader))
    writer->handle->casereader = reader;
  fh_close (writer->fh, "scratch file", "we");
  free (writer);
}

static struct casewriter_class scratch_writer_casewriter_class =
  {
    scratch_writer_casewriter_write,
    scratch_writer_casewriter_destroy,
    NULL,
  };
