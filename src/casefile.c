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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include "casefile.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "alloc.h"
#include "case.h"
#include "error.h"
#include "misc.h"
#include "mkfile.h"
#include "settings.h"
#include "var.h"

#ifdef HAVE_VALGRIND_VALGRIND_H
#include <valgrind/valgrind.h>
#endif

#define IO_BUF_SIZE (8192 / sizeof (union value))

/* A casefile is a sequentially accessible array of immutable
   cases.  It may be stored in memory or on disk as workspace
   allows.  Cases may be appended to the end of the file.  Cases
   may be read sequentially starting from the beginning of the
   file.  Once any cases have been read, no more cases may be
   appended.  The entire file is discarded at once. */

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
    int being_destroyed;                /* Does a destructive reader exist? */

    /* Memory storage. */
    struct ccase **cases;               /* Pointer to array of cases. */

    /* Disk storage. */
    int fd;                             /* File descriptor, -1 if none. */
    char *filename;                     /* Filename. */
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
    int destructive;                    /* Is this a destructive reader? */

    /* Disk storage. */
    int fd;                             /* File descriptor. */
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

static void reader_open_file (struct casereader *reader);
static void write_case_to_disk (struct casefile *cf, const struct ccase *c);
static void flush_buffer (struct casefile *cf);
static void fill_buffer (struct casereader *reader);

static int safe_open (const char *filename, int flags);
static int safe_close (int fd);
static int full_read (int fd, void *buffer, size_t size);
static int full_write (int fd, const void *buffer, size_t size);

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
  cf->cases = NULL;
  cf->fd = -1;
  cf->filename = NULL;
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
          
      if (cf->filename != NULL && remove (cf->filename) == -1) 
        msg (ME, _("%s: Removing temporary file: %s."),
             cf->filename, strerror (errno));
      free (cf->filename);

      free (cf->buffer);

      free (cf);
    }
}

/* Returns nonzero only if casefile CF is stored in memory (instead of on
   disk). */
int
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
   writers. */
void
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
   reader for CF has been created. */
void
casefile_append (struct casefile *cf, const struct ccase *c) 
{
  assert (cf != NULL);
  assert (c != NULL);
  assert (cf->mode == WRITE);

  /* Try memory first. */
  if (cf->storage == MEMORY) 
    {
      if (case_bytes < get_max_workspace ())
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
                  cf->cases = xrealloc (cf->cases,
                                        sizeof *cf->cases * block_cap);
                }

              cf->cases[block_idx] = xmalloc (sizeof **cf->cases
                                              * CASES_PER_BLOCK);
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
}

/* Appends case C to casefile CF, which takes over ownership of
   C.  Not valid after any reader for CF has been created. */
void
casefile_append_xfer (struct casefile *cf, struct ccase *c) 
{
  casefile_append (cf, c);
  case_destroy (c);
}

/* Writes case C to casefile CF's disk buffer, first flushing the buffer to
   disk if it would otherwise overflow. */
static void
write_case_to_disk (struct casefile *cf, const struct ccase *c) 
{
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
  if (cf->buffer_used > 0) 
    {
      if (!full_write (cf->fd, cf->buffer,
                       cf->buffer_size * sizeof *cf->buffer)) 
        msg (FE, _("Error writing temporary file: %s."), strerror (errno));

      cf->buffer_used = 0;
    } 
}


/* If CF is currently stored in memory, writes it to disk.  Readers, if any,
   retain their current positions. */
