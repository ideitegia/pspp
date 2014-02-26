/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2013, 2014 Free Software Foundation, Inc.

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


#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <xalloc.h>
#include <libpspp/assertion.h>

#include <byteswap.h>
#include <crc.h>

#include "inflate.h"

#include "str.h"

#include "zip-reader.h"
#include "zip-private.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)


static bool find_eocd (FILE *fp, off_t *off);

static int
stored_read (struct zip_member *zm, void *buf, size_t n)
{
  return fread (buf, 1, n, zm->fp);
}

static bool
stored_init (struct zip_member *zm UNUSED)
{
  return true;
}

static void
stored_finish (struct zip_member *zm UNUSED)
{
  /* Nothing required */
}


static struct decompressor decompressors[n_COMPRESSION] = 
  {
    {stored_init, stored_read, stored_finish},
    {inflate_init, inflate_read, inflate_finish}
  };

static enum compression
comp_code (struct zip_member *zm, uint16_t c)
{
  enum compression which;
  switch (c)
    {
    case 0:
      which = COMPRESSION_STORED;
      break;
    case 8:
      which = COMPRESSION_INFLATE;
      break;
    default:
      ds_put_format (zm->errs, _("Unsupported compression type (%d)"), c);
      which = n_COMPRESSION;
      break;
    }
  return which;
}


struct zip_reader
{
  char *filename;                  /* The name of the file from which the data is read */
  FILE *fr;                        /* The stream from which the meta data is read */
  uint16_t n_members;              /* The number of members in this archive */
  struct zip_member **members;     /* The members (may be null pointers until the headers have been read */
  int nm;
  struct string *errs;
};

void
zip_member_finish (struct zip_member *zm)
{
  ds_clear (zm->errs);
  /*  Probably not useful, because we would have to read right to the end of the member
  if (zm->expected_crc != zm->crc)
    {
      ds_put_cstr (zm->errs, _("CRC error reading zip"));
    }
  */
  zip_member_unref (zm);
}



/* Destroy the zip reader */
void
zip_reader_destroy (struct zip_reader *zr)
{
  int i;
  if (zr == NULL) 
    return;

  fclose (zr->fr);
  free (zr->filename);

  for (i = 0; i < zr->n_members; ++i)
    {
      zip_member_unref (zr->members[i]);
    }
  free (zr->members);
  free (zr);
}


void
zm_dump (const struct zip_member *zm)
{
  printf ("%d\t%08x\t %s\n", zm->ucomp_size, zm->expected_crc, zm->name);
}


/* Skip N bytes in F */
static void
skip_bytes (FILE *f, size_t n)
{
  fseeko (f, n, SEEK_CUR);
}

/* Read N bytes from F, storing the result in X */
static void
get_bytes (FILE *f, void *x, size_t n)
{
  fread (x, 1, n, f);
}

/* Read a 32 bit value from F */
static void
get_u32 (FILE *f, uint32_t *v)
{
  uint32_t x;
  get_bytes (f, &x, sizeof x);
#ifdef WORDS_BIGENDIAN
  *v = bswap_32 (x);
#else
  *v = x;
#endif
}

/* Read a 16 bit value from F */
static void
get_u16 (FILE *f, uint16_t *v)
{
  uint16_t x;
  get_bytes (f, &x, sizeof x);
#ifdef WORDS_BIGENDIAN
  *v = bswap_16 (x);
#else
  *v = x;
#endif
}


/* Read 32 bit integer and compare it with EXPECTED.
   place an error string in ERR if necessary. */
static bool
check_magic (FILE *f, uint32_t expected, struct string *err)
{
  uint32_t magic;

  get_u32 (f, &magic);

  if ((expected != magic))
    {
      ds_put_format (err,
		     _("Corrupt file at 0x%llx: Expected %"PRIx32"; got %"PRIx32), 
		     (long long int) ftello (f) - sizeof (uint32_t), expected, magic);

      return false;
    }
  return true;
}


/* Reads upto BYTES bytes from ZM and puts them in BUF.
   Returns the number of bytes read, or -1 on error */
int
zip_member_read (struct zip_member *zm, void *buf, size_t bytes)
{
  int bytes_read = 0;

  ds_clear (zm->errs);

  if ( bytes > zm->bytes_unread)
    bytes = zm->bytes_unread;

  bytes_read  = decompressors[zm->compression].read (zm, buf, bytes);
  if ( bytes_read < 0)
    return bytes_read;

  zm->crc = crc32_update (zm->crc, buf, bytes_read);

  zm->bytes_unread -= bytes_read;

  return bytes_read;
}


