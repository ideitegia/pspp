/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include "u8-istream.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <iconv.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <unistr.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/encoding-guesser.h"
#include "libpspp/i18n.h"

#include "gl/c-strcase.h"
#include "gl/localcharset.h"
#include "gl/minmax.h"

enum u8_istream_state
  {
    S_AUTO,                     /* Stream encoding not yet known. */
    S_UTF8,                     /* Stream encoding is known to be UTF-8. */
    S_CONVERT                   /* Stream encoding is known but not UTF-8. */
  };

struct u8_istream
  {
    int fd;
    iconv_t converter;
    enum u8_istream_state state;

    char *buffer;
    char *head;
    size_t length;

    char outbuf[4];
    size_t outlen;
  };

static ssize_t fill_buffer (struct u8_istream *);

/* Opens FILENAME, which is encoded in FROMCODE, for reading as an UTF-8
   stream, passing FLAGS to the open() function.  Returns a new u8_istream if
   successful, otherwise returns NULL and sets errno to an appropriate value.

   The accepted forms for FROMCODE are listed at the top of
   encoding-guesser.h. */
struct u8_istream *
u8_istream_for_file (const char *fromcode, const char *filename, int flags)
{
  struct u8_istream *is;
  int fd;

  assert (!(flags & O_CREAT));

  fd = open (filename, flags);
  if (fd < 0)
    return NULL;

  is = u8_istream_for_fd (fromcode, fd);
  if (is == NULL)
    {
      int save_errno = errno;
      close (fd);
      errno = save_errno;
    }

  return is;
}

/* Creates and returns a new u8_istream that reads its input from FD.  Returns
   a new u8_istream if successful, otherwise returns NULL and sets errno to an
   appropriate value.

   The accepted forms for FROMCODE are listed at the top of
   encoding-guesser.h. */
struct u8_istream *
u8_istream_for_fd (const char *fromcode, int fd)
{
  struct u8_istream *is;
  const char *encoding;

  is = malloc (sizeof *is);
  if (is == NULL)
    return NULL;

  is->fd = fd;
  is->converter = (iconv_t) -1;
  is->buffer = malloc (U8_ISTREAM_BUFFER_SIZE);
  if (is->buffer == NULL)
    goto error;
  is->head = is->buffer;
  is->length = 0;
  is->outlen = 0;

  if (fill_buffer (is) < 0)
    goto error;

  encoding = encoding_guess_head_encoding (fromcode, is->buffer, is->length);
  if (is_encoding_utf8 (encoding))
    {
      unsigned int bom_len;

      is->state = S_UTF8;
      bom_len = encoding_guess_bom_length (encoding, is->buffer, is->length);
      is->head += bom_len;
      is->length -= bom_len;
    }
  else
    {
      if (encoding_guess_encoding_is_auto (fromcode)
          && !strcmp (encoding, "ASCII"))
        {
          is->state = S_AUTO;
          encoding = encoding_guess_parse_encoding (fromcode);
        }
      else
        is->state = S_CONVERT;

      is->converter = iconv_open ("UTF-8", encoding);
      if (is->converter == (iconv_t) -1)
        goto error;
    }

  return is;

error:
  u8_istream_free (is);
  return NULL;
}

/* Closes IS and its underlying file descriptor and frees all associated
   resources.  Returns the return value from close(). */
int
u8_istream_close (struct u8_istream *is)
{
  if (is != NULL)
    {
      int fd = is->fd;
      u8_istream_free (is);
      return close (fd);
    }
  return 0;
}

/* Frees IS and associated resources, but does not close the underlying file
   descriptor.  (Thus, the client must close the file descriptor when it is no
   longer needed.) */
void
u8_istream_free (struct u8_istream *is)
{
  if (is != NULL)
    {
      if (is->converter != (iconv_t) -1)
        iconv_close (is->converter);
      free (is->buffer);
      free (is);
    }
}

static void
substitute_invalid_input_byte (struct u8_istream *is)
{
  assert (is->outlen == 0);
  is->head++;
  is->length--;
  is->outlen = u8_uctomb (CHAR_CAST (uint8_t *, is->outbuf),
                          0xfffd, sizeof is->outbuf);
}

static ssize_t
fill_buffer (struct u8_istream *is)
{
  ssize_t n;

  /* Move any unused bytes to the beginning of the input buffer. */
  if (is->length > 0 && is->buffer != is->head)
    memmove (is->buffer, is->head, is->length);
  is->head = is->buffer;

  /* Read more input. */
  n = 0;
  do
    {
      ssize_t retval = read (is->fd, is->buffer + is->length,
                             U8_ISTREAM_BUFFER_SIZE - is->length);
      if (retval > 0)
        {
          n += retval;
          is->length += retval;
        }
      else if (retval == 0)
        return n;
      else if (errno != EINTR)
        return n > 0 ? n : -1;
    }
  while (is->length < U8_ISTREAM_BUFFER_SIZE);
  return n;
}

static ssize_t
read_auto (struct u8_istream *is, char *buffer, size_t size)
{
  size_t original_size = size;
  int retval = 0;

  while (size > 0)
    {
      if (is->length > 0)
        {
          size_t n_ascii;

          n_ascii = encoding_guess_count_ascii (is->head,
                                                MIN (is->length, size));

          memcpy (buffer, is->head, n_ascii);
          buffer += n_ascii;
          size -= n_ascii;

          is->head += n_ascii;
          is->length -= n_ascii;

          if (size == 0)
            break;
        }

      if (is->length == 0)
        {
          retval = fill_buffer (is);
          if (retval > 0)
            continue;
          else
            break;
        }

      /* is->head points to a byte that isn't a printable ASCII character.
         Fill up the buffer and check for UTF-8. */
      fill_buffer (is);
      is->state = (encoding_guess_tail_is_utf8 (is->head, is->length)
                   ? S_UTF8 : S_CONVERT);
      if (size == original_size)
        return u8_istream_read (is, buffer, size);
      break;
    }

  return original_size - size;
}

