/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "libpspp/i18n.h"

#include <assert.h>
#include <errno.h>
#include <iconv.h>
#include <langinfo.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unigbrk.h>

#include "libpspp/assertion.h"
#include "libpspp/hmapx.h"
#include "libpspp/hash-functions.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "libpspp/version.h"

#include "gl/c-strcase.h"
#include "gl/localcharset.h"
#include "gl/xalloc.h"
#include "gl/relocatable.h"
#include "gl/xstrndup.h"

struct converter
 {
    char *tocode;
    char *fromcode;
    iconv_t conv;
  };

static char *default_encoding;
static struct hmapx map;

/* A wrapper around iconv_open */
static iconv_t
create_iconv (const char* tocode, const char* fromcode)
{
  size_t hash;
  struct hmapx_node *node;
  struct converter *converter;
  assert (fromcode);

  hash = hash_string (tocode, hash_string (fromcode, 0));
  HMAPX_FOR_EACH_WITH_HASH (converter, node, hash, &map)
    if (!strcmp (tocode, converter->tocode)
        && !strcmp (fromcode, converter->fromcode))
      return converter->conv;

  converter = xmalloc (sizeof *converter);
  converter->tocode = xstrdup (tocode);
  converter->fromcode = xstrdup (fromcode);
  converter->conv = iconv_open (tocode, fromcode);
  hmapx_insert (&map, converter, hash);

  /* I don't think it's safe to translate this string or to use messaging
     as the converters have not yet been set up */
  if ( (iconv_t) -1 == converter->conv && 0 != strcmp (tocode, fromcode))
    {
      const int err = errno;
      fprintf (stderr,
               "Warning: "
               "cannot create a converter for `%s' to `%s': %s\n",
               fromcode, tocode, strerror (err));
    }

  return converter->conv;
}

/* Converts the single byte C from encoding FROM to TO, returning the first
   byte of the result.

   This function probably shouldn't be used at all, but some code still does
   use it. */
char
recode_byte (const char *to, const char *from, char c)
{
  char x;
  char *s = recode_string (to, from, &c, 1);
  x = s[0];
  free (s);
  return x;
}

/* Similar to recode_string_pool, but allocates the returned value on the heap
   instead of in a pool.  It is the caller's responsibility to free the
   returned value. */
char *
recode_string (const char *to, const char *from,
	       const char *text, int length)
{
  return recode_string_pool (to, from, text, length, NULL);
}

/* Returns the length, in bytes, of the string that a similar recode_string()
   call would return. */
size_t
recode_string_len (const char *to, const char *from,
                   const char *text, int length)
{
  char *s = recode_string (to, from, text, length);
  size_t len = strlen (s);
  free (s);
  return len;
}

/* Uses CONV to convert the INBYTES starting at IP into the OUTBYTES starting
   at OP, and appends a null terminator to the output.

   Returns the output length if successful, -1 if the output buffer is too
   small. */
static ssize_t
try_recode (iconv_t conv,
            const char *ip, size_t inbytes,
            char *op_, size_t outbytes)
{
  /* FIXME: Need to ensure that this char is valid in the target encoding */
  const char fallbackchar = '?';
  char *op = op_;

  /* Put the converter into the initial shift state, in case there was any
     state information left over from its last usage. */
  iconv (conv, NULL, 0, NULL, 0);

  while (iconv (conv, (ICONV_CONST char **) &ip, &inbytes,
                &op, &outbytes) == -1)
    switch (errno)
      {
      case EINVAL:
        if (outbytes < 2)
          return -1;
        *op++ = fallbackchar;
        *op = '\0';
        return op - op_;

      case EILSEQ:
        if (outbytes == 0)
          return -1;
        *op++ = fallbackchar;
        outbytes--;
        ip++;
        inbytes--;
        break;

      case E2BIG:
        return -1;

      default:
        /* should never happen */
        fprintf (stderr, "Character conversion error: %s\n", strerror (errno));
        NOT_REACHED ();
        break;
      }

  if (outbytes == 0)
    return -1;

  *op = '\0';
  return op - op_;
}