/*
  Read a local file header from ZR and add it to ZR's internal array.
  Returns a pointer to the member read.  This pointer belongs to ZR.
  If the caller wishes to control it, she should ref it with 
  zip_member_ref.
*/
static struct zip_member *
zip_header_read_next (struct zip_reader *zr)
{
  struct zip_member *zm = xzalloc (sizeof *zm);

  uint16_t v, nlen, extralen;
  uint16_t gp, time, date;
  
  uint16_t clen, diskstart, iattr;
  uint32_t eattr;
  uint16_t comp_type;

  ds_clear (zr->errs);

  if ( ! check_magic (zr->fr, MAGIC_SOCD, zr->errs))
    return NULL;

  get_u16 (zr->fr, &v);

  get_u16 (zr->fr, &v);
  get_u16 (zr->fr, &gp);
  get_u16 (zr->fr, &comp_type);

  zm->compression = comp_code (zm, comp_type);

  get_u16 (zr->fr, &time);
  get_u16 (zr->fr, &date);
  get_u32 (zr->fr, &zm->expected_crc);
  get_u32 (zr->fr, &zm->comp_size);
  get_u32 (zr->fr, &zm->ucomp_size);
  get_u16 (zr->fr, &nlen);
  get_u16 (zr->fr, &extralen);
  get_u16 (zr->fr, &clen);
  get_u16 (zr->fr, &diskstart);
  get_u16 (zr->fr, &iattr);
  get_u32 (zr->fr, &eattr);
  get_u32 (zr->fr, &zm->offset);

  zm->name = xzalloc (nlen + 1);
  get_bytes (zr->fr, zm->name, nlen);

  skip_bytes (zr->fr, extralen);
  
  zr->members[zr->nm++] = zm;

  zm->fp = fopen (zr->filename, "rb");
  zm->ref_cnt = 1;
  zm->errs = zr->errs;

  return zm;
}


/* Create a reader from the zip called FILENAME */
struct zip_reader *
zip_reader_create (const char *filename, struct string *errs)
{
  uint16_t disknum, total_members;
  off_t offset = 0;
  uint32_t central_dir_start, central_dir_length;

  struct zip_reader *zr = xzalloc (sizeof *zr);
  zr->errs = errs;
  if ( zr->errs)
    ds_init_empty (zr->errs);

  zr->nm = 0;

  zr->fr = fopen (filename, "rb");
  if (NULL == zr->fr)
    {
      ds_put_cstr (zr->errs, strerror (errno));
      free (zr);
      return NULL;
    }

  if ( ! check_magic (zr->fr, MAGIC_LHDR, zr->errs))
    {
      fclose (zr->fr);
      free (zr);
      return NULL;
    }

  if ( ! find_eocd (zr->fr, &offset))
    {
      ds_put_format (zr->errs, _("Cannot find central directory"));
      fclose (zr->fr);
      free (zr);
      return NULL;
    }

  if ( 0 != fseeko (zr->fr, offset, SEEK_SET))
    {
      const char *mm = strerror (errno);
      ds_put_format (zr->errs, _("Failed to seek to end of central directory record: %s"), mm);
      fclose (zr->fr);
      free (zr);
      return NULL;
    }


  if ( ! check_magic (zr->fr, MAGIC_EOCD, zr->errs))
    {
      fclose (zr->fr);
      free (zr);
      return NULL;
    }
  
  get_u16 (zr->fr, &disknum);
  get_u16 (zr->fr, &disknum);

  get_u16 (zr->fr, &zr->n_members);
  get_u16 (zr->fr, &total_members);

  get_u32 (zr->fr, &central_dir_length);
  get_u32 (zr->fr, &central_dir_start);

  if ( 0 != fseeko (zr->fr, central_dir_start, SEEK_SET))
    {
      const char *mm = strerror (errno);
      ds_put_format (zr->errs, _("Failed to seek to central directory: %s"), mm);
      fclose (zr->fr);
      free (zr);
      return NULL;
    }

  zr->members = xcalloc (zr->n_members, sizeof (*zr->members));
  memset (zr->members, 0, zr->n_members * sizeof (*zr->members));

  zr->filename = strdup (filename);

  return zr;
}



