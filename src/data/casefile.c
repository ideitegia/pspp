/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libpspp/alloc.h>
#include "case.h"
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include "full-read.h"
#include "full-write.h"
#include <libpspp/misc.h>
#include "make-file.h"
#include "settings.h"
#include "variable.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#define IO_BUF_SIZE (8192 / sizeof (union value))

/* A casefile represents a sequentially accessible stream of
   immutable cases.

   If workspace allows, a casefile is maintained in memory.  If
   workspace overflows, then the casefile is pushed to disk.  In
   either case the interface presented to callers is kept the
   same.

   The life cycle of a casefile consists of up to three phases:

       1. Writing.  The casefile initially contains no cases.  In
          this phase, any number of cases may be appended to the
          end of a casefile.  (Cases are never inserted in the
          middle or before the beginning of a casefile.)

          Use casefile_append() or casefile_append_xfer() to
          append a case to a casefile.

       2. Reading.  The casefile may be read sequentially,
          starting from the beginning, by "casereaders".  Any
          number of casereaders may be created, at any time,
          during the reading phase.  Each casereader has an
          independent position in the casefile.

          Ordinary casereaders may only move forward.  They
          cannot move backward to arbitrary records or seek
          randomly.  Cloning casereaders is possible, but it is
          not yet implemented.

          Use casefile_get_reader() to create a casereader for
          use in phase 2.  This also transitions from phase 1 to
          phase 2.  Calling casefile_mode_reader() makes the same
          transition, without creating a casereader.

          Use casereader_read() or casereader_read_xfer() to read
          a case from a casereader.  Use casereader_destroy() to
          discard a casereader when it is no longer needed.

          "Random" casereaders, which support a seek operation,
          may also be created.  These should not, generally, be
          used for statistical procedures, because random access
          is much slower than sequential access.  They are
          intended for use by the GUI.

       3. Destruction.  This phase is optional.  The casefile is
          also read with casereaders in this phase, but the
          ability to create new casereaders is curtailed.

          In this phase, casereaders could still be cloned (once
          we eventually implement cloning).

          To transition from phase 1 or 2 to phase 3 and create a
          casereader, call casefile_get_destructive_reader().
          The same functions apply to the casereader obtained
          this way as apply to casereaders obtained in phase 2.
          
          After casefile_get_destructive_reader() is called, no
          more casereaders may be created with
          casefile_get_reader() or
          casefile_get_destructive_reader().  (If cloning of
          casereaders were implemented, it would still be
          possible.)

          The purpose of the limitations applied to casereaders
          in phase 3 is to allow in-memory casefiles to fully
          transfer ownership of cases to the casereaders,
          avoiding the need for extra copies of case data.  For
          relatively static data sets with many variables, I
          suspect (without evidence) that this may be a big
          performance boost.

   When a casefile is no longer needed, it may be destroyed with
   casefile_destroy().  This function will also destroy any
   remaining casereaders. */

/* FIXME: should we implement compression? */

/* In-memory cases are arranged in an array of arrays.  The top
   level is variable size and the size of each bottom level array
   is fixed at the number of cases defined here.  */
#define CASES_PER_BLOCK 128             

/* A casefile. */
struct casefile 
  {
    /* Basic data. */
    struct casefile *next, *prev;       /* Next, prev in global list. */
    size_t value_cnt;                   /* Case size in `union value's. */
    size_t case_acct_size;              /* Case size for accounting. */
    unsigned long case_cnt;             /* Number of cases stored. */
    enum { MEMORY, DISK } storage;      /* Where cases are stored. */
    enum { WRITE, READ } mode;          /* Is writing or reading allowed? */
    struct casereader *readers;         /* List of our readers. */
    bool being_destroyed;               /* Does a destructive reader exist? */
    bool ok;                            /* False after I/O error. */

    /* Memory storage. */
    struct ccase **cases;               /* Pointer to array of cases. */

    /* Disk storage. */
    int fd;                             /* File descriptor, -1 if none. */
    char *file_name;                    /* File name. */
    union value *buffer;                /* I/O buffer, NULL if none. */
    size_t buffer_used;                 /* Number of values used in buffer. */
    size_t buffer_size;                 /* Buffer size in values. */
  };

