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

#include "msg-ui.h"

#include "linebreak.h"

#include <libpspp/msg-locator.h>
#include <libpspp/getl.h>
#include <data/settings.h>
#include <libpspp/message.h>
#include <libpspp/str.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Number of errors, warnings reported. */
static int error_count;
static int warning_count;
static const char *error_file;

static void handle_msg (const struct msg *);

static FILE *msg_file ;

void 
msg_ui_set_error_file (const char *filename)
{
  error_file = filename;
}

void
msg_ui_init (struct source_stream *ss) 
{
  msg_file = stdout;

  if ( error_file ) 
    {
      msg_file = fopen (error_file, "a");
      if ( NULL == msg_file ) 
	{
	  int err = errno;
	  printf ( _("Cannot open %s (%s). "
		     "Writing errors to stdout instead.\n"), 
		   error_file, strerror(err) );
	  msg_file = stdout;
	}
    }
  msg_init (ss, handle_msg);
}

void
msg_ui_done (void) 
{
  msg_done ();
  msg_locator_done ();
  
  if ( msg_file ) /* FIXME: do we really want to close stdout ?? */
    fclose (msg_file);
}

/* Checks whether we've had so many errors that it's time to quit
   processing this syntax file. */
void
check_msg_count (struct source_stream *ss)
{
  if (!getl_is_interactive (ss)) 
    {
      if (get_errorbreak () && error_count)
        msg (MN, _("Terminating execution of syntax file due to error."));
      else if (error_count > get_mxerrs() )
        msg (MN, _("Errors (%d) exceeds limit (%d)."),
             error_count, get_mxerrs());
      else if (error_count + warning_count > get_mxwarns() )
        msg (MN, _("Warnings (%d) exceed limit (%d)."),
             error_count + warning_count, get_mxwarns() );
      else
        return;

      getl_abort_noninteractive (ss); 
    }
}

void
reset_msg_count (void) 
{
  error_count = warning_count = 0;
}

bool
any_errors (void) 
{
  return error_count > 0;
}

static void dump_message (char *msg, unsigned width, unsigned indent, FILE *);
static void dump_line (int line_indent, const char *line, size_t length,
                       FILE *);

static void
handle_msg (const struct msg *m)
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
      {N_("error"), &error_count},          /* MSG_ERROR. */
      {N_("warning"), &warning_count},      /* MSG_WARNING. */
      {NULL, NULL},                         /* MSG_NOTE. */
    };

  const struct category *category = &categories[m->category];
  const struct severity *severity = &severities[m->severity];
  struct string string = DS_EMPTY_INITIALIZER;

  if (category->show_file_location && m->where.file_name)
    {
      ds_put_format (&string, "%s:", m->where.file_name);
      if (m->where.line_number != -1)
	ds_put_format (&string, "%d:", m->where.line_number);
      ds_put_char (&string, ' ');
    }

  if (severity->name != NULL)
    ds_put_format (&string, "%s: ", gettext (severity->name));
  
  if (severity->count != NULL)
    ++*severity->count;
  
  if (category->show_command_name && msg_get_command_name () != NULL)
    ds_put_format (&string, "%s: ", msg_get_command_name ());

  ds_put_cstr (&string, m->text);

  if (msg_file != stdout || get_error_routing_to_terminal ())
    dump_message (ds_cstr (&string), get_viewwidth (), 8, msg_file);

  ds_destroy (&string);
}

/* Divides MSG into lines of WIDTH width for the first line and
   WIDTH - INDENT width for each succeeding line, and writes the
   lines to STREAM. */
static void
dump_message (char *msg, unsigned width, unsigned indent, FILE *stream)
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
      fputs (msg, stream);
      putc ('\n', stream);
      return;
    }

  /* Break into lines. */
  if (indent > width / 3)
    indent = width / 3;
  mbs_width_linebreaks (string, length,
                        width - indent, -indent, 0,
                        NULL, locale_charset (), breaks);

  /* Write out lines. */
  line_start = 0;
  line_indent = 0;
  for (i = 0; i < length; i++)
    switch (breaks[i]) 
      {
      case UC_BREAK_POSSIBLE:
        /* Break before this character,
           and include this character in the next line. */
        dump_line (line_indent, &string[line_start], i - line_start, stream);
        line_start = i;
        line_indent = indent;
        break;
      case UC_BREAK_MANDATORY:
        /* Break before this character,
           but don't include this character in the next line
           (because it'string a new-line). */
        dump_line (line_indent, &string[line_start], i - line_start, stream);
        line_start = i + 1;
        line_indent = indent;
        break;
      default:
        break;
      }
  if (line_start < length)
    dump_line (line_indent, &string[line_start], length - line_start, stream);

  free (string);
  free (breaks);
}

/* Write LINE_INDENT spaces, the LENGTH characters in LINE, then
   a new-line to STREAM. */
static void
dump_line (int line_indent, const char *line, size_t length, FILE *stream)
{
  int i;
  for (i = 0; i < line_indent; i++)
    putc (' ', stream);
  fwrite (line, 1, length, stream);
  putc ('\n', stream);
}

