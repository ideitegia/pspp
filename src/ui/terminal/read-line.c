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

#include "read-line.h"

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>

#include "msg-ui.h"

#include <data/file-name.h>
#include <data/file-name.h>
#include <data/settings.h>
#include <language/command.h>
#include <libpspp/message.h>
#include <libpspp/str.h>
#include <libpspp/version.h>
#include <output/table.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)


#if HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>

static char *history_file;
#endif /* HAVE_READLINE */

static bool initialised = false;

/* Initialize getl. */
void
readln_initialize (void)
{
  initialised = true;

#if HAVE_READLINE
  rl_basic_word_break_characters = "\n";
  rl_attempted_completion_function = pspp_attempted_completion_function;
#ifdef unix
  if (history_file == NULL)
    {
      history_file = tilde_expand ("~/.pspp_history");
      using_history ();
      read_history (history_file);
      stifle_history (500);
    }
#endif
#endif
}

/* Close getl. */
void
readln_uninitialize (void)
{
  initialised = false;

#if HAVE_READLINE && unix
  if (history_file != NULL)
    write_history (history_file);
#endif
}

/* Display a welcoming message. */
static void
welcome (void)
{
  static bool welcomed = false;
  if (welcomed)
    return;
  welcomed = true;
  fputs ("PSPP is free software and you are welcome to distribute copies of "
	 "it\nunder certain conditions; type \"show copying.\" to see the "
	 "conditions.\nThere is ABSOLUTELY NO WARRANTY for PSPP; type \"show "
	 "warranty.\" for details.\n", stdout);
  puts (stat_version);

#if HAVE_READLINE && unix
  if (history_file == NULL)
    {
      history_file = tilde_expand ("~/.pspp_history");
      using_history ();
      read_history (history_file);
      stifle_history (500);
    }
#endif
}

/* Gets a line from the user and stores it into LINE.
   Prompts the user with PROMPT.
   Returns true if successful, false at end of file.
   Suitable for passing to getl_append_interactive(). */
bool
readln_read (struct string *line, const char *prompt)
{
#if HAVE_READLINE
  char *string;
#endif
  
  assert(initialised);

  reset_msg_count ();

  welcome ();

#if HAVE_READLINE
  string = readline (prompt);
  if (string == NULL)
    return false;
  else 
    {
      if (string[0])
        add_history (string);
      ds_assign_c_str (line, string);
      free (string);
      return true; 
    }
#else
  fputs (prompt, stdout);
  fflush (stdout);
  if (ds_gets (line, stdin)) 
    {
      ds_chomp (line, '\n');
      return true;
    }
  else
    return false;
#endif
}
