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

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>

#include "readln.h"
#include "command.h"
#include "version.h"
#include "getl.h"
#include "str.h"
#include "tab.h"
#include "error.h"
#include "filename.h"
#include "settings.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


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


static int read_console (void);

static bool initialised = false;

/* Initialize getl. */
void
readln_initialize (void)
{
  initialised = true;
#if HAVE_LIBREADLINE 
  rl_completion_entry_function = pspp_completion_function;
#endif
}

/* Close getl. */
void
readln_uninitialize (void)
{
  initialised = false;
#if HAVE_LIBHISTORY && defined (unix)
  if (history_file)
    write_history (history_file);
#endif
}

static bool welcomed = false;

/* Display a welcoming message. */
static void
welcome (void)
{
  welcomed = true;
  fputs ("PSPP is free software and you are welcome to distribute copies of "
	 "it\nunder certain conditions; type \"show copying.\" to see the "
	 "conditions.\nThere is ABSOLUTELY NO WARRANTY for PSPP; type \"show "
	 "warranty.\" for details.\n", stdout);
  puts (stat_version);
}

/* From repeat.c. */
extern void perform_DO_REPEAT_substitutions (void);

  /* Global variables. */
int getl_mode;
int getl_interactive;
int getl_prompt;

/* 
extern struct cmd_set cmd;
*/


/* Reads a single line into getl_buf from the list of files.  Will not
   read from the eof of one file to the beginning of another unless
   the options field on the new file's getl_script is nonzero.  Return
   zero on eof. */
int
getl_read_line (void)
{
  assert (initialised);
  getl_mode = GETL_MODE_BATCH;
  
  while (getl_head)
    {
      struct getl_script *s = getl_head;

      ds_clear (&getl_buf);
      if (s->separate)
	return 0;

      if (s->first_line)
	{
	  if (!getl_handle_line_buffer ())
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
  
  if (!welcomed)
    welcome ();

  return read_console ();
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

  assert(initialised);

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
  assert(initialised);

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

