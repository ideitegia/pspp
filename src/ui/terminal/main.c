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
#include <gsl/gsl_errno.h>
#include <signal.h>
#include <stdio.h>
#include "command-line.h"
#include <language/command.h>
#include <libpspp/compiler.h>
#include <data/dictionary.h>
#include <libpspp/message.h>
#include <data/file-handle-def.h>
#include <data/filename.h>
#include <language/line-buffer.h>
#include <language/lexer/lexer.h>
#include <output/output.h>
#include "progname.h"
#include <math/random.h>
#include "read-line.h"
#include <data/settings.h>
#include <data/variable.h>
#include <libpspp/version.h>

#if HAVE_FPU_CONTROL_H
#include <fpu_control.h>
#endif

#if HAVE_LOCALE_H
#include <locale.h>
#endif

#if HAVE_FENV_H
#include <fenv.h>
#endif

#include "gettext.h"
#define _(msgid) gettext (msgid)

#include <stdlib.h>

static void i18n_init (void);
static void fpu_init (void);
static void handle_error (int code);
static int execute_command (void);

/* If a segfault happens, issue a message to that effect and halt */
void bug_handler(int sig);

/* Handle quit/term/int signals */
void interrupt_handler(int sig);

void terminate (bool success);

/* Program entry point. */
int
main (int argc, char **argv)
{
  signal (SIGSEGV, bug_handler);
  signal (SIGFPE, bug_handler);
  signal (SIGINT, interrupt_handler);

  set_program_name ("pspp");
  i18n_init ();
  fpu_init ();
  gsl_set_error_handler_off ();

  outp_init ();
  fn_init ();
  fh_init ();
  getl_initialize ();
  readln_initialize ();
  settings_init ();
  random_init ();

  default_dict = dict_create ();

  if (parse_command_line (argc, argv)) 
    {
      outp_read_devices ();
      lex_init ();

      for (;;)
        {
          int retval;

          err_check_count ();

          retval = execute_command ();
          if (retval == CMD_EOF)
            break;
          if (retval != CMD_SUCCESS)
            handle_error (retval);
        }
    }
  
  terminate (err_error_count == 0);
  abort ();
}

/* Parse and execute a command, returning its return code. */
static int
execute_command (void)
{
  int result;
  
  /* Read the command's first token.  
     The first token is part of the first line of the command. */
  getl_set_prompt_style (GETL_PROMPT_FIRST);
  lex_get ();
  if (token == T_STOP)
    return CMD_EOF;

  /* Parse the command.
     Any lines read after the first token must be continuation
     lines. */
  getl_set_prompt_style (GETL_PROMPT_LATER);
  result = cmd_parse ();
 
  /* Unset the /ALGORITHM subcommand if it was used */
  unset_cmd_algorithm ();

  /* Clear any auxiliary data from the dictionary. */
  dict_clear_aux (default_dict);

  return result;
}

/* Print an error message corresponding to the command return code
   CODE. */
static void
handle_error (int code)
{
  if (code == CMD_CASCADING_FAILURE && !getl_is_interactive ()) 
    {
      msg (SW, _("This command not executed.  Stopping here "
                 "to avoid cascading failures."));
      getl_abort_noninteractive ();
      return;
    }

  switch (code)
    {
    case CMD_FAILURE:
    case CMD_CASCADING_FAILURE:
      msg (SW,  _("This command not executed."));
      break;

    case CMD_PART_SUCCESS_MAYBE:
      msg (SW, _("Skipping the rest of this command.  Part of "
		 "this command may have been executed."));
      break;
		  
    case CMD_PART_SUCCESS:
      msg (SW, _("Skipping the rest of this command.  This "
		 "command was fully executed up to this point."));
      break;

    case CMD_TRAILING_GARBAGE:
      msg (SW, _("Trailing garbage was encountered following "
		 "this command.  The command was fully executed "
		 "to this point."));
      break;

    default:
      abort ();
    }

  if (!getl_is_interactive ())
    {
      while (token != T_STOP && token != '.')
	lex_get ();
    }
  else 
    {
      msg (SW, _("The rest of this command has been discarded."));
      lex_discard_line (); 
    }
}

static void
i18n_init (void) 
{
#if ENABLE_NLS
#if HAVE_LC_MESSAGES
  setlocale (LC_MESSAGES, "");
#endif
  setlocale (LC_MONETARY, "");
  bindtextdomain (PACKAGE, locale_dir);
  textdomain (PACKAGE);
#endif /* ENABLE_NLS */
}

static void
fpu_init (void) 
{
#if HAVE_FEHOLDEXCEPT
  fenv_t foo;
  feholdexcept (&foo);
#elif HAVE___SETFPUCW && defined(_FPU_IEEE)
  __setfpucw (_FPU_IEEE);
#endif
}

/* If a segfault happens, issue a message to that effect and halt */
void 
bug_handler(int sig)
{
  switch (sig) 
    {
    case SIGFPE:
      request_bug_report_and_abort("Floating Point Exception");
      break;
    case SIGSEGV:
      request_bug_report_and_abort("Segmentation Violation");
      break;
    default:
      request_bug_report_and_abort("");
      break;
    }
}

void 
interrupt_handler(int sig UNUSED)
{
  terminate (false);
}


/* Terminate PSPP.  SUCCESS should be true to exit successfully,
   false to exit as a failure.  */
void
terminate (bool success)
{
  static bool terminating = false;
  if (terminating)
    return;
  terminating = true;

  err_done ();
  outp_done ();

  cancel_transformations ();
  dict_destroy (default_dict);

  random_done ();
  settings_done ();
  fh_done ();
  lex_done ();
  getl_uninitialize ();

  exit (success ? EXIT_SUCCESS : EXIT_FAILURE);
}
