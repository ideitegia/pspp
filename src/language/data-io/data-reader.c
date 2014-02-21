/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-2004, 2006, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "language/data-io/data-reader.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "data/casereader.h"
#include "data/dataset.h"
#include "data/file-handle-def.h"
#include "data/file-name.h"
#include "language/command.h"
#include "language/data-io/file-handle.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/encoding-guesser.h"
#include "libpspp/integer-format.h"
#include "libpspp/line-reader.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

/* Flags for DFM readers. */
enum dfm_reader_flags
  {
    DFM_ADVANCE = 002,          /* Read next line on dfm_get_record() call? */
    DFM_SAW_BEGIN_DATA = 004,   /* For inline_file only, whether we've
                                   already read a BEGIN DATA line. */
    DFM_TABS_EXPANDED = 010,    /* Tabs have been expanded. */
    DFM_CONSUME = 020           /* read_inline_record() should get a token? */
  };

/* Data file reader. */
struct dfm_reader
  {
    struct file_handle *fh;     /* File handle. */
    struct fh_lock *lock;       /* Mutual exclusion lock for file. */
    int line_number;            /* Current line or record number. */
    struct string line;         /* Current line. */
    struct string scratch;      /* Extra line buffer. */
    enum dfm_reader_flags flags; /* Zero or more of DFM_*. */
    FILE *file;                 /* Associated file. */
    off_t file_size;            /* File size, or -1 if unavailable. */
    size_t pos;                 /* Offset in line of current character. */
    unsigned eof_cnt;           /* # of attempts to advance past EOF. */
    struct lexer *lexer;        /* The lexer reading the file */
    char *encoding;             /* Current encoding. */

    /* For FH_MODE_TEXT only. */
    struct line_reader *line_reader;

    /* For FH_MODE_360_VARIABLE and FH_MODE_360_SPANNED files only. */
    size_t block_left;          /* Bytes left in current block. */
  };

/* Closes reader R opened by dfm_open_reader(). */
void
dfm_close_reader (struct dfm_reader *r)
{
  if (r == NULL)
    return;

  if (fh_unlock (r->lock))
    {
      /* File is still locked by another client. */
      return;
    }

  /* This was the last client, so close the underlying file. */
  if (fh_get_referent (r->fh) != FH_REF_INLINE)
    fn_close (fh_get_file_name (r->fh), r->file);
  else
    {
      /* Skip any remaining data on the inline file. */
      if (r->flags & DFM_SAW_BEGIN_DATA)
        {
          dfm_reread_record (r, 0);
          while (!dfm_eof (r))
            dfm_forward_record (r);
        }
    }

  line_reader_free (r->line_reader);
  free (r->encoding);
  fh_unref (r->fh);
  ds_destroy (&r->line);
  ds_destroy (&r->scratch);
  free (r);
}

/* Opens the file designated by file handle FH for reading as a data file.
   Returns a reader if successful, or a null pointer otherwise.

   If FH is fh_inline_file() then the new reader reads data included inline in
   the command file between BEGIN FILE and END FILE, obtaining data from LEXER.
   LEXER must remain valid as long as the new reader is in use.  ENCODING is
   ignored.

   If FH is not fh_inline_file(), then the encoding of the file read is by
   default that of FH itself.  If ENCODING is nonnull, then it overrides the
   default encoding.  LEXER is ignored. */
