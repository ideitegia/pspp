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

#include <config.h>
#include "error.h"
#include "dfm.h"
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

#include "debug-print.h"

/* Flags for DFM readers. */
enum dfm_reader_flags
  {
    DFM_EOF = 001,              /* At end-of-file? */
    DFM_ADVANCE = 002,          /* Read next line on dfm_get_record() call? */
    DFM_SAW_BEGIN_DATA = 004,   /* For inline_file only, whether we've 
                                   already read a BEGIN DATA line. */
    DFM_TABS_EXPANDED = 010,    /* Tabs have been expanded. */
  };

/* file_handle extension structure. */
struct dfm_reader_ext
  {
    struct file_ext file;	/* Associated file. */

    struct file_locator where;  /* Current location in data file. */
    struct string line;         /* Current line. */
    size_t pos;                 /* Offset in line of current character. */
    struct string scratch;      /* Extra line buffer. */
    enum dfm_reader_flags flags; /* Zero or more of DFM_*. */
  };

static struct fh_ext_class dfm_r_class;

static void read_record (struct file_handle *h);

/* Asserts that H represents a DFM reader and returns H->ext
   converted to a struct dfm_reader_ext *. */
static inline struct dfm_reader_ext *
get_reader (struct file_handle *h) 
{
  assert (h != NULL);
  assert (h->class == &dfm_r_class);
  assert (h->ext != NULL);

  return h->ext;
}

/* Closes file handle H opened by dfm_open_for_reading(). */
static void
close_reader (struct file_handle *h)
{
  struct dfm_reader_ext *ext = get_reader (h);

  /* Skip any remaining data on the inline file. */
  if (h == inline_file)
    while ((ext->flags & DFM_EOF) == 0)
      read_record (h);
      
  msg (VM (2), _("%s: Closing data-file handle %s."),
       handle_get_filename (h), handle_get_name (h));
  assert (h->class == &dfm_r_class);
  if (ext->file.file)
    {
      fn_close_ext (&ext->file);
      free (ext->file.filename);
      ext->file.filename = NULL;
    }
  ds_destroy (&ext->line);
  ds_destroy (&ext->scratch);
  free (ext);
}

/* Opens a file handle for reading as a data file.  Returns
   nonzero only if successful. */
int
dfm_open_for_reading (struct file_handle *h)
{
  struct dfm_reader_ext *ext;

  if (h->class != NULL)
    {
      if (h->class == &dfm_r_class)
        return 1;
      else
        {
          msg (ME, _("Cannot read from file %s already opened for %s."),
               handle_get_name (h), gettext (h->class->name));
          return 0;
        }
    }

  ext = xmalloc (sizeof *ext);
  ext->where.filename = handle_get_filename (h);
  ext->where.line_number = 0;
  ext->file.file = NULL;
  ds_init (&ext->line, 64);
  ds_init (&ext->scratch, 0);
  ext->flags = DFM_ADVANCE;

  msg (VM (1), _("%s: Opening data-file handle %s for reading."),
       handle_get_filename (h), handle_get_name (h));
  
  assert (h != NULL);
  if (h != inline_file)
    {
      ext->file.filename = xstrdup (handle_get_filename (h));
      ext->file.mode = "rb";
      ext->file.file = NULL;
      ext->file.sequence_no = NULL;
      ext->file.param = NULL;
      ext->file.postopen = NULL;
      ext->file.preclose = NULL;
      if (!fn_open_ext (&ext->file))
	{
	  msg (ME, _("Could not open \"%s\" for reading "
                     "as a data file: %s."),
               handle_get_filename (h), strerror (errno));
          goto error;
	}
    }

  h->class = &dfm_r_class;
  h->ext = ext;
  return 1;

 error:
  err_cond_fail ();
  free (ext);
  return 0;
}

/* Reads a record from H->EXT->FILE into H->EXT->LINE, setting
   H->EXT->PTR to H->EXT->LINE, and setting H->EXT-LEN to the length
   of the line.  The line is not null-terminated.  If an error occurs
   or end-of-file is encountered, H->EXT->LINE is set to NULL. */
