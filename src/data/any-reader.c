/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010, 2011, 2012, 2014 Free Software Foundation, Inc.

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

#include "data/any-reader.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/file-name.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

static const struct any_reader_class dataset_reader_class;

static const struct any_reader_class *classes[] =
  {
    &sys_file_reader_class,
    &por_file_reader_class,
    &pcp_file_reader_class,
  };
enum { N_CLASSES = sizeof classes / sizeof *classes };

int
any_reader_detect (const char *file_name,
                   const struct any_reader_class **classp)
{
  struct detector
    {
      enum any_type type;
      int (*detect) (FILE *);
    };

  FILE *file;
  int retval;

  if (classp)
    *classp = NULL;

  file = fn_open (file_name, "rb");
  if (file == NULL)
    {
      msg (ME, _("An error occurred while opening `%s': %s."),
           file_name, strerror (errno));
      return -errno;
    }

  retval = 0;
  for (int i = 0; i < N_CLASSES; i++)
    {
      int rc = classes[i]->detect (file);
      if (rc == 1)
        {
          retval = 1;
          if (classp)
            *classp = classes[i];
          break;
        }
      else if (rc < 0)
        retval = rc;
    }

  if (retval < 0)
    msg (ME, _("Error reading `%s': %s."), file_name, strerror (-retval));

  fn_close (file_name, file);

  return retval;
}

struct any_reader *
any_reader_open (struct file_handle *handle)
{
  switch (fh_get_referent (handle))
    {
    case FH_REF_FILE:
      {
        const struct any_reader_class *class;
        int retval;

        retval = any_reader_detect (fh_get_file_name (handle), &class);
        if (retval <= 0)
          {
            if (retval == 0)
              msg (SE, _("`%s' is not a system or portable file."),
                   fh_get_file_name (handle));
            return NULL;
          }

        return class->open (handle);
      }

    case FH_REF_INLINE:
      msg (SE, _("The inline file is not allowed here."));
      return NULL;

    case FH_REF_DATASET:
      return dataset_reader_class.open (handle);
    }
  NOT_REACHED ();
}

bool
any_reader_close (struct any_reader *any_reader)
{
  return any_reader ? any_reader->klass->close (any_reader) : true;
}

struct casereader *
any_reader_decode (struct any_reader *any_reader,
                   const char *encoding,
                   struct dictionary **dictp,
                   struct any_read_info *info)
{
  const struct any_reader_class *class = any_reader->klass;
  struct casereader *reader;

  reader = any_reader->klass->decode (any_reader, encoding, dictp, info);
  if (reader && info)
    info->klass = class;
  return reader;
}

size_t
any_reader_get_strings (const struct any_reader *any_reader, struct pool *pool,
                        char ***labels, bool **ids, char ***values)
{
  return (any_reader->klass->get_strings
          ? any_reader->klass->get_strings (any_reader, pool, labels, ids,
                                            values)
          : 0);
}

struct casereader *
any_reader_open_and_decode (struct file_handle *handle,
                            const char *encoding,
                            struct dictionary **dictp,
                            struct any_read_info *info)
{
  struct any_reader *any_reader = any_reader_open (handle);
  return (any_reader
          ? any_reader_decode (any_reader, encoding, dictp, info)
          : NULL);
}

struct dataset_reader
  {
    struct any_reader any_reader;
    struct dictionary *dict;
    struct casereader *reader;
  };

/* Opens FH, which must have referent type FH_REF_DATASET, and returns a
   dataset_reader for it, or a null pointer on failure.  Stores a copy of the
   dictionary for the dataset file into *DICT.  The caller takes ownership of
   the casereader and the dictionary.  */
static struct any_reader *
dataset_reader_open (struct file_handle *fh)
{
  struct dataset_reader *reader;
  struct dataset *ds;

  /* We don't bother doing fh_lock or fh_ref on the file handle,
     as there's no advantage in this case, and doing these would
     require us to keep track of the "struct file_handle" and
     "struct fh_lock" and undo our work later. */
  assert (fh_get_referent (fh) == FH_REF_DATASET);

  ds = fh_get_dataset (fh);
  if (ds == NULL || !dataset_has_source (ds))
    {
      msg (SE, _("Cannot read from dataset %s because no dictionary or data "
                 "has been written to it yet."),
           fh_get_name (fh));
      return NULL;
    }

  reader = xmalloc (sizeof *reader);
  reader->any_reader.klass = &dataset_reader_class;
  reader->dict = dict_clone (dataset_dict (ds));
  reader->reader = casereader_clone (dataset_source (ds));
  return &reader->any_reader;
}

static struct dataset_reader *
dataset_reader_cast (const struct any_reader *r_)
{
  assert (r_->klass == &dataset_reader_class);
  return UP_CAST (r_, struct dataset_reader, any_reader);
}

static bool
dataset_reader_close (struct any_reader *r_)
{
  struct dataset_reader *r = dataset_reader_cast (r_);
  dict_destroy (r->dict);
  casereader_destroy (r->reader);
  free (r);

  return true;
}

static struct casereader *
dataset_reader_decode (struct any_reader *r_, const char *encoding UNUSED,
                       struct dictionary **dictp, struct any_read_info *info)
{
  struct dataset_reader *r = dataset_reader_cast (r_);
  struct casereader *reader;

  *dictp = r->dict;
  reader = r->reader;
  if (info)
    {
      memset (info, 0, sizeof *info);
      info->integer_format = INTEGER_NATIVE;
      info->float_format = FLOAT_NATIVE_DOUBLE;
      info->compression = ANY_COMP_NONE;
      info->case_cnt = casereader_get_case_cnt (reader);
    }
  free (r);

  return reader;
}

static const struct any_reader_class dataset_reader_class =
  {
    N_("Dataset"),
    NULL,
    dataset_reader_open,
    dataset_reader_close,
    dataset_reader_decode,
    NULL,
  };