struct dfm_reader *
dfm_open_reader (struct file_handle *fh, struct lexer *lexer,
                 const char *encoding)
{
  struct dfm_reader *r;
  struct fh_lock *lock;

  /* TRANSLATORS: this fragment will be interpolated into
     messages in fh_lock() that identify types of files. */
  lock = fh_lock (fh, FH_REF_FILE | FH_REF_INLINE, N_("data file"),
                  FH_ACC_READ, false);
  if (lock == NULL)
    return NULL;

  r = fh_lock_get_aux (lock);
  if (r != NULL)
    return r;

  r = xmalloc (sizeof *r);
  r->fh = fh_ref (fh);
  r->lock = lock;
  r->lexer = lexer;
  ds_init_empty (&r->line);
  ds_init_empty (&r->scratch);
  r->flags = DFM_ADVANCE;
  r->eof_cnt = 0;
  r->block_left = 0;
  if (fh_get_referent (fh) != FH_REF_INLINE)
    {
      struct stat s;
      r->line_number = 0;
      r->file = fn_open (fh_get_file_name (fh), "rb");
      if (r->file == NULL)
        {
          msg (ME, _("Could not open `%s' for reading as a data file: %s."),
               fh_get_file_name (r->fh), strerror (errno));
          goto error;
        }
      r->file_size = fstat (fileno (r->file), &s) == 0 ? s.st_size : -1;
    }
  else
    r->file_size = -1;
  fh_lock_set_aux (lock, r);

  if (encoding == NULL)
    encoding = fh_get_encoding (fh);
  if (fh_get_referent (fh) == FH_REF_FILE && fh_get_mode (fh) == FH_MODE_TEXT)
    {
      r->line_reader = line_reader_for_fd (encoding, fileno (r->file));
      if (r->line_reader == NULL)
        {
          msg (ME, _("Could not read `%s' as a text file with encoding `%s': "
                     "%s."),
               fh_get_file_name (r->fh), encoding, strerror (errno));
          goto error;
        }
      r->encoding = xstrdup (line_reader_get_encoding (r->line_reader));
    }
  else
    {
      r->line_reader = NULL;
      r->encoding = xstrdup (encoding_guess_parse_encoding (encoding));
    }

  return r;

error:
  fh_unlock (r->lock);
  fh_unref (fh);
  free (r);
  return NULL;
}

/* Returns true if an I/O error occurred on READER, false otherwise. */
bool
dfm_reader_error (const struct dfm_reader *r)
{
  return (fh_get_referent (r->fh) == FH_REF_FILE
          && (r->line_reader != NULL
              ? line_reader_error (r->line_reader) != 0
              : ferror (r->file)));
}

/* Reads a record from the inline file into R.
   Returns true if successful, false on failure. */
static bool
read_inline_record (struct dfm_reader *r)
{
  if ((r->flags & DFM_SAW_BEGIN_DATA) == 0)
    {
      r->flags |= DFM_SAW_BEGIN_DATA;
      r->flags &= ~DFM_CONSUME;

      while (lex_token (r->lexer) == T_ENDCMD)
        lex_get (r->lexer);

      if (!lex_force_match_id (r->lexer, "BEGIN")
          || !lex_force_match_id (r->lexer, "DATA"))
        return false;

      lex_match (r->lexer, T_ENDCMD);
    }

  if (r->flags & DFM_CONSUME)
    lex_get (r->lexer);

  if (!lex_is_string (r->lexer))
    {
      if (!lex_match_id (r->lexer, "END") || !lex_match_id (r->lexer, "DATA"))
        {
          msg (SE, _("Missing %s while reading inline data.  "
                     "This probably indicates a missing or incorrectly "
                     "formatted %s command.  %s must appear "
                     "by itself on a single line with exactly one space "
                     "between words."), "END DATA", "END DATA", "END DATA");
          lex_discard_rest_of_command (r->lexer);
        }
      return false;
    }

  ds_assign_substring (&r->line, lex_tokss (r->lexer));
  r->flags |= DFM_CONSUME;

  return true;
}

/* Report a read error on R. */
static void
read_error (struct dfm_reader *r)
{
  msg (ME, _("Error reading file %s: %s."),
       fh_get_name (r->fh), strerror (errno));
}

/* Report a partial read at end of file reading R. */
static void
partial_record (struct dfm_reader *r)
{
  msg (ME, _("Unexpected end of file in partial record reading %s."),
       fh_get_name (r->fh));
}

/* Tries to read SIZE bytes from R into BUFFER.  Returns 1 if
   successful, 0 if end of file was reached before any bytes
   could be read, and -1 if some bytes were read but fewer than
   SIZE due to end of file or an error mid-read.  In the latter
   case, reports an error. */
static int
try_to_read_fully (struct dfm_reader *r, void *buffer, size_t size)
{
  size_t bytes_read = fread (buffer, 1, size, r->file);
  if (bytes_read == size)
    return 1;
  else if (bytes_read == 0)
    return 0;
  else
    {
      partial_record (r);
      return -1;
    }
}

/* Type of a descriptor word. */
enum descriptor_type
  {
    BLOCK,
    RECORD
  };

/* Reads a block descriptor word or record descriptor word
   (according to TYPE) from R.  Returns 1 if successful, 0 if
   end of file was reached before any bytes could be read, -1 if
   an error occurred.  Reports an error in the latter case.

   If successful, stores the number of remaining bytes in the
   block or record (that is, the block or record length, minus
   the 4 bytes in the BDW or RDW itself) into *REMAINING_SIZE.
   If SEGMENT is nonnull, also stores the segment control
   character (SCC) into *SEGMENT. */
