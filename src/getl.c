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

/* Global variables. */
struct string getl_buf;
struct getl_script *getl_head;
struct getl_script *getl_tail;
int getl_interactive;
int getl_welcomed;
int getl_mode;
int getl_prompt;

#if HAVE_LIBREADLINE
#include <readline/readline.h>
#endif

#if HAVE_LIBHISTORY
static char *history_file;

#if HAVE_READLINE_HISTORY_H
#include <readline/history.h>
#else /* no readline/history.h */
extern void add_history (char *);
extern void using_history (void);
extern int read_history (char *);
extern void stifle_history (int);
extern int write_history (char *);
#endif /* no readline/history.h */
#endif /* -lhistory */


extern struct cmd_set cmd;

static struct string getl_include_path;

/* Number of levels of DO REPEAT structures we're nested inside.  If
   this is greater than zero then DO REPEAT macro substitutions are
   performed. */
static int DO_REPEAT_level;

static int read_console (void);

/* Initialize getl. */
void
getl_initialize (void)
{
  ds_create (&getl_include_path,
	     fn_getenv_default ("STAT_INCLUDE_PATH", include_path));
  ds_init (&getl_buf, 256);
#if HAVE_LIBREADLINE 
  rl_completion_entry_function = pspp_completion_function;
#endif
}

/* Close getl. */
void
getl_uninitialize (void)
{
  getl_close_all();
#if HAVE_LIBHISTORY && defined (unix)
  if (history_file)
    write_history (history_file);
#endif
  ds_destroy (&getl_buf);
  ds_destroy (&getl_include_path);
}

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
  /* getl_head == NULL can't happen. */
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

/* Display a welcoming message. */
static void
welcome (void)
{
  getl_welcomed = 1;
  fputs ("PSPP is free software and you are welcome to distribute copies of "
	 "it\nunder certain conditions; type \"show copying.\" to see the "
	 "conditions.\nThere is ABSOLUTELY NO WARRANTY for PSPP; type \"show "
	 "warranty.\" for details.\n", stdout);
  puts (stat_version);
}

/* Reads a single line from the user's terminal. */

/* From repeat.c. */
extern void perform_DO_REPEAT_substitutions (void);
  
/* Reads a single line from the line buffer associated with getl_head.
   Returns 1 if a line was successfully read or 0 if no more lines are
   available. */
static int
handle_line_buffer (void)
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

/* Reads a single line into getl_buf from the list of files.  Will not
   read from the eof of one file to the beginning of another unless
   the options field on the new file's getl_script is nonzero.  Return
   zero on eof. */
int
getl_read_line (void)
{
  getl_mode = GETL_MODE_BATCH;
  
  while (getl_head)
    {
      struct getl_script *s = getl_head;

      ds_clear (&getl_buf);
      if (s->separate)
	return 0;

      if (s->first_line)
	{
	  if (!handle_line_buffer ())
	    {
	      getl_close_file ();
	      continue;
	    }
	  perform_DO_REPEAT_substitutions ();
	  if (getl_head->print)
	    tab_output_text (TAB_LEFT | TAT_FIX | TAT_PRINTF, "+%s",
			     ds_c_str (&getl_buf));
	  return 1;
	}
      
      if (s->f == NULL)
	{
	  msg (VM (1), _("%s: Opening as syntax file."), s->fn);
	  s->f = fn_open (s->fn, "r");

	  if (s->f == NULL)
	    {
	      msg (ME, _("Opening `%s': %s."), s->fn, strerror (errno));
	      getl_close_file ();
	      continue;
	    }
	}

      if (!ds_gets (&getl_buf, s->f))
	{
	  if (ferror (s->f))
	    msg (ME, _("Reading `%s': %s."), s->fn, strerror (errno));
	  getl_close_file ();
	  continue;
	}
      if (ds_length (&getl_buf) > 0 && ds_end (&getl_buf)[-1] == '\n')
	ds_truncate (&getl_buf, ds_length (&getl_buf) - 1);

      if (get_echo())
	tab_output_text (TAB_LEFT | TAT_FIX, ds_c_str (&getl_buf));

      getl_head->ln++;

      /* Allows shebang invocation: `#! /usr/local/bin/pspp'. */
      if (ds_c_str (&getl_buf)[0] == '#'
	  && ds_c_str (&getl_buf)[1] == '!')
	continue;

      return 1;
    }

  if (getl_interactive == 0)
    return 0;

  getl_mode = GETL_MODE_INTERACTIVE;
  
  if (getl_welcomed == 0)
    welcome ();

  return read_console ();
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

/* PORTME: Adapt to your local system's idea of the terminal. */
#if HAVE_LIBREADLINE

#if HAVE_READLINE_READLINE_H
#include <readline/readline.h>
#else /* no readline/readline.h */
extern char *readline (char *);
#endif /* no readline/readline.h */

static int
read_console (void)
{
  char *line;
  const char *prompt;

  err_error_count = err_warning_count = 0;
  err_already_flagged = 0;

#if HAVE_LIBHISTORY
  if (!history_file)
    {
#ifdef unix
      history_file = tilde_expand (HISTORY_FILE);
#endif
      using_history ();
      read_history (history_file);
      stifle_history (MAX_HISTORY);
    }
#endif /* -lhistory */

  switch (getl_prompt)
    {
    case GETL_PRPT_STANDARD:
      prompt = get_prompt ();
      break;

    case GETL_PRPT_CONTINUATION:
      prompt = get_cprompt ();
      break;

    case GETL_PRPT_DATA:
      prompt = get_dprompt ();
      break;

    default:
      assert (0);
      abort ();
    }

  line = readline (prompt);
  if (!line)
    return 0;

#if HAVE_LIBHISTORY
  if (*line)
    add_history (line);
#endif

  ds_clear (&getl_buf);
  ds_puts (&getl_buf, line);

  free (line);

  return 1;
}
#else /* no -lreadline */
static int
read_console (void)
{
  err_error_count = err_warning_count = 0;
  err_already_flagged = 0;

  fputs (getl_prompt ? get_cprompt() : get_prompt(), stdout);
  ds_clear (&getl_buf);
  if (ds_gets (&getl_buf, stdin))
    return 1;

  if (ferror (stdin))
    msg (FE, "stdin: fgets(): %s.", strerror (errno));

  return 0;
}
#endif /* no -lreadline */

/* Closes all files. */
void
getl_close_all (void)
{
  while (getl_head)
    getl_close_file ();
}

/* Sets the options flag of the current script to 0, thus allowing it
   to be read in.  Returns nonzero if this action was taken, zero
   otherwise. */
int
getl_perform_delayed_reset (void)
{
  if (getl_head && getl_head->separate)
    {
      getl_head->separate = 0;
      discard_variables ();
      lex_reset_eof ();
      return 1;
    }
  return 0;
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
