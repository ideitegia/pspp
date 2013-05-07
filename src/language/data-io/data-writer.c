/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-2004, 2006, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include "language/data-io/data-writer.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "data/file-name.h"
#include "data/make-file.h"
#include "language/data-io/file-handle.h"
#include "libpspp/assertion.h"
#include "libpspp/encoding-guesser.h"
#include "libpspp/integer-format.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

/* Data file writer. */
struct dfm_writer
  {
    struct file_handle *fh;     /* File handle. */
    struct fh_lock *lock;       /* Exclusive access to file. */
    FILE *file;                 /* Associated file. */
    struct replace_file *rf;    /* Atomic file replacement support. */
    char *encoding;             /* Encoding. */
    enum fh_line_ends line_ends; /* Line ends for text files. */

    int unit;                   /* Unit width, in bytes. */
    char cr[MAX_UNIT];          /* \r in encoding, 'unit' bytes long. */
    char lf[MAX_UNIT];          /* \n in encoding, 'unit' bytes long. */
    char spaces[32];            /* 32 bytes worth of ' ' in encoding. */
  };

/* Opens a file handle for writing as a data file.

   The encoding of the file written is by default that of FH itself.  If
   ENCODING is nonnull, then it overrides the default encoding.

   *However*: ENCODING directly affects only text strings written by the data
   writer code itself, that is, new-lines in FH_MODE_TEXT and space padding in
   FH_MODE_FIXED mode.  The client must do its own encoding translation for the
   data that it writes.  (This is unavoidable because sometimes the data
   written includes binary data that reencoding would mangle.)  The client can
   obtain the encoding to re-encode into with dfm_writer_get_encoding(). */
struct dfm_writer *
dfm_open_writer (struct file_handle *fh, const char *encoding)
{
  struct encoding_info ei;
  struct dfm_writer *w;
  struct fh_lock *lock;
  int ofs;

  lock = fh_lock (fh, FH_REF_FILE, N_("data file"), FH_ACC_WRITE, false);
  if (lock == NULL)
    return NULL;

  w = fh_lock_get_aux (lock);
  if (w != NULL)
    return w;

  encoding = encoding_guess_parse_encoding (encoding != NULL
                                            ? encoding
                                            : fh_get_encoding (fh));
  get_encoding_info (&ei, encoding);

  w = xmalloc (sizeof *w);
  w->fh = fh_ref (fh);
  w->lock = lock;
  w->rf = replace_file_start (fh_get_file_name (w->fh), "wb", 0666,
                              &w->file, NULL);
  w->encoding = xstrdup (encoding);
  w->line_ends = fh_get_line_ends (fh);
  w->unit = ei.unit;
  memcpy (w->cr, ei.cr, sizeof w->cr);
  memcpy (w->lf, ei.lf, sizeof w->lf);
  for (ofs = 0; ofs + ei.unit <= sizeof w->spaces; ofs += ei.unit)
    memcpy (&w->spaces[ofs], ei.space, ei.unit);

  if (w->rf == NULL)
    {
      msg (ME, _("An error occurred while opening `%s' for writing "
                 "as a data file: %s."),
           fh_get_file_name (w->fh), strerror (errno));
      dfm_close_writer (w);
      return NULL;
    }
  fh_lock_set_aux (lock, w);

  return w;
}

/* Returns false if an I/O error occurred on WRITER, true otherwise. */
bool
dfm_write_error (const struct dfm_writer *writer)
{
  return ferror (writer->file);
}

/* Writes record REC (which need not be null-terminated) having
   length LEN to the file corresponding to HANDLE.  Adds any
   needed formatting, such as a trailing new-line.  Returns true
   on success, false on failure. */
bool
dfm_put_record (struct dfm_writer *w, const char *rec, size_t len)
{
  assert (w != NULL);

  if (dfm_write_error (w))
    return false;

  switch (fh_get_mode (w->fh))
    {
    case FH_MODE_TEXT:
      fwrite (rec, len, 1, w->file);
      if (w->line_ends == FH_END_CRLF)
        fwrite (w->cr, w->unit, 1, w->file);
      fwrite (w->lf, w->unit, 1, w->file);
      break;

    case FH_MODE_FIXED:
      {
        size_t record_width = fh_get_record_width (w->fh);
        size_t write_bytes = MIN (len, record_width);
        size_t pad_bytes = record_width - write_bytes;
        fwrite (rec, write_bytes, 1, w->file);
        while (pad_bytes > 0)
          {
            size_t chunk = MIN (pad_bytes, sizeof w->spaces);
            fwrite (w->spaces, chunk, 1, w->file);
            pad_bytes -= chunk;
          }
      }
      break;

    case FH_MODE_VARIABLE:
      {
        uint32_t size = len;
        integer_convert (INTEGER_NATIVE, &size, INTEGER_LSB_FIRST, &size,
                         sizeof size);
        fwrite (&size, sizeof size, 1, w->file);
        fwrite (rec, len, 1, w->file);
        fwrite (&size, sizeof size, 1, w->file);
      }
      break;

    case FH_MODE_360_VARIABLE:
    case FH_MODE_360_SPANNED:
      {
        size_t ofs = 0;
        if (fh_get_mode (w->fh) == FH_MODE_360_VARIABLE)
          len = MIN (65527, len);
        while (ofs < len)
          {
            size_t chunk = MIN (65527, len - ofs);
            uint32_t bdw = (chunk + 8) << 16;
            int scc = (ofs == 0 && chunk == len ? 0
                       : ofs == 0 ? 1
                       : ofs + chunk == len ? 2
                       : 3);
            uint32_t rdw = ((chunk + 4) << 16) | (scc << 8);

            integer_convert (INTEGER_NATIVE, &bdw, INTEGER_MSB_FIRST, &bdw,
                             sizeof bdw);
            integer_convert (INTEGER_NATIVE, &rdw, INTEGER_MSB_FIRST, &rdw,
                             sizeof rdw);
            fwrite (&bdw, 1, sizeof bdw, w->file);
            fwrite (&rdw, 1, sizeof rdw, w->file);
            fwrite (rec + ofs, 1, chunk, w->file);
            ofs += chunk;
          }
      }
      break;

    default:
      NOT_REACHED ();
    }

  return !dfm_write_error (w);
}

/* Closes data file writer W. */
bool
dfm_close_writer (struct dfm_writer *w)
{
  bool ok;

  if (w == NULL)
    return true;
  if (fh_unlock (w->lock))
    return true;

  ok = true;
  if (w->file != NULL)
    {
      const char *file_name = fh_get_file_name (w->fh);
      ok = !dfm_write_error (w) && !fn_close (file_name, w->file);

      if (!ok)
        msg (ME, _("I/O error occurred writing data file `%s'."), file_name);

      if (ok ? !replace_file_commit (w->rf) : !replace_file_abort (w->rf))
        ok = false;
    }
  fh_unref (w->fh);
  free (w->encoding);
  free (w);

  return ok;
}

/* Returns the encoding of data written to WRITER. */
const char *
dfm_writer_get_encoding (const struct dfm_writer *writer)
{
  return writer->encoding;
}
