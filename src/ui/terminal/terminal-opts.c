/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2010  Free Software Foundation

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

#include "terminal-opts.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>


#include "data/settings.h"
#include "data/file-name.h"
#include "language/lexer/include-path.h"
#include "libpspp/argv-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/llx.h"
#include "libpspp/str.h"
#include "libpspp/string-array.h"
#include "libpspp/string-map.h"
#include "libpspp/string-set.h"
#include "libpspp/version.h"
#include "output/driver.h"
#include "output/driver-provider.h"
#include "output/msglog.h"

#include "gl/error.h"
#include "gl/localcharset.h"
#include "gl/progname.h"
#include "gl/version-etc.h"
#include "gl/xmemdup0.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

struct terminal_opts
  {
    struct string_map options;  /* Output driver options. */
    bool has_output_driver;
    bool has_terminal_driver;
    bool has_error_file;
    enum lex_syntax_mode *syntax_mode;
    bool *process_statrc;
    char **syntax_encoding;
  };

enum
  {
    OPT_TESTING_MODE,
    OPT_ERROR_FILE,
    OPT_OUTPUT,
    OPT_OUTPUT_OPTION,
    OPT_NO_OUTPUT,
    OPT_BATCH,
    OPT_INTERACTIVE,
    OPT_SYNTAX_ENCODING,
    OPT_NO_STATRC,
    OPT_HELP,
    OPT_VERSION,
    N_TERMINAL_OPTIONS
  };

static struct argv_option terminal_argv_options[N_TERMINAL_OPTIONS] =
  {
    {"testing-mode", 0, no_argument, OPT_TESTING_MODE},
    {"error-file", 'e', required_argument, OPT_ERROR_FILE},
    {"output", 'o', required_argument, OPT_OUTPUT},
    {NULL, 'O', required_argument, OPT_OUTPUT_OPTION},
    {"no-output", 0, no_argument, OPT_NO_OUTPUT},
    {"batch", 'b', no_argument, OPT_BATCH},
    {"interactive", 'i', no_argument, OPT_INTERACTIVE},
    {"syntax-encoding", 0, required_argument, OPT_SYNTAX_ENCODING},
    {"no-statrc", 'r', no_argument, OPT_NO_STATRC},
    {"help", 'h', no_argument, OPT_HELP},
    {"version", 'V', no_argument, OPT_VERSION},
  };

static void
register_output_driver (struct terminal_opts *to)
{
  if (!string_map_is_empty (&to->options))
    {
      struct output_driver *driver;

      driver = output_driver_create (&to->options);
      if (driver != NULL)
        {
          output_driver_register (driver);

          to->has_output_driver = true;
          if (driver->device_type == SETTINGS_DEVICE_TERMINAL)
            to->has_terminal_driver = true;
        }
      string_map_clear (&to->options);
    }
}

static void
parse_output_option (struct terminal_opts *to, const char *option)
{
  const char *equals;
  char *key, *value;

  equals = strchr (option, '=');
  if (equals == NULL)
    {
      error (0, 0, _("%s: output option missing `='"), option);
      return;
    }

  key = xmemdup0 (option, equals - option);
  if (string_map_contains (&to->options, key))
    {
      error (0, 0, _("%s: output option specified more than once"), key);
      free (key);
      return;
    }

  value = xmemdup0 (equals + 1, strlen (equals + 1));
  string_map_insert_nocopy (&to->options, key, value);
}

static char *
get_supported_formats (void)
{
  const struct string_set_node *node;
  struct string_array format_array;
  struct string_set format_set;
  char *format_string;
  const char *format;
  size_t i;

  /* Get supported formats as unordered set. */
  string_set_init (&format_set);
  output_get_supported_formats (&format_set);

  /* Converted supported formats to sorted array. */
  string_array_init (&format_array);
  STRING_SET_FOR_EACH (format, node, &format_set)
    string_array_append (&format_array, format);
  string_array_sort (&format_array);
  string_set_destroy (&format_set);

  /* Converted supported formats to string. */
  format_string = string_array_join (&format_array, " ");
  string_array_destroy (&format_array);
  return format_string;
}

