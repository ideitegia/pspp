/* PSPP - a program for statistical analysis.
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

#include <data/case-tmpfile.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <libpspp/assertion.h>
#include <libpspp/taint.h>

#include "error.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* A temporary file that stores an array of cases. */
struct case_tmpfile
  {
    struct taint *taint;        /* Taint. */
    FILE *file;                 /* Underlying file. */
    size_t value_cnt;           /* Number of `union value's per case. */

    /* Current byte offset in file.  We track this manually,
       instead of using ftello, because in glibc ftello flushes
       the stream buffer, making the common case of sequential
       access to cases unreasonably slow. */
    off_t position;
  };

/* Creates and returns a new case_tmpfile. */
struct case_tmpfile *
case_tmpfile_create (size_t value_cnt)
{
  struct case_tmpfile *ctf = xmalloc (sizeof *ctf);
  ctf->taint = taint_create ();
  ctf->file = tmpfile ();
  if (ctf->file == NULL)
    {
      error (0, errno, _("failed to create temporary file"));
      taint_set_taint (ctf->taint);
    }
  ctf->value_cnt = value_cnt;
  ctf->position = 0;
  return ctf;
}

/* Destroys case_tmpfile CTF.
   Returns true if CTF was tainted, which is caused by an I/O
   error on case_tmpfile access or by taint propagation to the
   case_tmpfile. */
bool
case_tmpfile_destroy (struct case_tmpfile *ctf)
{
  bool ok = true;
  if (ctf != NULL)
    {
      struct taint *taint = ctf->taint;
      if (ctf->file != NULL)
        fclose (ctf->file);
      free (ctf);
      ok = taint_destroy (taint);
    }
  return ok;
}

/* Returns true if CTF is tainted, which is caused by an I/O
   error on case_tmpfile access or by taint propagation to the
   case_tmpfile. */
bool
case_tmpfile_error (const struct case_tmpfile *ctf)
{
  return taint_is_tainted (ctf->taint);
}

/* Marks CTF as tainted. */
void
case_tmpfile_force_error (struct case_tmpfile *ctf)
{
  taint_set_taint (ctf->taint);
}

/* Returns CTF's taint object. */
const struct taint *
case_tmpfile_get_taint (const struct case_tmpfile *ctf)
{
  return ctf->taint;
}

/* Seeks CTF's underlying file to the start of `union value'
   VALUE_IDX within case CASE_IDX.
   Returns true if the seek is successful and CTF is not
   otherwise tainted, false otherwise. */
static bool
do_seek (const struct case_tmpfile *ctf_,
         casenumber case_idx, size_t value_idx)
{
  struct case_tmpfile *ctf = (struct case_tmpfile *) ctf_;

  if (!case_tmpfile_error (ctf))
    {
      off_t value_ofs = value_idx + (off_t) ctf->value_cnt * case_idx;
      off_t byte_ofs = sizeof (union value) * value_ofs;

      if (ctf->position == byte_ofs)
        return true;
      else if (fseeko (ctf->file, byte_ofs, SEEK_SET) == 0)
        {
          ctf->position = byte_ofs;
          return true;
        }
      else
        {
          error (0, errno, _("seeking in temporary file"));
          case_tmpfile_force_error (ctf);
        }
    }

  return false;
}

/* Reads BYTES bytes from CTF's underlying file into BUFFER.
   CTF must not be tainted upon entry into this function.
   Returns true if successful, false upon an I/O error (in which
   case CTF is marked tainted). */
static bool
do_read (const struct case_tmpfile *ctf_, size_t bytes, void *buffer)
{
  struct case_tmpfile *ctf = (struct case_tmpfile *) ctf_;

  assert (!case_tmpfile_error (ctf));
  if (fread (buffer, bytes, 1, ctf->file) != 1)
    {
      case_tmpfile_force_error (ctf);
      if (ferror (ctf->file))
        error (0, errno, _("reading temporary file"));
      else if (feof (ctf->file))
        error (0, 0, _("unexpected end of file reading temporary file"));
      else
        NOT_REACHED ();
      return false;
    }
  ctf->position += bytes;
  return true;
}

/* Writes BYTES bytes from BUFFER into CTF's underlying file.
   CTF must not be tainted upon entry into this function.
   Returns true if successful, false upon an I/O error (in which
   case CTF is marked tainted). */
static bool
do_write (struct case_tmpfile *ctf, size_t bytes, const void *buffer)
{
  assert (!case_tmpfile_error (ctf));
  if (fwrite (buffer, bytes, 1, ctf->file) != 1)
    {
      case_tmpfile_force_error (ctf);
      error (0, errno, _("writing to temporary file"));
      return false;
    }
  ctf->position += bytes;
  return true;
}

/* Reads VALUE_CNT values into VALUES, from the case numbered
   CASE_IDX starting START_VALUE values into that case.
   Returns true if successful, false if CTF is tainted or an I/O
   error occurs during the operation.

   The results of this function are undefined if any of the
   values read have not been previously written to CTF. */
bool
case_tmpfile_get_values (const struct case_tmpfile *ctf,
                         casenumber case_idx, size_t start_value,
                         union value values[], size_t value_cnt)
{
  assert (value_cnt <= ctf->value_cnt);
  assert (value_cnt + start_value <= ctf->value_cnt);

  return (do_seek (ctf, case_idx, start_value)
          && do_read (ctf, sizeof *values * value_cnt, values));
}

/* Reads the case numbered CASE_IDX from CTF into C.
   Returns true if successful, false if CTF is tainted or an I/O
   error occurs during the operation.

   The results of this function are undefined if the case read
   from CTF had not previously been written. */
bool
case_tmpfile_get_case (const struct case_tmpfile *ctf, casenumber case_idx,
                       struct ccase *c)
{
  case_create (c, ctf->value_cnt);
  if (case_tmpfile_get_values (ctf, case_idx, 0,
                               case_data_all_rw (c), ctf->value_cnt))
    return true;
  else
    {
      case_destroy (c);
      case_nullify (c);
      return false;
    }
}

/* Writes VALUE_CNT values from VALUES, into the case numbered
   CASE_IDX starting START_VALUE values into that case.
   Returns true if successful, false if CTF is tainted or an I/O
   error occurs during the operation. */
bool
case_tmpfile_put_values (struct case_tmpfile *ctf,
                         casenumber case_idx, size_t start_value,
                         const union value values[], size_t value_cnt)

{
  assert (value_cnt <= ctf->value_cnt);
  assert (value_cnt + start_value <= ctf->value_cnt);

  return (do_seek (ctf, case_idx, start_value)
          && do_write (ctf, sizeof *values * value_cnt, values));
}

/* Writes C to CTF as the case numbered CASE_IDX.
   Returns true if successful, false if CTF is tainted or an I/O
   error occurs during the operation. */
bool
case_tmpfile_put_case (struct case_tmpfile *ctf, casenumber case_idx,
                       struct ccase *c)
{
  bool ok = case_tmpfile_put_values (ctf, case_idx, 0,
                                     case_data_all (c), ctf->value_cnt);
  case_destroy (c);
  return ok;
}

