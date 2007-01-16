/* PSPP - computes sample statistics.
   Copyright (C) 2004, 2006, 2007 Free Software Foundation, Inc.

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

#include "casefile.h"
#include "casefile-private.h"
#include "fastfile.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <data/case.h>
#include <data/make-file.h>
#include <data/settings.h>
#include <data/variable.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>

#include "full-read.h"
#include "full-write.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#define IO_BUF_SIZE (8192 / sizeof (union value))

/* A fastfile represents a sequentially accessible stream of
   immutable cases.

   If workspace allows, a fastfile is maintained in memory.  If
   workspace overflows, then the fastfile is pushed to disk.  In
   either case the interface presented to callers is kept the
   same.

   The life cycle of a fastfile consists of up to three phases:

       1. Writing.  The fastfile initially contains no cases.  In
          this phase, any number of cases may be appended to the
          end of a fastfile.  (Cases are never inserted in the
          middle or before the beginning of a fastfile.)

          Use casefile_append or casefile_append_xfer to
          append a case to a fastfile.

       2. Reading.  The fastfile may be read sequentially,
          starting from the beginning, by "casereaders".  Any
          number of casereaders may be created, at any time,
          during the reading phase.  Each casereader has an
          independent position in the fastfile.

          Ordinary casereaders may only move forward.  They
          cannot move backward to arbitrary records or seek
          randomly.  Cloning casereaders is possible, but it is
          not yet implemented.

          Use casefile_get_reader to create a casereader for
          use in phase 2.  This also transitions from phase 1 to
          phase 2.  Calling fastfile_mode_reader makes the same
          transition, without creating a casereader.

          Use casereader_read or casereader_read_xfer to read
          a case from a casereader.  Use casereader_destroy to
          discard a casereader when it is no longer needed.

       3. Destruction.  This phase is optional.  The fastfile is
          also read with casereaders in this phase, but the
          ability to create new casereaders is curtailed.

          In this phase, casereaders could still be cloned.

          To transition from phase 1 or 2 to phase 3 and create a
          casereader, call casefile_get_destructive_reader().
          The same functions apply to the casereader obtained
          this way as apply to casereaders obtained in phase 2.
          
          After casefile_get_destructive_reader is called, no
          more casereaders may be created.  (If cloning of
          casereaders were implemented, it would still be
          possible.)

          The purpose of the limitations applied to casereaders
          in phase 3 is to allow in-memory fastfiles to fully
          transfer ownership of cases to the casereaders,
          avoiding the need for extra copies of case data.  For
          relatively static data sets with many variables, I
          suspect (without evidence) that this may be a big
          performance boost.

   When a fastfile is no longer needed, it may be destroyed with
   casefile_destroy.  This function will also destroy any
   remaining casereaders. */

/* FIXME: should we implement compression? */

/* In-memory cases are arranged in an array of arrays.  The top
   level is variable size and the size of each bottom level array
   is fixed at the number of cases defined here.  */
#define CASES_PER_BLOCK 128

static const struct class_casefile class;

/* A fastfile. */
struct fastfile
{
  struct casefile cf;		/* Parent */

  size_t value_cnt;		/* Case size in `union value's. */
  size_t case_acct_size;	/* Case size for accounting. */
  unsigned long case_cnt;	/* Number of cases stored. */
  enum { MEMORY, DISK } storage;	/* Where cases are stored. */
  enum { WRITE, READ } mode;		/* Is writing or reading allowed? */

  bool ok;			/* False after I/O error. */

  /* Memory storage. */
  struct ccase **cases;		/* Pointer to array of cases. */

  /* Disk storage. */
  int fd;			/* File descriptor, -1 if none. */
  char *file_name;		/* File name. */
  union value *buffer;		/* I/O buffer, NULL if none. */
  size_t buffer_used;		/* Number of values used in buffer. */
  size_t buffer_size;		/* Buffer size in values. */
};


static const struct class_casereader class_reader;

/* For reading out the cases in a fastfile. */
struct fastfilereader
{
  struct casereader cr;		/* Parent */

  unsigned long case_idx;	/* Case number of current case. */

  /* Disk storage. */
  int fd;			/* File descriptor. */
  off_t file_ofs;		/* Current position in fd. */
  off_t buffer_ofs;		/* File offset of buffer start. */
  union value *buffer;		/* I/O buffer. */
  size_t buffer_pos;		/* Offset of buffer position. */
  struct ccase c;		/* Current case. */
};


static void io_error (struct fastfile *, const char *, ...) 
     PRINTF_FORMAT (2, 3);
