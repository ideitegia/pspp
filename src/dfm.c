/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
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

/* AIX requires this to be the first thing in the file.  */
#include <config.h>
#if __GNUC__
#define alloca __builtin_alloca
#else
#if HAVE_ALLOCA_H
#include <alloca.h>
#else
#ifdef _AIX
#pragma alloca
#else
#ifndef alloca			/* predefined by HP cc +Olibcalls */
char *alloca ();
#endif
#endif
#endif
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "file-handle.h"
#include "filename.h"
#include "getline.h"
#include "lexer.h"
#include "misc.h"
#include "str.h"
#include "vfm.h"

#undef DEBUGGING
/*#define DEBUGGING 1*/
#include "debug-print.h"

/* file_handle extension structure. */
struct dfm_fhuser_ext
  {
    struct file_ext file;	/* Associated file. */

    char *line;			/* Current line, not null-terminated. */
    size_t len;			/* Length of line. */

    char *ptr;			/* Pointer into line that is returned by
				   dfm_get_record(). */
    size_t size;		/* Number of bytes allocated for line. */
    int advance;		/* Nonzero=dfm_get_record() reads a new
				   record; otherwise returns current record. */
  };

/* These are defined at the end of this file. */
static struct fh_ext_class dfm_r_class;
static struct fh_ext_class dfm_w_class;

static void read_record (struct file_handle *h);

/* Internal (low level). */

/* Closes the file handle H which was opened by open_file_r() or
   open_file_w(). */
static void
dfm_close (struct file_handle *h)
{
  struct dfm_fhuser_ext *ext = h->ext;

  /* Skip any remaining data on the inline file. */
  if (h == inline_file)
    while (ext->line != NULL)
      read_record (h);
      
  msg (VM (2), _("%s: Closing data-file handle %s."),
       fh_handle_filename (h), fh_handle_name (h));
  assert (h->class == &dfm_r_class || h->class == &dfm_w_class);
  if (ext->file.file)
    {
      fn_close_ext (&ext->file);
      free (ext->file.filename);
      ext->file.filename = NULL;
    }
  free (ext->line);
  free (ext);
}

/* Initializes EXT properly as an inline data file. */
static void
open_inline_file (struct dfm_fhuser_ext *ext)
{
  /* We want to indicate that the file is open, that we are not at
     eof, and that another line needs to be read in.  */
#if __CHECKER__
  memset (&ext->file, 0, sizeof ext->file);
#endif
  ext->file.file = NULL;
  ext->line = xmalloc (128);
#if !PRODUCTION
  strcpy (ext->line, _("<<Bug in dfm.c>>"));
#endif
  ext->len = strlen (ext->line);
  ext->ptr = ext->line;
  ext->size = 128;
  ext->advance = 1;
}

