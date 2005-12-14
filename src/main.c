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
#include "main.h"
#include <gsl/gsl_errno.h>
#include <signal.h>
#include <stdio.h>
#include "cmdline.h"
#include "command.h"
#include "dictionary.h"
#include "error.h"
#include "file-handle.h"
#include "filename.h"
#include "getl.h"
#include "glob.h"
#include "lexer.h"
#include "output.h"
#include "progname.h"
#include "random.h"
#include "settings.h"
#include "var.h"
#include "version.h"

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

#include "debug-print.h"

static void i18n_init (void);
static void fpu_init (void);
static void handle_error (int code);
static int execute_command (void);

/* Whether FINISH. has been executed. */
int finished;

/* If a segfault happens, issue a message to that effect and halt */
void bug_handler(int sig);

/* Handle quit/term/int signals */
void interrupt_handler(int sig);

/* Whether we're dropping down to interactive mode immediately because
   we hit end-of-file unexpectedly (or whatever). */
int start_interactive;

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
  settings_init ();
  random_init ();

  default_dict = dict_create ();

  parse_command_line (argc, argv);
  outp_read_devices ();

  lex_init ();

  while (!finished)
    {
      err_check_count ();
      handle_error (execute_command ());
    }

  terminate (err_error_count == 0);
  abort ();
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

/* Parse and execute a command, returning its return code. */
static int
execute_command (void)
{
  int result;
  
  /* Read the command's first token.
     We may hit end of file.
     If so, give the line reader a chance to proceed to the next file.
     End of file is not handled transparently since the user may want
     the dictionary cleared between files. */
  getl_prompt = GETL_PRPT_STANDARD;
  for (;;)
    {
      lex_get ();
      if (token != T_STOP)
	break;

      if (!getl_perform_delayed_reset ())
	terminate (err_error_count == 0);
    }

  /* Parse the command. */
  getl_prompt = GETL_PRPT_CONTINUATION;
  result =  cmd_parse ();
 
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
  switch (code)
    {
    case CMD_SUCCESS:
      return;
	  
    case CMD_FAILURE:
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
      assert (0);
    }

  if (getl_reading_script)
    {
      err_break ();
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
