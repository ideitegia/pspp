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
#include "settings.h"
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include "format.h"
#include "value.h"
#include "xalloc.h"
#include <data/case.h>
#include <libpspp/i18n.h>
#include <libpspp/integer-format.h>
#include <libpspp/message.h>

#include "error.h"
#include "minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static int global_algorithm = ENHANCED;

struct settings
{
  /* Integer format used for IB and PIB input. */
  enum integer_format input_integer_format;

  /* Floating-point format used for RB and RBHEX input. */
  enum float_format input_float_format;

  /* Format of integers in output (SET WIB). */
  enum integer_format output_integer_format;

  /* Format of reals in output (SET WRB). */
  enum float_format output_float_format;

  int *viewlength;
  int *viewwidth;
  bool safer_mode;
  bool include;
  int epoch;
  bool route_errors_to_terminal;
  bool route_errors_to_listing;
  bool scompress;
  bool undefined;
  double blanks;
  int max_messages[MSG_N_SEVERITIES];
  bool printback;
  bool mprint;
  int mxloops;
  bool nulline;
  char endcmd;
  size_t workspace;
  struct fmt_spec default_format;
  bool testing_mode;

  int cmd_algorithm;
  int *algorithm;
  int syntax;

  struct fmt_settings *styles;

  enum settings_output_devices output_routing[SETTINGS_N_OUTPUT_TYPES];
};

static struct settings the_settings = {
  INTEGER_NATIVE,               /* input_integer_format */
  FLOAT_NATIVE_DOUBLE,          /* input_float_format */
  INTEGER_NATIVE,               /* output_integer_format */
  FLOAT_NATIVE_DOUBLE,          /* output_float_format */
  NULL,                         /* viewlength */
  NULL,                         /* viewwidth */
  false,                        /* safer_mode */
  true,                         /* include */
  -1,                           /* epoch */
  true,                         /* route_errors_to_terminal */
  true,                         /* route_errors_to_listing */
  true,                         /* scompress */
  true,                         /* undefined */
  SYSMIS,                       /* blanks */

  /* max_messages */
  {
    100,                        /* MSG_S_ERROR */
    100,                        /* MSG_S_WARNING */
    100                         /* MSG_S_NOTE */
  },

  true,                         /* printback */
  true,                         /* mprint */
  1,                            /* mxloops */
  true,                         /* nulline */
  '.',                          /* endcmd */
  64L * 1024 * 1024,            /* workspace */
  {FMT_F, 8, 2},                /* default_format */
  false,                        /* testing_mode */
  ENHANCED,                     /* cmd_algorithm */
  &global_algorithm,            /* algorithm */
  ENHANCED,                     /* syntax */
  NULL,                         /* styles */

  /* output_routing */
  {SETTINGS_DEVICE_LISTING | SETTINGS_DEVICE_TERMINAL,
   SETTINGS_DEVICE_LISTING | SETTINGS_DEVICE_TERMINAL,
   0,
   SETTINGS_DEVICE_LISTING | SETTINGS_DEVICE_TERMINAL}
};

static void init_viewport ( int *, int *);

void
settings_init (int *width, int *length)
{
  init_viewport (width, length);
  settings_set_epoch (-1);
  the_settings.styles = fmt_settings_create ();

  settings_set_decimal_char (get_system_decimal ());
}

void
settings_done (void)
{
  fmt_settings_destroy (the_settings.styles);
}

/* Returns the floating-point format used for RB and RBHEX
   input. */
enum float_format
settings_get_input_float_format (void)
{
  return the_settings.input_float_format;
}

/* Sets the floating-point format used for RB and RBHEX input to
   FORMAT. */
void
settings_set_input_float_format ( enum float_format format)
{
  the_settings.input_float_format = format;
}

/* Returns the integer format used for IB and PIB input. */
enum integer_format
settings_get_input_integer_format (void)
{
  return the_settings.input_integer_format;
}

/* Sets the integer format used for IB and PIB input to
   FORMAT. */
void
settings_set_input_integer_format ( enum integer_format format)
{
  the_settings.input_integer_format = format;
}

/* Returns the current output integer format. */
enum integer_format
settings_get_output_integer_format (void)
{
  return the_settings.output_integer_format;
}

