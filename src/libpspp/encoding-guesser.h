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

#ifndef LIBPSPP_ENCODING_GUESSER_H
#define LIBPSPP_ENCODING_GUESSER_H 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A library for autodetecting the encoding of a text file.

   Naming Encodings
   ----------------

   The encoding guesser starts with an encoding name in one of various
   different forms.  Some of the forms do not actually do any autodetection.
   The encoding guesser will return the specified encoding without looking at
   any file data:

     - A valid IANA or system encoding name: These are returned as-is.

     - "Locale": Translated to the encoding used by the system locale, as
       returned by locale_charset().

   The remaining forms that do perform autodetection are:

     - "Auto," followed by a valid IANA or system encoding name (the "fallback
       encoding"): Requests detection whether the input is encoded in UTF-8,
       UTF-16, UTF-32, or a few other easily identifiable charsets.  When a
       particular character set cannot be recognized, the guesser falls back to
       the encoding following the comma.  When the fallback encoding is UTF-8,
       but the input is invalid UTF-8, then the windows-1252 encoding (closely
       related to ISO 8859-1) is used instead.  UTF-8 detection works only for
       ASCII-compatible character sets.

     - NULL or "Auto": As above, with the encoding used by the system locale as
       the fallback encoding.

   The above are suggested capitalizations but encoding names are not
   case-sensitive.

   The encoding_guess_parse_encoding() and encoding_guess_encoding_is_auto()
   functions work with encoding names in these forms.

   Usage
   -----

   1. Call encoding_guess_head_encoding() with several bytes from the start of
      the text file.  Feed in at least ENCODING_GUESS_MIN bytes, unless the
      file is shorter than that, but as many more as are conveniently
      available.  ENCODING_GUESS_SUGGESTED is a reasonable amount.

      encoding_guess_head_encoding() returns its best guess at the file's
      encoding.  Ordinarily it returns a final guess that the client can use to
      interpret the file, and you're all done.  However, if it returns "ASCII"
      and the original encoding name requests autodetection (which you can find
      out by calling encoding_guess_encoding_is_auto()), then proceed to the
      next step.

   2. The encoding guesser is confident that the stream uses an ASCII
      compatible encoding, either UTF-8 or the fallback encoding.  The client
      may safely read and process the stream up to the first non-ASCII
      character.  If the stream continues to be ASCII all the way to its end,
      then we're done.

      The encoding guesser provides a pair of functions to detect non-ASCII
      characters: encoding_guess_is_ascii_text() for single characters and
      encoding_guess_count_ascii() as a convenient wrapper for whole buffers.

   3. Otherwise, the stream contains some non-ASCII data at some point.  Now
      the client should gather several bytes starting at this point, at least
      ENCODING_GUESS_MIN, unless the file ends before that, but as many more as
      are conveniently available.  ENCODING_GUESS_SUGGESTED is a reasonable
      amount.

      The client should pass these bytes to encoding_guess_tail_encoding(),
      which returns a best and final guess at the file's encoding, which is
      either UTF-8 or the fallback encoding.  Another alternative is
      encoding_guess_tail_is_utf8(), which guesses the same way but has a
      different form of return value.
*/

/* Minimum number of bytes for use in autodetection.
   You should only pass fewer bytes to the autodetection routines if the file
   is actually shorter than this. */
#define ENCODING_GUESS_MIN              16

/* Suggested minimum buffer size to use for autodetection. */
#define ENCODING_GUESS_SUGGESTED        1024

/* Parsing encoding names. */
const char *encoding_guess_parse_encoding (const char *encoding);
bool encoding_guess_encoding_is_auto (const char *encoding);

/* Making an initial coding guess based on the start of a file. */
const char *encoding_guess_head_encoding (const char *encoding,
                                          const void *, size_t);
size_t encoding_guess_bom_length (const char *encoding,
                                  const void *, size_t n);

/* Refining an initial ASCII coding guess using later non-ASCII bytes. */
static inline bool encoding_guess_is_ascii_text (uint8_t c);
size_t encoding_guess_count_ascii (const void *, size_t);
int encoding_guess_tail_is_utf8 (const void *, size_t);
const char *encoding_guess_tail_encoding (const char *encoding,
                                          const void *, size_t);

/* Guessing from entire file contents. */
const char *encoding_guess_whole_file (const char *encoding,
                                       const void *, size_t);

/* Returns true if C is a byte that might appear in an ASCII text file,
   false otherwise. */
static inline bool
encoding_guess_is_ascii_text (uint8_t c)
{
  return (c >= 0x20 && c < 0x7f) || (c >= 0x09 && c < 0x0e);
}

#endif /* libpspp/encoding-guesser.h */
