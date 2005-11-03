/* PSPP - computes sample statistics.
   Copyright (C) 1997-2004 Free Software Foundation, Inc.
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
#include "dfm-read.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "file-handle.h"
#include "file-handle-def.h"
#include "filename.h"
#include "getl.h"
#include "lexer.h"
#include "str.h"
#include "vfm.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

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

/* Data file reader. */
struct dfm_reader
  {
    struct file_handle *fh;     /* File handle. */
    struct file_ext file;	/* Associated file. */
    struct file_locator where;  /* Current location in data file. */
    struct string line;         /* Current line. */
    size_t pos;                 /* Offset in line of current character. */
    struct string scratch;      /* Extra line buffer. */
    enum dfm_reader_flags flags; /* Zero or more of DFM_*. */
  };

static int inline_open_cnt;
static struct dfm_reader *inline_file;

static void read_record (struct dfm_reader *r);

/* Closes reader R opened by dfm_open_reader(). */
void
dfm_close_reader (struct dfm_reader *r)
{
  int still_open;

  if (r == NULL)
    return;

  if (r->fh != NULL) 
    still_open = fh_close (r->fh, "data file", "rs");
  else
    {
      assert (inline_open_cnt > 0);
      still_open = --inline_open_cnt;

      if (!still_open) 
        {
          /* Skip any remaining data on the inline file. */
          if (r->flags & DFM_SAW_BEGIN_DATA)
            while ((r->flags & DFM_EOF) == 0)
              read_record (r);
          inline_file = NULL;
        }
    }
  if (still_open)
    return;

  if (r->fh != NULL && r->file.file)
    {
      fn_close_ext (&r->file);
      free (r->file.filename);
      r->file.filename = NULL;
    }
  ds_destroy (&r->line);
  ds_destroy (&r->scratch);
  free (r);
}

/* Opens the file designated by file handle FH for reading as a
   data file.  Providing a null pointer for FH designates the
   "inline file", that is, data included inline in the command
   file between BEGIN FILE and END FILE.  Returns nonzero only if
   successful. */
struct dfm_reader *
dfm_open_reader (struct file_handle *fh)
{
  struct dfm_reader *r;
  void **rp;

  if (fh != NULL) 
    {
      rp = fh_open (fh, "data file", "rs");
      if (rp == NULL)
        return NULL;
      if (*rp != NULL)
        return *rp; 
    }
  else 
    {
      assert (inline_open_cnt >= 0);
      if (inline_open_cnt++ > 0)
        return inline_file;
      rp = NULL;
    }
  
  r = xmalloc (sizeof *r);
  r->fh = fh;
  if (fh != NULL) 
    {
      r->where.filename = handle_get_filename (fh);
      r->where.line_number = 0; 
    }
  r->file.file = NULL;
  ds_init (&r->line, 64);
  ds_init (&r->scratch, 0);
  r->flags = DFM_ADVANCE;

  if (fh != NULL)
    {
      r->file.filename = xstrdup (handle_get_filename (r->fh));
      r->file.mode = "rb";
      r->file.file = NULL;
      r->file.sequence_no = NULL;
      r->file.param = NULL;
      r->file.postopen = NULL;
      r->file.preclose = NULL;
      if (!fn_open_ext (&r->file))
	{
	  msg (ME, _("Could not open \"%s\" for reading "
                     "as a data file: %s."),
               handle_get_filename (r->fh), strerror (errno));
          err_cond_fail ();
          fh_close (fh,"data file", "rs");
          free (r);
          return NULL;
	}
      *rp = r;
    }
  else
    inline_file = r;

  return r;
}

