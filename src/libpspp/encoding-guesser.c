/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include "libpspp/encoding-guesser.h"

#include <errno.h>
#include <iconv.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistr.h>

#include "libpspp/cast.h"
#include "libpspp/i18n.h"

#include "gl/localcharset.h"
#include "gl/c-strcase.h"

/* http://www.w3.org/TR/REC-xml/#sec-guessing-no-ext-info is a useful source
   of information about encoding detection.
*/

/* Returns the encoding specified by ENCODING, which must be in one of the
   forms described at the top of encoding-guesser.h.  The returned string might
   be ENCODING itself or a suffix of it, or it might be a statically allocated
   string. */
const char *
encoding_guess_parse_encoding (const char *encoding)
{
  if (encoding == NULL
      || !c_strcasecmp (encoding, "auto")
      || !c_strcasecmp (encoding, "auto,locale")
      || !c_strcasecmp (encoding, "locale"))
    return locale_charset ();
  else if (!c_strncasecmp (encoding, "auto,", 5))
    return encoding + 5;
  else
    return encoding;
}

/* Returns true if ENCODING, which must be in one of the forms described at the
   top of encoding-guesser.h, is one that performs encoding autodetection,
   false otherwise. */
bool
encoding_guess_encoding_is_auto (const char *encoding)
{
  return (encoding == NULL
          || (!c_strncasecmp (encoding, "auto", 4)
              && (encoding[4] == ',' || encoding[4] == '\0')));
}

static uint16_t
get_be16 (const uint8_t *data)
{
  return (data[0] << 8) | data[1];
}

static uint16_t
get_le16 (const uint8_t *data)
{
  return (data[1] << 8) | data[0];
}

static uint32_t
get_be32 (const uint8_t *data)
{
  return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];

}