/* Sets the output integer format to INTEGER_FORMAT. */
void
settings_set_output_integer_format (
			   enum integer_format integer_format)
{
  the_settings.output_integer_format = integer_format;
}

/* Returns the current output float format. */
enum float_format
settings_get_output_float_format (void)
{
  return the_settings.output_float_format;
}

/* Sets the output float format to FLOAT_FORMAT. */
void
settings_set_output_float_format ( enum float_format float_format)
{
  the_settings.output_float_format = float_format;
}

/* Screen length in lines. */
int
settings_get_viewlength (void)
{
  return *the_settings.viewlength;
}

/* Sets the view length. */
void
settings_set_viewlength ( int viewlength_)
{
  *the_settings.viewlength = viewlength_;
}

/* Screen width. */
int
settings_get_viewwidth(void)
{
  return *the_settings.viewwidth;
}

/* Sets the screen width. */
void
settings_set_viewwidth ( int viewwidth_)
{
  *the_settings.viewwidth = viewwidth_;
}

static void
init_viewport ( int  *width, int *length)
{
  the_settings.viewwidth = width;
  the_settings.viewlength = length;
}

/* Whether PSPP can erase and overwrite files. */
bool
settings_get_safer_mode (void)
{
  return the_settings.safer_mode;
}

/* Set safer mode. */
void
settings_set_safer_mode (void)
{
  the_settings.safer_mode = true;
}

/* If echo is on, whether commands from include files are echoed. */
bool
settings_get_include (void)
{
  return the_settings.include;
}

/* Set include file echo. */
void
settings_set_include ( bool include)
{
  the_settings.include = include;
}

/* What year to use as the start of the epoch. */
int
settings_get_epoch (void)
{
  assert (the_settings.epoch >= 0);

  return the_settings.epoch;
}

/* Sets the year that starts the epoch. */
void
settings_set_epoch ( int epoch)
{
  if (epoch < 0)
    {
      time_t t = time (0);
      struct tm *tm = localtime (&t);
      epoch = (tm != NULL ? tm->tm_year + 1900 : 2000) - 69;
    }

  the_settings.epoch = epoch;
  assert (the_settings.epoch >= 0);
}

/* Compress system files by default? */
bool
settings_get_scompression (void)
{
  return the_settings.scompress;
}

/* Set system file default compression. */
void
settings_set_scompression ( bool scompress)
{
  the_settings.scompress = scompress;
}

/* Whether to warn on undefined values in numeric data. */
bool
settings_get_undefined (void)
{
  return the_settings.undefined;
}

/* Set whether to warn on undefined values. */
void
settings_set_undefined ( bool undefined)
{
  the_settings.undefined = undefined;
}

/* The value that blank numeric fields are set to when read in. */
double
settings_get_blanks (void)
{
  return the_settings.blanks;
}

/* Set the value that blank numeric fields are set to when read
   in. */
void
settings_set_blanks ( double blanks)
{
  the_settings.blanks = blanks;
}

/* Returns the maximum number of messages to show of the given SEVERITY before
   aborting.  (The value for MSG_S_WARNING is interpreted as maximum number of
   warnings and errors combined.) */
int
settings_get_max_messages (enum msg_severity severity)
{
  assert (severity < MSG_N_SEVERITIES);
  return the_settings.max_messages[severity];
}

/* Sets the maximum number of messages to show of the given SEVERITY before
   aborting to MAX.  (The value for MSG_S_WARNING is interpreted as maximum
   number of warnings and errors combined.)  In addition, in the case of 
   warnings the special value of zero indicates that no warnings are to be
   issued. 
*/
void
settings_set_max_messages (enum msg_severity severity, int max)
{
  assert (severity < MSG_N_SEVERITIES);

  if (severity == MSG_S_WARNING)
    {
      if ( max == 0)
	{
	  msg (MW,
	       _("MXWARNS set to zero.  No further warnings will be given even when potentially problematic situations are encountered."));
	  msg_ui_disable_warnings (true);
	}
      else if ( the_settings.max_messages [MSG_S_WARNING] == 0)
	{
	  msg_ui_disable_warnings (false);
	  the_settings.max_messages[MSG_S_WARNING] = max;
	  msg (MW, _("Warnings re-enabled. %d warnings will be issued before aborting syntax processing."), max);
	}
    }

  the_settings.max_messages[severity] = max;
}

