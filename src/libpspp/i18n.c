/* PSPP - a program for statistical analysis.
   Copyright (C) 2006 Free Software Foundation, Inc.

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
#include <iconv.h>
#include <errno.h>
#include "assertion.h"

#include "i18n.h"

#include <localcharset.h>
#include "xstrndup.h"

#if HAVE_NL_LANGINFO
#include <langinfo.h>
#endif

static char *default_encoding;


/* A wrapper around iconv_open */
static iconv_t
create_iconv (const char* tocode, const char* fromcode)
{
  iconv_t conv = iconv_open (tocode, fromcode);

  /* I don't think it's safe to translate this string or to use messaging
     as the convertors have not yet been set up */
  if ( (iconv_t) -1 == conv && 0 != strcmp (tocode, fromcode))
    {
      const int err = errno;
      fprintf (stderr,
	"Warning: cannot create a convertor for \"%s\" to \"%s\": %s\n",
	fromcode, tocode, strerror (err));
    }

  return conv;
}

/* Return a string based on TEXT converted according to HOW.
   If length is not -1, then it must be the number of bytes in TEXT.
   The returned string must be freed when no longer required.
*/
char *
recode_string (const char *to, const char *from,
	       const char *text, int length)
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

  if ( 0 == strcmp (to, from))
    return xstrndup (text, length);

  for ( outbufferlength = 1 ; outbufferlength != 0; outbufferlength <<= 1 )
    if ( outbufferlength > length)
      break;

  outbuf = xmalloc(outbufferlength);
  op = outbuf;

  outbytes = outbufferlength;
  inbytes = length;


  conv = create_iconv (to, from);

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
	    outbuf = xmalloc (outbufferlength);
	    op = outbuf;
	    outbytes = outbufferlength;
	    inbytes = length;
	    text = ip;
	    break;
	  default:
	    /* should never happen */
	    NOT_REACHED ();
	    break;
	  }
      }
  } while ( -1 == result );


  iconv_close (conv);

  if (outbytes == 0 )
    {
      char *const oldaddr = outbuf;
      outbuf = xrealloc (outbuf, outbufferlength + 1);

      op += (outbuf - oldaddr) ;
    }

  *op = '\0';

  return outbuf;
}




void
i18n_init (void)
{
  free (default_encoding);
  default_encoding = strdup (locale_charset ());
}


void
i18n_done (void)
{
  free (default_encoding);
  default_encoding = NULL;
}




/* Return the system local's idea of the
   decimal seperator character */
char
get_system_decimal (void)
{
  char radix_char;

  char *ol = strdup (setlocale (LC_NUMERIC, NULL));
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

