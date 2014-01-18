/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2011, 2013 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>


#if HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#include <termios.h>

static char *history_file;

static char **complete_command_name (const char *, int, int);
static char **dont_complete (const char *, int, int);
static char *command_generator (const char *text, int state);

static const bool have_readline = true;

#else
static const bool have_readline = false;
static int rl_end;
#endif


#include "ui/terminal/terminal-reader.h"
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "data/file-name.h"
#include "data/settings.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/prompt.h"
#include "libpspp/str.h"
#include "libpspp/version.h"
#include "output/driver.h"
#include "output/journal.h"
#include "ui/terminal/terminal.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct terminal_reader
  {
    struct lex_reader reader;
    struct substring s;
    size_t offset;
    bool eof;
  };

static int n_terminal_readers;

static void readline_init (void);
static void readline_done (void);
static bool readline_read (struct substring *, enum prompt_style);

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
  journal_init ();
}

static struct terminal_reader *
terminal_reader_cast (struct lex_reader *r)
{
  return UP_CAST (r, struct terminal_reader, reader);
}


/* Older libreadline versions do not provide rl_outstream.
   However, it is almost always going to be the same as stdout. */
#if ! HAVE_RL_OUTSTREAM
# define rl_outstream stdout
#endif


#if HAVE_READLINE
/* Similarly, rl_echo_signal_char is fairly recent.
   We provide our own crude version if it is not present. */
#if ! HAVE_RL_ECHO_SIGNAL_CHAR
static void
rl_echo_signal_char (int sig)
{
#if HAVE_TERMIOS_H
  struct termios t;
  if (0 == tcgetattr (0, &t))
    {
      cc_t c = t.c_cc[VINTR];
  
      if (c >= 0  && c <= 'Z' - 'A')
	fprintf (rl_outstream, "^%c", 'A' + c - 1);
      else
	fprintf (rl_outstream, "%c", c);
    }
  else
#endif
    fprintf (rl_outstream, "^C");

  fflush (rl_outstream);
}  
#endif
#endif


static size_t
terminal_reader_read (struct lex_reader *r_, char *buf, size_t n,
                      enum prompt_style prompt_style)
{
  struct terminal_reader *r = terminal_reader_cast (r_);
  size_t chunk;

  if (r->offset >= r->s.length && !r->eof)
    {
      welcome ();
      msg_ui_reset_counts ();
      output_flush ();

      ss_dealloc (&r->s);
      if (! readline_read (&r->s, prompt_style))
	{
          *buf = '\n';
          fprintf (rl_outstream, "\n");
	  return 1;
	}
      r->offset = 0;
      r->eof = ss_is_empty (r->s);

      /* Check whether the size of the window has changed, so that
         the output drivers can adjust their settings as needed.  We
         only do this for the first line of a command, as it's
         possible that the output drivers are actually in use
         afterward, and we don't want to confuse them in the middle
         of output. */
      if (prompt_style == PROMPT_FIRST)
        terminal_check_size ();
    }

  chunk = MIN (n, r->s.length - r->offset);
  memcpy (buf, r->s.string + r->offset, chunk);
  r->offset += chunk;
  return chunk;
}

static void
terminal_reader_close (struct lex_reader *r_)
{
  struct terminal_reader *r = terminal_reader_cast (r_);

  ss_dealloc (&r->s);
  free (r->reader.file_name);
  free (r);

  if (!--n_terminal_readers)
    readline_done ();
}

static struct lex_reader_class terminal_reader_class =
  {
    terminal_reader_read,
    terminal_reader_close
  };

/* Creates a source which uses readln to get its line */
struct lex_reader *
terminal_reader_create (void)
{
  struct terminal_reader *r;

  if (!n_terminal_readers++)
    readline_init ();

  r = xzalloc (sizeof *r);
  r->reader.class = &terminal_reader_class;
  r->reader.syntax = LEX_SYNTAX_INTERACTIVE;
  r->reader.error = LEX_ERROR_TERMINAL;
  r->reader.file_name = NULL;
  r->s = ss_empty ();
  r->offset = 0;
  r->eof = false;
  return &r->reader;
}



