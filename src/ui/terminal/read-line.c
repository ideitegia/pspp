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
#include <data/settings.h>
#include <language/command.h>
#include <libpspp/message.h>
#include <libpspp/str.h>
#include <libpspp/version.h>
#include <language/prompt.h>

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#if HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>

static char *history_file;

static char **complete_command_name (const char *, int, int);
static char **dont_complete (const char *, int, int);
#endif /* HAVE_READLINE */


struct readln_source 
{
  struct getl_interface parent ;

  bool (*interactive_func) (struct string *line,
			    enum prompt_style) ;
};


static bool initialised = false;

/* Initialize getl. */
void
readln_initialize (void)
{
  initialised = true;

#if HAVE_READLINE
  rl_basic_word_break_characters = "\n";
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
  if (history_file != NULL && false == get_testing_mode() )
    write_history (history_file);
  clear_history ();
  free (history_file);
#endif
}


static bool
read_interactive (struct getl_interface *s, struct string *line)
{
  struct readln_source *is  =
    (struct readln_source *) s ;

  return is->interactive_func (line, prompt_get_style ());
}

static bool
always_true (const struct getl_interface *s UNUSED)
{
  return true;
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
   */
static bool
readln_read (struct string *line, enum prompt_style style)
{
  const char *prompt = prompt_get (style);
#if HAVE_READLINE
  char *string;
#endif
  
  assert (initialised);

  reset_msg_count ();

  welcome ();

#if HAVE_READLINE
  rl_attempted_completion_function = (style == PROMPT_FIRST
                                      ? complete_command_name
                                      : dont_complete);
  string = readline (prompt);
  if (string == NULL)
    return false;
  else 
    {
      if (string[0])
        add_history (string);
      ds_assign_cstr (line, string);
      free (string);
      return true; 
    }
#else
  fputs (prompt, stdout);
  fflush (stdout);
  if (ds_read_line (line, stdin)) 
    {
      ds_chomp (line, '\n');
      return true;
    }
  else
    return false;
#endif
}


static void
readln_close (struct getl_interface *i)
{
  free (i);
}

/* Creates a source which uses readln to get its line */
struct getl_interface *
create_readln_source (void)
{
  struct readln_source *rlns  = xzalloc (sizeof (*rlns));

  rlns->interactive_func = readln_read;

  rlns->parent.interactive = always_true;
  rlns->parent.read = read_interactive;
  rlns->parent.close = readln_close;

  return (struct getl_interface *) rlns;
}


#if HAVE_READLINE
static char *command_generator (const char *text, int state);

/* Returns a set of command name completions for TEXT.
   This is of the proper form for assigning to
   rl_attempted_completion_function. */
static char **
complete_command_name (const char *text, int start, int end UNUSED)
{
  if (start == 0) 
    {
      /* Complete command name at start of line. */
      return rl_completion_matches (text, command_generator); 
    }
  else 
    {
      /* Otherwise don't do any completion. */
      rl_attempted_completion_over = 1;
      return NULL; 
    }
}

/* Do not do any completion for TEXT. */
static char **
dont_complete (const char *text UNUSED, int start UNUSED, int end UNUSED)
{
  rl_attempted_completion_over = 1;
  return NULL; 
}

/* If STATE is 0, returns the first command name matching TEXT.
   Otherwise, returns the next command name matching TEXT.
   Returns a null pointer when no matches are left. */
static char *
command_generator (const char *text, int state) 
{ 
  static const struct command *cmd;
  const char *name;

  if (state == 0)
    cmd = NULL;
  name = cmd_complete (text, &cmd);
  return name ? xstrdup (name) : NULL;
}
#endif /* HAVE_READLINE */
