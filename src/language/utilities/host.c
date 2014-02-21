/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#if HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "data/settings.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/localcharset.h"
#include "gl/xalloc.h"
#include "gl/xmalloca.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#if HAVE_FORK && HAVE_EXECL
/* Spawn an interactive shell process. */
static bool
shell (void)
{
  int pid;

  pid = fork ();
  switch (pid)
    {
    case 0:
      {
	const char *shell_fn;
	char *shell_process;

	{
	  int i;

	  for (i = 3; i < 20; i++)
	    close (i);
	}

	shell_fn = getenv ("SHELL");
	if (shell_fn == NULL)
	  shell_fn = "/bin/sh";

	{
	  const char *cp = strrchr (shell_fn, '/');
	  cp = cp ? &cp[1] : shell_fn;
	  shell_process = xmalloca (strlen (cp) + 8);
	  strcpy (shell_process, "-");
	  strcat (shell_process, cp);
	  if (strcmp (cp, "sh"))
	    shell_process[0] = '+';
	}

	execl (shell_fn, shell_process, NULL);

	_exit (1);
      }

    case -1:
      msg (SE, _("Couldn't fork: %s."), strerror (errno));
      return false;

    default:
      assert (pid > 0);
      while (wait (NULL) != pid)
	;
      return true;
    }
}
#else /* !(HAVE_FORK && HAVE_EXECL) */
/* Don't know how to spawn an interactive shell. */
static bool
shell (void)
{
  msg (SE, _("Interactive shell not supported on this platform."));
  return false;
}
#endif

/* Executes the specified COMMAND in a subshell.  Returns true if
   successful, false otherwise. */
static bool
run_command (const char *command)
{
  if (system (NULL) == 0)
    {
      msg (SE, _("Command shell not supported on this platform."));
      return false;
    }

  /* Execute the command. */
  if (system (command) == -1)
    msg (SE, _("Error executing command: %s."), strerror (errno));

  return true;
}

int
cmd_host (struct lexer *lexer, struct dataset *ds UNUSED)
{
  if (settings_get_safer_mode ())
    {
      msg (SE, _("This command not allowed when the %s option is set."), "SAFER");
      return CMD_FAILURE;
    }

  if (lex_token (lexer) == T_ENDCMD)
    return shell () ? CMD_SUCCESS : CMD_FAILURE;
  else if (lex_match_id (lexer, "COMMAND"))
    {
      struct string command;
      char *locale_command;
      bool ok;

      lex_match (lexer, T_EQUALS);
      if (!lex_force_match (lexer, T_LBRACK))
        return CMD_FAILURE;

      ds_init_empty (&command);
      while (lex_is_string (lexer))
        {
          if (!ds_is_empty (&command))
            ds_put_byte (&command, '\n');
          ds_put_substring (&command, lex_tokss (lexer));
          lex_get (lexer);
        }
      if (!lex_force_match (lexer, T_RBRACK))
        {
          ds_destroy (&command);
          return CMD_FAILURE;
        }

      locale_command = recode_string (locale_charset (), "UTF-8",
                                      ds_cstr (&command),
                                      ds_length (&command));
      ds_destroy (&command);

      ok = run_command (locale_command);
      free (locale_command);

      return ok ? CMD_SUCCESS : CMD_FAILURE;
    }
  else
    {
      lex_error (lexer, NULL);
      return CMD_FAILURE;
    }
}
