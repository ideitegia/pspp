/* PSPP - computes sample statistics.
   Copyright (C) 1997-2004, 2006 Free Software Foundation, Inc.
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
#include <language/data-io/data-reader.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <libpspp/alloc.h>
#include <language/command.h>
#include <libpspp/message.h>
#include <language/data-io/file-handle.h>
#include <data/file-handle-def.h>
#include <data/file-name.h>
#include <language/line-buffer.h>
#include <language/lexer/lexer.h>
#include <libpspp/str.h>
#include <procedure.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Flags for DFM readers. */
enum dfm_reader_flags
  {
    DFM_ADVANCE = 002,          /* Read next line on dfm_get_record() call? */
    DFM_SAW_BEGIN_DATA = 004,   /* For inline_file only, whether we've 
                                   already read a BEGIN DATA line. */
    DFM_TABS_EXPANDED = 010,    /* Tabs have been expanded. */
  };

/* Data file reader. */
struct dfm_reader
  {
    struct file_handle *fh;     /* File handle. */
    struct file_locator where;  /* Current location in data file. */
    struct string line;         /* Current line. */
    struct string scratch;      /* Extra line buffer. */
    enum dfm_reader_flags flags; /* Zero or more of DFM_*. */
    FILE *file;                 /* Associated file. */
    size_t pos;                 /* Offset in line of current character. */
    unsigned eof_cnt;           /* # of attempts to advance past EOF. */
  };

/* Closes reader R opened by dfm_open_reader(). */
void
dfm_close_reader (struct dfm_reader *r)
{
  int still_open;
  bool is_inline;
  char *file_name;

  if (r == NULL)
    return;

  is_inline = r->fh == fh_inline_file ();
  file_name = is_inline ? NULL : xstrdup (fh_get_filename (r->fh));
  still_open = fh_close (r->fh, "data file", "rs");
  if (still_open) 
    {
      free (file_name);
      return; 
    }

  if (!is_inline)
    fn_close (file_name, r->file);
  else
    {
      /* Skip any remaining data on the inline file. */
      if (r->flags & DFM_SAW_BEGIN_DATA) 
        {
          dfm_reread_record (r, 0);
          while (!dfm_eof (r))
            dfm_forward_record (r); 
        }
    }

  ds_destroy (&r->line);
  ds_destroy (&r->scratch);
  free (r);
  free (file_name);
}

/* Opens the file designated by file handle FH for reading as a
   data file.  Providing fh_inline_file() for FH designates the
   "inline file", that is, data included inline in the command
   file between BEGIN FILE and END FILE.  Returns a reader if
   successful, or a null pointer otherwise. */
struct dfm_reader *
dfm_open_reader (struct file_handle *fh)
{
  struct dfm_reader *r;
  void **rp;

  rp = fh_open (fh, FH_REF_FILE | FH_REF_INLINE, "data file", "rs");
  if (rp == NULL)
    return NULL;
  if (*rp != NULL)
    return *rp; 
  
  r = xmalloc (sizeof *r);
  r->fh = fh;
  ds_init (&r->line, 64);
  ds_init (&r->scratch, 0);
  r->flags = DFM_ADVANCE;
  r->eof_cnt = 0;
  if (fh != fh_inline_file ()) 
    {
      r->where.filename = fh_get_filename (fh);
      r->where.line_number = 0; 
      r->file = fn_open (fh_get_filename (fh), "rb");
      if (r->file == NULL)
        {
          msg (ME, _("Could not open \"%s\" for reading as a data file: %s."),
               fh_get_filename (r->fh), strerror (errno));
          fh_close (fh,"data file", "rs");
          free (r);
          return NULL;
        }
    }
  *rp = r;

  return r;
}

/* Returns true if an I/O error occurred on READER, false otherwise. */
bool
dfm_reader_error (const struct dfm_reader *r) 
{
  return fh_get_referent (r->fh) == FH_REF_FILE && ferror (r->file);
}

/* Reads a record from the inline file into R.
   Returns true if successful, false on failure. */
static bool
read_inline_record (struct dfm_reader *r)
{
  if ((r->flags & DFM_SAW_BEGIN_DATA) == 0)
    {
      r->flags |= DFM_SAW_BEGIN_DATA;

      while (token == '.')
        lex_get ();
      if (!lex_force_match_id ("BEGIN") || !lex_force_match_id ("DATA"))
        return false;
      getl_set_prompt_style (GETL_PROMPT_DATA);
    }
      
  if (!getl_read_line (NULL))
    {
      msg (SE, _("Unexpected end-of-file while reading data in BEGIN "
                 "DATA.  This probably indicates "
                 "a missing or misformatted END DATA command.  "
                 "END DATA must appear by itself on a single line "
                 "with exactly one space between words."));
      return false;
    }

  if (ds_length (&getl_buf) >= 8
      && !strncasecmp (ds_c_str (&getl_buf), "end data", 8))
    {
      lex_set_prog (ds_c_str (&getl_buf) + ds_length (&getl_buf));
      return false;
    }

  ds_assign_string (&r->line, &getl_buf);
  return true;
}