static uint32_t
get_le32 (const uint8_t *data)
{
  return (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
}

static const char *
guess_utf16 (const uint8_t *data, size_t n)
{
  size_t even_nulls, odd_nulls;

  if (n < ENCODING_GUESS_MIN && n % 2 != 0)
    return NULL;

  even_nulls = odd_nulls = 0;
  while (n >= 2)
    {
      even_nulls += data[0] == 0;
      odd_nulls += data[1] == 0;
      if (data[0] == 0 && data[1] == 0)
        return NULL;

      data += 2;
      n -= 2;
    }

  if (odd_nulls > even_nulls)
    return "UTF-16LE";
  else if (even_nulls > 0)
    return "UTF-16BE";
  else
    return NULL;
}

static bool
is_utf32 (const uint8_t *data, size_t n, uint32_t (*get_u32) (const uint8_t *))
{
  if (n < ENCODING_GUESS_MIN && n % 4 != 0)
    return false;

  while (n >= 4)
    {
      uint32_t uc = get_u32 (data);

      if (uc < 0x09 || uc > 0x10ffff)
        return false;

      data += 4;
      n -= 4;
    }

  return true;
}

/* Counts and returns the number of bytes, but no more than N, starting at S
   that are ASCII text characters. */
size_t
encoding_guess_count_ascii (const void *s_, size_t n)
{
  const uint8_t *s = s_;
  size_t ofs;

  for (ofs = 0; ofs < n; ofs++)
    if (!encoding_guess_is_ascii_text (s[ofs]))
      break;
  return ofs;
}

static bool
is_all_utf8_text (const void *s_, size_t n)
{
  const uint8_t *s = s_;
  size_t ofs;

  ofs = 0;
  while (ofs < n)
    {
      uint8_t c = s[ofs];
      if (c < 0x80)
        {
          if (!encoding_guess_is_ascii_text (c))
            return false;
          ofs++;
        }
      else
        {
          ucs4_t uc;
          int mblen;

          mblen = u8_mbtoucr (&uc, s + ofs, n - ofs);
          if (mblen < 0)
            return mblen == -2;

          ofs += mblen;
        }
    }
  return true;
}

static bool
is_utf8_bom (const uint8_t *data, size_t n)
{
  return n >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf;
}

static bool
is_utf16le_bom (const uint8_t *data, size_t n)
{
  return (n >= ENCODING_GUESS_MIN || n % 2 == 0) && get_le16 (data) == 0xfeff;
}

static bool
is_utf16be_bom (const uint8_t *data, size_t n)
{
  return (n >= ENCODING_GUESS_MIN || n % 2 == 0) && get_be16 (data) == 0xfeff;
}

static bool
is_utf32le_bom (const uint8_t *data, size_t n)
{
  return (n >= ENCODING_GUESS_MIN || n % 4 == 0) && get_le32 (data) == 0xfeff;
}

static bool
is_utf32be_bom (const uint8_t *data, size_t n)
{
  return (n >= ENCODING_GUESS_MIN || n % 4 == 0) && get_be32 (data) == 0xfeff;
}

/* Attempts to guess the encoding of a text file based on ENCODING, an encoding
   name in one of the forms described at the top of encoding-guesser.h, and
   DATA, which contains the first N bytes of the file.  Returns the guessed
   encoding, which might be ENCODING itself or a suffix of it or a statically
   allocated string.

   Encoding autodetection only takes place if ENCODING actually specifies
   autodetection.  See encoding-guesser.h for details.

   UTF-8 cannot be distinguished from other ASCII-based encodings until a
   non-ASCII text character is encountered.  If ENCODING specifies
   autodetection and this function returns "ASCII", then the client should
   process the input until it encounters an non-ASCII character (as returned by
   encoding_guess_is_ascii_text()) and then use encoding_guess_tail_encoding()
   to make a final encoding guess.  See encoding-guesser.h for details.

   N must be at least ENCODING_GUESS_MIN, unless the file is shorter than
   that. */
const char *
encoding_guess_head_encoding (const char *encoding,
                              const void *data_, size_t n)
{
  const uint8_t *data = data_;
  const char *fallback_encoding;
  const char *guess;

  fallback_encoding = encoding_guess_parse_encoding (encoding);
  if (!encoding_guess_encoding_is_auto (encoding))
    return fallback_encoding;

  if (n == 0)
    return fallback_encoding;

  if (is_utf32be_bom (data, n) || is_utf32le_bom (data, n))
    return "UTF-32";

  if (n >= 4)
    {
      uint32_t x = get_be32 (data);
      if (x == 0x84319533)
        return "GB-18030";
      else if (x == 0xdd736673)
        return "UTF-EBCDIC";
    }

  if (is_utf16be_bom (data, n) || is_utf16le_bom (data, n))
    return "UTF-16";

  if (is_utf8_bom (data, n))
    return "UTF-8";

  guess = guess_utf16 (data, n);
  if (guess != NULL)
    return guess;

  if (is_utf32 (data, n, get_be32))
    return "UTF-32BE";
  if (is_utf32 (data, n, get_le32))
    return "UTF-32LE";

  /* We've tried all the "giveaways" that make the encoding obvious.  That
     rules out, incidentally, all the encodings with multibyte units
     (e.g. UTF-16, UTF-32).  Our remaining goal is to try to distinguish UTF-8
     from some ASCII-based fallback encoding. */

  /* If the fallback encoding isn't ASCII compatible, give up. */
  if (!is_encoding_ascii_compatible (fallback_encoding))
    return fallback_encoding;

  /* If the data we have clearly is not UTF-8, give up. */
  if (!encoding_guess_tail_is_utf8 (data, n))
    {
      /* If the fallback encoding is UTF-8, fall back on something else.*/
      if (is_encoding_utf8 (fallback_encoding))
        return "windows-1252";

      return fallback_encoding;
    }

  return "ASCII";
}

static bool
is_encoding_utf16 (const char *encoding)
{
  return (!c_strcasecmp (encoding, "utf-16")
          || !c_strcasecmp (encoding, "utf16"));
}

static bool
is_encoding_utf32 (const char *encoding)
{
  return (!c_strcasecmp (encoding, "utf-32")
          || !c_strcasecmp (encoding, "utf32"));
}

/* If ENCODING is the name of an encoding that could begin with a byte-order
   mark, and in fact the N bytes in DATA do begin with a byte-order mark,
   returns the number of bytes in the byte-order mark.  Otherwise, returns 0.

   N must be at least ENCODING_GUESS_MIN, unless the file is shorter than
   that. */
size_t
encoding_guess_bom_length (const char *encoding,
                           const void *data_, size_t n)
{
  const uint8_t *data = data_;

  return (is_utf8_bom (data, n) && is_encoding_utf8 (encoding) ? 3
          : is_utf16le_bom (data, n) && is_encoding_utf16 (encoding) ? 2
          : is_utf16be_bom (data, n) && is_encoding_utf16 (encoding) ? 2
          : is_utf32le_bom (data, n) && is_encoding_utf32 (encoding) ? 4
          : is_utf32be_bom (data, n) && is_encoding_utf32 (encoding) ? 4
          : 0);
}

/* Returns an encoding guess based on ENCODING and the N bytes of text starting
   at DATA.  DATA should start with the first non-ASCII text character (as
   determined by encoding_guess_is_ascii_text()) found in the input.

   The return value will either be "UTF-8" or the fallback encoding for
   ENCODING.

   See encoding-guesser.h for intended use of this function.

   N must be at least ENCODING_GUESS_MIN, unless the file has fewer bytes than
   that starting with the first non-ASCII text character. */
const char *
encoding_guess_tail_encoding (const char *encoding,
                              const void *data, size_t n)
{

  if (encoding_guess_tail_is_utf8 (data, n) != 0)
    return "UTF-8";
  else
    {
      /* The data is not UTF-8. */
      const char *fallback_encoding = encoding_guess_parse_encoding (encoding);

      /* If the fallback encoding is UTF-8, fall back on something else.*/
      if (is_encoding_utf8 (fallback_encoding))
        return "windows-1252";

      return fallback_encoding;
    }

}

/* Returns an encoding guess based on ENCODING and the N bytes of text starting
   at DATA.  DATA should start with the first non-ASCII text character (as
   determined by encoding_guess_is_ascii_text()) found in the input.

   The return value is:

       0, if the encoding is definitely not UTF-8 (because the input contains
       byte sequences that are not valid in UTF-8).

       1, if the encoding appears to be UTF-8 (because the input contains valid
       UTF-8 multibyte sequences).

       -1, if the input contains only ASCII characters.  (This means that the
       input may be treated as UTF-8, since ASCII is a subset of UTF-8.)

   See encoding-guesser.h for intended use of this function.

   N must be at least ENCODING_GUESS_MIN, unless the file has fewer bytes than
   that starting with the first non-ASCII text character. */
int
encoding_guess_tail_is_utf8 (const void *data, size_t n)
{
  /* If all the bytes are in the ASCII range, it's just ASCII. */
  if (encoding_guess_count_ascii (data, n) == n)
    return -1;

  return (n < ENCODING_GUESS_MIN
          ? u8_check (data, n) == NULL
          : is_all_utf8_text (data, n));
}

/* Attempts to guess the encoding of a text file based on ENCODING, an encoding
   name in one of the forms described at the top of encoding-guesser.h, and the
   SIZE byts in DATA, which contains the entire contents of the file.  Returns
   the guessed encoding, which might be ENCODING itself or a suffix of it or a
   statically allocated string.

   Encoding autodetection only takes place if ENCODING actually specifies
   autodetection.  See encoding-guesser.h for details. */
const char *
encoding_guess_whole_file (const char *encoding, const void *text, size_t size)
{
  const char *guess;

  guess = encoding_guess_head_encoding (encoding, text, size);
  if (!strcmp (guess, "ASCII") && encoding_guess_encoding_is_auto (encoding))
    return encoding_guess_tail_encoding (encoding, text, size);
  else
    return guess;
}