static void
read_record (struct file_handle *h)
{
  struct dfm_reader_ext *ext = get_reader (h);

  if (h == inline_file)
    {
      if ((ext->flags & DFM_SAW_BEGIN_DATA) == 0)
        {
          char *s;

          ext->flags |= DFM_SAW_BEGIN_DATA;

          /* FIXME: WTF can't this just be done with tokens?
             Is this really a special case? */
          do
            {
              char *cp;

              if (!getl_read_line ())
                {
                  msg (SE, _("BEGIN DATA expected."));
                  err_failure ();
                }

              /* Skip leading whitespace, separate out first
                 word, so that S points to a single word reduced
                 to lowercase. */
              s = ds_c_str (&getl_buf);
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
              lex_preprocess_line ();
              goto eof;
            }
          getl_prompt = GETL_PRPT_DATA;
        }
      
      if (!getl_read_line ())
	{
	  msg (SE, _("Unexpected end-of-file while reading data in BEGIN "
                     "DATA.  This probably indicates "
                     "a missing or misformatted END DATA command.  "
                     "END DATA must appear by itself on a single line "
                     "with exactly one space between words."));
	  err_failure ();
	}

      ext->where.line_number++;

      if (ds_length (&getl_buf) >= 8
	  && !strncasecmp (ds_c_str (&getl_buf), "end data", 8))
	{
	  lex_set_prog (ds_c_str (&getl_buf) + ds_length (&getl_buf));
	  goto eof;
	}

      ds_replace (&ext->line, ds_c_str (&getl_buf));
    }
  else
    {
      if (handle_get_mode (h) == MODE_TEXT)
	{
          ds_clear (&ext->line);
          if (!ds_gets (&ext->line, ext->file.file)) 
            {
	      if (ferror (ext->file.file))
		{
		  msg (ME, _("Error reading file %s: %s."),
		       handle_get_name (h), strerror (errno));
		  err_cond_fail ();
		}
	      goto eof;
	    }
	}
      else if (handle_get_mode (h) == MODE_BINARY)
	{
          size_t record_width = handle_get_record_width (h);
	  size_t amt;

          if (ds_length (&ext->line) < record_width) 
            ds_rpad (&ext->line, record_width, 0);
          
	  amt = fread (ds_c_str (&ext->line), 1, record_width,
                       ext->file.file);
	  if (record_width != amt)
	    {
	      if (ferror (ext->file.file))
		msg (ME, _("Error reading file %s: %s."),
		     handle_get_name (h), strerror (errno));
	      else if (amt != 0)
		msg (ME, _("%s: Partial record at end of file."),
		     handle_get_name (h));
	      else
		goto eof;

	      err_cond_fail ();
	      goto eof;
	    }
	}
      else
	assert (0);

      ext->where.line_number++;
    }

  ext->pos = 0;
  return;

eof:
  /* Hit eof or an error, clean up everything. */
  ext->flags |= DFM_EOF;
}

/* Returns nonzero if end of file has been reached on HANDLE.
   Reads forward in HANDLE's file, if necessary to tell. */
int
dfm_eof (struct file_handle *h) 
{
  struct dfm_reader_ext *ext = get_reader (h);
  if (ext->flags & DFM_ADVANCE)
    {
      ext->flags &= ~DFM_ADVANCE;
      if ((ext->flags & DFM_EOF) == 0)
        read_record (h);
      else
        {
          msg (SE, _("Attempt to read beyond end-of-file on file %s."),
               handle_get_name (h));
          err_cond_fail ();
        }
    }

  return (ext->flags & DFM_EOF) != 0;
}

/* Returns the current record in the file corresponding to
   HANDLE.  Aborts if reading from the file is necessary or at
   end of file, so call dfm_eof() first.  Sets *LINE to the line,
   which is not null-terminated.  The caller must not free or
   modify the returned string.  */
void
dfm_get_record (struct file_handle *h, struct len_string *line)
{
  struct dfm_reader_ext *ext = get_reader (h);
  assert ((ext->flags & DFM_ADVANCE) == 0);
  assert ((ext->flags & DFM_EOF) == 0);
  assert (ext->pos <= ds_length (&ext->line));

  line->string = ds_data (&ext->line) + ext->pos;
  line->length = ds_length (&ext->line) - ext->pos;
}

/* Expands tabs in the current line into the equivalent number of
   spaces, if appropriate for this kind of file.  Aborts if
   reading from the file is necessary or at end of file, so call
   dfm_eof() first.*/
