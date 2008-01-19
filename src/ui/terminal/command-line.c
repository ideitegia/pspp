/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007 Free Software Foundation, Inc.

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
#include "command-line.h"
#include "msg-ui.h"
#include <libpspp/message.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <libpspp/assertion.h>
#include <libpspp/copyleft.h>
#include <libpspp/message.h>
#include <language/syntax-file.h>
#include "progname.h"
#include <data/settings.h>
#include <output/output.h>
#include <data/file-name.h>
#include <libpspp/getl.h>
#include <libpspp/str.h>
#include <libpspp/version.h>
#include <libpspp/verbose-msg.h>
#include "read-line.h"

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void usage (void);

/* Parses the command line specified by ARGC and ARGV as received by
   main().  Returns true if normal execution should proceed,
   false if the command-line indicates that PSPP should exit. */
bool
parse_command_line (int argc, char **argv, struct source_stream *ss)
{
  static struct option long_options[] =
  {
    {"algorithm", required_argument, NULL, 'a'},
    {"command", required_argument, NULL, 'c'},
    {"config-directory", required_argument, NULL, 'B'},
    {"device", required_argument, NULL, 'o'},
    {"dry-run", no_argument, NULL, 'n'},
    {"edit", no_argument, NULL, 'n'},
    {"error-file", required_argument, NULL, 'e'},
    {"help", no_argument, NULL, 'h'},
    {"include-directory", required_argument, NULL, 'I'},
    {"interactive", no_argument, NULL, 'i'},
    {"just-print", no_argument, NULL, 'n'},
    {"list", no_argument, NULL, 'l'},
    {"no-include", no_argument, NULL, 'I'},
    {"no-statrc", no_argument, NULL, 'r'},
    {"out-file", required_argument, NULL, 'f'},
    {"pipe", no_argument, NULL, 'p'},
    {"recon", no_argument, NULL, 'n'},
    {"safer", no_argument, NULL, 's'},
    {"syntax", required_argument, NULL, 'x'},
    {"testing-mode", no_argument, NULL, 'T'},
    {"verbose", no_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {0, 0, 0, 0},
  };

  int c, i;

  bool cleared_device_defaults = false;
  bool process_statrc = true;
  bool interactive_mode = false;
  int syntax_files = 0;

  for (;;)
    {
      c = getopt_long (argc, argv, "a:x:B:c:e:f:hiI:lno:prsvV", long_options, NULL);
      if (c == -1)
	break;

      switch (c)
	{
	  /* Compatibility options */
        case 'a':
	  if ( 0 == strcmp(optarg,"compatible") )
	      settings_set_algorithm(COMPATIBLE);
	  else if ( 0 == strcmp(optarg,"enhanced"))
	      settings_set_algorithm(ENHANCED);
	  else
	    {
	      usage ();
              return false;
	    }
	  break;

	case 'x':
	  if ( 0 == strcmp(optarg,"compatible") )
	    settings_set_syntax (COMPATIBLE);
	  else if ( 0 == strcmp(optarg,"enhanced"))
	    settings_set_syntax (ENHANCED);
	  else
	    {
	      usage ();
              return false;
	    }
	  break;
	case 'e':
	  msg_ui_set_error_file (optarg);
	  break;
	case 'B':
	  config_path = optarg;
	  break;
	case 'f':
	  printf (_("%s is not yet implemented."), "-f");
          putchar('\n');
	  break;
	case 'h':
	  usage ();
          return false;
	case 'i':
	  interactive_mode = true;
	  break;
	case 'I':
	  if (optarg == NULL || !strcmp (optarg, "-"))
	    getl_clear_include_path (ss);
	  else
	    getl_add_include_dir (ss, optarg);
	  break;
	case 'l':
	  outp_list_classes ();
          return false;
	case 'n':
	  printf (_("%s is not yet implemented."),"-n");
          putchar('\n');
	  break;
	case 'o':
	  if (!cleared_device_defaults)
	    {
	      outp_configure_clear ();
	      cleared_device_defaults = true;
	    }
	  outp_configure_add (optarg);
	  break;
	case 'p':
	  printf (_("%s is not yet implemented."),"-p");
          putchar('\n');
	  break;
	case 'r':
	  process_statrc = false;
	  break;
	case 's':
	  settings_set_safer_mode ();
	  break;
	case 'v':
	  verbose_increment_level ();
	  break;
	case 'V':
	  puts (version);
	  puts (legal);
	  return false;
        case 'T':
          settings_set_testing_mode (true);
          break;
	case '?':
	  usage ();
          return false;
	case 0:
	  break;
	default:
	  NOT_REACHED ();
	}
    }

  if (process_statrc)
    {
      char *pspprc_fn = fn_search_path ("rc", config_path);
      if (pspprc_fn != NULL)
        {
	  getl_append_source (ss,
			      create_syntax_file_source (pspprc_fn),
			      GETL_BATCH,
			      ERRMODE_CONTINUE
			      );

          free (pspprc_fn);
        }
    }

  for (i = optind; i < argc; i++)
    if (strchr (argv[i], '='))
      outp_configure_macro (argv[i]);
    else
      {
	getl_append_source (ss,
			    create_syntax_file_source (argv[i]),
			    GETL_BATCH,
			    ERRMODE_CONTINUE
			    );
        syntax_files++;
      }

  if (!syntax_files || interactive_mode)
    {
      getl_append_source (ss, create_readln_source (),
			  GETL_INTERACTIVE,
			  ERRMODE_CONTINUE
			  );
      if (!cleared_device_defaults)
        outp_configure_add ("interactive");
    }

  return true;
}

/* Message that describes PSPP command-line syntax. */
static const char pre_syntax_message[] =
N_("PSPP, a program for statistical analysis of sample data.\n"
"\nUsage: %s [OPTION]... FILE...\n"
"\nIf a long option shows an argument as mandatory, then it is mandatory\n"
"for the equivalent short option also.  Similarly for optional arguments.\n"
"\nConfiguration:\n"
"  -a, --algorithm={compatible|enhanced}\n"
"                            set to `compatible' if you want output\n"
"                            calculated from broken algorithms\n"
"  -B, --config-dir=DIR      set configuration directory to DIR\n"
"  -o, --device=DEVICE       select output driver DEVICE and disable defaults\n"
"\nInput and output:\n"
"  -e, --error-file=FILE     send error messages to FILE (appended)\n"
"  -f, --out-file=FILE       send output to FILE (overwritten)\n"
"  -p, --pipe                read syntax from stdin, send output to stdout\n"
"  -I-, --no-include         clear include path\n"
"  -I, --include=DIR         append DIR to include path\n"
"\nLanguage modifiers:\n"
"  -i, --interactive         interpret syntax in interactive mode\n"
"  -n, --edit                just check syntax; don't actually run the code\n"
"  -r, --no-statrc           disable execution of .pspp/rc at startup\n"
"  -s, --safer               don't allow some unsafe operations\n"
"  -x, --syntax={compatible|enhanced}\n"
"                            set to `compatible' if you want only to accept\n"
"                            spss compatible syntax\n"
"\nInformative output:\n"
"  -h, --help                print this help, then exit\n"
"  -l, --list                print a list of known driver classes, then exit\n"
"  -V, --version             show PSPP version, then exit\n"
"  -v, --verbose             increments verbosity level\n"
"\nNon-option arguments:\n"
" FILE                       syntax file to execute\n"
" KEY=VALUE                  overrides macros in output initialization file\n"
"\n");

/* Message that describes PSPP command-line syntax, continued. */
static const char post_syntax_message[] = N_("\nReport bugs to <%s>.\n");

/* Writes a syntax description to stdout. */
static void
usage (void)
{
  printf (gettext (pre_syntax_message), program_name);
  outp_list_classes ();
  printf (gettext (post_syntax_message), PACKAGE_BUGREPORT);
}