void
casefile_to_disk (const struct casefile *cf_) 
{
  struct casefile *cf = (struct casefile *) cf_;
  struct casereader *reader;
  
  assert (cf != NULL);

  if (cf->storage == MEMORY)
    {
      size_t idx, block_cnt;
      
      assert (cf->filename == NULL);
      assert (cf->fd == -1);
      assert (cf->buffer_used == 0);

      cf->storage = DISK;
      if (!make_temp_file (&cf->fd, &cf->filename))
        err_failure ();
      cf->buffer = xmalloc (cf->buffer_size * sizeof *cf->buffer);
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
  reader->fd = -1;
  reader->buffer = NULL;
  reader->buffer_pos = 0;
  case_nullify (&reader->c);

  if (reader->cf->storage == DISK) 
    reader_open_file (reader);

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
  off_t file_ofs;

  if (reader->case_idx >= cf->case_cnt)
    return;

  if (cf->fd != -1) 
    {
      reader->fd = cf->fd;
      cf->fd = -1;
    }
  else 
    {
      reader->fd = safe_open (cf->filename, O_RDONLY);
      if (reader->fd < 0)
        msg (FE, _("%s: Opening temporary file: %s."),
             cf->filename, strerror (errno));
    }

  if (cf->buffer != NULL) 
    {
      reader->buffer = cf->buffer;
      cf->buffer = NULL; 
    }
  else 
    {
      reader->buffer = xmalloc (cf->buffer_size * sizeof *cf->buffer);
      memset (reader->buffer, 0, cf->buffer_size * sizeof *cf->buffer); 
    }

  if (cf->value_cnt != 0) 
    {
      size_t buffer_case_cnt = cf->buffer_size / cf->value_cnt;
      file_ofs = ((off_t) reader->case_idx / buffer_case_cnt
                  * cf->buffer_size * sizeof *cf->buffer);
      reader->buffer_pos = (reader->case_idx % buffer_case_cnt
                            * cf->value_cnt);
    }
  else 
    file_ofs = 0;
  if (lseek (reader->fd, file_ofs, SEEK_SET) != file_ofs)
    msg (FE, _("%s: Seeking temporary file: %s."),
         cf->filename, strerror (errno));

  if (cf->case_cnt > 0 && cf->value_cnt > 0)
    fill_buffer (reader);

  case_create (&reader->c, cf->value_cnt);
}

/* Fills READER's buffer by reading a block from disk. */
static void
fill_buffer (struct casereader *reader)
{
  int retval = full_read (reader->fd, reader->buffer,
                          reader->cf->buffer_size * sizeof *reader->buffer);
  if (retval < 0)
    msg (FE, _("%s: Reading temporary file: %s."),
         reader->cf->filename, strerror (errno));
  else if (retval != reader->cf->buffer_size * sizeof *reader->buffer)
    msg (FE, _("%s: Temporary file ended unexpectedly."),
         reader->cf->filename); 
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
int
casereader_read (struct casereader *reader, struct ccase *c) 
{
  assert (reader != NULL);
  
  if (reader->case_idx >= reader->cf->case_cnt) 
    return 0;

  if (reader->cf->storage == MEMORY) 
    {
      size_t block_idx = reader->case_idx / CASES_PER_BLOCK;
      size_t case_idx = reader->case_idx % CASES_PER_BLOCK;

      case_clone (c, &reader->cf->cases[block_idx][case_idx]);
      reader->case_idx++;
      return 1;
    }
  else 
    {
      if (reader->buffer_pos + reader->cf->value_cnt > reader->cf->buffer_size)
        {
          fill_buffer (reader);
          reader->buffer_pos = 0;
        }

      case_from_values (&reader->c, reader->buffer + reader->buffer_pos,
                        reader->cf->value_cnt);
      reader->buffer_pos += reader->cf->value_cnt;
      reader->case_idx++;

      case_clone (c, &reader->c);
      return 1;
    }
}

/* Reads the next case from READER into C and transfers ownership
   to the caller.  Caller is responsible for destroying C.
   Returns true if successful, false at end of file. */
int
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
      return 1;
    }
}

/* Reads the next case from READER into C and transfers ownership
   to the caller.  Caller is responsible for destroying C.
   Assert-fails at end of file. */
