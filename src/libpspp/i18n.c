/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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
#include <unicase.h>
#include <unigbrk.h>

#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/hmapx.h"
#include "libpspp/hash-functions.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "libpspp/version.h"

#include "gl/c-strcase.h"
#include "gl/localcharset.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/relocatable.h"
#include "gl/xstrndup.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct converter
 {
    char *tocode;
    char *fromcode;
    iconv_t conv;
    int error;
  };

static char *default_encoding;
static struct hmapx map;

/* A wrapper around iconv_open */
static struct converter *
create_iconv__ (const char* tocode, const char* fromcode)
{
  size_t hash;
  struct hmapx_node *node;
  struct converter *converter;
  assert (fromcode);

  hash = hash_string (tocode, hash_string (fromcode, 0));
  HMAPX_FOR_EACH_WITH_HASH (converter, node, hash, &map)
    if (!strcmp (tocode, converter->tocode)
        && !strcmp (fromcode, converter->fromcode))
      return converter;

  converter = xmalloc (sizeof *converter);
  converter->tocode = xstrdup (tocode);
  converter->fromcode = xstrdup (fromcode);
  converter->conv = iconv_open (tocode, fromcode);
  converter->error = converter->conv == (iconv_t) -1 ? errno : 0;
  hmapx_insert (&map, converter, hash);

  return converter;
}