/* Converts the string TEXT, which should be encoded in FROM-encoding, to a
   dynamically allocated string in TO-encoding.  Any characters which cannot be
   converted will be represented by '?'.

   LENGTH should be the length of the string or -1, if null terminated.

   The returned string will be allocated on POOL.

   This function's behaviour differs from that of g_convert_with_fallback
   provided by GLib.  The GLib function will fail (returns NULL) if any part of
   the input string is not valid in the declared input encoding.  This function
   however perseveres even in the presence of badly encoded input. */
char *
recode_string_pool (const char *to, const char *from,
                    const char *text, int length, struct pool *pool)
{
  struct substring out;

  if ( text == NULL )
    return NULL;

  if ( length == -1 )
     length = strlen (text);

  out = recode_substring_pool (to, from, ss_buffer (text, length), pool);
  return out.string;
}

/* Returns the name of the encoding that should be used for file names.

   This is meant to be the same encoding used by g_filename_from_uri() and
   g_filename_to_uri() in GLib. */
static const char *
filename_encoding (void)
{
#if defined _WIN32 || defined __WIN32__
  return "UTF-8";
#else
  return locale_charset ();
#endif
}

static char *
xconcat2 (const char *a, size_t a_len,
          const char *b, size_t b_len)
{
  char *s = xmalloc (a_len + b_len + 1);
  memcpy (s, a, a_len);
  memcpy (s + a_len, b, b_len);
  s[a_len + b_len] = '\0';
  return s;
}

/* Conceptually, this function concatenates HEAD_LEN-byte string HEAD and
   TAIL_LEN-byte string TAIL, both encoded in UTF-8, then converts them to
   ENCODING.  If the re-encoded result is no more than MAX_LEN bytes long, then
   it returns HEAD_LEN.  Otherwise, it drops one character[*] from the end of
   HEAD and tries again, repeating as necessary until the concatenated result
   fits or until HEAD_LEN reaches 0.

   [*] Actually this function drops grapheme clusters instead of characters, so
       that, e.g. a Unicode character followed by a combining accent character
       is either completely included or completely excluded from HEAD_LEN.  See
       UAX #29 at http://unicode.org/reports/tr29/ for more information on
       grapheme clusters.

   A null ENCODING is treated as UTF-8.

   Sometimes this function has to actually construct the concatenated string to
   measure its length.  When this happens, it sets *RESULTP to that
   null-terminated string, allocated with malloc(), for the caller to use if it
   needs it.  Otherwise, it sets *RESULTP to NULL.

   Simple examples for encoding="UTF-8", max_len=6:

       head="abc",  tail="xyz"     => 3
       head="abcd", tail="xyz"     => 3 ("d" dropped).
       head="abc",  tail="uvwxyz"  => 0 ("abc" dropped).
       head="abc",  tail="tuvwxyz" => 0 ("abc" dropped).

   Examples for encoding="ISO-8859-1", max_len=6:

       head="éèä",  tail="xyz"     => 6
         (each letter in head is only 1 byte in ISO-8859-1 even though they
          each take 2 bytes in UTF-8 encoding)
*/
static size_t
utf8_encoding_concat__ (const char *head, size_t head_len,
                        const char *tail, size_t tail_len,
                        const char *encoding, size_t max_len,
                        char **resultp)
{
  *resultp = NULL;
  if (head_len == 0)
    return 0;
  else if (encoding == NULL || !c_strcasecmp (encoding, "UTF-8"))
    {
      if (head_len + tail_len <= max_len)
        return head_len;
      else if (tail_len >= max_len)
        return 0;
      else
        {
          size_t copy_len;
          size_t prev;
          size_t ofs;
          int mblen;

          copy_len = 0;
          for (ofs = u8_mbtouc (&prev, CHAR_CAST (const uint8_t *, head),
                                head_len);
               ofs <= max_len - tail_len;
               ofs += mblen)
            {
              ucs4_t next;

              mblen = u8_mbtouc (&next,
                                 CHAR_CAST (const uint8_t *, head + ofs),
                                 head_len - ofs);
              if (uc_is_grapheme_break (prev, next))
                copy_len = ofs;

              prev = next;
            }

          return copy_len;
        }
    }
  else
    {
      char *result;

      result = (tail_len > 0
                ? xconcat2 (head, head_len, tail, tail_len)
                : CONST_CAST (char *, head));
      if (recode_string_len (encoding, "UTF-8", result,
                             head_len + tail_len) <= max_len)
        {
          *resultp = result != head ? result : NULL;
          return head_len;
        }
      else
        {
          bool correct_result = false;
          size_t copy_len;
          size_t prev;
          size_t ofs;
          int mblen;

          copy_len = 0;
          for (ofs = u8_mbtouc (&prev, CHAR_CAST (const uint8_t *, head),
                                head_len);
               ofs <= head_len;
               ofs += mblen)
            {
              ucs4_t next;

              mblen = u8_mbtouc (&next,
                                 CHAR_CAST (const uint8_t *, head + ofs),
                                 head_len - ofs);
              if (uc_is_grapheme_break (prev, next))
                {
                  if (result != head)
                    {
                      memcpy (result, head, ofs);
                      memcpy (result + ofs, tail, tail_len);
                      result[ofs + tail_len] = '\0';
                    }

                  if (recode_string_len (encoding, "UTF-8", result,
                                         ofs + tail_len) <= max_len)
                    {
                      correct_result = true;
                      copy_len = ofs;
                    }
                  else
                    correct_result = false;
                }

              prev = next;
            }

          if (result != head)
            {
              if (correct_result)
                *resultp = result;
              else
                free (result);
            }

          return copy_len;
        }
    }
}

