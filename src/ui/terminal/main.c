/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

#include <signal.h>
#include <stdio.h>

#include <ui/debugger.h>
#include "command-line.h"
#include "msg-ui.h"
#include "progname.h"
#include "read-line.h"

#include <data/fastfile-factory.h>
#include <data/dictionary.h>
#include <data/file-handle-def.h>
#include <libpspp/getl.h>
#include <data/file-name.h>
#include <data/format.h>
#include <data/procedure.h>
#include <data/settings.h>
#include <data/variable.h>
#include <gsl/gsl_errno.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/prompt.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <libpspp/version.h>
#include <math/random.h>
#include <output/output.h>

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
static void terminate (bool success) NO_RETURN;

/* If a segfault happens, issue a message to that effect and halt */
void bug_handler(int sig);

/* Handle quit/term/int signals */
void interrupt_handler(int sig);
static struct dataset * the_dataset = NULL;

static struct lexer *the_lexer;
static struct source_stream *the_source_stream ;


/* Program entry point. */
int
main (int argc, char **argv)
{
  struct casefile_factory *factory;
  signal (SIGABRT, bug_handler);
  signal (SIGSEGV, bug_handler);
  signal (SIGFPE, bug_handler);
  signal (SIGINT, interrupt_handler);

  set_program_name ("pspp");
  i18n_init ();
  fpu_init ();
  gsl_set_error_handler_off ();

  fmt_init ();
  outp_init ();
  fn_init ();
  fh_init ();
  the_source_stream =
    create_source_stream (
			  fn_getenv_default ("STAT_INCLUDE_PATH", include_path)
			  );
  prompt_init ();
  readln_initialize ();
  settings_init ();
  random_init ();

  factory = fastfile_factory_create ();

  the_dataset = create_dataset (factory, NULL, NULL);

  if (parse_command_line (argc, argv, the_source_stream))
    {
      msg_ui_init (the_source_stream);
      outp_read_devices ();
      the_lexer = lex_create (the_source_stream);

      for (;;)
        {
          int result = cmd_parse (the_lexer, the_dataset);

          if (result == CMD_EOF || result == CMD_FINISH)
            break;
          if (result == CMD_CASCADING_FAILURE &&
	      !getl_is_interactive (the_source_stream))
            {
              msg (SE, _("Stopping syntax file processing here to avoid "
                         "a cascade of dependent command failures."));
              getl_abort_noninteractive (the_source_stream); 
            }
          else
            check_msg_count (the_source_stream);
        }
    }

  terminate (!any_errors ());
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
#if DEBUGGING
  connect_debugger ();
#endif
  switch (sig) 
    {
    case SIGABRT:
      request_bug_report_and_abort("Assertion Failure/Abort");
    case SIGFPE:
      request_bug_report_and_abort("Floating Point Exception");
    case SIGSEGV:
      request_bug_report_and_abort("Segmentation Violation");
    default:
      request_bug_report_and_abort("Unknown");
    }
}

void 
interrupt_handler(int sig UNUSED)
{
  terminate (false);
}


/* Terminate PSPP.  SUCCESS should be true to exit successfully,
   false to exit as a failure.  */
static void
terminate (bool success)
{
  static bool terminating = false;
  if (!terminating) 
    {
      terminating = true;

      destroy_dataset (the_dataset);

      random_done ();
      settings_done ();
      fh_done ();
      lex_destroy (the_lexer);
      destroy_source_stream (the_source_stream);
      prompt_done ();
      readln_uninitialize ();

      outp_done ();
      msg_ui_done ();
      fmt_done ();
    }
  exit (success ? EXIT_SUCCESS : EXIT_FAILURE);
}