/* Opens a file handle for reading as a data file. */
static int
open_file_r (struct file_handle *h)
{
  struct dfm_fhuser_ext ext;

  h->where.line_number = 0;
  ext.file.file = NULL;
  ext.line = NULL;
  ext.len = 0;
  ext.ptr = NULL;
  ext.size = 0;
  ext.advance = 0;

  msg (VM (1), _("%s: Opening data-file handle %s for reading."),
       fh_handle_filename (h), fh_handle_name (h));
  
  assert (h != NULL);
  if (h == inline_file)
    {
      char *s;

      /* WTF can't this just be done with tokens?
	 Is this really a special case?
	 FIXME! */
      do
	{
	  char *cp;

	  if (!getl_read_line ())
	    {
	      msg (SE, _("BEGIN DATA expected."));
	      err_failure ();
	    }

	  /* Skip leading whitespace, separate out first word, so that
	     S points to a single word reduced to lowercase. */
	  s = ds_value (&getl_buf);
	  while (isspace ((unsigned char) *s))
	    s++;
	  for (cp = s; isalpha ((unsigned char) *cp); cp++)
	    *cp = tolower ((unsigned char) (*cp));
	  ds_truncate (&getl_buf, cp - s);
	}
      while (*s == '\0');

      if (!lex_id_match_len ("begin", 5, s, strcspn (s, " \t\r\v\n")))
	{
	  msg (SE, _("BEGIN DATA expected."));
	  err_cond_fail ();
	  lex_preprocess_line ();
	  return 0;
	}
      getl_prompt = GETL_PRPT_DATA;

      open_inline_file (&ext);
    }
  else
    {
      ext.file.filename = xstrdup (h->norm_fn);
      ext.file.mode = "rb";
      ext.file.file = NULL;
      ext.file.sequence_no = NULL;
      ext.file.param = NULL;
      ext.file.postopen = NULL;
      ext.file.preclose = NULL;
      if (!fn_open_ext (&ext.file))
	{
	  msg (ME, _("An error occurred while opening \"%s\" for reading "
	       "as a data file: %s."), h->fn, strerror (errno));
	  err_cond_fail ();
	  return 0;
	}
    }

  h->class = &dfm_r_class;
  h->ext = xmalloc (sizeof (struct dfm_fhuser_ext));
  memcpy (h->ext, &ext, sizeof (struct dfm_fhuser_ext));

  return 1;
}

/* Opens a file handle for writing as a data file. */
static int
open_file_w (struct file_handle *h)
{
  struct dfm_fhuser_ext ext;
  
  ext.file.file = NULL;
  ext.line = NULL;
  ext.len = 0;
  ext.ptr = NULL;
  ext.size = 0;
  ext.advance = 0;

  h->where.line_number = 0;

  msg (VM (1), _("%s: Opening data-file handle %s for writing."),
       fh_handle_filename (h), fh_handle_name (h));
  
  assert (h != NULL);
  if (h == inline_file)
    {
      msg (ME, _("Cannot open the inline file for writing."));
      err_cond_fail ();
      return 0;
    }

  ext.file.filename = xstrdup (h->norm_fn);
  ext.file.mode = "wb";
  ext.file.file = NULL;
  ext.file.sequence_no = NULL;
  ext.file.param = NULL;
  ext.file.postopen = NULL;
  ext.file.preclose = NULL;
      
  if (!fn_open_ext (&ext.file))
    {
      msg (ME, _("An error occurred while opening \"%s\" for writing "
	   "as a data file: %s."), h->fn, strerror (errno));
      err_cond_fail ();
      return 0;
    }

  h->class = &dfm_w_class;
  h->ext = xmalloc (sizeof (struct dfm_fhuser_ext));
  memcpy (h->ext, &ext, sizeof (struct dfm_fhuser_ext));

  return 1;
}

/* Ensures that the line buffer in file handle with extension EXT is
   big enough to hold a line of length EXT->LEN characters not
   including null terminator. */
#define force_line_buffer_expansion()				\
	do							\
	  {							\
	    if (ext->len + 1 > ext->size)			\
	      {							\
		ext->size = ext->len * 2;			\
		ext->line = xrealloc (ext->line, ext->size);	\
	      }							\
	  }							\
	while (0)

/* Counts the number of tabs in string STRING of length LEN. */
static inline int
count_tabs (char *s, size_t len)
{
  int n_tabs = 0;
  
  for (;;)
    {
      char *cp = memchr (s, '\t', len);
      if (cp == NULL)
	return n_tabs;
      n_tabs++;
      len -= cp - s + 1;
      s = cp + 1;
    }
}
   
/* Converts all the tabs in H->EXT->LINE to an equivalent number of
   spaces, if necessary. */