/* Concatenates a prefix of HEAD with all of TAIL and returns the result as a
   null-terminated string owned by the caller.  HEAD, TAIL, and the returned
   string are all encoded in UTF-8.  As many characters[*] from the beginning
   of HEAD are included as will fit within MAX_LEN bytes supposing that the
   resulting string were to be re-encoded in ENCODING.  All of TAIL is always
   included, even if TAIL by itself is longer than MAX_LEN in ENCODING.

   [*] Actually this function drops grapheme clusters instead of characters, so
       that, e.g. a Unicode character followed by a combining accent character
       is either completely included or completely excluded from the returned
       string.  See UAX #29 at http://unicode.org/reports/tr29/ for more
       information on grapheme clusters.

   A null ENCODING is treated as UTF-8.

   Simple examples for encoding="UTF-8", max_len=6:

       head="abc",  tail="xyz"     => "abcxyz"
       head="abcd", tail="xyz"     => "abcxyz"
       head="abc",  tail="uvwxyz"  => "uvwxyz"
       head="abc",  tail="tuvwxyz" => "tuvwxyz"

   Examples for encoding="ISO-8859-1", max_len=6:

       head="éèä",  tail="xyz"    => "éèäxyz"
         (each letter in HEAD is only 1 byte in ISO-8859-1 even though they
          each take 2 bytes in UTF-8 encoding)
*/
char *
utf8_encoding_concat (const char *head, const char *tail,
                      const char *encoding, size_t max_len)
{
  size_t tail_len = strlen (tail);
  size_t prefix_len;
  char *result;

  prefix_len = utf8_encoding_concat__ (head, strlen (head), tail, tail_len,
                                       encoding, max_len, &result);
  return (result != NULL
          ? result
          : xconcat2 (head, prefix_len, tail, tail_len));
}

/* Returns the length, in bytes, of the string that would be returned by
   utf8_encoding_concat() if passed the same arguments, but the implementation
   is often more efficient. */
size_t
utf8_encoding_concat_len (const char *head, const char *tail,
                          const char *encoding, size_t max_len)
{
  size_t tail_len = strlen (tail);
  size_t prefix_len;
  char *result;

  prefix_len = utf8_encoding_concat__ (head, strlen (head), tail, tail_len,
                                       encoding, max_len, &result);
  free (result);
  return prefix_len + tail_len;
}