/* For reading out the cases in a casefile. */
struct casereader 
  {
    struct casereader *next, *prev;     /* Next, prev in casefile's list. */
    struct casefile *cf;                /* Our casefile. */
    unsigned long case_idx;             /* Case number of current case. */
    bool destructive;                   /* Is this a destructive reader? */
    bool random;                        /* Is this a random reader? */

    /* Disk storage. */
    int fd;                             /* File descriptor. */
    off_t file_ofs;                     /* Current position in fd. */
    off_t buffer_ofs;                   /* File offset of buffer start. */
    union value *buffer;                /* I/O buffer. */
    size_t buffer_pos;                  /* Offset of buffer position. */
    struct ccase c;                     /* Current case. */
  };

/* Return the case number of the current case */
unsigned long
casereader_cnum(const struct casereader *r)
{
  return r->case_idx;
}

/* Doubly linked list of all casefiles. */
static struct casefile *casefiles;

/* Number of bytes of case allocated in in-memory casefiles. */
static size_t case_bytes;

static void register_atexit (void);
static void exit_handler (void);

static void reader_open_file (struct casereader *);
static void write_case_to_disk (struct casefile *, const struct ccase *);
static void flush_buffer (struct casefile *);
static void seek_and_fill_buffer (struct casereader *);
static bool fill_buffer (struct casereader *);

static void io_error (struct casefile *, const char *, ...)
     PRINTF_FORMAT (2, 3);
static int safe_open (const char *file_name, int flags);
static int safe_close (int fd);

/* Creates and returns a casefile to store cases of VALUE_CNT
   `union value's each. */
struct casefile *
casefile_create (size_t value_cnt) 
{
  struct casefile *cf = xmalloc (sizeof *cf);
  cf->next = casefiles;
  cf->prev = NULL;
  if (cf->next != NULL)
    cf->next->prev = cf;
  casefiles = cf;
  cf->value_cnt = value_cnt;
  cf->case_acct_size = (cf->value_cnt + 4) * sizeof *cf->buffer;
  cf->case_cnt = 0;
  cf->storage = MEMORY;
  cf->mode = WRITE;
  cf->readers = NULL;
  cf->being_destroyed = 0;
  cf->ok = true;
  cf->cases = NULL;
  cf->fd = -1;
  cf->file_name = NULL;
  cf->buffer = NULL;
  cf->buffer_size = ROUND_UP (cf->value_cnt, IO_BUF_SIZE);
  if (cf->value_cnt > 0 && cf->buffer_size % cf->value_cnt > 64)
    cf->buffer_size = cf->value_cnt;
  cf->buffer_used = 0;
  register_atexit ();
  return cf;
}

/* Destroys casefile CF. */
void
casefile_destroy (struct casefile *cf) 
{
  if (cf != NULL) 
    {
      if (cf->next != NULL)
        cf->next->prev = cf->prev;
      if (cf->prev != NULL)
        cf->prev->next = cf->next;
      if (casefiles == cf)
        casefiles = cf->next;

      while (cf->readers != NULL) 
        casereader_destroy (cf->readers);

      if (cf->cases != NULL) 
        {
          size_t idx, block_cnt;

          case_bytes -= cf->case_cnt * cf->case_acct_size;
          for (idx = 0; idx < cf->case_cnt; idx++)
            {
              size_t block_idx = idx / CASES_PER_BLOCK;
              size_t case_idx = idx % CASES_PER_BLOCK;
              struct ccase *c = &cf->cases[block_idx][case_idx];
              case_destroy (c);
            }

          block_cnt = DIV_RND_UP (cf->case_cnt, CASES_PER_BLOCK);
          for (idx = 0; idx < block_cnt; idx++)
            free (cf->cases[idx]);

          free (cf->cases);
        }

      if (cf->fd != -1)
        safe_close (cf->fd);
          
      if (cf->file_name != NULL && remove (cf->file_name) == -1) 
        io_error (cf, _("%s: Removing temporary file: %s."),
                  cf->file_name, strerror (errno));
      free (cf->file_name);

      free (cf->buffer);

      free (cf);
    }
}