static const char *
readline_prompt (enum prompt_style style)
{
  switch (style)
    {
    case PROMPT_FIRST:
      return "PSPP> ";

    case PROMPT_LATER:
      return "    > ";

    case PROMPT_DATA:
      return "data> ";

    case PROMPT_COMMENT:
      return "comment> ";

    case PROMPT_DOCUMENT:
      return "document> ";

    case PROMPT_DO_REPEAT:
      return "DO REPEAT> ";
    }

  NOT_REACHED ();
}



static int pfd[2];
static bool sigint_received ;

static void 
handler (int sig)
{
  rl_end = 0;

  write (pfd[1], "x", 1);
  rl_echo_signal_char (sig);
}


/* 
   A function similar to getc from stdio.
   However this one may be interrupted by SIGINT.
   If that happens it will return EOF and the global variable
   sigint_received will be set to true.
 */
static int
interruptible_getc (FILE *fp)
{
  int c  = 0;
  int ret = -1;
  do
    {
      struct timeval timeout = {0, 50000};
      fd_set what;
      int max_fd = 0;
      int fd ;
      FD_ZERO (&what);
      fd = fileno (fp);
      max_fd = (max_fd > fd) ? max_fd : fd;
      FD_SET (fd, &what);
      fd = pfd[0];
      max_fd = (max_fd > fd) ? max_fd : fd;
      FD_SET (fd, &what);
      ret = select (max_fd + 1, &what, NULL, NULL, &timeout);
      if ( ret == -1 && errno != EINTR)
	{
	  perror ("Select failed");
	  continue;
	}

      if (ret > 0 )
	{
	  if (FD_ISSET (pfd[0], &what))
	    {
	      char dummy[1];
	      read (pfd[0], dummy, 1);
	      sigint_received = true;
	      return EOF;
	    }
	}
    }
  while (ret <= 0);

  /* This will not block! */
  read (fileno (fp), &c, 1);

  return c;
}



#if HAVE_READLINE

static void
readline_init (void)
{
  if ( 0 != pipe2 (pfd, O_NONBLOCK))
    perror ("Cannot create pipe");

  if ( SIG_ERR == signal (SIGINT, handler))
    perror ("Cannot add signal handler");

  rl_catch_signals = 0;
  rl_getc_function = interruptible_getc;
  rl_basic_word_break_characters = "\n";
  using_history ();
  stifle_history (500);
  if (history_file == NULL)
    {
      const char *home_dir = getenv ("HOME");
      if (home_dir != NULL)
        {
          history_file = xasprintf ("%s/.pspp_history", home_dir);
          read_history (history_file);
        }
    }
}

static void
readline_done (void)
{
  if (history_file != NULL && false == settings_get_testing_mode () )
    write_history (history_file);
  clear_history ();
  free (history_file);
}

/* Prompt the user for a line of input and return it in LINE. 
   Returns true if the LINE should be considered valid, false otherwise.
 */
static bool
readline_read (struct substring *line, enum prompt_style style)
{
  char *string;

  rl_attempted_completion_function = (style == PROMPT_FIRST
                                      ? complete_command_name
                                      : dont_complete);
  sigint_received = false;
  string = readline (readline_prompt (style));
  if (sigint_received)
    {
      *line = ss_empty ();
      return false;
    }

  if (string != NULL)
    {
      char *end;

      if (string[0])
        add_history (string);

      end = strchr (string, '\0');
      *end = '\n';
      *line = ss_buffer (string, end - string + 1);
    }
  else
    *line = ss_empty ();

  return true;
}

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

#else  /* !HAVE_READLINE */

static void
readline_init (void)
{
}

static void
readline_done (void)
{
}

static bool
readline_read (struct substring *line, enum prompt_style style)
{
  struct string string;
  const char *prompt = readline_prompt (style);

  fputs (prompt, stdout);
  fflush (stdout);
  ds_init_empty (&string);
  ds_read_line (&string, stdin, SIZE_MAX);
  
  *line = string.ss;
  
  return false;
}
#endif /* !HAVE_READLINE */