static iconv_t
create_iconv (const char* tocode, const char* fromcode)
{
  struct converter *converter;

  converter = create_iconv__ (tocode, fromcode);

  /* I don't think it's safe to translate this string or to use messaging
     as the converters have not yet been set up */
  if (converter->error && strcmp (tocode, fromcode))
    {
      fprintf (stderr,
               "Warning: "
               "cannot create a converter for `%s' to `%s': %s\n",
               fromcode, tocode, strerror (converter->error));
      converter->error = 0;
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
            const char *in, size_t inbytes,
            char *out_, size_t outbytes)
{
  /* FIXME: Need to ensure that this char is valid in the target encoding */
  const char fallbackchar = '?';
  char *out = out_;
  int i;

  /* Put the converter into the initial shift state, in case there was any
     state information left over from its last usage. */
  iconv (conv, NULL, 0, NULL, 0);

  /* Do two rounds of iconv() calls:

     - The first round does the bulk of the conversion using the
       caller-supplied input data..

     - The second round flushes any leftover output.  This has a real effect
       with input encodings that use combining diacritics, e.g. without the
       second round the last character tends to gets dropped when converting
       from windows-1258 to other encodings.
  */
  for (i = 0; i < 2; i++)
    {
      ICONV_CONST char **inp = i ? NULL : (ICONV_CONST char **) &in;
      size_t *inbytesp = i ? NULL : &inbytes;

      while (iconv (conv, inp, inbytesp, &out, &outbytes) == -1)
        switch (errno)
          {
          case EINVAL:
            if (outbytes < 2)
              return -1;
            *out++ = fallbackchar;
            *out = '\0';
            return out - out_;

          case EILSEQ:
            if (outbytes == 0)
              return -1;
            *out++ = fallbackchar;
            outbytes--;
            if (inp)
              {
                in++;
                inbytes--;
              }
            break;

          case E2BIG:
            return -1;

          default:
            /* should never happen */
            fprintf (stderr, "Character conversion error: %s\n",
                     strerror (errno));
            NOT_REACHED ();
            break;
          }
    }

  if (outbytes == 0)
    return -1;

  *out = '\0';
  return out - out_;
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
          ucs4_t prev;
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
          ucs4_t prev;
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

   The returned string will be null-terminated and allocated on POOL with
   pool_malloc().

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

      out.string = pool_malloc (pool, text.length + 1);
      out.length = text.length;
      memcpy (out.string, text.string, text.length);
      out.string[out.length] = '\0';
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
  setlocale (LC_ALL, "");
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

#if HAVE_NL_LANGINFO
  radix_char = nl_langinfo (RADIXCHAR)[0];
#else
  {
    char buf[10];
    snprintf (buf, sizeof buf, "%f", 2.5);
    radix_char = buf[1];
  }
#endif

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

/* UTF-8 functions that deal with uppercase/lowercase distinctions. */

/* Returns a hash value for the N bytes of UTF-8 encoded data starting at S,
   with lowercase and uppercase letters treated as equal, starting from
   BASIS. */
unsigned int
utf8_hash_case_bytes (const char *s, size_t n, unsigned int basis)
{
  uint8_t folded_buf[2048];
  size_t folded_len = sizeof folded_buf;
  uint8_t *folded_s;
  unsigned int hash;

  folded_s = u8_casefold (CHAR_CAST (const uint8_t *, s), n,
                          NULL, UNINORM_NFKD, folded_buf, &folded_len);
  if (folded_s != NULL)
    {
      hash = hash_bytes (folded_s, folded_len, basis);
      if (folded_s != folded_buf)
        free (folded_s);
    }
  else
    {
      if (errno == ENOMEM)
        xalloc_die ();
      hash = hash_bytes (s, n, basis);
    }

  return hash;
}

/* Returns a hash value for null-terminated UTF-8 string S, with lowercase and
   uppercase letters treated as equal, starting from BASIS. */
unsigned int
utf8_hash_case_string (const char *s, unsigned int basis)
{
  return utf8_hash_case_bytes (s, strlen (s), basis);
}

/* Compares UTF-8 strings A and B case-insensitively.
   Returns a negative value if A < B, zero if A == B, positive if A > B. */
int
utf8_strcasecmp (const char *a, const char *b)
{
  return utf8_strncasecmp (a, strlen (a), b, strlen (b));
}

/* Compares UTF-8 strings A (with length AN) and B (with length BN)
   case-insensitively.
   Returns a negative value if A < B, zero if A == B, positive if A > B. */
int
utf8_strncasecmp (const char *a, size_t an, const char *b, size_t bn)
{
  int result;

  if (u8_casecmp (CHAR_CAST (const uint8_t *, a), an,
                  CHAR_CAST (const uint8_t *, b), bn,
                  NULL, UNINORM_NFKD, &result))
    {
      if (errno == ENOMEM)
        xalloc_die ();

      result = memcmp (a, b, MIN (an, bn));
      if (result == 0)
        result = an < bn ? -1 : an > bn;
    }

  return result;
}

static char *
utf8_casemap (const char *s,
              uint8_t *(*f) (const uint8_t *, size_t, const char *, uninorm_t,
                             uint8_t *, size_t *))
{
  char *result;
  size_t size;

  result = CHAR_CAST (char *,
                      f (CHAR_CAST (const uint8_t *, s), strlen (s) + 1,
                         NULL, NULL, NULL, &size));
  if (result == NULL)
    {
      if (errno == ENOMEM)
        xalloc_die ();

      result = xstrdup (s);
    }
  return result;
}

char *
utf8_to_upper (const char *s)
{
  return utf8_casemap (s, u8_toupper);
}

char *
utf8_to_lower (const char *s)
{
  return utf8_casemap (s, u8_tolower);
}

bool
get_encoding_info (struct encoding_info *e, const char *name)
{
  const struct substring in = SS_LITERAL_INITIALIZER (
    "\t\n\v\f\r "
    "!\"#$%&'()*+,-./0123456789:;<=>?@"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
    "abcdefghijklmnopqrstuvwxyz{|}~");

  struct substring out, cr, lf, space;
  bool ok;

  memset (e, 0, sizeof *e);

  cr = recode_substring_pool (name, "UTF-8", ss_cstr ("\r"), NULL);
  lf = recode_substring_pool (name, "UTF-8", ss_cstr ("\n"), NULL);
  space = recode_substring_pool (name, "UTF-8", ss_cstr (" "), NULL);
  ok = (cr.length >= 1
        && cr.length <= MAX_UNIT
        && cr.length == lf.length
        && cr.length == space.length);
  if (!ok)
    {
      fprintf (stderr, "warning: encoding `%s' is not supported.\n", name);
      ss_dealloc (&cr);
      ss_dealloc (&lf);
      ss_dealloc (&space);
      ss_alloc_substring (&cr, ss_cstr ("\r"));
      ss_alloc_substring (&lf, ss_cstr ("\n"));
      ss_alloc_substring (&space, ss_cstr (" "));
    }

  e->unit = cr.length;
  memcpy (e->cr, cr.string, e->unit);
  memcpy (e->lf, lf.string, e->unit);
  memcpy (e->space, space.string, e->unit);

  ss_dealloc (&cr);
  ss_dealloc (&lf);
  ss_dealloc (&space);

  out = recode_substring_pool ("UTF-8", name, in, NULL);
  e->is_ascii_compatible = ss_equals (in, out);
  ss_dealloc (&out);

  if (!e->is_ascii_compatible && e->unit == 1)
    {
      out = recode_substring_pool ("UTF-8", name, ss_cstr ("A"), NULL);
      e->is_ebcdic_compatible = (out.length == 1
                                 && (uint8_t) out.string[0] == 0xc1);
      ss_dealloc (&out);
    }
  else
    e->is_ebcdic_compatible = false;

  return ok;
}

bool
is_encoding_ascii_compatible (const char *encoding)
{
  struct encoding_info e;

  get_encoding_info (&e, encoding);
  return e.is_ascii_compatible;
}

bool
is_encoding_ebcdic_compatible (const char *encoding)
{
  struct encoding_info e;

  get_encoding_info (&e, encoding);
  return e.is_ebcdic_compatible;
}

/* Returns true if iconv can convert ENCODING to and from UTF-8,
   otherwise false. */
bool
is_encoding_supported (const char *encoding)
{
  return (create_iconv__ ("UTF-8", encoding)->conv != (iconv_t) -1
          && create_iconv__ (encoding, "UTF-8")->conv != (iconv_t) -1);
}

/* Returns true if E is the name of a UTF-8 encoding.

   XXX Possibly we should test not E as a string but its properties via
   iconv. */
bool
is_encoding_utf8 (const char *e)
{
  return ((e[0] == 'u' || e[0] == 'U')
          && (e[1] == 't' || e[1] == 'T')
          && (e[2] == 'f' || e[2] == 'F')
          && ((e[3] == '8' && e[4] == '\0')
              || (e[3] == '-' && e[4] == '8' && e[5] == '\0')));
}

static struct encoding_category *categories;
static int n_categories;

static void SENTINEL (0)
add_category (size_t *allocated_categories, const char *category, ...)
{
  struct encoding_category *c;
  const char *encodings[16];
  va_list args;
  int i, n;

  /* Count encoding arguments. */
  va_start (args, category);
  n = 0;
  while ((encodings[n] = va_arg (args, const char *)) != NULL)
    {
      const char *encoding = encodings[n];
      if (!strcmp (encoding, "Auto") || is_encoding_supported (encoding))
        n++;
    }
  assert (n < sizeof encodings / sizeof *encodings);
  va_end (args);

  if (n == 0)
    return;

  if (n_categories >= *allocated_categories)
    categories = x2nrealloc (categories,
                             allocated_categories, sizeof *categories);

  c = &categories[n_categories++];
  c->category = category;
  c->encodings = xmalloc (n * sizeof *c->encodings);
  for (i = 0; i < n; i++)
    c->encodings[i] = encodings[i];
  c->n_encodings = n;
}

static void
init_encoding_categories (void)
{
  static bool inited;
  size_t alloc;

  if (inited)
    return;
  inited = true;

  alloc = 0;
  add_category (&alloc, "Unicode", "UTF-8", "UTF-16", "UTF-16BE", "UTF-16LE",
                "UTF-32", "UTF-32BE", "UTF-32LE", NULL_SENTINEL);
  add_category (&alloc, _("Arabic"), "IBM864", "ISO-8859-6", "Windows-1256",
                NULL_SENTINEL);
  add_category (&alloc, _("Armenian"), "ARMSCII-8", NULL_SENTINEL);
  add_category (&alloc, _("Baltic"), "ISO-8859-13", "ISO-8859-4",
                "Windows-1257", NULL_SENTINEL);
  add_category (&alloc, _("Celtic"), "ISO-8859-14", NULL_SENTINEL);
  add_category (&alloc, _("Central European"), "IBM852", "ISO-8859-2",
                "Mac-CentralEurope", "Windows-1250", NULL_SENTINEL);
  add_category (&alloc, _("Chinese Simplified"), "GB18030", "GB2312", "GBK",
                "HZ-GB-2312", "ISO-2022-CN", NULL_SENTINEL);
  add_category (&alloc, _("Chinese Traditional"), "Big5", "Big5-HKSCS",
                "EUC-TW", NULL_SENTINEL);
  add_category (&alloc, _("Croatian"), "MacCroatian", NULL_SENTINEL);
  add_category (&alloc, _("Cyrillic"), "IBM855", "ISO-8859-5", "ISO-IR-111",
                "KOI8-R", "MacCyrillic", NULL_SENTINEL);
  add_category (&alloc, _("Cyrillic/Russian"), "IBM866", NULL_SENTINEL);
  add_category (&alloc, _("Cyrillic/Ukrainian"), "KOI8-U", "MacUkrainian",
                NULL_SENTINEL);
  add_category (&alloc, _("Georgian"), "GEOSTD8", NULL_SENTINEL);
  add_category (&alloc, _("Greek"), "ISO-8859-7", "MacGreek", NULL_SENTINEL);
  add_category (&alloc, _("Gujarati"), "MacGujarati", NULL_SENTINEL);
  add_category (&alloc, _("Gurmukhi"), "MacGurmukhi", NULL_SENTINEL);
  add_category (&alloc, _("Hebrew"), "IBM862", "ISO-8859-8-I", "Windows-1255",
                NULL_SENTINEL);
  add_category (&alloc, _("Hebrew Visual"), "ISO-8859-8", NULL_SENTINEL);
  add_category (&alloc, _("Hindi"), "MacDevangari", NULL_SENTINEL);
  add_category (&alloc, _("Icelandic"), "MacIcelandic", NULL_SENTINEL);
  add_category (&alloc, _("Japanese"), "EUC-JP", "ISO-2022-JP", "Shift_JIS",
                NULL_SENTINEL);
  add_category (&alloc, _("Korean"), "EUC-KR", "ISO-2022-KR", "JOHAB", "UHC",
                NULL_SENTINEL);
  add_category (&alloc, _("Nordic"), "ISO-8859-10", NULL_SENTINEL);
  add_category (&alloc, _("Romanian"), "ISO-8859-16", "MacRomanian",
                NULL_SENTINEL);
  add_category (&alloc, _("South European"), "ISO-8859-3", NULL_SENTINEL);
  add_category (&alloc, _("Thai"), "ISO-8859-11", "TIS-620", "Windows-874",
                NULL_SENTINEL);
  add_category (&alloc, _("Turkish"), "IBM857", "ISO-8859-9", "Windows-1254",
                NULL_SENTINEL);
  add_category (&alloc, _("Vietnamese"), "TVCN", "VISCII", "VPS",
                "Windows-1258", NULL_SENTINEL);
  add_category (&alloc, _("Western European"), "ISO-8859-1", "ISO-8859-15",
                "Windows-1252", "IBM850", "MacRoman", NULL_SENTINEL);
}

/* Returns an array of "struct encoding_category" that contains only the
   categories and encodings that the system supports. */
struct encoding_category *
get_encoding_categories (void)
{
  init_encoding_categories ();
  return categories;
}

/* Returns the number of elements in the array returned by
   get_encoding_categories().  */
size_t
get_n_encoding_categories (void)
{
  init_encoding_categories ();
  return n_categories;
}