static int
read_descriptor_word (struct dfm_reader *r, enum descriptor_type type,
                      size_t *remaining_size, int *segment)
{
  uint8_t raw_descriptor[4];
  int status;

  status = try_to_read_fully (r, raw_descriptor, sizeof raw_descriptor);
  if (status <= 0)
    return status;

  *remaining_size = (raw_descriptor[0] << 8) | raw_descriptor[1];
  if (segment != NULL)
    *segment = raw_descriptor[2];

  if (*remaining_size < 4)
    {
      msg (ME,
           (type == BLOCK
            ? _("Corrupt block descriptor word at offset 0x%lx in %s.")
            : _("Corrupt record descriptor word at offset 0x%lx in %s.")),
           (long) ftello (r->file) - 4, fh_get_name (r->fh));
      return -1;
    }

  *remaining_size -= 4;
  return 1;
}

/* Reports that reader R has read a corrupt record size. */
static void
corrupt_size (struct dfm_reader *r)
{
  msg (ME, _("Corrupt record size at offset 0x%lx in %s."),
       (long) ftello (r->file) - 4, fh_get_name (r->fh));
}

/* Reads a 32-byte little-endian signed number from R and stores
   its value into *SIZE_OUT.  Returns 1 if successful, 0 if end
   of file was reached before any bytes could be read, -1 if an
   error occurred.  Reports an error in the latter case.  Numbers
   less than 0 are considered errors. */
static int
read_size (struct dfm_reader *r, size_t *size_out)
{
  int32_t size;
  int status;

  status = try_to_read_fully (r, &size, sizeof size);
  if (status <= 0)
    return status;

  integer_convert (INTEGER_LSB_FIRST, &size, INTEGER_NATIVE, &size,
                   sizeof size);
  if (size < 0)
    {
      corrupt_size (r);
      return -1;
    }

  *size_out = size;
  return 1;
}

static bool
read_text_record (struct dfm_reader *r)
{
  bool is_auto;
  bool ok;

  /* Read a line.  If the line reader's encoding changes, update r->encoding to
     match. */
  is_auto = line_reader_is_auto (r->line_reader);
  ok = line_reader_read (r->line_reader, &r->line, SIZE_MAX);
  if (is_auto && !line_reader_is_auto (r->line_reader))
    {
      free (r->encoding);
      r->encoding = xstrdup (line_reader_get_encoding (r->line_reader));
    }

  /* Detect and report read error. */
  if (!ok)
    {
      int error = line_reader_error (r->line_reader);
      if (error != 0)
        msg (ME, _("Error reading file %s: %s."),
             fh_get_name (r->fh), strerror (error));
    }

  return ok;
}

/* Reads a record from a disk file into R.
   Returns true if successful, false on error or at end of file. */
static bool
read_file_record (struct dfm_reader *r)
{
  assert (r->fh != fh_inline_file ());

  ds_clear (&r->line);
  switch (fh_get_mode (r->fh))
    {
    case FH_MODE_TEXT:
      return read_text_record (r);

    case FH_MODE_FIXED:
      if (ds_read_stream (&r->line, 1, fh_get_record_width (r->fh), r->file))
        return true;
      else
        {
          if (ferror (r->file))
            read_error (r);
          else if (!ds_is_empty (&r->line))
            partial_record (r);
          return false;
        }

    case FH_MODE_VARIABLE:
      {
        size_t leading_size;
        size_t trailing_size;
        int status;

        /* Read leading record size. */
        status = read_size (r, &leading_size);
        if (status <= 0)
          return false;

        /* Read record data. */
        if (!ds_read_stream (&r->line, leading_size, 1, r->file))
          {
            if (ferror (r->file))
              read_error (r);
            else
              partial_record (r);
            return false;
          }

        /* Read trailing record size and check that it's the same
           as the leading record size. */
        status = read_size (r, &trailing_size);
        if (status <= 0)
          {
            if (status == 0)
              partial_record (r);
            return false;
          }
        if (leading_size != trailing_size)
          {
            corrupt_size (r);
            return false;
          }

        return true;
      }

    case FH_MODE_360_VARIABLE:
    case FH_MODE_360_SPANNED:
      for (;;)
        {
          size_t record_size;
          int segment;
          int status;

          /* If we've exhausted our current block, start another
             one by reading the new block descriptor word. */
          if (r->block_left == 0)
            {
              status = read_descriptor_word (r, BLOCK, &r->block_left, NULL);
              if (status < 0)
                return false;
              else if (status == 0)
                return !ds_is_empty (&r->line);
            }

          /* Read record descriptor. */
          if (r->block_left < 4)
            {
              partial_record (r);
              return false;
            }
          r->block_left -= 4;
          status = read_descriptor_word (r, RECORD, &record_size, &segment);
          if (status <= 0)
            {
              if (status == 0)
                partial_record (r);
              return false;
            }
          if (record_size > r->block_left)
            {
              msg (ME, _("Record exceeds remaining block length."));
              return false;
            }

          /* Read record data. */
          if (!ds_read_stream (&r->line, record_size, 1, r->file))
            {
              if (ferror (r->file))
                read_error (r);
              else
                partial_record (r);
              return false;
            }
          r->block_left -= record_size;

          /* In variable mode, read only a single record.
             In spanned mode, a segment value of 0 should
             designate a whole record without spanning, 1 the
             first segment in a record, 2 the last segment in a
             record, and 3 an intermediate segment in a record.
             For compatibility, though, we actually pay attention
             only to whether the segment value is even or odd. */
          if (fh_get_mode (r->fh) == FH_MODE_360_VARIABLE
              || (segment & 1) == 0)
            return true;
        }
    }

  NOT_REACHED ();
}