/* Return the member called MEMBER from the reader ZR  */
struct zip_member *
zip_member_open (struct zip_reader *zr, const char *member)
{
  uint16_t v, nlen, extra_len;
  uint16_t gp, comp_type, time, date;
  uint32_t ucomp_size, comp_size;
  
  uint32_t crc;
  bool new_member = false;
  char *name = NULL;

  int i;
  struct zip_member *zm = NULL;

  if ( zr == NULL)
    return NULL;

  for (i = 0; i < zr->n_members; ++i)
  {
    zm = zr->members[i];

    if (zm == NULL)
      {
	zm = zr->members[i] = zip_header_read_next (zr);
	new_member = true;
      }
    if (zm && 0 == strcmp (zm->name, member))
      break;
    else
      zm = NULL;
  }
  
  if ( zm == NULL)
    return NULL;

  if ( 0 != fseeko (zm->fp, zm->offset, SEEK_SET))
    {
      const char *mm = strerror (errno);
      ds_put_format (zm->errs, _("Failed to seek to start of member `%s': %s"), zm->name, mm);
      return NULL;
    }

  if ( ! check_magic (zm->fp, MAGIC_LHDR, zr->errs))
    {
      return NULL;
    }

  get_u16 (zm->fp, &v);
  get_u16 (zm->fp, &gp);
  get_u16 (zm->fp, &comp_type);
  zm->compression = comp_code (zm, comp_type);
  get_u16 (zm->fp, &time);
  get_u16 (zm->fp, &date);
  get_u32 (zm->fp, &crc);
  get_u32 (zm->fp, &comp_size);

  get_u32 (zm->fp, &ucomp_size);
  get_u16 (zm->fp, &nlen);
  get_u16 (zm->fp, &extra_len);

  name = xzalloc (nlen + 1);

  get_bytes (zm->fp, name, nlen);

  skip_bytes (zm->fp, extra_len);

  if (strcmp (name, zm->name) != 0)
    {
      ds_put_format (zm->errs,
		     _("Name mismatch in zip archive. Central directory says `%s'; local file header says `%s'"),
		     zm->name, name);
      free (name);
      free (zm);
      return NULL;
    }

  free (name);

  zm->bytes_unread = zm->ucomp_size;
  
  if ( !new_member)
    decompressors[zm->compression].finish (zm);

  if (!decompressors[zm->compression].init (zm) )
    return NULL;

  return zm;
}

void
zip_member_ref (struct zip_member *zm)
{
  zm->ref_cnt++;
}




void
zip_member_unref (struct zip_member *zm)
{
  if ( zm == NULL)
    return;

  if (--zm->ref_cnt == 0)
    {
      decompressors[zm->compression].finish (zm);
      if (zm->fp)
	fclose (zm->fp);
      free (zm->name);
      free (zm);
    }
}




static bool probe_magic (FILE *fp, uint32_t magic, off_t start, off_t stop, off_t *off);


/* Search for something that looks like the End Of Central Directory in FP.
   If found, the offset of the record will be placed in OFF.
   Returns true if found false otherwise.
*/
static bool
find_eocd (FILE *fp, off_t *off)
{
  off_t start, stop;
  const uint32_t magic = MAGIC_EOCD;
  bool found = false;

  /* The magic cannot be more than 22 bytes from the end of the file, 
     because that is the minimum length of the EndOfCentralDirectory
     record.
   */
  if ( 0 > fseeko (fp, -22, SEEK_END))
    {
      return false;
    }
  start = ftello (fp);
  stop = start + sizeof (magic);
  do 
    {
      found = probe_magic (fp, magic, start, stop, off);
      /* FIXME: For extra confidence lookup the directory start record here*/
      if ( start == 0)
	break;
      stop = start + sizeof (magic);
      start >>= 1;
    }
  while (!found );

  return found;
}


/*
  Search FP for MAGIC starting at START and reaching until STOP.
  Returns true iff MAGIC is found.  False otherwise.
  OFF receives the location of the magic.
*/
static bool
probe_magic (FILE *fp, uint32_t magic, off_t start, off_t stop, off_t *off)
{
  int i;
  int state = 0;
  unsigned char seq[4];
  unsigned char byte;

  if ( 0 > fseeko (fp, start, SEEK_SET))
    {
      return -1;
    }

  for (i = 0; i < 4 ; ++i)
    {
      seq[i] = (magic >> i * 8) & 0xFF;
    }

  do
    {
      if (1 != fread (&byte, 1, 1, fp))
	break;

      if ( byte == seq[state])
	state++;
      else
	state = 0;
      
      if ( state == 4)
	{
	  *off = ftello (fp) - 4;
	  return true;
	}
      start++;
      if ( start >= stop)
	break;
    }
  while (!feof (fp));

  return false;
}

