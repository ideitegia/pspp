/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007 Free Software Foundation, Inc.

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
#include <libpspp/i18n.h>

#include "error.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static int *viewlength = NULL;
static int *viewwidth = NULL;
static bool long_view = false;

static bool safer_mode = false;

static bool do_echo = false;
static bool include = true;

static int epoch = -1;

static bool errorbreak = false;

static bool route_errors_to_terminal = true;
static bool route_errors_to_listing = true;

static bool scompress = true;

static bool undefined = true;
static double blanks = SYSMIS;

static int mxwarns = 100;
static int mxerrs = 100;

static bool printback = true;
static bool mprint = true;

static int mxloops = 1;

static bool nulline = true;

static char endcmd = '.';

static size_t workspace = 4L * 1024 * 1024;

static struct fmt_spec default_format = {FMT_F, 8, 2};

static bool testing_mode = false;

static int global_algorithm = ENHANCED;
static int cmd_algorithm = ENHANCED;
static int *algorithm = &global_algorithm;

static int syntax = ENHANCED;

static void init_viewport (int *, int *);

void
settings_init (int *width, int *length)
{
  init_viewport (width, length);
  i18n_init ();
}

void
settings_done (void)
{
  i18n_done ();
}

/* Screen length in lines. */
int
get_viewlength (void)
{
  return *viewlength;
}

/* Sets the view length. */
void
set_viewlength (int viewlength_)
{
  *viewlength = viewlength_;
}

/* Set view width to a very long value, and prevent it from ever
   changing. */
void
force_long_view (void)
{
  long_view = true;
}

/* Screen width. */
int
get_viewwidth(void)
{
  if (long_view)
    return 9999;

  return *viewwidth;
}

/* Sets the screen width. */
void
set_viewwidth (int viewwidth_)
{
  *viewwidth = viewwidth_;
}

static void
init_viewport (int  *width, int *length)
{
  viewwidth = width;
  viewlength = length;

  if (long_view)
    return;

}

/* Whether PSPP can erase and overwrite files. */
bool
get_safer_mode (void)
{
  return safer_mode;
}

/* Set safer mode. */
void
set_safer_mode (void)
{
  safer_mode = true;
}

/* Echo commands to the listing file/printer? */
bool
get_echo (void)
{
  return do_echo;
}

/* Set echo. */
void
set_echo (bool echo_)
{
  do_echo = echo_;
}

/* If echo is on, whether commands from include files are echoed. */
bool
get_include (void)
{
  return include;
}

/* Set include file echo. */
void
set_include (bool include_)
{
  include = include_;
}

/* What year to use as the start of the epoch. */
int
get_epoch (void)
{
  if (epoch < 0)
    {
      time_t t = time (0);
      struct tm *tm = localtime (&t);
      epoch = (tm != NULL ? tm->tm_year + 1900 : 2000) - 69;
    }

  return epoch;
}

/* Sets the year that starts the epoch. */
void
set_epoch (int epoch_)
{
  epoch = epoch_;
}

/* Does an error stop execution? */
bool
get_errorbreak (void)
{
  return errorbreak;
}

/* Sets whether an error stops execution. */
void
set_errorbreak (bool errorbreak_)
{
  errorbreak = errorbreak_;
}

/* Route error messages to terminal? */
bool
get_error_routing_to_terminal (void)
{
  return route_errors_to_terminal;
}

/* Sets whether error messages should be routed to the
   terminal. */
void
set_error_routing_to_terminal (bool route_to_terminal)
{
  route_errors_to_terminal = route_to_terminal;
}

/* Route error messages to listing file? */
bool
get_error_routing_to_listing (void)
{
  return route_errors_to_listing;
}

/* Sets whether error messages should be routed to the
   listing file. */
void
set_error_routing_to_listing (bool route_to_listing)
{
  route_errors_to_listing = route_to_listing;
}

/* Compress system files by default? */
bool
get_scompression (void)
{
  return scompress;
}

/* Set system file default compression. */
void
set_scompression (bool scompress_)
{
  scompress = scompress_;
}

/* Whether to warn on undefined values in numeric data. */
bool
get_undefined (void)
{
  return undefined;
}

