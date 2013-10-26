/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2013 Free Software Foundation, Inc.

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

#include "inflate.h"

#include <xalloc.h>
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include "zip-reader.h"

#include "str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

#define UCOMPSIZE 4096

struct inflator
{
  z_stream zss;
  int state;
  unsigned char ucomp[UCOMPSIZE];
  size_t bytes_uncomp;
  size_t ucomp_bytes_read;

  /* Two bitfields as defined by RFC1950 */
  uint16_t cmf_flg ;
};

void
inflate_finish (struct zip_member *zm)
{
  struct inflator *inf = zm->aux;

  inflateEnd (&inf->zss);

  free (inf);
}

bool
inflate_init (struct zip_member *zm)
{
  int r;
  struct inflator *inf = xzalloc (sizeof *inf);
  
  uint16_t flg = 0 ; 
  uint16_t cmf = 0x8; /* Always 8 for inflate */

  const uint16_t cinfo = 7;  /* log_2(Window size) - 8 */

  cmf |= cinfo << 4;     /* Put cinfo into the high nibble */

  /* make these into a 16 bit word */
  inf->cmf_flg = (cmf << 8 ) | flg;

  /* Set the check bits */
  inf->cmf_flg += 31 - (inf->cmf_flg % 31);
  assert (inf->cmf_flg % 31 == 0);

  inf->zss.next_in = Z_NULL;
  inf->zss.avail_in = 0;
  inf->zss.zalloc = Z_NULL;
  inf->zss.zfree  = Z_NULL;
  inf->zss.opaque = Z_NULL;
  r = inflateInit (&inf->zss);

  if ( Z_OK != r)
    {
      ds_put_format (zm->errs, _("Cannot initialize inflator: %s"), zError (r));
      return false;
    }

  zm->aux = inf;

  return true;
}


int
inflate_read (struct zip_member *zm, void *buf, size_t n)
{
  int r;
  struct inflator *inf = zm->aux;

  if (inf->zss.avail_in == 0)
    {
      int bytes_read;
      int bytes_to_read;
      int pad = 0;

      if ( inf->state == 0)
	{
	  inf->ucomp[1] = inf->cmf_flg ;
      	  inf->ucomp[0] = inf->cmf_flg >> 8 ;

	  pad = 2;
	  inf->state++;
	}

      bytes_to_read = zm->comp_size - inf->ucomp_bytes_read;
      
      if (bytes_to_read == 0)
	return 0;

      if (bytes_to_read > UCOMPSIZE)
	bytes_to_read = UCOMPSIZE;

      bytes_read = fread (inf->ucomp + pad, 1, bytes_to_read - pad, zm->fp);

      inf->ucomp_bytes_read += bytes_read;

      inf->zss.avail_in = bytes_read + pad;
      inf->zss.next_in = inf->ucomp;
    }
  inf->zss.avail_out = n;
  inf->zss.next_out = buf;

  r = inflate (&inf->zss, Z_NO_FLUSH);
  if ( Z_OK == r)
    {
      return n - inf->zss.avail_out;
    }
  
  ds_put_format (zm->errs, _("Error inflating: %s"), zError (r));

  return -1;
}