/* Returns an allocated, null-terminated string, owned by the caller,
   containing as many characters[*] from the beginning of S that would fit
   within MAX_LEN bytes if the returned string were to be re-encoded in
   ENCODING.  Both S and the returned string are encoded in UTF-8.

   [*] Actually this function drops grapheme clusters instead of characters, so
       that, e.g. a Unicode character followed by a combining accent character
       is either completely included or completely excluded from the returned
       string.  See UAX #29 at http://unicode.org/reports/tr29/ for more
       information on grapheme clusters.

   A null ENCODING is treated as UTF-8.
*/
char *
utf8_encoding_trunc (const char *s, const char *encoding, size_t max_len)
{
  return utf8_encoding_concat (s, "", encoding, max_len);
}

/* Returns the length, in bytes, of the string that would be returned by
   utf8_encoding_trunc() if passed the same arguments, but the implementation
   is often more efficient. */
size_t
utf8_encoding_trunc_len (const char *s, const char *encoding, size_t max_len)
{
  return utf8_encoding_concat_len (s, "", encoding, max_len);
}

/* Returns FILENAME converted from UTF-8 to the filename encoding.
   On Windows the filename encoding is UTF-8; elsewhere it is based on the
   current locale. */
char *
utf8_to_filename (const char *filename)
{
  return recode_string (filename_encoding (), "UTF-8", filename, -1);
}

/* Returns FILENAME converted from the filename encoding to UTF-8.
   On Windows the filename encoding is UTF-8; elsewhere it is based on the
   current locale. */
char *
filename_to_utf8 (const char *filename)
{
  return recode_string ("UTF-8", filename_encoding (), filename, -1);
}

/* Converts the string TEXT, which should be encoded in FROM-encoding, to a
   dynamically allocated string in TO-encoding.  Any characters which cannot be
   converted will be represented by '?'.

   The returned string will be null-terminated and allocated on POOL.

   This function's behaviour differs from that of g_convert_with_fallback
   provided by GLib.  The GLib function will fail (returns NULL) if any part of
   the input string is not valid in the declared input encoding.  This function
   however perseveres even in the presence of badly encoded input. */
struct substring
recode_substring_pool (const char *to, const char *from,
                       struct substring text, struct pool *pool)
{
  size_t outbufferlength;
  iconv_t conv ;

  if (to == NULL)
    to = default_encoding;

  if (from == NULL)
    from = default_encoding;

  conv = create_iconv (to, from);

  if ( (iconv_t) -1 == conv )
    {
      struct substring out;
      ss_alloc_substring_pool (&out, text, pool);
      return out;
    }

  for ( outbufferlength = 1 ; outbufferlength != 0; outbufferlength <<= 1 )
    if ( outbufferlength > text.length)
      {
        char *output = pool_malloc (pool, outbufferlength);
        ssize_t output_len = try_recode (conv, text.string, text.length,
                                         output, outbufferlength);
        if (output_len >= 0)
          return ss_buffer (output, output_len);
        pool_free (pool, output);
      }

  NOT_REACHED ();
}

void
i18n_init (void)
{
  setlocale (LC_CTYPE, "");
  setlocale (LC_MESSAGES, "");
#if HAVE_LC_PAPER
  setlocale (LC_PAPER, "");
#endif
  bindtextdomain (PACKAGE, relocate(locale_dir));
  textdomain (PACKAGE);

  assert (default_encoding == NULL);
  default_encoding = xstrdup (locale_charset ());

  hmapx_init (&map);
}

const char *
get_default_encoding (void)
{
  return default_encoding;
}

void
set_default_encoding (const char *enc)
{
  free (default_encoding);
  default_encoding = xstrdup (enc);
}


