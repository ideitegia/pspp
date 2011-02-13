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

#include "libpspp/assertion.h"
#include "libpspp/hmapx.h"
#include "libpspp/hash-functions.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "libpspp/version.h"

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