void
dfm_expand_tabs (struct file_handle *h) 
{
  struct dfm_reader_ext *ext = get_reader (h);
  struct string temp;
  size_t ofs, new_pos, tab_width;

  assert ((ext->flags & DFM_ADVANCE) == 0);
  assert ((ext->flags & DFM_EOF) == 0);
  assert (ext->pos <= ds_length (&ext->line));

  if (ext->flags & DFM_TABS_EXPANDED)
    return;
  ext->flags |= DFM_TABS_EXPANDED;

  if (handle_get_mode (h) == MODE_BINARY
      || handle_get_tab_width (h) == 0
      || memchr (ds_c_str (&ext->line), '\t', ds_length (&ext->line)) == NULL)
    return;

  /* Expand tabs from ext->line into ext->scratch, and figure out
     new value for ext->pos. */
  tab_width = handle_get_tab_width (h);
  ds_clear (&ext->scratch);
  new_pos = 0;
  for (ofs = 0; ofs < ds_length (&ext->line); ofs++)
    {
      unsigned char c;
      
      if (ofs == ext->pos)
        new_pos = ds_length (&ext->scratch);

      c = ds_c_str (&ext->line)[ofs];
      if (c != '\t')
        ds_putc (&ext->scratch, c);
      else 
        {
          do
            ds_putc (&ext->scratch, ' ');
          while (ds_length (&ext->scratch) % tab_width != 0);
        }
    }

  /* Swap ext->line and ext->scratch and set new ext->pos. */
  temp = ext->line;
  ext->line = ext->scratch;
  ext->scratch = temp;
  ext->pos = new_pos;
}

/* Causes dfm_get_record() to read in the next record the next time it
   is executed on file HANDLE. */
void
dfm_forward_record (struct file_handle *h)
{
  struct dfm_reader_ext *ext = get_reader (h);
  ext->flags |= DFM_ADVANCE;
}

/* Cancels the effect of any previous dfm_fwd_record() executed
   on file HANDLE.  Sets the current line to begin in the 1-based
   column COLUMN.  */
void
dfm_reread_record (struct file_handle *h, size_t column)
{
  struct dfm_reader_ext *ext = get_reader (h);
  ext->flags &= ~DFM_ADVANCE;
  if (column < 1)
    ext->pos = 0;
  else if (column > ds_length (&ext->line))
    ext->pos = ds_length (&ext->line);
  else
    ext->pos = column - 1;
}

/* Sets the current line to begin COLUMNS characters following
   the current start. */
void
dfm_forward_columns (struct file_handle *h, size_t columns)
{
  struct dfm_reader_ext *ext = get_reader (h);
  dfm_reread_record (h, (ext->pos + 1) + columns);
}

/* Returns the 1-based column to which the line pointer in HANDLE
   is set.  Unless dfm_reread_record() or dfm_forward_columns()
   have been called, this is 1. */
size_t
dfm_column_start (struct file_handle *h)
{
  struct dfm_reader_ext *ext = get_reader (h);
  return ext->pos + 1;
}

/* Pushes the filename and line number on the fn/ln stack. */
void
dfm_push (struct file_handle *h)
{
  struct dfm_reader_ext *ext = get_reader (h);
  if (h != inline_file)
    err_push_file_locator (&ext->where);
}

/* Pops the filename and line number from the fn/ln stack. */
void
dfm_pop (struct file_handle *h)
{
  struct dfm_reader_ext *ext = get_reader (h);
  if (h != inline_file)
    err_pop_file_locator (&ext->where);
}

/* DFM reader class. */
static struct fh_ext_class dfm_r_class =
{
  1,
  N_("reading as a data file"),
  close_reader,
};

/* file_handle extension structure. */
struct dfm_writer_ext
  {
    struct file_ext file;	/* Associated file. */
    struct file_locator where;  /* Current location in data file. */
    char *bounce;               /* Bounce buffer for fixed-size fields. */
  };

static struct fh_ext_class dfm_w_class;

