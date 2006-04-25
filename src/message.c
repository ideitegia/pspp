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
#include <libpspp/message.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <libpspp/alloc.h>
#include <data/file-name.h>
#include <language/line-buffer.h>
#include <language/lexer/lexer.h>
#include <data/settings.h>
#include <ui/terminal/read-line.h>
#include <libpspp/version.h>
#include "exit.h"
#include "linebreak.h"
#include "progname.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

int err_error_count;
int err_warning_count;

int err_already_flagged;

int err_verbosity;

static char *command_name;

/* Public functions. */

/* Writes error message in CLASS, with text FORMAT, formatted with
   printf, to the standard places. */
void
msg (enum msg_class class, const char *format, ...)
{
  struct msg m;
  va_list args;

  m.category = msg_class_to_category (class);
  m.severity = msg_class_to_severity (class);
  msg_location (&m.where);
  va_start (args, format);
  m.text = xvasprintf (format, args);
  va_end (args);

  msg_emit (&m);
}

/* Writes MESSAGE formatted with printf, to stderr, if the
   verbosity level is at least LEVEL. */
void
verbose_msg (int level, const char *format, ...)
{
  if (err_verbosity >= level)
    {
      va_list args;
  
      va_start (args, format);
      fprintf (stderr, "%s: ", program_name);
      vfprintf (stderr, format, args);
      putc ('\n', stderr);
      va_end (args);
    }
}

/* Checks whether we've had so many errors that it's time to quit
   processing this syntax file. */
void
err_check_count (void)
{
  if (get_errorbreak() && err_error_count)
    msg (MN, _("Terminating execution of syntax file due to error."));
  else if (err_error_count > get_mxerrs() )
    msg (MN, _("Errors (%d) exceeds limit (%d)."),
	 err_error_count, get_mxerrs());
  else if (err_error_count + err_warning_count > get_mxwarns() )
    msg (MN, _("Warnings (%d) exceed limit (%d)."),
	 err_error_count + err_warning_count, get_mxwarns() );
  else
    return;

  getl_abort_noninteractive ();
}

static void puts_stdout (int line_indent, const char *line, size_t length);
static void dump_message (char *msg,
                          void (*func) (int line_indent,
                                        const char *line, size_t length),
                          unsigned width, unsigned indent);

void
msg_done (void) 
{
  lex_done();
  getl_uninitialize ();
  readln_uninitialize();
}

/* Emits E as an error message.
   Frees `text' member in E. */
void
msg_emit (const struct msg *m)
{
  struct category 
    {
      bool show_command_name;   /* Show command name with error? */
      bool show_file_location;  /* Show syntax file location? */
    };

  static const struct category categories[] = 
    {
      {false, false},           /* MSG_GENERAL. */
      {true, true},             /* MSG_SYNTAX. */
      {false, true},            /* MSG_DATA. */
    };

  struct severity 
    {
      const char *name;         /* How to identify this severity. */
      int *count;               /* Number of msgs with this severity so far. */
    };
  
  static struct severity severities[] = 
    {
      {N_("error"), &err_error_count},          /* MSG_ERROR. */
      {N_("warning"), &err_warning_count},      /* MSG_WARNING. */
      {NULL, NULL},                             /* MSG_NOTE. */
    };

  const struct category *category = &categories[m->category];
  const struct severity *severity = &severities[m->severity];
  struct string string = DS_INITIALIZER;

  if (category->show_file_location && m->where.file_name)
    {
      ds_printf (&string, "%s:", m->where.file_name);
      if (m->where.line_number != -1)
	ds_printf (&string, "%d:", m->where.line_number);
      ds_putc (&string, ' ');
    }

  if (severity->name != NULL)
    ds_printf (&string, "%s: ", gettext (severity->name));
  
  if (severity->count != NULL)
    ++*severity->count;
  
  if (category->show_command_name && command_name != NULL)
    ds_printf (&string, "%s: ", command_name);

  ds_puts (&string, m->text);

  /* FIXME: Check set_messages and set_errors to determine where to
     send errors and messages. */
  dump_message (ds_c_str (&string), puts_stdout, get_viewwidth (), 8);

  ds_destroy (&string);
  free (m->text);
}

