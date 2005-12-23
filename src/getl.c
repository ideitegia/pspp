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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include "getl.h"
#include "error.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "filename.h"
#include "lexer.h"
#include "repeat.h"
#include "settings.h"
#include "str.h"
#include "tab.h"
#include "var.h"
#include "version.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static struct string getl_include_path;

/* Number of levels of DO REPEAT structures we're nested inside.  If
   this is greater than zero then DO REPEAT macro substitutions are
   performed. */
static int DO_REPEAT_level;

struct string getl_buf;


/* Initialize getl. */
void
getl_initialize (void)
{
  ds_create (&getl_include_path,
	     fn_getenv_default ("STAT_INCLUDE_PATH", include_path));
  ds_init (&getl_buf, 256);
}



struct getl_script *getl_head;
struct getl_script *getl_tail;


/* Returns a string that represents the directory that the syntax file
   currently being read resides in.  If there is no syntax file then
   returns the OS current working directory.  Return value must be
   free()'d. */
char *
getl_get_current_directory (void)
{
  return getl_head ? fn_dirname (getl_head->fn) : fn_get_cwd ();
}

/* Delete everything from the include path. */
void
getl_clear_include_path (void)
{
  ds_clear (&getl_include_path);
}

/* Add to the include path. */
void
getl_add_include_dir (const char *path)
{
  if (ds_length (&getl_include_path))
    ds_putc (&getl_include_path, PATH_DELIMITER);

  ds_puts (&getl_include_path, path);
}

/* Adds FN to the tail end of the list of script files to execute.
   OPTIONS is the value to stick in the options field of the
   getl_script struct.  If WHERE is zero then the file is added after
   all other files; otherwise it is added before all other files (this
   can be done only if parsing has not yet begun). */
void
getl_add_file (const char *fn, int separate, int where)
{
  struct getl_script *n = xmalloc (sizeof *n);

  assert (fn != NULL);
  n->next = NULL;
  if (getl_tail == NULL)
    getl_head = getl_tail = n;
  else if (!where)
    getl_tail = getl_tail->next = n;
  else
    {
      assert (getl_head->f == NULL);
      n->next = getl_head;
      getl_head = n;
    }
  n->included_from = n->includes = NULL;
  n->fn = xstrdup (fn);
  n->ln = 0;
  n->f = NULL;
  n->separate = separate;
  n->first_line = NULL;
}

/* Inserts the given file with filename FN into the current file after
   the current line. */
void
getl_include (const char *fn)
{
  struct getl_script *n;
  char *real_fn;

  {
    char *cur_dir = getl_get_current_directory ();
    real_fn = fn_search_path (fn, ds_c_str (&getl_include_path), cur_dir);
    free (cur_dir);
  }

  if (!real_fn)
    {
      msg (SE, _("Can't find `%s' in include file search path."), fn);
      return;
    }

  if (!getl_head)
    {
      getl_add_file (real_fn, 0, 0);
      free (real_fn);
    }
  else
    {
      n = xmalloc (sizeof *n);
      n->included_from = getl_head;
      getl_head = getl_head->includes = n;
      n->includes = NULL;
      n->next = NULL;
      n->fn = real_fn;
      n->ln = 0;
      n->f = NULL;
      n->separate = 0;
      n->first_line = NULL;
    }
}

/* Add the virtual file FILE to the list of files to be processed.
   The first_line field in FILE must already have been initialized. */
void 
getl_add_virtual_file (struct getl_script *file)
{
  if (getl_tail == NULL)
    getl_head = getl_tail = file;
  else
    getl_tail = getl_tail->next = file;
  file->included_from = file->includes = NULL;
  file->next = NULL;
  file->fn = file->first_line->line;
  file->ln = -file->first_line->len - 1;
  file->separate = 0;
  file->f = NULL;
  file->cur_line = NULL;
  file->remaining_loops = 1;
  file->loop_index = -1;
  file->macros = NULL;
}

/* Causes the DO REPEAT virtual file passed in FILE to be included in
   the current file.  The first_line, cur_line, remaining_loops,
   loop_index, and macros fields in FILE must already have been
   initialized. */