/* Returns true if an I/O error has occurred in casefile CF. */
bool
casefile_error (const struct casefile *cf) 
{
  return !cf->ok;
}

/* Returns true only if casefile CF is stored in memory (instead of on
   disk), false otherwise. */
bool
casefile_in_core (const struct casefile *cf) 
{
  assert (cf != NULL);

  return cf->storage == MEMORY;
}

/* Puts a casefile to "sleep", that is, minimizes the resources
   needed for it by closing its file descriptor and freeing its
   buffer.  This is useful if we need so many casefiles that we
   might not have enough memory and file descriptors to go
   around.

   For simplicity, this implementation always converts the
   casefile to reader mode.  If this turns out to be a problem,
   with a little extra work we could also support sleeping
   writers.

   Returns true if successful, false if an I/O error occurred. */
bool
casefile_sleep (const struct casefile *cf_) 
{
  struct casefile *cf = (struct casefile *) cf_;
  assert (cf != NULL);

  casefile_mode_reader (cf);
  casefile_to_disk (cf);
  flush_buffer (cf);

  if (cf->fd != -1) 
    {
      safe_close (cf->fd);
      cf->fd = -1;
    }
  if (cf->buffer != NULL) 
    {
      free (cf->buffer);
      cf->buffer = NULL;
    }

  return cf->ok;
}

/* Returns the number of `union value's in a case for CF. */
size_t
casefile_get_value_cnt (const struct casefile *cf) 
{
  assert (cf != NULL);

  return cf->value_cnt;
}

/* Returns the number of cases in casefile CF. */
unsigned long
casefile_get_case_cnt (const struct casefile *cf) 
{
  assert (cf != NULL);

  return cf->case_cnt;
}

/* Appends a copy of case C to casefile CF.  Not valid after any
   reader for CF has been created.
   Returns true if successful, false if an I/O error occurred. */
bool
casefile_append (struct casefile *cf, const struct ccase *c) 
{
  assert (cf != NULL);
  assert (c != NULL);
  assert (cf->mode == WRITE);

  /* Try memory first. */
  if (cf->storage == MEMORY) 
    {
      if (case_bytes < get_workspace ())
        {
          size_t block_idx = cf->case_cnt / CASES_PER_BLOCK;
          size_t case_idx = cf->case_cnt % CASES_PER_BLOCK;
          struct ccase new_case;

          case_bytes += cf->case_acct_size;
          case_clone (&new_case, c);
          if (case_idx == 0) 
            {
              if ((block_idx & (block_idx - 1)) == 0) 
                {
                  size_t block_cap = block_idx == 0 ? 1 : block_idx * 2;
                  cf->cases = xnrealloc (cf->cases,
                                         block_cap, sizeof *cf->cases);
                }

              cf->cases[block_idx] = xnmalloc (CASES_PER_BLOCK,
                                               sizeof **cf->cases);
            }

          case_move (&cf->cases[block_idx][case_idx], &new_case);
        }
      else
        {
          casefile_to_disk (cf);
          assert (cf->storage == DISK);
          write_case_to_disk (cf, c);
        }
    }
  else
    write_case_to_disk (cf, c);

  cf->case_cnt++;
  return cf->ok;
}

/* Appends case C to casefile CF, which takes over ownership of
   C.  Not valid after any reader for CF has been created.
   Returns true if successful, false if an I/O error occurred. */
bool
casefile_append_xfer (struct casefile *cf, struct ccase *c) 
{
  casefile_append (cf, c);
  case_destroy (c);
  return cf->ok;
}

/* Writes case C to casefile CF's disk buffer, first flushing the buffer to
   disk if it would otherwise overflow.
   Returns true if successful, false if an I/O error occurred. */