/* Private functions. */

/* Write LINE_INDENT spaces, the LENGTH characters in LINE, then
   a new-line to stdout. */
static void puts_stdout (int line_indent,
                         const char *line, size_t length)
{
  int i;
  for (i = 0; i < line_indent; i++)
    putchar (' ');
  fwrite (line, 1, length, stdout);
  putchar ('\n');
}

/* Divides MSG into lines of WIDTH width for the first line and
   WIDTH - INDENT width for each succeeding line.  Each line is
   passed to FUNC as a null-terminated string (no new-line
   character is included in the string). */
static void
dump_message (char *msg,
              void (*func) (int line_indent, const char *line, size_t length),
	      unsigned width, unsigned indent)
{
  size_t length = strlen (msg);
  char *string, *breaks;
  int line_indent;
  size_t line_start, i;

  /* Allocate temporary buffers.
     If we can't get memory for them, then just dump the whole
     message. */
  string = strdup (msg);
  breaks = malloc (length);
  if (string == NULL || breaks == NULL)
    {
      free (string);
      free (breaks);
      func (0, msg, length);
      return;
    }

  /* Break into lines. */
  if (indent > width / 3)
    indent = width / 3;
  mbs_width_linebreaks (string, length,
                        width - indent, -indent, 0,
                        NULL, locale_charset (), breaks);

  /* Pass lines to FUNC. */
  line_start = 0;
  line_indent = 0;
  for (i = 0; i < length; i++)
    switch (breaks[i]) 
      {
      case UC_BREAK_POSSIBLE:
        /* Break before this character,
           and include this character in the next line. */
        func (line_indent, &string[line_start], i - line_start);
        line_start = i;
        line_indent = indent;
        break;
      case UC_BREAK_MANDATORY:
        /* Break before this character,
           but don't include this character in the next line
           (because it'string a new-line). */
        func (line_indent, &string[line_start], i - line_start);
        line_start = i + 1;
        line_indent = indent;
        break;
      default:
        break;
      }
  if (line_start < length)
    func (line_indent, &string[line_start], length - line_start);

  free (string);
  free (breaks);
}

/* Sets COMMAND_NAME as the command name included in some kinds
   of error messages. */
void
msg_set_command_name (const char *command_name_) 
{
  free (command_name);
  command_name = command_name_ ? xstrdup (command_name_) : NULL;
}

void 
request_bug_report_and_abort(const char *msg )
{
  fprintf(stderr,
	  "******************************************************************\n"
	  "You have discovered a bug in PSPP.\n\n"
	  "  Please report this, by sending "
	  "an email to " PACKAGE_BUGREPORT ",\n"
	  "explaining what you were doing when this happened, and including\n"
	  "a sample of your input file which caused it.\n");

  fprintf(stderr,
	  "Also, please copy the following lines into your bug report:\n\n"
	  "bare_version:        %s\n" 
	  "version:             %s\n"
	  "stat_version:        %s\n"
	  "host_system:         %s\n"
	  "build_system:        %s\n"
	  "default_config_path: %s\n"
	  "include_path:        %s\n"
	  "groff_font_path:     %s\n"
	  "locale_dir:          %s\n"
	  "compiler version:    %s\n"
	  ,

	  bare_version,         
	  version,
	  stat_version,
	  host_system,        
	  build_system,
	  default_config_path,
	  include_path, 
	  groff_font_path,
	  locale_dir,
#ifdef __VERSION__
	  __VERSION__
#else
	  "Unknown"
#endif
	  );     

  if ( msg )
    fprintf(stderr,"Diagnosis: %s\n",msg);

  fprintf(stderr,
    "******************************************************************\n");

  abort();
}

void 
msg_assert_fail(const char *expr, const char *file, int line)
{
  char msg[256];
  snprintf(msg,256,"Assertion failed: %s:%d; (%s)",file,line,expr);
  request_bug_report_and_abort( msg );
}