/* Reads a record from a disk file into R.
   Returns true if successful, false on failure. */
static bool
read_file_record (struct dfm_reader *r)
{
  assert (r->fh != fh_inline_file ());
  if (fh_get_mode (r->fh) == FH_MODE_TEXT)
    {
      ds_clear (&r->line);
      if (!ds_gets (&r->line, r->file)) 
        {
          if (ferror (r->file))
            msg (ME, _("Error reading file %s: %s."),
                 fh_get_name (r->fh), strerror (errno));
          return false;
        }
    }
  else if (fh_get_mode (r->fh) == FH_MODE_BINARY)
    {
      size_t record_width = fh_get_record_width (r->fh);
      size_t amt;

      if (ds_length (&r->line) < record_width) 
        ds_rpad (&r->line, record_width, 0);
          
      amt = fread (ds_c_str (&r->line), 1, record_width, r->file);
      if (record_width != amt)
        {
          if (ferror (r->file))
            msg (ME, _("Error reading file %s: %s."),
                 fh_get_name (r->fh), strerror (errno));
          else if (amt != 0)
            msg (ME, _("%s: Partial record at end of file."),
                 fh_get_name (r->fh));

          return false;
        }
    }
  else
    abort ();

  r->where.line_number++;

  return true;
}

/* Reads a record from R, setting the current position to the
   start of the line.  If an error occurs or end-of-file is
   encountered, the current line is set to null. */
static bool
read_record (struct dfm_reader *r)
{
  return (fh_get_referent (r->fh) == FH_REF_FILE
          ? read_file_record (r)
          : read_inline_record (r));
}

/* Returns the number of attempts, thus far, to advance past
   end-of-file in reader R.  Reads forward in HANDLE's file, if
   necessary, to find out.

   Normally, the user stops attempting to read from the file the
   first time EOF is reached (a return value of 1).  If the user
   tries to read past EOF again (a return value of 2 or more),
   an error message is issued, and the caller should more
   forcibly abort to avoid an infinite loop. */
unsigned
dfm_eof (struct dfm_reader *r) 
{
  if (r->flags & DFM_ADVANCE)
    {
      r->flags &= ~DFM_ADVANCE;

      if (r->eof_cnt == 0 && read_record (r)) 
        {
          r->pos = 0;
          return 0; 
        }

      r->eof_cnt++;
      if (r->eof_cnt == 2)
        {
          if (r->fh != fh_inline_file ())
            msg (ME, _("Attempt to read beyond end-of-file on file %s."),
                 fh_get_name (r->fh));
          else
            msg (ME, _("Attempt to read beyond END DATA."));
        }
    }

  return r->eof_cnt;
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
  assert (r->eof_cnt == 0);
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
  assert (r->eof_cnt == 0);
  assert (r->pos <= ds_length (&r->line));

  if (r->flags & DFM_TABS_EXPANDED)
    return;
  r->flags |= DFM_TABS_EXPANDED;

  if (r->fh != fh_inline_file ()
      && (fh_get_mode (r->fh) == FH_MODE_BINARY
          || fh_get_tab_width (r->fh) == 0
          || memchr (ds_c_str (&r->line), '\t', ds_length (&r->line)) == NULL))
    return;

  /* Expand tabs from r->line into r->scratch, and figure out
     new value for r->pos. */
  tab_width = fh_get_tab_width (r->fh);
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
  if (r->fh != fh_inline_file ())
    err_push_file_locator (&r->where);
}

/* Pops the filename and line number from the fn/ln stack. */
void
dfm_pop (struct dfm_reader *r)
{
  if (r->fh != fh_inline_file ())
    err_pop_file_locator (&r->where);
}

/* BEGIN DATA...END DATA procedure. */

/* Perform BEGIN DATA...END DATA as a procedure in itself. */
int
cmd_begin_data (void)
{
  struct dfm_reader *r;
  bool ok;

  if (!fh_is_open (fh_inline_file ()))
    {
      msg (SE, _("This command is not valid here since the current "
                 "input program does not access the inline file."));
      return CMD_CASCADING_FAILURE;
    }

  /* Open inline file. */
  r = dfm_open_reader (fh_inline_file ());
  r->flags |= DFM_SAW_BEGIN_DATA;

  /* Input procedure reads from inline file. */
  getl_set_prompt_style (GETL_PROMPT_DATA);
  ok = procedure (NULL, NULL);

  dfm_close_reader (r);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}