static void
write_case_to_disk (struct casefile *cf, const struct ccase *c) 
{
  if (!cf->ok)
    return;
  
  case_to_values (c, cf->buffer + cf->buffer_used, cf->value_cnt);
  cf->buffer_used += cf->value_cnt;
  if (cf->buffer_used + cf->value_cnt > cf->buffer_size)
    flush_buffer (cf);
}

/* If any bytes in CF's output buffer are used, flush them to
   disk. */
static void
flush_buffer (struct casefile *cf) 
{
  if (cf->ok && cf->buffer_used > 0) 
    {
      if (!full_write (cf->fd, cf->buffer,
                       cf->buffer_size * sizeof *cf->buffer))
        io_error (cf, _("Error writing temporary file: %s."),
                  strerror (errno));
      cf->buffer_used = 0;
    }
}

/* If CF is currently stored in memory, writes it to disk.  Readers, if any,
   retain their current positions.
   Returns true if successful, false if an I/O error occurred. */
bool
casefile_to_disk (const struct casefile *cf_) 
{
  struct casefile *cf = (struct casefile *) cf_;
  struct casereader *reader;
  
  assert (cf != NULL);

  if (cf->storage == MEMORY)
    {
      size_t idx, block_cnt;
      
      assert (cf->file_name == NULL);
      assert (cf->fd == -1);
      assert (cf->buffer_used == 0);

      if (!make_temp_file (&cf->fd, &cf->file_name))
        {
          cf->ok = false;
          return false;
        }
      cf->storage = DISK;

      cf->buffer = xnmalloc (cf->buffer_size, sizeof *cf->buffer);
      memset (cf->buffer, 0, cf->buffer_size * sizeof *cf->buffer);

      case_bytes -= cf->case_cnt * cf->case_acct_size;
      for (idx = 0; idx < cf->case_cnt; idx++)
        {
          size_t block_idx = idx / CASES_PER_BLOCK;
          size_t case_idx = idx % CASES_PER_BLOCK;
          struct ccase *c = &cf->cases[block_idx][case_idx];
          write_case_to_disk (cf, c);
          case_destroy (c);
        }

      block_cnt = DIV_RND_UP (cf->case_cnt, CASES_PER_BLOCK);
      for (idx = 0; idx < block_cnt; idx++)
        free (cf->cases[idx]);

      free (cf->cases);
      cf->cases = NULL;

      if (cf->mode == READ)
        flush_buffer (cf);

      for (reader = cf->readers; reader != NULL; reader = reader->next)
        reader_open_file (reader);
    }
  return cf->ok;
}

/* Changes CF to reader mode, ensuring that no more cases may be
   added.  Creating a casereader for CF has the same effect. */
void
casefile_mode_reader (struct casefile *cf) 
{
  assert (cf != NULL);
  cf->mode = READ;
}

/* Creates and returns a casereader for CF.  A casereader can be used to
   sequentially read the cases in a casefile. */
struct casereader *
casefile_get_reader (const struct casefile *cf_) 
{
  struct casefile *cf = (struct casefile *) cf_;
  struct casereader *reader;

  assert (cf != NULL);
  assert (!cf->being_destroyed);

  /* Flush the buffer to disk if it's not empty. */
  if (cf->mode == WRITE && cf->storage == DISK)
    flush_buffer (cf);
  
  cf->mode = READ;

  reader = xmalloc (sizeof *reader);
  reader->next = cf->readers;
  if (cf->readers != NULL)
    reader->next->prev = reader;
  cf->readers = reader;
  reader->prev = NULL;
  reader->cf = cf;
  reader->case_idx = 0;
  reader->destructive = 0;
  reader->random = false;
  reader->fd = -1;
  reader->buffer = NULL;
  reader->buffer_pos = 0;
  case_nullify (&reader->c);

  if (reader->cf->storage == DISK) 
    reader_open_file (reader);

  return reader;
}

/* Creates and returns a random casereader for CF.  A random
   casereader can be used to randomly read the cases in a
   casefile. */
