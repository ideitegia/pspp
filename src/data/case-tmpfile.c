/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "data/case-tmpfile.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "libpspp/assertion.h"
#include "libpspp/taint.h"
#include "libpspp/ext-array.h"

#include "gl/xalloc.h"

/* A temporary file that stores an array of cases. */
struct case_tmpfile
  {
    struct taint *taint;        /* Taint. */
    struct caseproto *proto;    /* Format of cases in the tmpfile. */
    size_t case_size;           /* Number of bytes per case. */
    size_t *offsets;            /* Offset to each value. */
    struct ext_array *ext_array; /* Temporary file. */
  };

/* Returns the number of bytes needed to store a value with the
   given WIDTH on disk. */
static size_t
width_to_n_bytes (int width)
{
  return width == 0 ? sizeof (double) : width;
}

/* Returns the address of the data in VALUE (for reading or
   writing to/from disk).  VALUE must have the given WIDTH. */
static void *
value_to_data (const union value *value_, int width)
{
  union value *value = CONST_CAST (union value *, value_);
  assert (sizeof value->f == sizeof (double));
  if (width == 0)
    return &value->f;
  else
    return value_str_rw (value, width);
}

/* Creates and returns a new case_tmpfile that will store cases
   that match case prototype PROTO.  The caller retains
   ownership of PROTO. */
struct case_tmpfile *
case_tmpfile_create (const struct caseproto *proto)
{
  struct case_tmpfile *ctf;
  size_t n_values;
  size_t i;

  ctf = xmalloc (sizeof *ctf);
  ctf->taint = taint_create ();
  ctf->ext_array = ext_array_create ();
  ctf->proto = caseproto_ref (proto);
  ctf->case_size = 0;
  n_values = caseproto_get_n_widths (proto);
  ctf->offsets = xmalloc (n_values * sizeof *ctf->offsets);
  for (i = 0; i < n_values; i++)
    {
      size_t width = caseproto_get_width (proto, i);
      ctf->offsets[i] = ctf->case_size;
      ctf->case_size += width == -1 ? 0 : width == 0 ? sizeof (double) : width;
    }
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
      ext_array_destroy (ctf->ext_array);
      caseproto_unref (ctf->proto);
      free (ctf->offsets);
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

/* Reads N_VALUES values into VALUES, from the case numbered
   CASE_IDX starting START_VALUE values into that case.  Returns
   true if successful, false if CTF is tainted or an I/O error
   occurs during the operation.

   The results of this function are undefined if any of the
   values read have not been previously written to CTF. */
bool
case_tmpfile_get_values (const struct case_tmpfile *ctf,
                         casenumber case_idx, size_t start_value,
                         union value values[], size_t n_values)
{
  off_t case_offset = (off_t) ctf->case_size * case_idx;
  size_t i;

  assert (caseproto_range_is_valid (ctf->proto, start_value, n_values));
  for (i = start_value; i < start_value + n_values; i++)
    {
      int width = caseproto_get_width (ctf->proto, i);
      if (width != -1
          && !ext_array_read (ctf->ext_array, case_offset + ctf->offsets[i],
                              width_to_n_bytes (width),
                              value_to_data (&values[i], width)))
          return false;
    }
  return true;
}

/* Reads the case numbered CASE_IDX from CTF.
   Returns the case if successful or a null pointer if CTF is
   tainted or an I/O error occurs during the operation.

   The results of this function are undefined if the case read
   from CTF had not previously been written. */
struct ccase *
case_tmpfile_get_case (const struct case_tmpfile *ctf, casenumber case_idx)
{
  struct ccase *c = case_create (ctf->proto);
  if (case_tmpfile_get_values (ctf, case_idx, 0, case_data_all_rw (c),
                               caseproto_get_n_widths (ctf->proto)))
    return c;
  else
    {
      case_unref (c);
      return NULL;
    }
}

/* Writes N_VALUES values from VALUES, into the case numbered
   CASE_IDX starting START_VALUE values into that case.
   Returns true if successful, false if CTF is tainted or an I/O
   error occurs during the operation. */
bool
case_tmpfile_put_values (struct case_tmpfile *ctf,
                         casenumber case_idx, size_t start_value,
                         const union value values[], size_t n_values)
{
  off_t case_offset = (off_t) ctf->case_size * case_idx;
  size_t i;

  assert (caseproto_range_is_valid (ctf->proto, start_value, n_values));
  for (i = start_value; i < start_value + n_values; i++)
    {
      int width = caseproto_get_width (ctf->proto, i);
      if (width != -1
          && !ext_array_write (ctf->ext_array, case_offset + ctf->offsets[i],
                               width_to_n_bytes (width),
                               value_to_data (values++, width)))
          return false;
    }
  return true;
}

/* Writes C to CTF as the case numbered CASE_IDX.
   Returns true if successful, false if CTF is tainted or an I/O
   error occurs during the operation. */
bool
case_tmpfile_put_case (struct case_tmpfile *ctf, casenumber case_idx,
                       struct ccase *c)
{
  bool ok = case_tmpfile_put_values (ctf, case_idx, 0, case_data_all (c),
                                     caseproto_get_n_widths (ctf->proto));
  case_unref (c);
  return ok;
}