static void
tabs_to_spaces (struct file_handle *h)
{
  struct dfm_fhuser_ext *ext = h->ext;
  
  char *first_tab;		/* Location of first tab (if any). */
  char *second_tab;		/* Location of second tab (if any). */
  size_t orig_len;	/* Line length at function entry. */

  /* If there aren't any tabs then there's nothing to do. */
  first_tab = memchr (ext->line, '\t', ext->len);
  if (first_tab == NULL)
    return;
  orig_len = ext->len;
  
  /* If there's just one tab then expand it inline.  Otherwise do a
     full string copy to another buffer. */
  second_tab = memchr (first_tab + 1, '\t',
		       ext->len - (first_tab - ext->line + 1));
  if (second_tab == NULL)
    {
      int n_spaces = 8 - (first_tab - ext->line) % 8;

      ext->len += n_spaces - 1;

      /* Expand the line if necessary, keeping the first_tab pointer
         valid. */
      {
	size_t ofs = first_tab - ext->line;
	force_line_buffer_expansion ();
	first_tab = ext->line + ofs;
      }
      
      memmove (first_tab + n_spaces, first_tab + 1,
	       orig_len - (first_tab - ext->line + 1));
      memset (first_tab, ' ', n_spaces);
    } else {
      /* Make a local copy of original text. */
      char *orig_line = local_alloc (ext->len + 1);
      memcpy (orig_line, ext->line, ext->len);
	      
      /* Allocate memory assuming we need to add 8 spaces for every tab. */
      ext->len += 2 + count_tabs (second_tab + 1,
				  ext->len - (second_tab - ext->line + 1));
      
      /* Expand the line if necessary, keeping the first_tab pointer
         valid. */
      {
	size_t ofs = first_tab - ext->line;
	force_line_buffer_expansion ();
	first_tab = ext->line + ofs;
      }

      /* Walk through orig_line, expanding tabs into ext->line. */
      {
	char *src_p = orig_line + (first_tab - ext->line);
	char *dest_p = first_tab;

	for (; src_p < orig_line + orig_len; src_p++)
	  {
	    /* Most characters simply pass through untouched. */
	    if (*src_p != '\t')
	      {
		*dest_p++ = *src_p;
		continue;
	      }

	    /* Tabs are expanded into an equivalent number of
               spaces. */
	    {
	      int n_spaces = 8 - (dest_p - ext->line) % 8;

	      memset (dest_p, ' ', n_spaces);
	      dest_p += n_spaces;
	    }
	  }

	/* Supply null terminator and actual string length. */
	*dest_p = 0;
	ext->len = dest_p - ext->line;
      }

      local_free (orig_line);
    }
}

/* Reads a record from H->EXT->FILE into H->EXT->LINE, setting
   H->EXT->PTR to H->EXT->LINE, and setting H->EXT-LEN to the length
   of the line.  The line is not null-terminated.  If an error occurs
   or end-of-file is encountered, H->EXT->LINE is set to NULL. */