static int safe_open (const char *file_name, int flags);
static int safe_close (int fd);
static void write_case_to_disk (struct fastfile *, const struct ccase *);
static void flush_buffer (struct fastfile *);

static void reader_open_file (struct fastfilereader *);

static void seek_and_fill_buffer (struct fastfilereader *);
static bool fill_buffer (struct fastfilereader *);


/* Number of bytes of case allocated in in-memory fastfiles. */
static size_t case_bytes;

/* Destroys READER. */
static void fastfilereader_destroy (struct casereader *cr)
{
  struct fastfilereader *reader = (struct fastfilereader *) cr;
  struct fastfile *ff = (struct fastfile *) casereader_get_casefile (cr);

  if (ff->buffer == NULL)
    ff->buffer = reader->buffer;
  else
    free (reader->buffer);

  if (reader->fd != -1)
    {
      if (ff->fd == -1)
	ff->fd = reader->fd;
      else
	safe_close (reader->fd);
    }

  case_destroy (&reader->c);

  free (reader);
}



/* Return the case number of the current case */
static unsigned long
fastfilereader_cnum (const struct casereader *cr)
{
  const struct fastfilereader *ffr = (const struct fastfilereader *) cr;
  return ffr->case_idx;
}


/* Returns the next case pointed to by FFR and increments
   FFR's pointer.  Returns NULL if FFR points beyond the last case.
*/
static struct ccase *
fastfilereader_get_next_case (struct casereader *cr)
{
  struct fastfile *ff = (struct fastfile *) casereader_get_casefile (cr);
  struct fastfilereader *ffr = (struct fastfilereader *) cr;
  struct ccase *read_case = NULL ;

  if ( ffr->case_idx >= ff->case_cnt  ) 
    return NULL ;

  if (ff->storage == MEMORY )
    {
      size_t block_idx = ffr->case_idx / CASES_PER_BLOCK;
      size_t case_idx = ffr->case_idx % CASES_PER_BLOCK;
      read_case = &ff->cases[block_idx][case_idx];
    }
  else
    {
      if (ffr->buffer_pos + ff->value_cnt > ff->buffer_size)
	{
	  if (!fill_buffer (ffr))
	    return NULL;
	  ffr->buffer_pos = 0;
	}

      case_from_values (&ffr->c, ffr->buffer + ffr->buffer_pos,
			ff->value_cnt);
      ffr->buffer_pos += ff->value_cnt;
      
      read_case = &ffr->c;
    }
  ffr->case_idx++;

  return read_case;
}

/* Creates and returns a casereader for CF.  A casereader can be used to
   sequentially read the cases in a fastfile. */
static struct casereader *
fastfile_get_reader (const struct casefile *cf_)
{
  struct casefile *cf = (struct casefile *) cf_;
  struct fastfilereader *ffr = xzalloc (sizeof *ffr);
  struct casereader *reader = (struct casereader *) ffr;
  struct fastfile *ff = (struct fastfile *) cf;

  assert (!cf->being_destroyed);

  /* Flush the buffer to disk if it's not empty. */
  if (ff->mode == WRITE && ff->storage == DISK)
    flush_buffer (ff);

  ff->mode = READ;

  casereader_register (cf, reader, &class_reader);

  ffr->case_idx = 0;
  reader->destructive = 0;
  ffr->fd = -1;
  ffr->buffer = NULL;
  ffr->buffer_pos = 0;
  case_nullify (&ffr->c);

  if (ff->storage == DISK)
    reader_open_file (ffr);

  return reader;
}


/* Creates a copy of the casereader CR, and returns it */
static struct casereader *
fastfilereader_clone (const struct casereader *cr)
{
  const struct fastfilereader *ffr = (const struct fastfilereader *) cr ;
  struct fastfilereader *new_ffr = xzalloc (sizeof *new_ffr);

  struct casereader *new_reader = (struct casereader *) new_ffr;

  struct casefile *cf =  casereader_get_casefile (cr);
  struct fastfile *ff = (struct fastfile *) cf;

  assert (!cf->being_destroyed);

  /* Flush the buffer to disk if it's not empty. */
  if (ff->mode == WRITE && ff->storage == DISK)
    flush_buffer (ff);

  ff->mode = READ;

  casereader_register (cf, new_reader, &class_reader);

  new_ffr->case_idx = ffr->case_idx ;
  new_reader->destructive = cr->destructive;
  new_ffr->fd = ffr->fd ;
  new_ffr->buffer = ffr->buffer ;
  new_ffr->buffer_pos = ffr->buffer_pos;

  if (ff->storage == DISK)
    reader_open_file (new_ffr);

  return new_reader;
}




