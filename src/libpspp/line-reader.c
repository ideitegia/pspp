/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2014 Free Software Foundation, Inc.

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

#include "line-reader.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libpspp/assertion.h"
#include "libpspp/encoding-guesser.h"
#include "libpspp/i18n.h"
#include "libpspp/str.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

enum line_reader_state
  {
    S_UNIBYTE,                  /* Known stream encoding, 1-byte unit. */
    S_MULTIBYTE,                /* Known stream encoding, multibyte unit. */
    S_AUTO                      /* Encoding autodetection in progress. */
  };

struct line_reader
  {
    int fd;
    enum line_reader_state state;
    struct encoding_info encoding_info;

    char *encoding;             /* Current encoding. */
    char *auto_encoding;        /* In S_AUTO mode, user-specified encoding. */

    char *buffer;
    char *head;
    size_t length;

    int error;
    bool eof;
  };

static ssize_t fill_buffer (struct line_reader *);

/* Opens FILENAME, which is encoded in ENCODING, for reading line by line,
   passing FLAGS to the open() function.  Returns a new line_reader if
   successful, otherwise returns NULL and sets errno to an appropriate value.

   The accepted forms for ENCODING are listed at the top of
   encoding-guesser.h. */
struct line_reader *
line_reader_for_file (const char *encoding, const char *filename, int flags)
{
  struct line_reader *r;
  int fd;

  assert (!(flags & O_CREAT));

  fd = open (filename, flags);
  if (fd < 0)
    return NULL;

  r = line_reader_for_fd (encoding, fd);
  if (r == NULL)
    {
      int save_errno = errno;
      close (fd);
      errno = save_errno;
    }

  return r;
}

/* Creates and returns a new line_reader that reads its input from FD.  Returns
   a new line_reader if successful, otherwise returns NULL and sets errno to an
   appropriate value.

   The accepted forms for ENCODING are listed at the top of
   encoding-guesser.h. */
struct line_reader *
line_reader_for_fd (const char *encoding, int fd)
{
  struct line_reader *r;

  r = calloc (1, sizeof *r);
  if (r == NULL)
    return NULL;

  r->fd = fd;
  r->buffer = malloc (LINE_READER_BUFFER_SIZE);
  if (r->buffer == NULL)
    goto error;
  r->head = r->buffer;
  r->length = 0;

  if (fill_buffer (r) < 0)
    goto error;

  r->encoding = xstrdup (encoding_guess_head_encoding (
                           encoding, r->buffer, r->length));
  if (!get_encoding_info (&r->encoding_info, r->encoding))
    {
      errno = EINVAL;
      goto error;
    }

  if (encoding_guess_encoding_is_auto (encoding)
      && !strcmp (r->encoding, "ASCII"))
    {
      r->state = S_AUTO;
      r->auto_encoding = encoding ? xstrdup (encoding) : NULL;
    }
  else
    r->state = r->encoding_info.unit == 1 ? S_UNIBYTE : S_MULTIBYTE;

  return r;

error:
  line_reader_free (r);
  return NULL;
}

/* Closes R and its underlying file descriptor and frees all associated
   resources.  Returns the return value from close(). */
int
line_reader_close (struct line_reader *r)
{
  if (r != NULL)
    {
      int fd = r->fd;
      line_reader_free (r);
      return close (fd);
    }
  return 0;
}

/* Frees R and associated resources, but does not close the underlying file
   descriptor.  (Thus, the client must close the file descriptor when it is no
   longer needed.) */
void
line_reader_free (struct line_reader *r)
{
  if (r != NULL)
    {
      free (r->buffer);
      free (r->encoding);
      free (r->auto_encoding);
      free (r);
    }
}

static ssize_t
fill_buffer (struct line_reader *r)
{
  ssize_t n;

  /* Move any unused bytes to the beginning of the input buffer. */
  if (r->length > 0 && r->buffer != r->head)
    memmove (r->buffer, r->head, r->length);
  r->head = r->buffer;

  /* Read more input. */
  do
    {
      n = read (r->fd, r->buffer + r->length,
                LINE_READER_BUFFER_SIZE - r->length);
    }
  while (n < 0 && errno == EINTR);
  if (n > 0)
    r->length += n;
  else if (n < 0)
    r->error = errno;
  else
    r->eof = true;
  return n;
}