struct casereader *
casefile_get_random_reader (const struct casefile *cf) 
{
  struct casereader *reader = casefile_get_reader (cf);
  reader->random = true;
  return reader;
}

/* Creates and returns a destructive casereader for CF.  Like a
   normal casereader, a destructive casereader sequentially reads
   the cases in a casefile.  Unlike a normal casereader, a
   destructive reader cannot operate concurrently with any other
   reader.  (This restriction could be relaxed in a few ways, but
   it is so far unnecessary for other code.) */
struct casereader *
casefile_get_destructive_reader (struct casefile *cf) 
{
  struct casereader *reader;
  
  assert (cf->readers == NULL);
  reader = casefile_get_reader (cf);
  reader->destructive = 1;
  cf->being_destroyed = 1;
  return reader;
}

/* Opens a disk file for READER and seeks to the current position as indicated
   by case_idx.  Normally the current position is the beginning of the file,
   but casefile_to_disk may cause the file to be opened at a different
   position. */
static void
reader_open_file (struct casereader *reader) 
{
  struct casefile *cf = reader->cf;
  if (!cf->ok || reader->case_idx >= cf->case_cnt)
    return;

  if (cf->fd != -1) 
    {
      reader->fd = cf->fd;
      cf->fd = -1;
    }
  else 
    {
      reader->fd = safe_open (cf->file_name, O_RDONLY);
      if (reader->fd < 0)
        io_error (cf, _("%s: Opening temporary file: %s."),
                  cf->file_name, strerror (errno));
    }

  if (cf->buffer != NULL) 
    {
      reader->buffer = cf->buffer;
      cf->buffer = NULL; 
    }
  else 
    {
      reader->buffer = xnmalloc (cf->buffer_size, sizeof *cf->buffer);
      memset (reader->buffer, 0, cf->buffer_size * sizeof *cf->buffer); 
    }

  case_create (&reader->c, cf->value_cnt);

  reader->buffer_ofs = -1;
  reader->file_ofs = -1;
  seek_and_fill_buffer (reader);
}

/* Seeks the backing file for READER to the proper position and
   refreshes the buffer contents. */
static void
seek_and_fill_buffer (struct casereader *reader) 
{
  struct casefile *cf = reader->cf;
  off_t new_ofs;

  if (cf->value_cnt != 0) 
    {
      size_t buffer_case_cnt = cf->buffer_size / cf->value_cnt;
      new_ofs = ((off_t) reader->case_idx / buffer_case_cnt
                  * cf->buffer_size * sizeof *cf->buffer);
      reader->buffer_pos = (reader->case_idx % buffer_case_cnt
                            * cf->value_cnt);
    }
  else 
    new_ofs = 0;
  if (new_ofs != reader->file_ofs) 
    {
      if (lseek (reader->fd, new_ofs, SEEK_SET) != new_ofs)
        io_error (cf, _("%s: Seeking temporary file: %s."),
                  cf->file_name, strerror (errno));
      else
        reader->file_ofs = new_ofs;
    }

  if (cf->case_cnt > 0 && cf->value_cnt > 0 && reader->buffer_ofs != new_ofs)
    fill_buffer (reader);
}

/* Fills READER's buffer by reading a block from disk. */
static bool
fill_buffer (struct casereader *reader)
{
  if (reader->cf->ok) 
    {
      int bytes = full_read (reader->fd, reader->buffer,
                             reader->cf->buffer_size * sizeof *reader->buffer);
      if (bytes < 0) 
        io_error (reader->cf, _("%s: Reading temporary file: %s."),
                  reader->cf->file_name, strerror (errno));
      else if (bytes != reader->cf->buffer_size * sizeof *reader->buffer) 
        io_error (reader->cf, _("%s: Temporary file ended unexpectedly."),
                  reader->cf->file_name);
      else 
        {
          reader->buffer_ofs = reader->file_ofs;
          reader->file_ofs += bytes; 
        }
    }
  return reader->cf->ok;
}

/* Returns the casefile that READER reads. */
const struct casefile *
casereader_get_casefile (const struct casereader *reader) 
{
  assert (reader != NULL);
  
  return reader->cf;
}