/* Independent of get_printback, controls whether the commands
   generated by macro invocations are displayed. */
bool
settings_get_mprint (void)
{
  return the_settings.mprint;
}

/* Sets whether the commands generated by macro invocations are
   displayed. */
void
settings_set_mprint ( bool mprint)
{
  the_settings.mprint = mprint;
}

/* Implied limit of unbounded loop. */
int
settings_get_mxloops (void)
{
  return the_settings.mxloops;
}

/* Set implied limit of unbounded loop. */
void
settings_set_mxloops ( int mxloops)
{
  the_settings.mxloops = mxloops;
}

/* Whether a blank line is a command terminator. */
bool
settings_get_nulline (void)
{
  return the_settings.nulline;
}

/* Set whether a blank line is a command terminator. */
void
settings_set_nulline ( bool nulline)
{
  the_settings.nulline = nulline;
}

/* The character used to terminate commands. */
char
settings_get_endcmd (void)
{
  return the_settings.endcmd;
}

/* Set the character used to terminate commands. */
void
settings_set_endcmd ( char endcmd)
{
  the_settings.endcmd = endcmd;
}

/* Approximate maximum amount of memory to use for cases, in
   bytes. */
size_t
settings_get_workspace (void)
{
  return the_settings.workspace;
}

/* Approximate maximum number of cases to allocate in-core, given
   that each case has the format given in PROTO. */
size_t
settings_get_workspace_cases (const struct caseproto *proto)
{
  size_t n_cases = settings_get_workspace () / case_get_cost (proto);
  return MAX (n_cases, 4);
}

/* Set approximate maximum amount of memory to use for cases, in
   bytes. */

void
settings_set_workspace ( size_t workspace)
{
  the_settings.workspace = workspace;
}

/* Default format for variables created by transformations and by
   DATA LIST {FREE,LIST}. */
const struct fmt_spec *
settings_get_format (void)
{
  return &the_settings.default_format;
}

/* Set default format for variables created by transformations
   and by DATA LIST {FREE,LIST}. */
void
settings_set_format ( const struct fmt_spec *default_format)
{
  the_settings.default_format = *default_format;
}

/* Are we in testing mode?  (e.g. --testing-mode command line
   option) */
bool
settings_get_testing_mode (void)
{
  return the_settings.testing_mode;
}

/* Set testing mode. */
void
settings_set_testing_mode ( bool testing_mode)
{
  the_settings.testing_mode = testing_mode;
}

/* Return the current algorithm setting */
enum behavior_mode
settings_get_algorithm (void)
{
  return *the_settings.algorithm;
}

/* Set the algorithm option globally. */
void
settings_set_algorithm (enum behavior_mode mode)
{
  global_algorithm = mode;
}

/* Set the algorithm option for this command only */
void
settings_set_cmd_algorithm ( enum behavior_mode mode)
{
  the_settings.cmd_algorithm = mode;
  the_settings.algorithm = &the_settings.cmd_algorithm;
}

/* Unset the algorithm option for this command */
void
unset_cmd_algorithm (void)
{
  the_settings.algorithm = &global_algorithm;
}

/* Get the current syntax setting */
enum behavior_mode
settings_get_syntax (void)
{
  return the_settings.syntax;
}

/* Set the syntax option */
void
settings_set_syntax ( enum behavior_mode mode)
{
  the_settings.syntax = mode;
}



/* Find the grouping characters in CC_STRING and set CC's
   grouping and decimal members appropriately.  Returns true if
   successful, false otherwise. */
static bool
find_cc_separators (const char *cc_string, struct fmt_number_style *cc)
{
  const char *sp;
  int comma_cnt, dot_cnt;

  /* Count commas and periods.  There must be exactly three of
     one or the other, except that an apostrophe escapes a
     following comma or period. */
  comma_cnt = dot_cnt = 0;
  for (sp = cc_string; *sp; sp++)
    if (*sp == ',')
      comma_cnt++;
    else if (*sp == '.')
      dot_cnt++;
    else if (*sp == '\'' && (sp[1] == '.' || sp[1] == ',' || sp[1] == '\''))
      sp++;

  if ((comma_cnt == 3) == (dot_cnt == 3))
    return false;

  if (comma_cnt == 3)
    {
      cc->decimal = '.';
      cc->grouping = ',';
    }
  else
    {
      cc->decimal = ',';
      cc->grouping = '.';
    }
  return true;
}

