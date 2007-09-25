/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

#include "msg-ui.h"

#include "linebreak.h"
#include "localcharset.h"

#include <libpspp/msg-locator.h>
#include <libpspp/getl.h>
#include <data/settings.h>
#include <libpspp/message.h>
#include <libpspp/str.h>
#include <output/journal.h>
#include <output/output.h>
#include <output/table.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

typedef void write_line_func (int indent, struct substring line, void *aux);
static void dump_message (char *msg, unsigned width, unsigned indent,
                          write_line_func *, void *aux);
static write_line_func write_stream;
static write_line_func write_journal;

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
    dump_message (ds_cstr (&string),
                  isatty (fileno (msg_file)) ? get_viewwidth () : INT_MAX, 8,
                  write_stream, msg_file);

  dump_message (ds_cstr (&string), 78, 0, write_journal, NULL);

  if (get_error_routing_to_listing ())
    {
      /* Disable screen output devices, because the error should
         already have been reported to the screen with the
         dump_message call above. */
      outp_enable_device (false, OUTP_DEV_SCREEN);
      tab_output_text (TAB_LEFT, ds_cstr (&string));
      outp_enable_device (true, OUTP_DEV_SCREEN);
    }

  ds_destroy (&string);
}

/* Divides MSG into lines of WIDTH width for the first line and
   WIDTH - INDENT width for each succeeding line, and writes the
   lines by calling DUMP_LINE for each line, passing AUX as
   auxiliary data. */
static void
dump_message (char *msg, unsigned width, unsigned indent,
              write_line_func *dump_line, void *aux)
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
      dump_line (0, ss_cstr (msg), aux);
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
    if (breaks[i] == UC_BREAK_POSSIBLE || breaks[i] == UC_BREAK_MANDATORY)
      {
        dump_line (line_indent,
                   ss_buffer (string + line_start, i - line_start), aux);
        line_indent = indent;

        /* UC_BREAK_POSSIBLE means that a line break can be
           inserted, and that the character should be included
           in the next line.
           UC_BREAK_MANDATORY means that this character is a line
           break, so it should not be included in the next line. */
        line_start = i + (breaks[i] == UC_BREAK_MANDATORY);
      }
  if (line_start < length)
    dump_line (line_indent,
               ss_buffer (string + line_start, length - line_start), aux);

  free (string);
  free (breaks);
}

/* Write LINE_INDENT spaces, LINE, then a new-line to STREAM. */
static void
write_stream (int line_indent, struct substring line, void *stream_)
{
  FILE *stream = stream_;
  int i;
  for (i = 0; i < line_indent; i++)
    putc (' ', stream);
  fwrite (ss_data (line), 1, ss_length (line), stream);
  putc ('\n', stream);
}

/* Writes LINE to the journal. */
static void
write_journal (int line_indent, struct substring line, void *unused UNUSED)
{
  char *s = xstrndup (ss_data (line), ss_length (line));
  journal_write (true, s);
  free (s);
}