static void
usage (void)
{
  char *supported_formats = get_supported_formats ();
  char *inc_path = string_array_join (include_path_default (), " ");

  printf (_("\
PSPP, a program for statistical analysis of sampled data.\n\
Usage: %s [OPTION]... FILE...\n\
\n\
Arguments to long options also apply to equivalent short options.\n\
\n\
Output options:\n\
  -o, --output=FILE         output to FILE, default format from FILE's name\n\
  -O format=FORMAT          override format for previous -o\n\
  -O OPTION=VALUE           set output option to customize previous -o\n\
  -O device={terminal|listing}  override device type for previous -o\n\
  -e, --error-file=FILE     append errors, warnings, and notes to FILE\n\
  --no-output               disable default output driver\n\
Supported output formats: %s\n\
\n\
Language options:\n\
  -I, --include=DIR         append DIR to search path\n\
  -I-, --no-include         clear search path\n\
  -r, --no-statrc           disable running rc file at startup\n\
  -a, --algorithm={compatible|enhanced}\n\
                            set to `compatible' if you want output\n\
                            calculated from broken algorithms\n\
  -x, --syntax={compatible|enhanced}\n\
                            set to `compatible' to disable PSPP extensions\n\
  -b, --batch               interpret syntax in batch mode\n\
  -i, --interactive         interpret syntax in interactive mode\n\
  --syntax-encoding=ENCODING  specify encoding for syntax files\n\
  -s, --safer               don't allow some unsafe operations\n\
Default search path: %s\n\
\n\
Informative output:\n\
  -h, --help                display this help and exit\n\
  -V, --version             output version information and exit\n\
\n\
Non-option arguments are interpreted as syntax files to execute.\n"),
          program_name, supported_formats, inc_path);

  free (supported_formats);
  free (inc_path);

  emit_bug_reporting_address ();
  exit (EXIT_SUCCESS);
}

static void
terminal_option_callback (int id, void *to_)
{
  struct terminal_opts *to = to_;

  switch (id)
    {
    case OPT_TESTING_MODE:
      settings_set_testing_mode (true);
      break;

    case OPT_ERROR_FILE:
      if (!strcmp (optarg, "none") || msglog_create (optarg))
        to->has_error_file = true;
      break;

    case OPT_OUTPUT:
      register_output_driver (to);
      string_map_insert (&to->options, "output-file", optarg);
      break;

    case OPT_OUTPUT_OPTION:
      parse_output_option (to, optarg);
      break;

    case OPT_NO_OUTPUT:
      /* Pretend that we already have an output driver, which disables adding
         one in terminal_opts_done() when we don't already have one. */
      to->has_output_driver = true;
      break;

    case OPT_BATCH:
      *to->syntax_mode = LEX_SYNTAX_BATCH;
      break;

    case OPT_INTERACTIVE:
      *to->syntax_mode = LEX_SYNTAX_INTERACTIVE;
      break;

    case OPT_SYNTAX_ENCODING:
      *to->syntax_encoding = optarg;
      break;

    case OPT_NO_STATRC:
      *to->process_statrc = false;
      break;

    case OPT_HELP:
      usage ();
      exit (EXIT_SUCCESS);

    case OPT_VERSION:
      version_etc (stdout, "pspp", PACKAGE_NAME, PACKAGE_VERSION,
                   "Ben Pfaff", "John Darrington", "Jason Stover",
                   NULL_SENTINEL);
      exit (EXIT_SUCCESS);

    default:
      NOT_REACHED ();
    }
}

struct terminal_opts *
terminal_opts_init (struct argv_parser *ap,
                    enum lex_syntax_mode *syntax_mode, bool *process_statrc,
                    char **syntax_encoding)
{
  struct terminal_opts *to;

  *syntax_mode = LEX_SYNTAX_AUTO;
  *process_statrc = true;
  *syntax_encoding = "Auto";

  to = xzalloc (sizeof *to);
  to->syntax_mode = syntax_mode;
  string_map_init (&to->options);
  to->has_output_driver = false;
  to->has_error_file = false;
  to->syntax_mode = syntax_mode;
  to->process_statrc = process_statrc;
  to->syntax_encoding = syntax_encoding;

  argv_parser_add_options (ap, terminal_argv_options, N_TERMINAL_OPTIONS,
                           terminal_option_callback, to);
  return to;
}

/* Return true iff the terminal appears to be an xterm with 
   UTF-8 capabilities */
static bool
term_is_utf8_xterm (void)
{
  char *s = NULL;

  if ( (s = getenv ("TERM")) && (0 == strcmp ("xterm", s)) )
    if ( (s = getenv ("XTERM_LOCALE")) )
      return strcasestr (s, "utf8") || strcasestr (s, "utf-8");

  return false;
}

void
terminal_opts_done (struct terminal_opts *to, int argc, char *argv[])
{
  register_output_driver (to);
  if (!to->has_output_driver)
    {
      if ((0 == strcmp (locale_charset (), "UTF-8"))
	  ||
	  (term_is_utf8_xterm ()) )
	{
	  string_map_insert (&to->options, "box", "unicode");
	}
  
      string_map_insert (&to->options, "output-file", "-");
      string_map_insert (&to->options, "format", "txt");
      register_output_driver (to);
    }

  if (!to->has_terminal_driver && !to->has_error_file)
    msglog_create ("-");

  string_map_destroy (&to->options);
  free (to);
}