static int
convert_iconv (iconv_t converter,
               char **inbufp, size_t *inbytesleft,
               char **outbufp, size_t *outbytesleft)
{
  size_t n = iconv (converter, (ICONV_CONST char **) inbufp, inbytesleft,
                    outbufp, outbytesleft);
  return n == SIZE_MAX ? errno : 0;
}

static int
convert_utf8 (iconv_t converter UNUSED,
              char **inbufp, size_t *inbytesleft,
              char **outbufp, size_t *outbytesleft)
{
  const uint8_t *in = CHAR_CAST (const uint8_t *, *inbufp);
  size_t n = MIN (*inbytesleft, *outbytesleft);
  size_t ofs = 0;
  int error;

  for (;;)
    {
      ucs4_t uc;
      int mblen;

      if (ofs >= n)
        {
          error = ofs < *inbytesleft ? E2BIG : 0;
          break;
        }

      mblen = u8_mbtouc (&uc, in + ofs, n - ofs);
      if (uc == 0xfffd)
        {
          int retval = u8_mbtoucr (&uc, in + ofs, *inbytesleft - ofs);
          if (retval == mblen)
            {
              /* There's an actual U+FFFD in the input stream.  Carry on. */
            }
          else
            {
              error = (retval == -1 ? EILSEQ
                       : retval == -2 ? EINVAL
                       : E2BIG);
              break;
            }
        }

      ofs += mblen;
    }

  if (ofs > 0)
    {
      memcpy (*outbufp, *inbufp, ofs);
      *inbufp += ofs;
      *inbytesleft -= ofs;
      *outbufp += ofs;
      *outbytesleft -= ofs;
    }

  return error;
}

static ssize_t
read_convert (struct u8_istream *is,
              int (*convert) (iconv_t converter,
                              char **inbufp, size_t *inbytesleft,
                              char **outbufp, size_t *outbytesleft),
              char *buffer, size_t size)
{
  size_t original_size = size;

  while (size > 0)
    {
      ssize_t n_read;

      if (is->outlen > 0)
        {
          size_t n = MIN (size, is->outlen);

          memcpy (buffer, is->outbuf, n);
          is->outlen -= n;
          if (is->outlen > 0)
            memmove (is->outbuf, is->outbuf + n, is->outlen);

          buffer += n;
          size -= n;

          if (size == 0)
            break;
        }

      if (is->length)
        {
          int error = convert (is->converter,
                               &is->head, &is->length,
                               &buffer, &size);
          if (size == 0)
            break;

          switch (error)
            {
            case 0:
              /* Converted all of the input into output, possibly with space
                 for output left over.

                 Read more input. */
              break;

            case EILSEQ:
              substitute_invalid_input_byte (is);
              continue;

            case EINVAL:
              /* Incomplete byte sequence at end of input.  Read more
                 input. */
              break;

            default:
              /* A real error of some kind (ENOMEM?). */
              return -1;

            case E2BIG:
              /* Ran out of room for output.
                 Convert into outbuf and copy from there instead. */
              {
                char *outptr = is->outbuf;
                size_t outleft = sizeof is->outbuf;

                error = convert (is->converter,
                                 &is->head, &is->length,
                                 &outptr, &outleft);
                is->outlen = outptr - is->outbuf;
                if (is->outlen > 0)
                  continue;

                switch (error)
                  {
                  case EILSEQ:
                    substitute_invalid_input_byte (is);
                    continue;

                  case E2BIG:
                  case EINVAL:
                    continue;

                  default:
                    /* A real error of some kind (ENOMEM?). */
                    return -1;
                  }
              }
            }
        }

      assert (is->length <= MB_LEN_MAX);
      n_read = fill_buffer (is);
      if (n_read <= 0)
        {
          if (original_size != size)
            {
              /* We produced some output so don't report EOF or error yet. */
              break;
            }
          else if (n_read == 0 && is->length != 0)
            {
              /* Incomplete byte sequence at end of file. */
              substitute_invalid_input_byte (is);
            }
          else
            {
              /* Propagate end-of-file or error to caller. */
              return n_read;
            }
        }
    }

  return original_size - size;
}

/* Reads up to SIZE bytes of UTF-8 text from IS into BUFFER.  Returns the
   number of bytes read if successful, 0 at end of file, or -1 if an error
   occurred before any data could be read.  Upon error, sets errno to an
   appropriate value. */
ssize_t
u8_istream_read (struct u8_istream *is, char *buffer, size_t size)
{
  switch (is->state)
    {
    case S_CONVERT:
      return read_convert (is, convert_iconv, buffer, size);

    case S_AUTO:
      return read_auto (is, buffer, size);

    case S_UTF8:
      return read_convert (is, convert_utf8, buffer, size);
    }

  NOT_REACHED ();
}

/* Returns the file descriptor underlying IS. */
int
u8_istream_fileno (const struct u8_istream *is)
{
  return is->fd;
}

/* Test functions.

   These functions are probably useful only for white-box testing. */

/* Returns true if the encoding of the file being read by IS is not yet
   known. */
bool
u8_istream_is_auto (const struct u8_istream *is)
{
  return is->state == S_AUTO;
}

/* Returns true if the encoding of the file being read by IS has been
   determined to be UTF-8. */
bool
u8_istream_is_utf8 (const struct u8_istream *is)
{
  return is->state == S_UTF8;
}