static int
read_inline_record (struct dfm_reader *r)
{
  if ((r->flags & DFM_SAW_BEGIN_DATA) == 0)
    {
      char *s;

      r->flags |= DFM_SAW_BEGIN_DATA;

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
          return 0;
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

  if (r->fh != NULL)
    r->where.line_number++;

  if (ds_length (&getl_buf) >= 8
      && !strncasecmp (ds_c_str (&getl_buf), "end data", 8))
    {
      lex_set_prog (ds_c_str (&getl_buf) + ds_length (&getl_buf));
      return 0;
    }

  ds_replace (&r->line, ds_c_str (&getl_buf));
  return 1;
}

static int
read_file_record (struct dfm_reader *r)
{
  assert (r->fh != NULL);
  if (handle_get_mode (r->fh) == MODE_TEXT)
    {
      ds_clear (&r->line);
      if (!ds_gets (&r->line, r->file.file)) 
        {
          if (ferror (r->file.file))
            {
              msg (ME, _("Error reading file %s: %s."),
                   handle_get_name (r->fh), strerror (errno));
              err_cond_fail ();
            }
          return 0;
        }
    }
  else if (handle_get_mode (r->fh) == MODE_BINARY)
    {
      size_t record_width = handle_get_record_width (r->fh);
      size_t amt;

      if (ds_length (&r->line) < record_width) 
        ds_rpad (&r->line, record_width, 0);
          
      amt = fread (ds_c_str (&r->line), 1, record_width,
                   r->file.file);
      if (record_width != amt)
        {
          if (ferror (r->file.file))
            msg (ME, _("Error reading file %s: %s."),
                 handle_get_name (r->fh), strerror (errno));
          else if (amt != 0)
            msg (ME, _("%s: Partial record at end of file."),
                 handle_get_name (r->fh));
          else
            return 0;

          err_cond_fail ();
          return 0;
        }
    }
  else
    assert (0);

  r->where.line_number++;

  return 1;
}

/* Reads a record from R, setting the current position to the
   start of the line.  If an error occurs or end-of-file is
   encountered, the current line is set to null. */
static void
read_record (struct dfm_reader *r)
{
  int success = r->fh != NULL ? read_file_record (r) : read_inline_record (r);
  if (success)
    r->pos = 0;
  else
    r->flags |= DFM_EOF;
}

/* Returns nonzero if end of file has been reached on HANDLE.
   Reads forward in HANDLE's file, if necessary to tell. */
int
dfm_eof (struct dfm_reader *r) 
{
  if (r->flags & DFM_ADVANCE)
    {
      r->flags &= ~DFM_ADVANCE;
      if ((r->flags & DFM_EOF) == 0)
        read_record (r);
      else
        {
          if (r->fh != NULL)
            msg (SE, _("Attempt to read beyond end-of-file on file %s."),
                 handle_get_name (r->fh));
          else
            msg (SE, _("Attempt to read beyond END DATA."));
          err_cond_fail ();
        }
    }

  return (r->flags & DFM_EOF) != 0;
}

/* Returns the current record in the file corresponding to
   HANDLE.  Aborts if reading from the file is necessary or at
   end of file, so call dfm_eof() first.  Sets *LINE to the line,
   which is not null-terminated.  The caller must not free or
   modify the returned string.  */
void
dfm_get_record (struct dfm_reader *r, struct fixed_string *line)
{
  assert ((r->flags & DFM_ADVANCE) == 0);
  assert ((r->flags & DFM_EOF) == 0);
  assert (r->pos <= ds_length (&r->line));

  line->string = ds_data (&r->line) + r->pos;
  line->length = ds_length (&r->line) - r->pos;
}

/* Expands tabs in the current line into the equivalent number of
   spaces, if appropriate for this kind of file.  Aborts if
   reading from the file is necessary or at end of file, so call
   dfm_eof() first.*/
void
dfm_expand_tabs (struct dfm_reader *r) 
{
  struct string temp;
  size_t ofs, new_pos, tab_width;

  assert ((r->flags & DFM_ADVANCE) == 0);
  assert ((r->flags & DFM_EOF) == 0);
  assert (r->pos <= ds_length (&r->line));

  if (r->flags & DFM_TABS_EXPANDED)
    return;
  r->flags |= DFM_TABS_EXPANDED;

  if (r->fh != NULL
      && (handle_get_mode (r->fh) == MODE_BINARY
          || handle_get_tab_width (r->fh) == 0
          || memchr (ds_c_str (&r->line), '\t', ds_length (&r->line)) == NULL))
    return;

  /* Expand tabs from r->line into r->scratch, and figure out
     new value for r->pos. */
  tab_width = r->fh != NULL ? handle_get_tab_width (r->fh) : 8;
  ds_clear (&r->scratch);
  new_pos = 0;
  for (ofs = 0; ofs < ds_length (&r->line); ofs++)
    {
      unsigned char c;
      
      if (ofs == r->pos)
        new_pos = ds_length (&r->scratch);

      c = ds_c_str (&r->line)[ofs];
      if (c != '\t')
        ds_putc (&r->scratch, c);
      else 
        {
          do
            ds_putc (&r->scratch, ' ');
          while (ds_length (&r->scratch) % tab_width != 0);
        }
    }

  /* Swap r->line and r->scratch and set new r->pos. */
  temp = r->line;
  r->line = r->scratch;
  r->scratch = temp;
  r->pos = new_pos;
}

/* Causes dfm_get_record() to read in the next record the next time it
   is executed on file HANDLE. */
void
dfm_forward_record (struct dfm_reader *r)
{
  r->flags |= DFM_ADVANCE;
}

/* Cancels the effect of any previous dfm_fwd_record() executed
   on file HANDLE.  Sets the current line to begin in the 1-based
   column COLUMN.  */
void
dfm_reread_record (struct dfm_reader *r, size_t column)
{
  r->flags &= ~DFM_ADVANCE;
  if (column < 1)
    r->pos = 0;
  else if (column > ds_length (&r->line))
    r->pos = ds_length (&r->line);
  else
    r->pos = column - 1;
}

/* Sets the current line to begin COLUMNS characters following
   the current start. */
void
dfm_forward_columns (struct dfm_reader *r, size_t columns)
{
  dfm_reread_record (r, (r->pos + 1) + columns);
}

/* Returns the 1-based column to which the line pointer in HANDLE
   is set.  Unless dfm_reread_record() or dfm_forward_columns()
   have been called, this is 1. */
size_t
dfm_column_start (struct dfm_reader *r)
{
  return r->pos + 1;
}

/* Pushes the filename and line number on the fn/ln stack. */
void
dfm_push (struct dfm_reader *r)
{
  if (r->fh != NULL)
    err_push_file_locator (&r->where);
}

/* Pops the filename and line number from the fn/ln stack. */
void
dfm_pop (struct dfm_reader *r)
{
  if (r->fh != NULL)
    err_pop_file_locator (&r->where);
}

/* BEGIN DATA...END DATA procedure. */

/* Perform BEGIN DATA...END DATA as a procedure in itself. */
int
cmd_begin_data (void)
{
  struct dfm_reader *r;

  /* FIXME: figure out the *exact* conditions, not these really
     lenient conditions. */
  if (vfm_source == NULL
      || case_source_is_class (vfm_source, &storage_source_class))
    {
      msg (SE, _("This command is not valid here since the current "
                 "input program does not access the inline file."));
      err_cond_fail ();
      return CMD_FAILURE;
    }

  /* Open inline file. */
  r = dfm_open_reader (NULL);
  r->flags |= DFM_SAW_BEGIN_DATA;

  /* Input procedure reads from inline file. */
  getl_prompt = GETL_PRPT_DATA;
  procedure (NULL, NULL);

  dfm_close_reader (r);

  return CMD_SUCCESS;
}