void
casereader_read_xfer_assert (struct casereader *reader, struct ccase *c) 
{
  bool success = casereader_read_xfer (reader, c);
  assert (success);
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

/* Calls open(), passing FILENAME and FLAGS, repeating as necessary
   to deal with interrupted calls. */
static int
safe_open (const char *filename, int flags) 
{
  int fd;

  do 
    {
      fd = open (filename, flags);
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

/* Calls read(), passing FD, BUFFER, and SIZE, repeating as
   necessary to deal with interrupted calls. */
static int
full_read (int fd, void *buffer_, size_t size) 
{
  char *buffer = buffer_;
  size_t bytes_read = 0;
  
  while (bytes_read < size)
    {
      int retval = read (fd, buffer + bytes_read, size - bytes_read);
      if (retval > 0) 
        bytes_read += retval; 
      else if (retval == 0) 
        return bytes_read;
      else if (errno != EINTR)
        return -1;
    }

  return bytes_read;
}

/* Calls write(), passing FD, BUFFER, and SIZE, repeating as
   necessary to deal with interrupted calls. */
static int
full_write (int fd, const void *buffer_, size_t size) 
{
  const char *buffer = buffer_;
  size_t bytes_written = 0;
  
  while (bytes_written < size)
    {
      int retval = write (fd, buffer + bytes_written, size - bytes_written);
      if (retval >= 0) 
        bytes_written += retval; 
      else if (errno != EINTR)
        return -1;
    }

  return bytes_written;
}


/* Registers our exit handler with atexit() if it has not already
   been registered. */
static void
register_atexit (void) 
{
  static int registered = 0;
  if (!registered) 
    {
      registered = 1;
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

#include <gsl/gsl_rng.h>
#include <stdarg.h>
#include "command.h"
#include "lexer.h"

static void test_casefile (int pattern, size_t value_cnt, size_t case_cnt);
static void get_random_case (struct ccase *, size_t value_cnt,
                             size_t case_idx);
static void write_random_case (struct casefile *cf, size_t case_idx);
static void read_and_verify_random_case (struct casefile *cf,
                                         struct casereader *reader,
                                         size_t case_idx);
static void fail_test (const char *message, ...);

int
cmd_debug_casefile (void) 
{
  static const size_t sizes[] =
    {
      1, 2, 3, 4, 5, 6, 7, 14, 15, 16, 17, 31, 55, 73,
      100, 137, 257, 521, 1031, 2053
    };
  int size_max;
  int case_max;
  int pattern;

  size_max = sizeof sizes / sizeof *sizes;
  if (lex_match_id ("SMALL")) 
    {
      size_max -= 4;
      case_max = 511; 
    }
  else
    case_max = 4095;
  if (token != '.')
    return lex_end_of_command ();
    
  for (pattern = 0; pattern < 6; pattern++) 
    {
      const size_t *size;

      for (size = sizes; size < sizes + size_max; size++) 
        {
          size_t case_cnt;

          for (case_cnt = 0; case_cnt <= case_max;
               case_cnt = (case_cnt * 2) + 1)
            test_casefile (pattern, *size, case_cnt);
        }
    }
  printf ("Casefile tests succeeded.\n");
  return CMD_SUCCESS;
}

static void
test_casefile (int pattern, size_t value_cnt, size_t case_cnt) 
{
  struct casefile *cf;
  struct casereader *r1, *r2;
  struct ccase c;
  gsl_rng *rng;
  size_t i, j;

  rng = gsl_rng_alloc (gsl_rng_mt19937);
  cf = casefile_create (value_cnt);
  if (pattern == 5)
    casefile_to_disk (cf);
  for (i = 0; i < case_cnt; i++)
    write_random_case (cf, i);
  if (pattern == 5)
    casefile_sleep (cf);
  r1 = casefile_get_reader (cf);
  r2 = casefile_get_reader (cf);
  switch (pattern) 
    {
    case 0:
    case 5:
      for (i = 0; i < case_cnt; i++) 
        {
          read_and_verify_random_case (cf, r1, i);
          read_and_verify_random_case (cf, r2, i);
        } 
      break;
    case 1:
      for (i = 0; i < case_cnt; i++)
        read_and_verify_random_case (cf, r1, i);
      for (i = 0; i < case_cnt; i++) 
        read_and_verify_random_case (cf, r2, i);
      break;
    case 2:
    case 3:
    case 4:
      for (i = j = 0; i < case_cnt; i++) 
        {
          read_and_verify_random_case (cf, r1, i);
          if (gsl_rng_get (rng) % pattern == 0) 
            read_and_verify_random_case (cf, r2, j++); 
          if (i == case_cnt / 2)
            casefile_to_disk (cf);
        }
      for (; j < case_cnt; j++) 
        read_and_verify_random_case (cf, r2, j);
      break;
    }
  if (casereader_read (r1, &c))
    fail_test ("Casereader 1 not at end of file.");
  if (casereader_read (r2, &c))
    fail_test ("Casereader 2 not at end of file.");
  if (pattern != 1)
    casereader_destroy (r1);
  if (pattern != 2)
    casereader_destroy (r2);
  if (pattern > 2) 
    {
      r1 = casefile_get_destructive_reader (cf);
      for (i = 0; i < case_cnt; i++) 
        {
          struct ccase read_case, expected_case;
          
          get_random_case (&expected_case, value_cnt, i);
          if (!casereader_read_xfer (r1, &read_case)) 
            fail_test ("Premature end of casefile.");
          for (j = 0; j < value_cnt; j++) 
            {
              double a = case_num (&read_case, j);
              double b = case_num (&expected_case, j);
              if (a != b)
                fail_test ("Case %lu fails comparison.", (unsigned long) i); 
            }
          case_destroy (&expected_case);
          case_destroy (&read_case);
        }
      casereader_destroy (r1);
    }
  casefile_destroy (cf);
  gsl_rng_free (rng);
}

static void
get_random_case (struct ccase *c, size_t value_cnt, size_t case_idx) 
{
  int i;
  case_create (c, value_cnt);
  for (i = 0; i < value_cnt; i++)
    case_data_rw (c, i)->f = case_idx % 257 + i;
}

static void
write_random_case (struct casefile *cf, size_t case_idx) 
{
  struct ccase c;
  get_random_case (&c, casefile_get_value_cnt (cf), case_idx);
  casefile_append_xfer (cf, &c);
}

static void
read_and_verify_random_case (struct casefile *cf,
                             struct casereader *reader, size_t case_idx) 
{
  struct ccase read_case, expected_case;
  size_t value_cnt;
  size_t i;
  
  value_cnt = casefile_get_value_cnt (cf);
  get_random_case (&expected_case, value_cnt, case_idx);
  if (!casereader_read (reader, &read_case)) 
    fail_test ("Premature end of casefile.");
  for (i = 0; i < value_cnt; i++) 
    {
      double a = case_num (&read_case, i);
      double b = case_num (&expected_case, i);
      if (a != b)
        fail_test ("Case %lu fails comparison.", (unsigned long) case_idx); 
    }
  case_destroy (&read_case);
  case_destroy (&expected_case);
}

static void
fail_test (const char *message, ...) 
{
  va_list args;

  va_start (args, message);
  vprintf (message, args);
  putchar ('\n');
  va_end (args);
  
  exit (1);
}