void
getl_add_DO_REPEAT_file (struct getl_script *file)
{
  assert (getl_head);

  DO_REPEAT_level++;
  file->included_from = getl_head;
  getl_head = getl_head->includes = file;
  file->includes = NULL;
  file->next = NULL;
  assert (file->first_line->len < 0);
  file->fn = file->first_line->line;
  file->ln = -file->first_line->len - 1;
  file->separate = 0;
  file->f = NULL;
}

/* Reads a single line from the line buffer associated with getl_head.
   Returns 1 if a line was successfully read or 0 if no more lines are
   available. */
int
getl_handle_line_buffer (void)
{
  struct getl_script *s = getl_head;

  /* Check that we're not all done. */
  do
    {
      if (s->cur_line == NULL)
	{
	  s->loop_index++;
	  if (s->remaining_loops-- == 0)
	    return 0;
	  s->cur_line = s->first_line;
	}

      if (s->cur_line->len < 0)
	{
	  s->ln = -s->cur_line->len - 1;
	  s->fn = s->cur_line->line;
	  s->cur_line = s->cur_line->next;
	  continue;
	}
    }
  while (s->cur_line == NULL);

  ds_concat (&getl_buf, s->cur_line->line, s->cur_line->len);

  /* Advance pointers. */
  s->cur_line = s->cur_line->next;
  s->ln++;

  return 1;
}

/* Closes the current file, whether it be a main file or included
   file, then moves getl_head to the next file in the chain. */
void
getl_close_file (void)
{
  struct getl_script *s = getl_head;

  if (!s)
    return;
  assert (getl_tail != NULL);

  if (s->first_line)
    {
      struct getl_line_list *cur, *next;

      s->fn = NULL; /* It will be freed below. */
      for (cur = s->first_line; cur; cur = next)
	{
	  next = cur->next;
	  free (cur->line);
	  free (cur);
	}

      DO_REPEAT_level--;
    }
  
  if (s->f && EOF == fn_close (s->fn, s->f))
    msg (MW, _("Closing `%s': %s."), s->fn, strerror (errno));
  free (s->fn);

  if (s->included_from)
    {
      getl_head = s->included_from;
      getl_head->includes = NULL;
    }
  else
    {
      getl_head = s->next;
      if (NULL == getl_head)
	getl_tail = NULL;
    }
  
  free (s);
}

/* Closes all files. */
void
getl_close_all (void)
{
  while (getl_head)
    getl_close_file ();
}

bool
getl_is_separate(void)
{
  return (getl_head && getl_head->separate);
}

void
getl_set_separate(bool sep)
{
  assert (getl_head);

  getl_head->separate = sep ;
}


/* Puts the current file and line number in *FN and *LN, respectively,
   or NULL and -1 if none. */
void
getl_location (const char **fn, int *ln)
{
  if (fn != NULL)
    *fn = getl_head ? getl_head->fn : NULL;
  if (ln != NULL)
    *ln = getl_head ? getl_head->ln : -1;
}

bool 
getl_reading_script (void)
{
  return (getl_head != NULL);
}

/* File locator stack. */
static const struct file_locator **file_loc;
static int nfile_loc, mfile_loc;

/* Close getl. */
void
getl_uninitialize (void)
{
  getl_close_all();
  ds_destroy (&getl_buf);
  ds_destroy (&getl_include_path);
  free(file_loc);
  file_loc = NULL;
  nfile_loc = mfile_loc = 0;
}


/* File locator stack functions. */

/* Pushes F onto the stack of file locations. */
void
err_push_file_locator (const struct file_locator *f)
{
  if (nfile_loc >= mfile_loc)
    {
      if (mfile_loc == 0)
	mfile_loc = 8;
      else
	mfile_loc *= 2;

      file_loc = xnrealloc (file_loc, mfile_loc, sizeof *file_loc);
    }

  file_loc[nfile_loc++] = f;
}

/* Pops F off the stack of file locations.
   Argument F is only used for verification that that is actually the
   item on top of the stack. */
void
err_pop_file_locator (const struct file_locator *f)
{
  assert (nfile_loc >= 0 && file_loc[nfile_loc - 1] == f);
  nfile_loc--;
}

/* Puts the current file and line number in F, or NULL and -1 if
   none. */
void
err_location (struct file_locator *f)
{
  if (nfile_loc)
    *f = *file_loc[nfile_loc - 1];
  else
    getl_location (&f->filename, &f->line_number);
}