/* Attempts to set the encoding from a locale name
   returns true if successfull.
   This function does not (should not!) alter the current locale.
*/
bool
set_encoding_from_locale (const char *loc)
{
  bool ok = true;
  char *c_encoding;
  char *loc_encoding;
  char *tmp = xstrdup (setlocale (LC_CTYPE, NULL));

  setlocale (LC_CTYPE, "C");
  c_encoding = xstrdup (locale_charset ());

  setlocale (LC_CTYPE, loc);
  loc_encoding = xstrdup (locale_charset ());


  if ( 0 == strcmp (loc_encoding, c_encoding))
    {
      ok = false;
    }


  setlocale (LC_CTYPE, tmp);

  free (tmp);

  if (ok)
    {
      free (default_encoding);
      default_encoding = loc_encoding;
    }
  else
    free (loc_encoding);

  free (c_encoding);

  return ok;
}

void
i18n_done (void)
{
  struct hmapx_node *node;
  struct converter *cvtr;

  HMAPX_FOR_EACH (cvtr, node, &map)
    {
      free (cvtr->tocode);
      free (cvtr->fromcode);
      if (cvtr->conv != (iconv_t) -1)
        iconv_close (cvtr->conv);
      free (cvtr);
    }

  hmapx_destroy (&map);

  free (default_encoding);
  default_encoding = NULL;
}



bool
valid_encoding (const char *enc)
{
  iconv_t conv = iconv_open (UTF8, enc);

  if ( conv == (iconv_t) -1)
    return false;

  iconv_close (conv);

  return true;
}


/* Return the system local's idea of the
   decimal seperator character */
char
get_system_decimal (void)
{
  char radix_char;

  char *ol = xstrdup (setlocale (LC_NUMERIC, NULL));
  setlocale (LC_NUMERIC, "");

#if HAVE_NL_LANGINFO
  radix_char = nl_langinfo (RADIXCHAR)[0];
#else
  {
    char buf[10];
    snprintf (buf, sizeof buf, "%f", 2.5);
    radix_char = buf[1];
  }
#endif

  /* We MUST leave LC_NUMERIC untouched, since it would
     otherwise interfere with data_{in,out} */
  setlocale (LC_NUMERIC, ol);
  free (ol);
  return radix_char;
}

const char *
uc_name (ucs4_t uc, char buffer[16])
{
  if (uc >= 0x20 && uc < 0x7f)
    snprintf (buffer, 16, "`%c'", uc);
  else
    snprintf (buffer, 16, "U+%04X", uc);
  return buffer;
}

bool
get_encoding_info (struct encoding_info *e, const char *name)
{
  const struct substring in = SS_LITERAL_INITIALIZER (
    "\t\n\v\f\r "
    "!\"#$%&'()*+,-./0123456789:;<=>?@"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
    "abcdefghijklmnopqrstuvwxyz{|}~");

  struct substring out, cr, lf;
  bool ok;

  memset (e, 0, sizeof *e);

  cr = recode_substring_pool (name, "UTF-8", ss_cstr ("\r"), NULL);
  lf = recode_substring_pool (name, "UTF-8", ss_cstr ("\n"), NULL);
  ok = cr.length >= 1 && cr.length <= MAX_UNIT && cr.length == lf.length;
  if (!ok)
    {
      fprintf (stderr, "warning: encoding `%s' is not supported.\n", name);
      ss_dealloc (&cr);
      ss_dealloc (&lf);
      ss_alloc_substring (&cr, ss_cstr ("\r"));
      ss_alloc_substring (&lf, ss_cstr ("\n"));
    }

  e->unit = cr.length;
  memcpy (e->cr, cr.string, e->unit);
  memcpy (e->lf, lf.string, e->unit);

  ss_dealloc (&cr);
  ss_dealloc (&lf);

  out = recode_substring_pool ("UTF-8", name, in, NULL);
  e->is_ascii_compatible = ss_equals (in, out);
  ss_dealloc (&out);

  return ok;
}

bool
is_encoding_ascii_compatible (const char *encoding)
{
  struct encoding_info e;

  get_encoding_info (&e, encoding);
  return e.is_ascii_compatible;
}
