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
#include "error.h"
#include "misc.h"
#include "var.h"
#include "workspace.h"

#define IO_BUF_SIZE 8192

/* A casefile is a sequentially accessible array of immutable
   cases.  It may be stored in memory or on disk as workspace
   allows.  Cases may be appended to the end of the file.  Cases
   may be read sequentially starting from the beginning of the
   file.  Once any cases have been read, no more cases may be
   appended.  The entire file is discarded at once. */

/* A casefile. */
struct casefile 
  {
    /* Basic data. */
    struct casefile *next, *prev;       /* Next, prev in global list. */
    size_t case_size;                   /* Case size in bytes. */
    size_t case_list_size;              /* Bytes to allocate for case_lists. */
    unsigned long case_cnt;             /* Number of cases stored. */
    enum { MEMORY, DISK } storage;      /* Where cases are stored. */
    enum { WRITE, READ } mode;          /* Is writing or reading allowed? */
    struct casereader *readers;         /* List of our readers. */

    /* Memory storage. */
    struct case_list *head, *tail;      /* Beginning, end of list of cases. */

    /* Disk storage. */
    int fd;                             /* File descriptor, -1 if none. */
    char *filename;                     /* Filename. */
    unsigned char *buffer;              /* I/O buffer, NULL if none. */
    size_t buffer_used;                 /* Number of bytes used in buffer. */
    size_t buffer_size;                 /* Buffer size in bytes. */
  };

/* For reading out the casing in a casefile. */
struct casereader 
  {
    struct casereader *next, *prev;     /* Next, prev in casefile's list. */
    struct casefile *cf;                /* Our casefile. */
    unsigned long case_idx;             /* Case number of current case. */

    /* Memory storage. */
    struct case_list *cur;              /* Current case. */

    /* Disk storage. */
    int fd;                             /* File descriptor. */
    unsigned char *buffer;              /* I/O buffer. */
    size_t buffer_pos;                  /* Byte offset of buffer position. */
  };

struct casefile *casefiles;

static void register_atexit (void);
static void exit_handler (void);

static void reader_open_file (struct casereader *reader);
static void write_case_to_disk (struct casefile *cf, const struct ccase *c);
static void flush_buffer (struct casefile *cf);
static void fill_buffer (struct casereader *reader);

static int safe_open (const char *filename, int flags);
static int safe_close (int fd);
static int full_read (int fd, char *buffer, size_t size);
static int full_write (int fd, const char *buffer, size_t size);