static void
read_record (struct file_handle *h)
{
  struct dfm_fhuser_ext *ext = h->ext;

  if (h == inline_file)
    {
      if (!getl_read_line ())
	{
	  msg (SE, _("Unexpected end-of-file while reading data in BEGIN "
	       "DATA.  This probably indicates "
	       "a missing or misformatted END DATA command.  "
	       "END DATA must appear by itself on a single line "
	       "with exactly one space between words."));
	  err_failure ();
	}

      h->where.line_number++;

      if (ds_length (&getl_buf) >= 8
	  && !strncasecmp (ds_value (&getl_buf), "end data", 8))
	{
	  lex_set_prog (ds_value (&getl_buf) + ds_length (&getl_buf));
	  goto eof;
	}

      ext->len = ds_length (&getl_buf);
      force_line_buffer_expansion ();
      strcpy (ext->line, ds_value (&getl_buf));
    }
  else
    {
      if (h->recform == FH_RF_VARIABLE)
	{
	  /* PORTME: here you should adapt the routine to your
	     system's concept of a "line" of text. */
	  int read_len = getline (&ext->line, &ext->size, ext->file.file);

	  if (read_len == -1)
	    {
	      if (ferror (ext->file.file))
		{
		  msg (ME, _("Error reading file %s: %s."),
		       fh_handle_name (h), strerror (errno));
		  err_cond_fail ();
		}
	      goto eof;
	    }
	  ext->len = (size_t) read_len;
	}
      else if (h->recform == FH_RF_FIXED)
	{
	  size_t amt;

	  if (ext->size < h->lrecl)
	    {
	      ext->size = h->lrecl;
	      ext->line = xmalloc (ext->size);
	    }
	  amt = fread (ext->line, 1, h->lrecl, ext->file.file);
	  if (h->lrecl != amt)
	    {
	      if (ferror (ext->file.file))
		msg (ME, _("Error reading file %s: %s."),
		     fh_handle_name (h), strerror (errno));
	      else if (amt != 0)
		msg (ME, _("%s: Partial record at end of file."),
		     fh_handle_name (h));
	      else
		goto eof;

	      err_cond_fail ();
	      goto eof;
	    }
	}
      else
	assert (0);

      h->where.line_number++;
    }

  /* Strip trailing whitespace, I forget why.  But there's a good
     reason, I'm sure.  I'm too scared to eliminate this code.  */
  if (h->recform == FH_RF_VARIABLE)
    {
      while (ext->len && isspace ((unsigned char) ext->line[ext->len - 1]))
	ext->len--;

      /* Convert tabs to spaces. */
      tabs_to_spaces (h);
		
      ext->ptr = ext->line;
    }
  return;

eof:
  /*hit eof or an error, clean up everything. */
  if (ext->line)
    free (ext->line);
  ext->size = 0;
  ext->line = ext->ptr = NULL;
  return;
}

/* Public (high level). */

/* Returns the current record in the file corresponding to HANDLE.
   Opens files and reads records, etc., as necessary.  Sets *LEN to
   the length of the line.  The line returned is not null-terminated.
   Returns NULL at end of file.  Calls fail() on attempt to read past
   end of file.  */
char *
dfm_get_record (struct file_handle *h, int *len)
{
  if (h->class == NULL)
    {
      if (!open_file_r (h))
	return NULL;
      read_record (h);
    }
  else if (h->class != &dfm_r_class)
    {
      msg (ME, _("Cannot read from file %s already opened for %s."),
	   fh_handle_name (h), gettext (h->class->name));
      goto lossage;
    }
  else
    {
      struct dfm_fhuser_ext *ext = h->ext;

      if (ext->advance)
	{
	  if (ext->line)
	    read_record (h);
	  else
	    {
	      msg (SE, _("Attempt to read beyond end-of-file on file %s."),
		   fh_handle_name (h));
	      goto lossage;
	    }
	}
    }

  {
    struct dfm_fhuser_ext *ext = h->ext;

    if (ext)
      {
	ext->advance = 0;
	if (len)
	  *len = ext->len - (ext->ptr - ext->line);
	return ext->ptr;
      }
  }

  return NULL;

lossage:
  /* Come here on reading beyond eof or reading from a file already
     open for something else. */
  err_cond_fail ();

  return NULL;
}

/* Causes dfm_get_record() to read in the next record the next time it
   is executed on file HANDLE. */
void
dfm_fwd_record (struct file_handle *h)
{
  struct dfm_fhuser_ext *ext = h->ext;

  assert (h->class == &dfm_r_class);
  ext->advance = 1;
}

/* Cancels the effect of any previous dfm_fwd_record() executed on
   file HANDLE.  Sets the current line to begin in the 1-based column
   COLUMN, as with dfm_set_record but based on a column number instead
   of a character pointer. */
void
dfm_bkwd_record (struct file_handle *h, int column)
{
  struct dfm_fhuser_ext *ext = h->ext;

  assert (h->class == &dfm_r_class);
  ext->advance = 0;
  ext->ptr = ext->line + min ((int) ext->len + 1, column) - 1;
}

/* Sets the current line in HANDLE to NEW_LINE, which must point
   somewhere in the line last returned by dfm_get_record().  Used by
   DATA LIST FREE to strip the leading portion off the current line.  */