/* Reads a record from R, setting the current position to the
   start of the line.  If an error occurs or end-of-file is
   encountered, the current line is set to null. */
static bool
read_record (struct dfm_reader *r)
{
  if (fh_get_referent (r->fh) == FH_REF_FILE)
    {
      bool ok = read_file_record (r);
      if (ok)
        r->line_number++;
      return ok;
    }
  else
    return read_inline_record (r);
}

/* Returns the number of attempts, thus far, to advance past
   end-of-file in reader R.  Reads forward in HANDLE's file, if
   necessary, to find out.

   Normally, the user stops attempting to read from the file the
   first time EOF is reached (a return value of 1).  If the user
   tries to read past EOF again (a return value of 2 or more),
   an error message is issued, and the caller should more
   forcibly abort to avoid an infinite loop. */
unsigned
dfm_eof (struct dfm_reader *r)
{
  if (r->flags & DFM_ADVANCE)
    {
      r->flags &= ~DFM_ADVANCE;

      if (r->eof_cnt == 0 && read_record (r) )
        {
          r->pos = 0;
          return 0;
        }

      r->eof_cnt++;
      if (r->eof_cnt == 2)
        {
          if (r->fh != fh_inline_file ())
            msg (ME, _("Attempt to read beyond end-of-file on file %s."),
                 fh_get_name (r->fh));
          else
            msg (ME, _("Attempt to read beyond %s."), "END DATA");
        }
    }

  return r->eof_cnt;
}

/* Returns the current record in the file corresponding to
   HANDLE.  Aborts if reading from the file is necessary or at
   end of file, so call dfm_eof() first. */
struct substring
dfm_get_record (struct dfm_reader *r)
{
  assert ((r->flags & DFM_ADVANCE) == 0);
  assert (r->eof_cnt == 0);

  return ds_substr (&r->line, r->pos, SIZE_MAX);
}

/* Expands tabs in the current line into the equivalent number of
   spaces, if appropriate for this kind of file.  Aborts if
   reading from the file is necessary or at end of file, so call
   dfm_eof() first.*/
void
dfm_expand_tabs (struct dfm_reader *r)
{
  size_t ofs, new_pos, tab_width;

  assert ((r->flags & DFM_ADVANCE) == 0);
  assert (r->eof_cnt == 0);

  if (r->flags & DFM_TABS_EXPANDED)
    return;
  r->flags |= DFM_TABS_EXPANDED;

  if (r->fh != fh_inline_file ()
      && (fh_get_mode (r->fh) != FH_MODE_TEXT
          || fh_get_tab_width (r->fh) == 0
          || ds_find_byte (&r->line, '\t') == SIZE_MAX))
    return;

  /* Expand tabs from r->line into r->scratch, and figure out
     new value for r->pos. */
  tab_width = fh_get_tab_width (r->fh);
  ds_clear (&r->scratch);
  new_pos = SIZE_MAX;
  for (ofs = 0; ofs < ds_length (&r->line); ofs++)
    {
      unsigned char c;

      if (ofs == r->pos)
        new_pos = ds_length (&r->scratch);

      c = ds_data (&r->line)[ofs];
      if (c != '\t')
        ds_put_byte (&r->scratch, c);
      else
        {
          do
            ds_put_byte (&r->scratch, ' ');
          while (ds_length (&r->scratch) % tab_width != 0);
        }
    }
  if (new_pos == SIZE_MAX)
    {
      /* Maintain the same relationship between position and line
         length that we had before.  DATA LIST uses a
         beyond-the-end position to deal with an empty field at
         the end of the line. */
      assert (r->pos >= ds_length (&r->line));
      new_pos = (r->pos - ds_length (&r->line)) + ds_length (&r->scratch);
    }

  /* Swap r->line and r->scratch and set new r->pos. */
  ds_swap (&r->line, &r->scratch);
  r->pos = new_pos;
}