/* Creates and returns a casefile to store cases of CASE_SIZE bytes each. */
struct casefile *
casefile_create (size_t case_size) 
{
  struct casefile *cf = xmalloc (sizeof *cf);
  cf->next = casefiles;
  cf->prev = NULL;
  if (cf->next != NULL)
    cf->next->prev = cf;
  casefiles = cf;
  cf->case_size = case_size;
  cf->case_list_size = sizeof *cf->head + case_size - sizeof *cf->head->c.data;
  cf->case_cnt = 0;
  cf->storage = MEMORY;
  cf->mode = WRITE;
  cf->readers = NULL;
  cf->head = cf->tail = NULL;
  cf->fd = -1;
  cf->filename = NULL;
  cf->buffer = NULL;
  cf->buffer_size = ROUND_UP (case_size, IO_BUF_SIZE);
  if (case_size > 0 && cf->buffer_size % case_size > 512)
    cf->buffer_size = case_size;
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
      struct case_list *iter, *next;

      if (cf->next != NULL)
        cf->next->prev = cf->prev;
      if (cf->prev != NULL)
        cf->prev->next = cf->next;
      if (casefiles == cf)
        casefiles = cf->next;

      while (cf->readers != NULL) 
        casereader_destroy (cf->readers);

      for (iter = cf->head; iter != NULL; iter = next) 
        {
          next = iter->next;
          workspace_free (iter, cf->case_list_size);
        }

      if (cf->fd != -1)
        safe_close (cf->fd);
          
      if (cf->filename != NULL && remove (cf->filename) == -1) 
        msg (ME, _("%s: Removing temporary file: %s."),
             cf->filename, strerror (errno));

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

/* Returns the number of bytes in a case for CF. */
size_t
casefile_get_case_size (const struct casefile *cf) 
{
  assert (cf != NULL);

  return cf->case_size;
}

/* Returns the number of cases in casefile CF. */
unsigned long
casefile_get_case_cnt (const struct casefile *cf) 
{
  assert (cf != NULL);

  return cf->case_cnt;
}

/* Appends case C to casefile CF.  Not valid after any reader for CF has been
   created. */
void
casefile_append (struct casefile *cf, const struct ccase *c) 
{
  assert (cf != NULL);
  assert (c != NULL);
  assert (cf->mode == WRITE);

  cf->case_cnt++;

  /* Try memory first. */
  if (cf->storage == MEMORY) 
    {
      struct case_list *new_case;

      new_case = workspace_malloc (cf->case_list_size);
      if (new_case != NULL) 
        {
          memcpy (&new_case->c, c, cf->case_size);
          new_case->next = NULL;
          if (cf->head != NULL)
            cf->tail->next = new_case;
          else
            cf->head = new_case;
          cf->tail = new_case;
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
}

/* Writes case C to casefile CF's disk buffer, first flushing the buffer to
   disk if it would otherwise overflow. */
static void
write_case_to_disk (struct casefile *cf, const struct ccase *c) 
{
  memcpy (cf->buffer + cf->buffer_used, c->data, cf->case_size);
  cf->buffer_used += cf->case_size;
  if (cf->buffer_used + cf->case_size > cf->buffer_size)
    flush_buffer (cf);

}

static void
flush_buffer (struct casefile *cf) 
{
  if (cf->buffer_used > 0) 
    {
      if (!full_write (cf->fd, cf->buffer, cf->buffer_size)) 
        msg (FE, _("Error writing temporary file: %s."), strerror (errno));

      cf->buffer_used = 0;
    } 
}

/* Creates a temporary file and stores its name in *FILENAME and
   a file descriptor for it in *FD.  Returns success.  Caller is
   responsible for freeing *FILENAME. */
static int
make_temp_file (int *fd, char **filename)
{
  const char *parent_dir;

  assert (filename != NULL);
  assert (fd != NULL);

  if (getenv ("TMPDIR") != NULL)
    parent_dir = getenv ("TMPDIR");
  else
    parent_dir = P_tmpdir;

  *filename = xmalloc (strlen (parent_dir) + 32);
  sprintf (*filename, "%s%cpsppXXXXXX", parent_dir, DIR_SEPARATOR);
  *fd = mkstemp (*filename);
  if (*fd < 0)
    {
      msg (FE, _("%s: Creating temporary file: %s."),
           *filename, strerror (errno));
      free (*filename);
      *filename = NULL;
      return 0;
    }
  return 1;
}

/* If CF is currently stored in memory, writes it to disk.  Readers, if any,
   retain their current positions. */
void
casefile_to_disk (struct casefile *cf) 
{
  struct case_list *iter, *next;
  struct casereader *reader;
  
  assert (cf != NULL);
  
  if (cf->storage == MEMORY)
    {
      assert (cf->filename == NULL);
      assert (cf->fd == -1);
      assert (cf->buffer_used == 0);

      cf->storage = DISK;
      if (!make_temp_file (&cf->fd, &cf->filename))
        err_failure ();
#if HAVE_POSIX_FADVISE
      posix_fadvise (cf->fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
      cf->buffer = xmalloc (cf->buffer_size);
      memset (cf->buffer, 0, cf->buffer_size);

      for (iter = cf->head; iter != NULL; iter = next) 
        {
          next = iter->next;
          write_case_to_disk (cf, &iter->c);
          workspace_free (iter, cf->case_list_size);
        }
      flush_buffer (cf);
      cf->head = cf->tail = NULL;

      for (reader = cf->readers; reader != NULL; reader = reader->next)
        reader_open_file (reader);
    }
}

/* Merges lists A and B into a single list, which is returned.  Cases are
   compared according to comparison function COMPARE, which receives auxiliary
   data AUX. */
static struct case_list *
merge (struct case_list *a, struct case_list *b,
       int (*compare) (const struct ccase *,
                       const struct ccase *, void *aux),
       void *aux) 
{
  struct case_list head;
  struct case_list *tail = &head;

  while (a != NULL && b != NULL)
    if (compare (&a->c, &b->c, aux) < 0) 
      {
        tail->next = a;
        tail = a;
        a = a->next;
      }
    else 
      {
        tail->next = b;
        tail = b;
        b = b->next;
      }

  tail->next = a == NULL ? b : a;

  return head.next;
}

/* Sorts the list beginning at FIRST, returning the new first case.  Cases are
   compared according to comparison function COMPARE, which receives auxiliary
   data AUX. */
static struct case_list *
merge_sort (struct case_list *first,
            int (*compare) (const struct ccase *,
                            const struct ccase *, void *aux),
            void *aux) 
{
  /* FIXME: we should use a "natural" merge sort to take
     advantage of the natural order of the data. */
  struct case_list *middle, *last, *tmp;

  /* A list of zero or one elements is already sorted. */
  if (first == NULL || first->next == NULL)
    return first;

  middle = first;
  last = first->next;
  while (last != NULL && last->next != NULL) 
    {
      middle = middle->next;
      last = last->next->next;
    }
  tmp = middle;
  middle = middle->next;
  tmp->next = NULL;
  return merge (merge_sort (first, compare, aux),
                merge_sort (middle, compare, aux),
                compare, aux);
}

/* Tries to sort casefile CF according to comparison function
   COMPARE, which is passes auxiliary data AUX.  If successful,
   returns nonzero.  Currently only sorting of in-memory
   casefiles is implemented. */
int
casefile_sort (struct casefile *cf,
               int (*compare) (const struct ccase *,
                               const struct ccase *, void *aux),
               void *aux)
{
  assert (cf != NULL);
  assert (compare != NULL);

  cf->mode = WRITE;

  if (cf->case_cnt < 2)
    return 1;
  else if (cf->storage == DISK)
    return 0;
  else 
    {
      cf->head = cf->tail = merge_sort (cf->head, compare, aux);
      while (cf->tail->next != NULL)
        cf->tail = cf->tail->next;

      return 1; 
    }
}

/* Creates and returns a casereader for CF.  A casereader can be used to
   sequentially read the cases in a casefile. */
struct casereader *
casefile_get_reader (const struct casefile *cf_) 
{
  struct casefile *cf = (struct casefile *) cf_;
  struct casereader *reader;

  /* Flush the buffer to disk if it's not empty. */
  if (cf->mode == WRITE && cf->storage == DISK)
    flush_buffer (cf);
  
  cf->mode = READ;

  reader = xmalloc (sizeof *reader);
  reader->cf = cf;
  reader->next = cf->readers;
  if (cf->readers != NULL)
    reader->next->prev = reader;
  reader->prev = NULL;
  cf->readers = reader;
  reader->case_idx = 0;
  reader->cur = NULL;
  reader->fd = -1;
  reader->buffer = NULL;
  reader->buffer_pos = 0;

  if (reader->cf->storage == MEMORY) 
    reader->cur = cf->head;
  else
    reader_open_file (reader);

  return reader;
}

/* Opens a disk file for READER and seeks to the current position as indicated
   by case_idx.  Normally the current position is the beginning of the file,
   but casefile_to_disk may cause the file to be opened at a different
   position. */
static void
reader_open_file (struct casereader *reader) 
{
  size_t buffer_case_cnt;
  off_t file_ofs;

  if (reader->case_idx >= reader->cf->case_cnt)
    return;

  if (reader->cf->fd != -1) 
    {
      reader->fd = reader->cf->fd;
      reader->cf->fd = -1;
    }
  else 
    {
      reader->fd = safe_open (reader->cf->filename, O_RDONLY);
      if (reader->fd < 0)
        msg (FE, _("%s: Opening temporary file: %s."),
             reader->cf->filename, strerror (errno));
    }

  if (reader->cf->buffer != NULL) 
    {
      reader->buffer = reader->cf->buffer;
      reader->cf->buffer = NULL; 
    }
  else 
    {
      reader->buffer = xmalloc (reader->cf->buffer_size);
      memset (reader->buffer, 0, reader->cf->buffer_size); 
    }

  if (reader->cf->case_size != 0) 
    {
      buffer_case_cnt = reader->cf->buffer_size / reader->cf->case_size;
      file_ofs = ((off_t) reader->case_idx
                  / buffer_case_cnt * reader->cf->buffer_size);
      reader->buffer_pos = (reader->case_idx % buffer_case_cnt
                            * reader->cf->case_size);
    }
  else 
    file_ofs = 0;
#if HAVE_POSIX_FADVISE
  posix_fadvise (reader->fd, file_ofs, 0, POSIX_FADV_SEQUENTIAL);
#endif
  if (lseek (reader->fd, file_ofs, SEEK_SET) != file_ofs)
    msg (FE, _("%s: Seeking temporary file: %s."),
         reader->cf->filename, strerror (errno));

  if (reader->cf->case_cnt > 0 && reader->cf->case_size > 0)
    fill_buffer (reader);
}

/* Fills READER's buffer by reading a block from disk. */
static void
fill_buffer (struct casereader *reader)
{
  int retval = full_read (reader->fd, reader->buffer, reader->cf->buffer_size);
  if (retval < 0)
    msg (FE, _("%s: Reading temporary file: %s."),
         reader->cf->filename, strerror (errno));
  else if (retval != reader->cf->buffer_size)
    msg (FE, _("%s: Temporary file ended unexpectedly."),
         reader->cf->filename); 
}

int
casereader_read (struct casereader *reader, const struct ccase **c) 
{
  assert (reader != NULL);
  
  if (reader->case_idx >= reader->cf->case_cnt) 
    {
      *c = NULL;
      return 0;
    }

  reader->case_idx++;
  if (reader->cf->storage == MEMORY) 
    {
      assert (reader->cur != NULL);
      *c = &reader->cur->c;
      reader->cur = reader->cur->next;
      return 1;
    }
  else 
    {
      if (reader->buffer_pos + reader->cf->case_size > reader->cf->buffer_size)
        {
          fill_buffer (reader);
          reader->buffer_pos = 0;
        }

      *c = (struct ccase *) (reader->buffer + reader->buffer_pos);
      reader->buffer_pos += reader->cf->case_size;
      return 1;
    }
}

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

  if (reader->cf->fd == -1)
    reader->cf->fd = reader->fd;
  else
    safe_close (reader->fd);

  free (reader);
}

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

static int
full_read (int fd, char *buffer, size_t size) 
{
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

static int
full_write (int fd, const char *buffer, size_t size) 
{
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

static void
exit_handler (void) 
{
  while (casefiles != NULL)
    casefile_destroy (casefiles);
}

#include <stdarg.h>
#include "command.h"
#include "random.h"
#include "lexer.h"

static void test_casefile (int pattern, size_t case_size, size_t case_cnt);
static struct ccase *get_random_case (size_t case_size, size_t case_idx);
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
    
  for (pattern = 0; pattern < 5; pattern++) 
    {
      const size_t *size;

      for (size = sizes; size < sizes + size_max; size++) 
        {
          size_t case_cnt;

          for (case_cnt = 0; case_cnt <= case_max;
               case_cnt = (case_cnt * 2) + 1)
            test_casefile (pattern, *size * sizeof (union value), case_cnt);
        }
    }
  printf ("Casefile tests succeeded.\n");
  return CMD_SUCCESS;
}

static void
test_casefile (int pattern, size_t case_size, size_t case_cnt) 
{
  int zero = 0;
  struct casefile *cf;
  struct casereader *r1, *r2;
  const struct ccase *c;
  struct rng *rng;
  size_t i, j;

  rng = rng_create ();
  rng_seed (rng, &zero, sizeof zero);
  cf = casefile_create (case_size);
  for (i = 0; i < case_cnt; i++)
    write_random_case (cf, i);
  r1 = casefile_get_reader (cf);
  r2 = casefile_get_reader (cf);
  switch (pattern) 
    {
    case 0:
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
          if (rng_get_int (rng) % pattern == 0)
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
  casefile_destroy (cf);
  rng_destroy (rng);
}

static struct ccase *
get_random_case (size_t case_size, size_t case_idx) 
{
  struct ccase *c = xmalloc (case_size);
  memset (c, case_idx % 257, case_size);
  return c;
}

static void
write_random_case (struct casefile *cf, size_t case_idx) 
{
  struct ccase *c = get_random_case (casefile_get_case_size (cf), case_idx);
  casefile_append (cf, c);
  free (c);
}

static void
read_and_verify_random_case (struct casefile *cf,
                             struct casereader *reader, size_t case_idx) 
{
  const struct ccase *read_case;
  struct ccase *expected_case;
  size_t case_size;

  case_size = casefile_get_case_size (cf);
  expected_case = get_random_case (case_size, case_idx);
  if (!casereader_read (reader, &read_case)) 
    fail_test ("Premature end of casefile.");
  if (memcmp (read_case, expected_case, case_size))
    fail_test ("Case %lu fails comparison.", (unsigned long) case_idx);
  free (expected_case);
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