void
dfm_set_record (struct file_handle *h, char *new_line)
{
  struct dfm_fhuser_ext *ext = h->ext;

  assert (h->class == &dfm_r_class);
  ext->ptr = new_line;
}

/* Returns the 0-based current column to which the line pointer in
   HANDLE is set.  Unless dfm_set_record() or dfm_bkwd_record() have
   been called, this is 0. */
int
dfm_get_cur_col (struct file_handle *h)
{
  struct dfm_fhuser_ext *ext = h->ext;

  assert (h->class == &dfm_r_class);
  return ext->ptr - ext->line;
}

/* Writes record REC having length LEN to the file corresponding to
   HANDLE.  REC is not null-terminated.  Returns nonzero on success,
   zero on failure. */
int
dfm_put_record (struct file_handle *h, const char *rec, size_t len)
{
  char *ptr;
  size_t amt;

  if (h->class == NULL)
    {
      if (!open_file_w (h))
	return 0;
    }
  else if (h->class != &dfm_w_class)
    {
      msg (ME, _("Cannot write to file %s already opened for %s."),
	   fh_handle_name (h), gettext (h->class->name));
      err_cond_fail ();
      return 0;
    }

  if (h->recform == FH_RF_FIXED && len < h->lrecl)
    {
      int ch;

      amt = h->lrecl;
      ptr = local_alloc (amt);
      memcpy (ptr, rec, len);
      ch = h->mode == FH_MD_CHARACTER ? ' ' : 0;
      memset (&ptr[len], ch, amt - len);
    }
  else
    {
      ptr = (char *) rec;
      amt = len;
    }

  if (1 != fwrite (ptr, amt, 1, ((struct dfm_fhuser_ext *) h->ext)->file.file))
    {
      msg (ME, _("Error writing file %s: %s."), fh_handle_name (h),
	   strerror (errno));
      err_cond_fail ();
      return 0;
    }

  if (ptr != rec)
    local_free (ptr);

  return 1;
}

/* Pushes the filename and line number on the fn/ln stack. */
void
dfm_push (struct file_handle *h)
{
  if (h != inline_file)
    err_push_file_locator (&h->where);
}

/* Pops the filename and line number from the fn/ln stack. */
void
dfm_pop (struct file_handle *h)
{
  if (h != inline_file)
    err_pop_file_locator (&h->where);
}

/* BEGIN DATA...END DATA procedure. */

/* Perform BEGIN DATA...END DATA as a procedure in itself. */
int
cmd_begin_data (void)
{
  struct dfm_fhuser_ext *ext;

  /* FIXME: figure out the *exact* conditions, not these really
     lenient conditions. */
  if (vfm_source == NULL
      || vfm_source == &vfm_memory_stream
      || vfm_source == &vfm_disk_stream
      || vfm_source == &sort_stream)
    {
      msg (SE, _("This command is not valid here since the current "
	   "input program does not access the inline file."));
      err_cond_fail ();
      return CMD_FAILURE;
    }

  /* Initialize inline_file. */
  msg (VM (1), _("inline file: Opening for reading."));
  inline_file->class = &dfm_r_class;
  inline_file->ext = xmalloc (sizeof (struct dfm_fhuser_ext));
  open_inline_file (inline_file->ext);

  /* We don't actually read from the inline file.  The input procedure
     is what reads from it. */
  getl_prompt = GETL_PRPT_DATA;
  procedure (NULL, NULL, NULL);

  ext = inline_file->ext;

  if (ext && ext->line)
    {
      msg (MW, _("Skipping remaining inline data."));
      for (read_record (inline_file); ext->line; read_record (inline_file))
	;
    }
  assert (inline_file->ext == NULL);

  return CMD_SUCCESS;
}

static struct fh_ext_class dfm_r_class =
{
  1,
  N_("reading as a data file"),
  dfm_close,
};

static struct fh_ext_class dfm_w_class =
{
  2,
  N_("writing as a data file"),
  dfm_close,
};
