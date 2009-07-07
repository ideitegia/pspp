/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009 Free Software Foundation, Inc.

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
#include <xalloc.h>
#include <assert.h>
#include <locale.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libintl.h>
#include <iconv.h>
#include <errno.h>
#include <relocatable.h>
#include "assertion.h"
#include "hmapx.h"
#include "hash-functions.h"
#include "pool.h"

#include "i18n.h"

#include "version.h"

#include <localcharset.h>
#include "xstrndup.h"

#if HAVE_NL_LANGINFO
#include <langinfo.h>
#endif

struct converter
  {
    const char *tocode;
    const char *fromcode;
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
     as the convertors have not yet been set up */
  if ( (iconv_t) -1 == converter->conv && 0 != strcmp (tocode, fromcode))
    {
      const int err = errno;
      fprintf (stderr,
               "Warning: "
               "cannot create a convertor for \"%s\" to \"%s\": %s\n",
               fromcode, tocode, strerror (err));
    }

  return converter->conv;
}

char *
recode_string (const char *to, const char *from,
	       const char *text, int length)
{
  return recode_string_pool (to, from, text, length, NULL);
}


/* Return a string based on TEXT which must be encoded using FROM.
   The returned string will be encoded in TO.
   If length is not -1, then it must be the number of bytes in TEXT.
   The returned string must be freed when no longer required.
*/
char *
recode_string_pool (const char *to, const char *from,
	       const char *text, int length, struct pool *pool)
{
  char *outbuf = 0;
  size_t outbufferlength;
  size_t result;
  char *op ;
  size_t inbytes = 0;
  size_t outbytes ;
  iconv_t conv ;

  /* FIXME: Need to ensure that this char is valid in the target encoding */
  const char fallbackchar = '?';

  if ( text == NULL )
    return NULL;

  if ( length == -1 )
     length = strlen(text);

  if (to == NULL)
    to = default_encoding;

  if (from == NULL)
    from = default_encoding;

  for ( outbufferlength = 1 ; outbufferlength != 0; outbufferlength <<= 1 )
    if ( outbufferlength > length)
      break;

  outbuf = pool_malloc (pool, outbufferlength);
  op = outbuf;

  outbytes = outbufferlength;
  inbytes = length;


  conv = create_iconv (to, from);

  if ( (iconv_t) -1 == conv )
	return xstrdup (text);

  do {
    const char *ip = text;
    result = iconv (conv, (ICONV_CONST char **) &text, &inbytes,
		   &op, &outbytes);

    if ( -1 == result )
      {
	int the_error = errno;

	switch (the_error)
	  {
	  case EILSEQ:
	  case EINVAL:
	    if ( outbytes > 0 )
	      {
		*op++ = fallbackchar;
		outbytes--;
		text++;
		inbytes--;
		break;
	      }
	    /* Fall through */
	  case E2BIG:
	    free (outbuf);
	    outbufferlength <<= 1;
	    outbuf = pool_malloc (pool, outbufferlength);
	    op = outbuf;
	    outbytes = outbufferlength;
	    inbytes = length;
	    text = ip;
	    break;
	  default:
	    /* should never happen */
            fprintf (stderr, "Character conversion error: %s\n", strerror (the_error));
	    NOT_REACHED ();
	    break;
	  }
      }
  } while ( -1 == result );

  if (outbytes == 0 )
    {
      char *const oldaddr = outbuf;
      outbuf = pool_realloc (pool, outbuf, outbufferlength + 1);

      op += (outbuf - oldaddr) ;
    }

  *op = '\0';

  return outbuf;
}


void
i18n_init (void)
{
#if ENABLE_NLS
  setlocale (LC_CTYPE, "");
#ifdef LC_MESSAGES
  setlocale (LC_MESSAGES, "");
#endif
#if HAVE_LC_PAPER
  setlocale (LC_PAPER, "");
#endif
  bindtextdomain (PACKAGE, relocate(locale_dir));
  textdomain (PACKAGE);
#endif /* ENABLE_NLS */

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
  iconv_t conv = iconv_open ("UTF8", enc);

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