/* Reads a copy of the next case from READER into C.
   Caller is responsible for destroying C.
   Returns true if successful, false at end of file. */
bool
casereader_read (struct casereader *reader, struct ccase *c) 
{
  assert (reader != NULL);
  
  if (!reader->cf->ok || reader->case_idx >= reader->cf->case_cnt) 
    return false;

  if (reader->cf->storage == MEMORY) 
    {
      size_t block_idx = reader->case_idx / CASES_PER_BLOCK;
      size_t case_idx = reader->case_idx % CASES_PER_BLOCK;

      case_clone (c, &reader->cf->cases[block_idx][case_idx]);
      reader->case_idx++;
      return true;
    }
  else 
    {
      if (reader->buffer_pos + reader->cf->value_cnt > reader->cf->buffer_size)
        {
          if (!fill_buffer (reader))
            return false;
          reader->buffer_pos = 0;
        }

      case_from_values (&reader->c, reader->buffer + reader->buffer_pos,
                        reader->cf->value_cnt);
      reader->buffer_pos += reader->cf->value_cnt;
      reader->case_idx++;

      case_clone (c, &reader->c);
      return true;
    }
}

/* Reads the next case from READER into C and transfers ownership
   to the caller.  Caller is responsible for destroying C.
   Returns true if successful, false at end of file or on I/O
   error. */
bool
casereader_read_xfer (struct casereader *reader, struct ccase *c)
{
  assert (reader != NULL);

  if (reader->destructive == 0
      || reader->case_idx >= reader->cf->case_cnt
      || reader->cf->storage == DISK) 
    return casereader_read (reader, c);
  else 
    {
      size_t block_idx = reader->case_idx / CASES_PER_BLOCK;
      size_t case_idx = reader->case_idx % CASES_PER_BLOCK;
      struct ccase *read_case = &reader->cf->cases[block_idx][case_idx];

      case_move (c, read_case);
      reader->case_idx++;
      return true;
    }
}

/* Sets the next case to be read by READER to CASE_IDX,
   which must be less than the number of cases in the casefile.
   Allowed only for random readers. */
void
casereader_seek (struct casereader *reader, unsigned long case_idx) 
{
  assert (reader != NULL);
  assert (reader->random);
  assert (case_idx < reader->cf->case_cnt);

  reader->case_idx = case_idx;
  if (reader->cf->storage == DISK)
    seek_and_fill_buffer (reader);
}

/* Destroys READER. */
void
casereader_destroy (struct casereader *reader)
{
  assert (reader != NULL);

  if (reader->next != NULL)
    reader->next->prev = reader->prev;
  if (reader->prev != NULL)
    reader->prev->next = reader->next;
  if (reader->cf->readers == reader)
    reader->cf->readers = reader->next;

  if (reader->cf->buffer == NULL)
    reader->cf->buffer = reader->buffer;
  else
    free (reader->buffer);

  if (reader->fd != -1) 
    {
      if (reader->cf->fd == -1)
        reader->cf->fd = reader->fd;
      else
        safe_close (reader->fd);
    }
  
  case_destroy (&reader->c);

  free (reader);
}

/* Marks CF as having encountered an I/O error.
   If this is the first error on CF, reports FORMAT to the user,
   doing printf()-style substitutions. */
static void
io_error (struct casefile *cf, const char *format, ...)
{
  if (cf->ok) 
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
  cf->ok = false;
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
static int safe_close (int fd) 
{
  int retval;

  do 
    {
      retval = close (fd);
    }
  while (retval == -1 && errno == EINTR);

  return retval;
}

/* Registers our exit handler with atexit() if it has not already
   been registered. */
static void
register_atexit (void) 
{
  static bool registered = false;
  if (!registered) 
    {
      registered = true;
      atexit (exit_handler);
    }
}

/* atexit() handler that closes and deletes our temporary
   files. */
static void
exit_handler (void) 
{
  while (casefiles != NULL)
    casefile_destroy (casefiles);
}