/* Returns the number of `union value's in a case for CF. */
static size_t
fastfile_get_value_cnt (const struct casefile *cf)
{
  const struct fastfile *ff = (const struct fastfile *) cf;
  return ff->value_cnt;
}

/* Appends a copy of case C to fastfile CF.  Not valid after any
   reader for CF has been created.
   Returns true if successful, false if an I/O error occurred. */
static bool
fastfile_append (struct casefile *cf, const struct ccase *c)
{
  struct fastfile *ff = (struct fastfile *) cf;
  assert (ff->mode == WRITE);
  assert (c != NULL);

  /* Try memory first. */
  if (ff->storage == MEMORY)
    {
      if (case_bytes < get_workspace ())
	{
	  size_t block_idx = ff->case_cnt / CASES_PER_BLOCK;
	  size_t case_idx = ff->case_cnt % CASES_PER_BLOCK;
	  struct ccase new_case;

	  case_bytes += ff->case_acct_size;
	  case_clone (&new_case, c);
	  if (case_idx == 0)
	    {
	      if ((block_idx & (block_idx - 1)) == 0)
		{
		  size_t block_cap = block_idx == 0 ? 1 : block_idx * 2;
		  ff->cases = xnrealloc (ff->cases,
					 block_cap, sizeof *ff->cases);
		}

	      ff->cases[block_idx] = xnmalloc (CASES_PER_BLOCK,
					       sizeof **ff->cases);
	    }

	  case_move (&ff->cases[block_idx][case_idx], &new_case);
	}
      else
	{
	  casefile_to_disk (cf);
	  assert (ff->storage == DISK);
	  write_case_to_disk (ff, c);
	}
    }
  else
    write_case_to_disk (ff, c);

  ff->case_cnt++;
  return ff->ok;
}


/* Returns the number of cases in fastfile CF. */
static unsigned long
fastfile_get_case_cnt (const struct casefile *cf)
{
  const struct fastfile *ff = (const struct fastfile *) cf;
  return ff->case_cnt;
}


/* Returns true only if fastfile CF is stored in memory (instead of on
   disk), false otherwise. */
static bool
fastfile_in_core (const struct casefile *cf)
{
  const struct fastfile *ff = (const struct fastfile *) cf;
  return (ff->storage == MEMORY);
}


/* If CF is currently stored in memory, writes it to disk.  Readers, if any,
   retain their current positions.
   Returns true if successful, false if an I/O error occurred. */
static bool
fastfile_to_disk (const struct casefile *cf_)
{
  struct fastfile *ff = (struct fastfile *) cf_;
  struct casefile *cf = &ff->cf;

  if (ff->storage == MEMORY)
    {
      size_t idx, block_cnt;
      struct casereader *reader;

      assert (ff->file_name == NULL);
      assert (ff->fd == -1);
      assert (ff->buffer_used == 0);

      if (!make_temp_file (&ff->fd, &ff->file_name))
	{
	  ff->ok = false;
	  return false;
	}
      ff->storage = DISK;

      ff->buffer = xnmalloc (ff->buffer_size, sizeof *ff->buffer);
      memset (ff->buffer, 0, ff->buffer_size * sizeof *ff->buffer);

      case_bytes -= ff->case_cnt * ff->case_acct_size;
      for (idx = 0; idx < ff->case_cnt; idx++)
	{
	  size_t block_idx = idx / CASES_PER_BLOCK;
	  size_t case_idx = idx % CASES_PER_BLOCK;
	  struct ccase *c = &ff->cases[block_idx][case_idx];
	  write_case_to_disk (ff, c);
	  case_destroy (c);
	}

      block_cnt = DIV_RND_UP (ff->case_cnt, CASES_PER_BLOCK);
      for (idx = 0; idx < block_cnt; idx++)
	free (ff->cases[idx]);

      free (ff->cases);
      ff->cases = NULL;

      if (ff->mode == READ)
	flush_buffer (ff);

      ll_for_each (reader, struct casereader, ll, &cf->reader_list)
	reader_open_file ((struct fastfilereader *) reader);

    }
  return ff->ok;
}

/* Puts a fastfile to "sleep", that is, minimizes the resources
   needed for it by closing its file descriptor and freeing its
   buffer.  This is useful if we need so many fastfiles that we
   might not have enough memory and file descriptors to go
   around.

   For simplicity, this implementation always converts the
   fastfile to reader mode.  If this turns out to be a problem,
   with a little extra work we could also support sleeping
   writers.

   Returns true if successful, false if an I/O error occurred. */