/* Opens a file handle for writing as a data file. */
int
dfm_open_for_writing (struct file_handle *h)
{
  struct dfm_writer_ext *ext;
  
  if (h->class != NULL)
    {
      if (h->class == &dfm_w_class)
        return 1;
      else
        {
          msg (ME, _("Cannot write to file %s already opened for %s."),
               handle_get_name (h), gettext (h->class->name));
          err_cond_fail ();
          return 0;
        }
    }

  ext = xmalloc (sizeof *ext);
  ext->where.filename = handle_get_filename (h);
  ext->where.line_number = 0;
  ext->file.file = NULL;
  ext->bounce = NULL;

  msg (VM (1), _("%s: Opening data-file handle %s for writing."),
       handle_get_filename (h), handle_get_name (h));
  
  assert (h != NULL);
  if (h == inline_file)
    {
      msg (ME, _("Cannot open the inline file for writing."));
      goto error;
    }

  ext->file.filename = xstrdup (handle_get_filename (h));
  ext->file.mode = "wb";
  ext->file.file = NULL;
  ext->file.sequence_no = NULL;
  ext->file.param = NULL;
  ext->file.postopen = NULL;
  ext->file.preclose = NULL;
      
  if (!fn_open_ext (&ext->file))
    {
      msg (ME, _("An error occurred while opening \"%s\" for writing "
                 "as a data file: %s."),
           handle_get_filename (h), strerror (errno));
      goto error;
    }

  h->class = &dfm_w_class;
  h->ext = ext;
  return 1;

 error:
  free (ext);
  err_cond_fail ();
  return 0;
}

/* Writes record REC having length LEN to the file corresponding to
   HANDLE.  REC is not null-terminated.  Returns nonzero on success,
   zero on failure. */
int
dfm_put_record (struct file_handle *h, const char *rec, size_t len)
{
  struct dfm_writer_ext *ext;

  assert (h != NULL);
  assert (h->class == &dfm_w_class);
  assert (h->ext != NULL);

  ext = h->ext;
  if (handle_get_mode (h) == MODE_BINARY && len < handle_get_record_width (h))
    {
      size_t rec_width = handle_get_record_width (h);
      if (ext->bounce == NULL)
        ext->bounce = xmalloc (rec_width);
      memcpy (ext->bounce, rec, len);
      memset (&ext->bounce[len], 0, rec_width - len);
      rec = ext->bounce;
      len = rec_width;
    }

  if (fwrite (rec, len, 1, ext->file.file) != 1)
    {
      msg (ME, _("Error writing file %s: %s."),
           handle_get_name (h), strerror (errno));
      err_cond_fail ();
      return 0;
    }

  return 1;
}

/* Closes file handle H opened by dfm_open_for_writing(). */
static void
close_writer (struct file_handle *h)
{
  struct dfm_writer_ext *ext;

  assert (h->class == &dfm_w_class);
  ext = h->ext;

  msg (VM (2), _("%s: Closing data-file handle %s."),
       handle_get_filename (h), handle_get_name (h));
  if (ext->file.file)
    {
      fn_close_ext (&ext->file);
      free (ext->file.filename);
      ext->file.filename = NULL;
    }
  free (ext->bounce);
  free (ext);
}

/* DFM writer class. */
static struct fh_ext_class dfm_w_class =
{
  2,
  N_("writing as a data file"),
  close_writer,
};

/* BEGIN DATA...END DATA procedure. */

/* Perform BEGIN DATA...END DATA as a procedure in itself. */
int
cmd_begin_data (void)
{
  struct dfm_reader_ext *ext;

  /* FIXME: figure out the *exact* conditions, not these really
     lenient conditions. */
  if (vfm_source == NULL
      || case_source_is_class (vfm_source, &storage_source_class)
      || case_source_is_class (vfm_source, &sort_source_class))
    {
      msg (SE, _("This command is not valid here since the current "
                 "input program does not access the inline file."));
      err_cond_fail ();
      return CMD_FAILURE;
    }

  /* Initialize inline_file. */
  msg (VM (1), _("inline file: Opening for reading."));
  dfm_open_for_reading (inline_file);
  ext = inline_file->ext;
  ext->flags |= DFM_SAW_BEGIN_DATA;

  /* We don't actually read from the inline file.  The input procedure
     is what reads from it. */
  getl_prompt = GETL_PRPT_DATA;
  procedure (NULL, NULL);
  
  ext = inline_file->ext;
  if (ext && (ext->flags & DFM_EOF) == 0)
    {
      msg (MW, _("Skipping remaining inline data."));
      while ((ext->flags & DFM_EOF) == 0)
        read_record (inline_file);
    }
  assert (inline_file->ext == NULL);

  return CMD_SUCCESS;
}
