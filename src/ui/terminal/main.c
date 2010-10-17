/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009, 2010 Free Software Foundation, Inc.

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
#include <stdio.h>
#include <stdlib.h>
#if HAVE_FPU_CONTROL_H
#include <fpu_control.h>
#endif
#if HAVE_FENV_H
#include <fenv.h>
#endif
#if HAVE_IEEEFP_H
#include <ieeefp.h>
#endif
#include <unistd.h>

#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/file-name.h"
#include "data/procedure.h"
#include "data/settings.h"
#include "data/variable.h"
#include "gsl/gsl_errno.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/prompt.h"
#include "language/syntax-file.h"
#include "libpspp/argv-parser.h"
#include "libpspp/compiler.h"
#include "libpspp/getl.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/version.h"
#include "math/random.h"
#include "output/driver.h"
#include "ui/debugger.h"
#include "ui/source-init-opts.h"
#include "ui/terminal/msg-ui.h"
#include "ui/terminal/read-line.h"
#include "ui/terminal/terminal-opts.h"
#include "ui/terminal/terminal.h"

#include "gl/fatal-signal.h"
#include "gl/progname.h"
#include "gl/relocatable.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static struct dataset * the_dataset = NULL;

static struct lexer *the_lexer;
static struct source_stream *the_source_stream ;

static void add_syntax_file (struct source_stream *, enum syntax_mode,
                             const char *file_name);
static void bug_handler(int sig);
static void fpu_init (void);

/* Program entry point. */
int
main (int argc, char **argv)
{
  struct terminal_opts *terminal_opts;
  struct argv_parser *parser;
  enum syntax_mode syntax_mode;
  bool process_statrc;

  set_program_name (argv[0]);

  signal (SIGABRT, bug_handler);
  signal (SIGSEGV, bug_handler);
  signal (SIGFPE, bug_handler);

  i18n_init ();
  fpu_init ();
  gsl_set_error_handler_off ();

  fh_init ();
  the_source_stream = create_source_stream ();
  prompt_init ();
  readln_initialize ();
  settings_init ();
  terminal_check_size ();
  random_init ();

  the_dataset = create_dataset ();

  parser = argv_parser_create ();
  terminal_opts = terminal_opts_init (parser, &syntax_mode, &process_statrc);
  source_init_register_argv_parser (parser, the_source_stream);
  if (!argv_parser_run (parser, argc, argv))
    exit (EXIT_FAILURE);
  terminal_opts_done (terminal_opts, argc, argv);
  argv_parser_destroy (parser);

  msg_ui_init (the_source_stream);

  /* Add syntax files to source stream. */
  if (process_statrc)
    {
      char *rc = fn_search_path ("rc", getl_include_path (the_source_stream));
      if (rc != NULL)
        {
          add_syntax_file (the_source_stream, GETL_BATCH, rc);
          free (rc);
        }
    }
  if (optind < argc)
    {
      int i;

      for (i = optind; i < argc; i++)
        add_syntax_file (the_source_stream, syntax_mode, argv[i]);
    }
  else
    add_syntax_file (the_source_stream, syntax_mode, "-");

  /* Parse and execute syntax. */
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
      else if (msg_ui_too_many_errors ())
        getl_abort_noninteractive (the_source_stream);
    }


  destroy_dataset (the_dataset);

  random_done ();
  settings_done ();
  fh_done ();
  lex_destroy (the_lexer);
  destroy_source_stream (the_source_stream);
  prompt_done ();
  readln_uninitialize ();
  output_close ();
  msg_ui_done ();
  i18n_done ();

  return msg_ui_any_errors ();
}

static void
fpu_init (void)
{
#if HAVE_FEHOLDEXCEPT
  fenv_t foo;
  feholdexcept (&foo);
#elif HAVE___SETFPUCW && defined(_FPU_IEEE)
  __setfpucw (_FPU_IEEE);
#elif HAVE_FPSETMASK
  fpsetmask (0);
#endif
}

/* If a segfault happens, issue a message to that effect and halt */
static void
bug_handler(int sig)
{
  /* Reset SIG to its default handling so that if it happens again we won't
     recurse. */
  signal (sig, SIG_DFL);

#if DEBUGGING
  connect_debugger ();
#endif
  switch (sig)
    {
    case SIGABRT:
      request_bug_report("Assertion Failure/Abort");
      break;
    case SIGFPE:
      request_bug_report("Floating Point Exception");
      break;
    case SIGSEGV:
      request_bug_report("Segmentation Violation");
      break;
    default:
      request_bug_report("Unknown");
      break;
    }

  /* Re-raise the signal so that we terminate with the correct status. */
  raise (sig);
}

static void
add_syntax_file (struct source_stream *ss, enum syntax_mode syntax_mode,
                 const char *file_name)
{
  struct getl_interface *source;

  source = (!strcmp (file_name, "-") && isatty (STDIN_FILENO)
           ? create_readln_source ()
           : create_syntax_file_source (file_name));
  getl_append_source (ss, source, syntax_mode, ERRMODE_CONTINUE);
}