static bool
fastfile_sleep (const struct casefile *cf_)
{
  struct fastfile *ff = (struct fastfile *) cf_;
  struct casefile *cf = &ff->cf;

  fastfile_to_disk (cf);
  flush_buffer (ff);

  if (ff->fd != -1)
    {
      safe_close (ff->fd);
      ff->fd = -1;
    }
  if (ff->buffer != NULL)
    {
      free (ff->buffer);
      ff->buffer = NULL;
    }

  return ff->ok;
}


/* Returns true if an I/O error has occurred in fastfile CF. */
static bool
fastfile_error (const struct casefile *cf)
{
  const struct fastfile *ff = (const struct fastfile *) cf;
  return !ff->ok;
}

/* Destroys fastfile CF. */
static void
fastfile_destroy (struct casefile *cf)
{
  struct fastfile *ff = (struct fastfile *) cf;

  if (cf != NULL)
    {
      if (ff->cases != NULL)
	{
	  size_t idx, block_cnt;

	  case_bytes -= ff->case_cnt * ff->case_acct_size;
	  for (idx = 0; idx < ff->case_cnt; idx++)
	    {
	      size_t block_idx = idx / CASES_PER_BLOCK;
	      size_t case_idx = idx % CASES_PER_BLOCK;
	      struct ccase *c = &ff->cases[block_idx][case_idx];
	      case_destroy (c);
	    }

	  block_cnt = DIV_RND_UP (ff->case_cnt, CASES_PER_BLOCK);
	  for (idx = 0; idx < block_cnt; idx++)
	    free (ff->cases[idx]);

	  free (ff->cases);
	}

      if (ff->fd != -1)
	safe_close (ff->fd);

      if (ff->file_name != NULL && remove (ff->file_name) == -1)
	io_error (ff, _("%s: Removing temporary file: %s."),
		  ff->file_name, strerror (errno));
      free (ff->file_name);

      free (ff->buffer);

      free (ff);
    }
}


/* Creates and returns a fastfile to store cases of VALUE_CNT
   `union value's each. */
struct casefile *
fastfile_create (size_t value_cnt)
{
  struct fastfile *ff = xzalloc (sizeof *ff);
  struct casefile *cf = &ff->cf;

  casefile_register (cf, &class);

  ff->value_cnt = value_cnt;
  ff->case_acct_size = (ff->value_cnt + 4) * sizeof *ff->buffer;
  ff->case_cnt = 0;
  ff->storage = MEMORY;
  ff->mode = WRITE;
  cf->being_destroyed = false;
  ff->ok = true;
  ff->cases = NULL;
  ff->fd = -1;
  ff->file_name = NULL;
  ff->buffer = NULL;
  ff->buffer_size = ROUND_UP (ff->value_cnt, IO_BUF_SIZE);
  if (ff->value_cnt > 0 && ff->buffer_size % ff->value_cnt > 64)
    ff->buffer_size = ff->value_cnt;
  ff->buffer_used = 0;

  return cf;
}



/* Marks FF as having encountered an I/O error.
   If this is the first error on CF, reports FORMAT to the user,
   doing printf()-style substitutions. */
static void
io_error (struct fastfile *ff, const char *format, ...)
{
  if (ff->ok)
    {
      struct msg m;
      va_list args;

      m.category = MSG_GENERAL;
      m.severity = MSG_ERROR;
      m.where.file_name = NULL;
      m.where.line_number = -1;
      va_start (args, format);
      m.text = xvasprintf (format, args);
      va_end (args);

      msg_emit (&m);
    }
  ff->ok = false;
}

/* Calls open(), passing FILE_NAME and FLAGS, repeating as necessary
   to deal with interrupted calls. */
static int
safe_open (const char *file_name, int flags)
{
  int fd;

  do
    {
      fd = open (file_name, flags);
    }
  while (fd == -1 && errno == EINTR);

  return fd;
}

/* Calls close(), passing FD, repeating as necessary to deal with
   interrupted calls. */
static int
safe_close (int fd)
{
  int retval;

  do
    {
      retval = close (fd);
    }
  while (retval == -1 && errno == EINTR);

  return retval;
}


/* Writes case C to fastfile CF's disk buffer, first flushing the buffer to
   disk if it would otherwise overflow.
   Returns true if successful, false if an I/O error occurred. */