/* Set whether to warn on undefined values. */
void
set_undefined (bool undefined_)
{
  undefined = undefined_;
}

/* The value that blank numeric fields are set to when read in. */
double
get_blanks (void)
{
  return blanks;
}

/* Set the value that blank numeric fields are set to when read
   in. */
void
set_blanks (double blanks_)
{
  blanks = blanks_;
}

/* Maximum number of warnings + errors. */
int
get_mxwarns (void)
{
  return mxwarns;
}

/* Sets maximum number of warnings + errors. */
void
set_mxwarns (int mxwarns_)
{
  mxwarns = mxwarns_;
}

/* Maximum number of errors. */
int
get_mxerrs (void)
{
  return mxerrs;
}

/* Sets maximum number of errors. */
void
set_mxerrs (int mxerrs_)
{
  mxerrs = mxerrs_;
}

/* Whether commands are written to the display. */
bool
get_printback (void)
{
  return printback;
}

/* Sets whether commands are written to the display. */
void
set_printback (bool printback_)
{
  printback = printback_;
}

/* Independent of get_printback, controls whether the commands
   generated by macro invocations are displayed. */
bool
get_mprint (void)
{
  return mprint;
}

/* Sets whether the commands generated by macro invocations are
   displayed. */
void
set_mprint (bool mprint_)
{
  mprint = mprint_;
}

/* Implied limit of unbounded loop. */
int
get_mxloops (void)
{
  return mxloops;
}

/* Set implied limit of unbounded loop. */
void
set_mxloops (int mxloops_)
{
  mxloops = mxloops_;
}

/* Whether a blank line is a command terminator. */
bool
get_nulline (void)
{
  return nulline;
}

/* Set whether a blank line is a command terminator. */
void
set_nulline (bool nulline_)
{
  nulline = nulline_;
}

/* The character used to terminate commands. */
char
get_endcmd (void)
{
  return endcmd;
}

/* Set the character used to terminate commands. */
void
set_endcmd (char endcmd_)
{
  endcmd = endcmd_;
}

/* Approximate maximum amount of memory to use for cases, in
   bytes. */
size_t
get_workspace (void)
{
  return workspace;
}

/* Approximate maximum number of cases to allocate in-core, given
   that each case contains VALUE_CNT values. */
size_t
get_workspace_cases (size_t value_cnt)
{
  size_t case_size = sizeof (union value) * value_cnt + 4 * sizeof (void *);
  size_t case_cnt = MAX (get_workspace () / case_size, 4);
  return case_cnt;
}

/* Set approximate maximum amount of memory to use for cases, in
   bytes. */

void
set_workspace (size_t workspace_)
{
  workspace = workspace_;
}

/* Default format for variables created by transformations and by
   DATA LIST {FREE,LIST}. */
const struct fmt_spec *
get_format (void)
{
  return &default_format;
}

/* Set default format for variables created by transformations
   and by DATA LIST {FREE,LIST}. */
void
set_format (const struct fmt_spec *default_format_)
{
  default_format = *default_format_;
}

/* Are we in testing mode?  (e.g. --testing-mode command line
   option) */
bool
get_testing_mode (void)
{
  return testing_mode;
}

/* Set testing mode. */
void
set_testing_mode (bool testing_mode_)
{
  testing_mode = testing_mode_;
}

/* Return the current algorithm setting */
enum behavior_mode
get_algorithm (void)
{
  return *algorithm;
}

/* Set the algorithm option globally. */
void
set_algorithm (enum behavior_mode mode)
{
  global_algorithm = mode;
}

/* Set the algorithm option for this command only */
void
set_cmd_algorithm (enum behavior_mode mode)
{
  cmd_algorithm = mode;
  algorithm = &cmd_algorithm;
}

/* Unset the algorithm option for this command */
void
unset_cmd_algorithm (void)
{
  algorithm = &global_algorithm;
}

/* Get the current syntax setting */
enum behavior_mode
get_syntax (void)
{
  return syntax;
}

/* Set the syntax option */
void
set_syntax (enum behavior_mode mode)
{
  syntax = mode;
}