static void
output_bytes (struct line_reader *r, struct string *s, size_t n)
{
  ds_put_substring (s, ss_buffer (r->head, n));
  r->head += n;
  r->length -= n;
}

static void
output_line (struct line_reader *r, struct string *s, size_t n)
{
  int unit = r->encoding_info.unit;

  output_bytes (r, s, n);

  r->head += unit;
  r->length -= unit;

  ds_chomp (s, ss_buffer (r->encoding_info.cr, unit));
}

/* Reads a line of text, but no more than MAX_LENGTH bytes, from R and appends
   it to S, omitting the final new-line and the carriage return that
   immediately precedes it, if one is present.  The line is left in its
   original encoding.

   Returns true if anything was successfully read from the file.  (If an empty
   line was read, then nothing is appended to S.)  Returns false if end of file
   was reached or a read error occurred before any text could be read. */
bool
line_reader_read (struct line_reader *r, struct string *s, size_t max_length)
{
  size_t original_length = ds_length (s);
  int unit = r->encoding_info.unit;

  do
    {
      size_t max_out = max_length - (ds_length (s) - original_length);
      size_t max_in = r->length;
      size_t max = MIN (max_in, max_out);
      size_t n;
      char *p;

      if (max_out < unit)
        break;

      switch (r->state)
        {
        case S_UNIBYTE:
          p = memchr (r->head, r->encoding_info.lf[0], max);
          if (p != NULL)
            {
              output_line (r, s, p - r->head);
              return true;
            }
          n = max;
          break;

        case S_MULTIBYTE:
          for (n = 0; n + unit <= max; n += unit)
            if (!memcmp (r->head + n, r->encoding_info.lf, unit))
              {
                output_line (r, s, n);
                return true;
              }
          break;

        case S_AUTO:
          for (n = 0; n < max; n++)
            if (!encoding_guess_is_ascii_text (r->head[n]))
              {
                char *encoding;

                output_bytes (r, s, n);
                fill_buffer (r);
                r->state = S_UNIBYTE;

                encoding = xstrdup (encoding_guess_tail_encoding (
                                      r->auto_encoding, r->head, r->length));
                free (r->encoding);
                r->encoding = encoding;

                free (r->auto_encoding);
                r->auto_encoding = NULL;

                n = 0;
                break;
              }
            else if (r->head[n] == '\n')
              {
                output_line (r, s, n);
                return true;
              }
          break;

        default:
          NOT_REACHED ();
        }

      output_bytes (r, s, n);
    }
  while (r->length >= unit || fill_buffer (r) > 0);

  return ds_length (s) > original_length;
}

/* Returns the file descriptor underlying R. */
int
line_reader_fileno (const struct line_reader *r)
{
  return r->fd;
}

/* Returns the offset in the file of the next byte to be read from R, or -1 on
   error (e.g. if the file is not seekable). */
off_t
line_reader_tell (const struct line_reader *r)
{
  off_t pos = lseek (r->fd, 0, SEEK_CUR);
  return (pos < 0 ? pos
          : pos >= r->length ? pos - r->length
          : 0);
}

/* Returns true if end of file has been encountered reading R. */
bool
line_reader_eof (const struct line_reader *r)
{
  return r->eof && !r->length;
}

/* Returns an nonzero errno value if an error has been encountered reading
   R, zero otherwise. */
int
line_reader_error (const struct line_reader *r)
{
  return !r->length ? r->error : 0;
}

/* Returns the encoding of R.  If line_reader_is_auto(R) returns true, the
   encoding might change as more lines are read. */
const char *
line_reader_get_encoding (const struct line_reader *r)
{
  return r->encoding;
}

/* Returns true if the encoding of the file being read by R is not yet
   completely known.  If this function returns true, then the encoding returned
   by line_reader_get_encoding() might change as more lines are read (and after
   the change, this function will return false). */
bool
line_reader_is_auto (const struct line_reader *r)
{
  return r->state == S_AUTO;
}