static void
write_case_to_disk (struct fastfile *ff, const struct ccase *c)
{
  if (!ff->ok)
    return;

  case_to_values (c, ff->buffer + ff->buffer_used, ff->value_cnt);
  ff->buffer_used += ff->value_cnt;
  if (ff->buffer_used + ff->value_cnt > ff->buffer_size)
    flush_buffer (ff);
}


/* If any bytes in FF's output buffer are used, flush them to
   disk. */
static void
flush_buffer (struct fastfile *ff)
{
  if (ff->ok && ff->buffer_used > 0)
    {
      if (!full_write (ff->fd, ff->buffer,
		       ff->buffer_size * sizeof *ff->buffer))
	io_error (ff, _("Error writing temporary file: %s."),
		  strerror (errno));
      ff->buffer_used = 0;
    }
}


/* Opens a disk file for READER and seeks to the current position as indicated
   by case_idx.  Normally the current position is the beginning of the file,
   but fastfile_to_disk may cause the file to be opened at a different
   position. */
static void
reader_open_file (struct fastfilereader *reader)
{
  struct casefile *cf = casereader_get_casefile(&reader->cr);
  struct fastfile *ff = (struct fastfile *) cf;
  if (!ff->ok || reader->case_idx >= ff->case_cnt)
    return;

  if (ff->fd != -1)
    {
      reader->fd = ff->fd;
      ff->fd = -1;
    }
  else
    {
      reader->fd = safe_open (ff->file_name, O_RDONLY);
      if (reader->fd < 0)
	io_error (ff, _("%s: Opening temporary file: %s."),
		  ff->file_name, strerror (errno));
    }

  if (ff->buffer != NULL)
    {
      reader->buffer = ff->buffer;
      ff->buffer = NULL;
    }
  else
    {
      reader->buffer = xnmalloc (ff->buffer_size, sizeof *ff->buffer);
      memset (reader->buffer, 0, ff->buffer_size * sizeof *ff->buffer);
    }

  case_create (&reader->c, ff->value_cnt);

  reader->buffer_ofs = -1;
  reader->file_ofs = -1;
  seek_and_fill_buffer (reader);
}

/* Seeks the backing file for READER to the proper position and
   refreshes the buffer contents. */
static void
seek_and_fill_buffer (struct fastfilereader *reader)
{
  struct casefile *cf = casereader_get_casefile(&reader->cr);
  struct fastfile *ff = (struct fastfile *) cf;
  off_t new_ofs;

  if (ff->value_cnt != 0)
    {
      size_t buffer_case_cnt = ff->buffer_size / ff->value_cnt;
      new_ofs = ((off_t) reader->case_idx / buffer_case_cnt
		 * ff->buffer_size * sizeof *ff->buffer);
      reader->buffer_pos = (reader->case_idx % buffer_case_cnt
			    * ff->value_cnt);
    }
  else
    new_ofs = 0;
  if (new_ofs != reader->file_ofs)
    {
      if (lseek (reader->fd, new_ofs, SEEK_SET) != new_ofs)
	io_error (ff, _("%s: Seeking temporary file: %s."),
		  ff->file_name, strerror (errno));
      else
	reader->file_ofs = new_ofs;
    }

  if (ff->case_cnt > 0 && ff->value_cnt > 0 && reader->buffer_ofs != new_ofs)
    fill_buffer (reader);
}

/* Fills READER's buffer by reading a block from disk. */
static bool
fill_buffer (struct fastfilereader *reader)
{
  struct casefile *cf = casereader_get_casefile(&reader->cr);
  struct fastfile *ff = (struct fastfile *) cf;
  if (ff->ok)
    {
      int bytes = full_read (reader->fd, reader->buffer,
			     ff->buffer_size *
			     sizeof *reader->buffer);
      if (bytes < 0)
	io_error (ff, _("%s: Reading temporary file: %s."),
		  ff->file_name, strerror (errno));
      else if (bytes != ff->buffer_size * sizeof *reader->buffer)
	io_error (ff, _("%s: Temporary file ended unexpectedly."),
		  ff->file_name);
      else
	{
	  reader->buffer_ofs = reader->file_ofs;
	  reader->file_ofs += bytes;
	}
    }
  return ff->ok;
}

static const struct class_casefile class = 
  {
    fastfile_destroy,
    fastfile_error,
    fastfile_get_value_cnt,
    fastfile_get_case_cnt,
    fastfile_get_reader,
    fastfile_append,


    fastfile_in_core,
    fastfile_to_disk,
    fastfile_sleep,
  };

static const struct class_casereader class_reader = 
  {
    fastfilereader_get_next_case,
    fastfilereader_cnum,
    fastfilereader_destroy,
    fastfilereader_clone,
  };