/* Extracts a token from IN into AFFIX, using BUFFER for storage.  BUFFER must
   have at least FMT_STYLE_AFFIX_MAX + 1 bytes of space.  Tokens are delimited
   by GROUPING.  The token is truncated to at most FMT_STYLE_AFFIX_MAX bytes,
   followed by a null terminator.  Returns the first character following the
   token. */
static const char *
extract_cc_token (const char *in, int grouping, struct substring *affix,
                  char buffer[FMT_STYLE_AFFIX_MAX + 1])
{
  size_t ofs = 0;

  for (; *in != '\0' && *in != grouping; in++)
    {
      if (*in == '\'' && in[1] == grouping)
        in++;
      if (ofs < FMT_STYLE_AFFIX_MAX)
        buffer[ofs++] = *in;
    }
  *affix = ss_buffer (buffer, ofs);

  if (*in == grouping)
    in++;
  return in;
}

/* Sets custom currency specifier CC having name CC_NAME ('A' through
   'E') to correspond to the settings in CC_STRING. */
bool
settings_set_cc (const char *cc_string, enum fmt_type type)
{
  char a[FMT_STYLE_AFFIX_MAX + 1];
  char b[FMT_STYLE_AFFIX_MAX + 1];
  char c[FMT_STYLE_AFFIX_MAX + 1];
  char d[FMT_STYLE_AFFIX_MAX + 1];
  struct fmt_number_style cc;

  assert (fmt_get_category (type) == FMT_CAT_CUSTOM);

  /* Determine separators. */
  if (!find_cc_separators (cc_string, &cc))
    {
      msg (SE, _("%s: Custom currency string `%s' does not contain "
                 "exactly three periods or commas (or it contains both)."),
           fmt_name (type), cc_string);
      return false;
    }

  cc_string = extract_cc_token (cc_string, cc.grouping, &cc.neg_prefix, a);
  cc_string = extract_cc_token (cc_string, cc.grouping, &cc.prefix, b);
  cc_string = extract_cc_token (cc_string, cc.grouping, &cc.suffix, c);
  cc_string = extract_cc_token (cc_string, cc.grouping, &cc.neg_suffix, d);

  fmt_settings_set_style (the_settings.styles, type, &cc);

  return true;
}

/* Returns the decimal point character for TYPE. */
int
settings_get_decimal_char (enum fmt_type type)
{
  return fmt_settings_get_style (the_settings.styles, type)->decimal;
}

void
settings_set_decimal_char (char decimal)
{
  fmt_settings_set_decimal (the_settings.styles, decimal);
}

/* Returns the number formatting style associated with the given
   format TYPE. */
const struct fmt_number_style *
settings_get_style (enum fmt_type type)
{
  assert (is_fmt_type (type));
  return fmt_settings_get_style (the_settings.styles, type);
}

/* Returns a string of the form "$#,###.##" according to FMT,
   which must be of type FMT_DOLLAR.  The caller must free the
   string. */
char *
settings_dollar_template (const struct fmt_spec *fmt)
{
  struct string str = DS_EMPTY_INITIALIZER;
  int c;
  const struct fmt_number_style *fns ;

  assert (fmt->type == FMT_DOLLAR);

  fns = fmt_settings_get_style (the_settings.styles, fmt->type);

  ds_put_char (&str, '$');
  for (c = MAX (fmt->w - fmt->d - 1, 0); c > 0; )
    {
      ds_put_char (&str, '#');
      if (--c % 4 == 0 && c > 0)
        {
          ds_put_char (&str, fns->grouping);
          --c;
        }
    }
  if (fmt->d > 0)
    {
      ds_put_char (&str, fns->decimal);
      ds_put_char_multiple (&str, '#', fmt->d);
    }

  return ds_cstr (&str);
}

void
settings_set_output_routing (enum settings_output_type type,
                             enum settings_output_devices devices)
{
  assert (type < SETTINGS_N_OUTPUT_TYPES);
  the_settings.output_routing[type] = devices;
}

enum settings_output_devices
settings_get_output_routing (enum settings_output_type type)
{
  assert (type < SETTINGS_N_OUTPUT_TYPES);
  return the_settings.output_routing[type] | SETTINGS_DEVICE_UNFILTERED;
}
