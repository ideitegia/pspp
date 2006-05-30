/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include <xalloc.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>
#include <errno.h>

#include "i18n.h"

#include <localcharset.h>


static char *locale = 0;
static const char *charset;


static iconv_t convertor[n_CONV];

/* Return a string based on TEXT converted according to HOW.
   If length is not -1, then it must be the number of bytes in TEXT.
   The returned string must be freed when no longer required.
*/
char *
recode_string(enum conv_id how,  const char *text, int length)
{
  char *outbuf = 0;
  size_t outbufferlength;
  size_t result;
  char *ip ;
  char *op ;
  size_t inbytes = 0;
  size_t outbytes ;

  /* FIXME: Need to ensure that this char is valid in the target encoding */
  const char fallbackchar = '?';

  if ( length == -1 ) 
     length = strlen(text);

  assert(how < n_CONV);

  for ( outbufferlength = 1 ; outbufferlength != 0; outbufferlength <<= 1 )
    if ( outbufferlength > length) 
      break;

  outbuf = xmalloc(outbufferlength);
  op = outbuf;
  ip = (char *) text;

  outbytes = outbufferlength;
  inbytes = length;
  
  do {

  
    result = iconv(convertor[how], &ip, &inbytes, 
		   &op, &outbytes);

    if ( -1 == result ) 
      {
	int the_error = errno;

	switch ( the_error)
	  {
	  case EILSEQ:
	  case EINVAL:
	    if ( outbytes > 0 ) 
	      {
		*op++ = fallbackchar;
		outbytes--;
		ip++;
		inbytes--;
		break;
	      }
	    /* Fall through */
	  case E2BIG:
	    free(outbuf);
	    outbufferlength <<= 1;
	    outbuf = xmalloc(outbufferlength);
	    op = outbuf;
	    ip = (char *) text;
	    outbytes = outbufferlength;
	    inbytes = length;
	    break;
	  default:
	    /* should never happen */
	    break;
	  }

      }

  } while ( -1 == result );

  *op = '\0';
  

  return outbuf;
}


/* Returns the current PSPP locale */
const char *
get_pspp_locale(void)
{
  assert ( locale);
  return locale;
}

/* Set the PSPP locale */
void 
set_pspp_locale(const char *l)
{
  char *current_locale;
  const char *current_charset;

  free(locale);
  locale = strdup(l);

  current_locale = setlocale(LC_CTYPE, 0);
  current_charset = locale_charset();
  setlocale(LC_CTYPE, locale);
  
  charset = locale_charset();
  setlocale(LC_CTYPE, current_locale);

  iconv_close(convertor[CONV_PSPP_TO_UTF8]);
  convertor[CONV_PSPP_TO_UTF8] = iconv_open("UTF-8", charset);

  iconv_close(convertor[CONV_SYSTEM_TO_PSPP]);
  convertor[CONV_SYSTEM_TO_PSPP] = iconv_open(charset, current_charset);
}

void
i18n_init(void)
{
  assert ( ! locale) ;
  locale = strdup(setlocale(LC_CTYPE, NULL));

  setlocale(LC_CTYPE, locale);
  charset = locale_charset();

  convertor[CONV_PSPP_TO_UTF8] = iconv_open("UTF-8", charset);
  convertor[CONV_SYSTEM_TO_PSPP] = iconv_open(charset, charset);
}


void 
i18n_done(void)
{
  int i;
  free(locale);
  locale = 0;

  for(i = 0 ; i < n_CONV; ++i ) 
    iconv_close(convertor[i]);
}