/* Returns the character encoding of data read from READER. */
const char *
dfm_reader_get_encoding (const struct dfm_reader *reader)
{
  return reader->encoding;
}

/* Returns a number between 0 and 100 that approximates the
   percentage of the data in READER that has already been read,
   or -1 if this value cannot be estimated.

   ftello is slow in glibc (it flushes the read buffer), so don't
   call this function unless you need to. */
int
dfm_get_percent_read (const struct dfm_reader *reader)
{
  if (reader->file_size >= 0)
    {
      off_t position;

      position = (reader->line_reader != NULL
                  ? line_reader_tell (reader->line_reader)
                  : ftello (reader->file));
      if (position >= 0)
        {
          double p = 100.0 * position / reader->file_size;
          return p < 0 ? 0 : p > 100 ? 100 : p;
        }
    }
  return -1;
}

/* Causes dfm_get_record() or dfm_get_whole_record() to read in
   the next record the next time it is executed on file
   HANDLE. */
void
dfm_forward_record (struct dfm_reader *r)
{
  r->flags |= DFM_ADVANCE;
}

/* Cancels the effect of any previous dfm_fwd_record() executed
   on file HANDLE.  Sets the current line to begin in the 1-based
   column COLUMN.  */
void
dfm_reread_record (struct dfm_reader *r, size_t column)
{
  r->flags &= ~DFM_ADVANCE;
  r->pos = MAX (column, 1) - 1;
}

/* Sets the current line to begin COLUMNS characters following
   the current start. */
void
dfm_forward_columns (struct dfm_reader *r, size_t columns)
{
  dfm_reread_record (r, (r->pos + 1) + columns);
}

/* Returns the 1-based column to which the line pointer in HANDLE
   is set.  Unless dfm_reread_record() or dfm_forward_columns()
   have been called, this is 1. */
size_t
dfm_column_start (const struct dfm_reader *r)
{
  return r->pos + 1;
}

/* Returns the number of columns we are currently beyond the end
   of the line.  At or before end-of-line, this is 0; one column
   after end-of-line, this is 1; and so on. */
size_t
dfm_columns_past_end (const struct dfm_reader *r)
{
  return r->pos < ds_length (&r->line) ? 0 : ds_length (&r->line) - r->pos;
}

/* Returns the 1-based column within the current line that P
   designates. */
size_t
dfm_get_column (const struct dfm_reader *r, const char *p)
{
  return ds_pointer_to_position (&r->line, p) + 1;
}

const char *
dfm_get_file_name (const struct dfm_reader *r)
{
  return (fh_get_referent (r->fh) == FH_REF_FILE
          ? fh_get_file_name (r->fh)
          : NULL);
}

int
dfm_get_line_number (const struct dfm_reader *r)
{
  return fh_get_referent (r->fh) == FH_REF_FILE ? r->line_number : -1;
}

/* BEGIN DATA...END DATA procedure. */

/* Perform BEGIN DATA...END DATA as a procedure in itself. */
int
cmd_begin_data (struct lexer *lexer, struct dataset *ds)
{
  struct dfm_reader *r;
  bool ok;

  if (!fh_is_locked (fh_inline_file (), FH_ACC_READ))
    {
      msg (SE, _("This command is not valid here since the current "
                 "input program does not access the inline file."));
      return CMD_CASCADING_FAILURE;
    }
  lex_match (lexer, T_ENDCMD);

  /* Open inline file. */
  r = dfm_open_reader (fh_inline_file (), lexer, NULL);
  r->flags |= DFM_SAW_BEGIN_DATA;
  r->flags &= ~DFM_CONSUME;

  /* Input procedure reads from inline file. */
  casereader_destroy (proc_open (ds));
  ok = proc_commit (ds);
  dfm_close_reader (r);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}
