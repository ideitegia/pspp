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
#include "cmdline.h"
#include "error.h"
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include "alloc.h"
#include "error.h"
#include "filename.h"
#include "getl.h"
#include "main.h"
#include "output.h"
#include "settings.h"
#include "str.h"
#include "var.h"
#include "version.h"
#include "copyleft.h"
#include "glob.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

void welcome (void);
static void usage (void);

char *subst_vars (char *);

/* Parses the command line specified by ARGC and ARGV as received by
   main(). */
void
parse_command_line (int argc, char **argv)
{
  static int testing_mode = 0;
  static struct option long_options[] =
  {
    {"algorithm", required_argument, NULL, 'a'},
    {"command", required_argument, NULL, 'c'},
    {"config-directory", required_argument, NULL, 'B'},
    {"device", required_argument, NULL, 'o'},
    {"dry-run", no_argument, NULL, 'n'},
    {"edit", no_argument, NULL, 'n'},
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
    {"testing-mode", no_argument, &testing_mode, 1},
    {"verbose", no_argument, NULL, 'v'},
    {"version", no_argument, NULL, 'V'},
    {0, 0, 0, 0},
  };

  int c, i;

  int cleared_device_defaults = 0;
  int no_statrc = 0;

  for (;;)
    {
      c = getopt_long (argc, argv, "a:x:B:c:f:hiI:lno:prsvV", long_options, NULL);
      if (c == -1)
	break;

      switch (c)
	{
	  /* Compatibility options */
        case 'a':
	  if ( 0 == strcmp(optarg,"compatible") )
	      set_algorithm(COMPATIBLE);
	  else if ( 0 == strcmp(optarg,"enhanced"))
	      set_algorithm(ENHANCED);
	  else
	    {
	      usage();
	      assert(0);
	    }
	  break;

	case 'x':	  
	  if ( 0 == strcmp(optarg,"compatible") )
	    set_syntax(COMPATIBLE);
	  else if ( 0 == strcmp(optarg,"enhanced"))
	    set_syntax(ENHANCED);
	  else
	    {
	      usage();
	      assert(0);
	    }
	  break;

	case 'c':
	  {
	    static int n_cmds;
	    
	    struct getl_script *script = xmalloc (sizeof *script);
	    
	    {
	      struct getl_line_list *line;

	      script->first_line = line = xmalloc (sizeof *line);
	      line->line = xstrdup ("commandline");
	      line->len = --n_cmds;
	      line = line->next = xmalloc (sizeof *line);
	      line->line = xstrdup (optarg);
	      line->len = strlen (optarg);
	      line->next = NULL;
	    }

	    getl_add_virtual_file (script);
	  }
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
	  assert (0);
	case 'i':
	  getl_interactive = 2;
	  break;
	case 'I':
	  if (optarg == NULL || !strcmp (optarg, "-"))
	    getl_clear_include_path ();
	  else
	    getl_add_include_dir (optarg);
	  break;
	case 'l':
	  outp_list_classes ();
	  err_hcf (1);
	case 'n':
	  printf (_("%s is not yet implemented."),"-n");
          putchar('\n');
	  break;
	case 'o':
	  if (!cleared_device_defaults)
	    {
	      outp_configure_clear ();
	      cleared_device_defaults = 1;
	    }
	  outp_configure_add (optarg);
	  break;
	case 'p':
	  printf (_("%s is not yet implemented."),"-p");
          putchar('\n');
	  break;
	case 'r':
	  no_statrc = 1;
	  break;
	case 's':
	  make_safe();
	  break;
	case 'v':
	  err_verbosity++;
	  break;
	case 'V':
	  puts (version);
	  puts (legal);
	  err_hcf (1);
	case '?':
	  usage ();
	  assert (0);
	case 0:
	  break;
	default:
	  assert (0);
	}
    }


  if (testing_mode)
    {
      /* FIXME: Later this option should do some other things, too. */
      force_long_view();
      test_mode = 1;
    }
    

  for (i = optind; i < argc; i++)
    {
      int separate = 1;

      if (!strcmp (argv[i], "+"))
	{
	  separate = 0;
	  if (++i >= argc)
	    usage ();
	}
      else if (strchr (argv[i], '='))
	{
	  outp_configure_macro (argv[i]);
	  continue;
	}
      getl_add_file (argv[i], separate, 0);
    }
  if (getl_head)
    getl_head->separate = 0;

  if (getl_am_interactive)
    getl_interactive = 1;

  if (!no_statrc)
    {
      char *pspprc_fn = fn_search_path ("rc", config_path, NULL);

      if (pspprc_fn)
	getl_add_file (pspprc_fn, 0, 1);

      free (pspprc_fn);
    }
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
"  -d, --define=VAR[=VALUE]  set environment variable VAR to VALUE, or empty\n"
"  -u, --undef=VAR           undefine environment variable VAR\n"
"\nInput and output:\n"
"  -f, --out-file=FILE       send output to FILE (overwritten)\n"
"  -p, --pipe                read script from stdin, send output to stdout\n"
"  -I-, --no-include         clear include path\n"
"  -I, --include=DIR         append DIR to include path\n"
"  -c, --command=COMMAND     execute COMMAND before .pspp/rc at startup\n"
"\nLanguage modifiers:\n"
"  -i, --interactive         interpret scripts in interactive mode\n"
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
" FILE1 FILE2                run FILE1, clear the dictionary, run FILE2\n"
" FILE1 + FILE2              run FILE1 then FILE2 without clearing dictionary\n"
" KEY=VALUE                  overrides macros in output initialization file\n"
"\n");

/* Message that describes PSPP command-line syntax, continued. */
static const char post_syntax_message[] = N_("\nReport bugs to <%s>.\n");

/* Writes a syntax description to stdout and terminates. */
static void
usage (void)
{
  printf (gettext (pre_syntax_message), pgmname);
  outp_list_classes ();
  printf (gettext (post_syntax_message),PACKAGE_BUGREPORT);

  err_hcf (1);
}
