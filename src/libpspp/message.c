/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010 Free Software Foundation, Inc.

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

#include "message.h"
#include "msg-locator.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libpspp/str.h>
#include <libpspp/version.h>
#include <data/settings.h>

#include "gl/progname.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Message handler as set by msg_init(). */
static void (*msg_handler)  (const struct msg *);

/* Disables emitting messages if positive. */
static int messages_disabled;

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
  va_start (args, format);
  m.text = xvasprintf (format, args);
  m.where.file_name = NULL;
  m.where.line_number = 0;
  m.where.first_column = 0;
  m.where.last_column = 0;
  va_end (args);

  msg_emit (&m);
}

static struct source_stream *s_stream;

void
msg_init (struct source_stream *ss,  void (*handler) (const struct msg *) )
{
  s_stream = ss;
  msg_handler = handler;
}

void
msg_done (void)
{
}

/* Working with messages. */

/* Duplicate a message */
struct msg *
msg_dup (const struct msg *m)
{
  struct msg *new_msg;

  new_msg = xmemdup (m, sizeof *m);
  if (m->where.file_name != NULL)
    new_msg->where.file_name = xstrdup (m->where.file_name);
  new_msg->text = xstrdup (m->text);

  return new_msg;
}

/* Frees a message created by msg_dup().

   (Messages not created by msg_dup(), as well as their where.file_name
   members, are typically not dynamically allocated, so this function should
   not be used to destroy them.) */
void
msg_destroy (struct msg *m)
{
  free (m->where.file_name);
  free (m->text);
  free (m);
}

char *
msg_to_string (const struct msg *m, const char *command_name)
{
  const char *label;
  struct string s;

  ds_init_empty (&s);

  if (m->category != MSG_C_GENERAL
      && (m->where.file_name
          || m->where.line_number > 0
          || m->where.first_column > 0))
    {
      if (m->where.file_name)
        ds_put_format (&s, "%s", m->where.file_name);
      if (m->where.line_number > 0)
        {
          if (!ds_is_empty (&s))
            ds_put_char (&s, ':');
          ds_put_format (&s, "%d", m->where.line_number);
        }
      if (m->where.first_column > 0)
        {
          ds_put_format (&s, ".%d", m->where.first_column);
          if (m->where.last_column > m->where.first_column + 1)
            ds_put_format (&s, "-%d", m->where.last_column - 1);
        }
      ds_put_cstr (&s, ": ");
    }

  switch (m->severity)
    {
    case MSG_S_ERROR:
      label = _("error");
      break;
    case MSG_S_WARNING:
      label = _("warning");
      break;
    case MSG_S_NOTE:
    default:
      label = _("note");
      break;
    }
  ds_put_format (&s, "%s: ", label);

  if (m->category == MSG_C_SYNTAX && command_name != NULL)
    ds_put_format (&s, "%s: ", command_name);

  ds_put_cstr (&s, m->text);

  return ds_cstr (&s);
}


/* Number of messages reported, by severity level. */
static int counts[MSG_N_SEVERITIES];

/* True after the maximum number of errors or warnings has been exceeded. */
static bool too_many_errors;

/* True after the maximum number of notes has been exceeded. */
static bool too_many_notes;

/* True iff warnings have been explicitly disabled (MXWARNS = 0) */
static bool warnings_off = false;

/* Checks whether we've had so many errors that it's time to quit
   processing this syntax file. */
bool
msg_ui_too_many_errors (void)
{
  return too_many_errors;
}

void
msg_ui_disable_warnings (bool x)
{
  warnings_off = x;
}


void
msg_ui_reset_counts (void)
{
  int i;

  for (i = 0; i < MSG_N_SEVERITIES; i++)
    counts[i] = 0;
  too_many_errors = false;
  too_many_notes = false;
}

bool
msg_ui_any_errors (void)
{
  return counts[MSG_S_ERROR] > 0;
}

static void
submit_note (char *s)
{
  struct msg m;

  m.category = MSG_C_GENERAL;
  m.severity = MSG_S_NOTE;
  m.where.file_name = NULL;
  m.where.line_number = 0;
  m.where.first_column = 0;
  m.where.last_column = 0;
  m.text = s;
  msg_handler (&m);
  free (s);
}



static void
process_msg (const struct msg *m)
{
  int n_msgs, max_msgs;


  if (too_many_errors
      || (too_many_notes && m->severity == MSG_S_NOTE)
      || (warnings_off && m->severity == MSG_S_WARNING) )
    return;

  msg_handler (m);

  counts[m->severity]++;
  max_msgs = settings_get_max_messages (m->severity);
  n_msgs = counts[m->severity];
  if (m->severity == MSG_S_WARNING)
    n_msgs += counts[MSG_S_ERROR];
  if (n_msgs > max_msgs)
    {
      if (m->severity == MSG_S_NOTE)
        {
          too_many_notes = true;
          submit_note (xasprintf (_("Notes (%d) exceed limit (%d).  "
                                    "Suppressing further notes."),
                                  n_msgs, max_msgs));
        }
      else
        {
          too_many_errors = true;
          if (m->severity == MSG_S_WARNING)
            submit_note (xasprintf (_("Warnings (%d) exceed limit (%d).  Syntax processing will be halted."),
                                    n_msgs, max_msgs));
          else
            submit_note (xasprintf (_("Errors (%d) exceed limit (%d).  Syntax processing will be halted."),
                                    n_msgs, max_msgs));
        }
    }
}


/* Emits M as an error message.
   Frees allocated data in M. */
void
msg_emit (struct msg *m)
{
  if ( s_stream && m->where.file_name == NULL )
    {
      struct msg_locator loc;

      get_msg_location (s_stream, &loc);
      m->where.file_name = loc.file_name;
      m->where.line_number = loc.line_number;
    }
  else
    {
      m->where.file_name = NULL;
      m->where.line_number = 0;
    }

  if (!messages_disabled)
     process_msg (m);

  free (m->text);
}

/* Disables message output until the next call to msg_enable.  If
   this function is called multiple times, msg_enable must be
   called an equal number of times before messages are actually
   re-enabled. */
void
msg_disable (void)
{
  messages_disabled++;
}

/* Enables message output that was disabled by msg_disable. */
void
msg_enable (void)
{
  assert (messages_disabled > 0);
  messages_disabled--;
}

/* Private functions. */

void
request_bug_report (const char *msg)
{
  fprintf (stderr, "******************************************************\n");
  fprintf (stderr, "You have discovered a bug in PSPP.  Please report this\n");
  fprintf (stderr, "to " PACKAGE_BUGREPORT ".  Please include this entire\n");
  fprintf (stderr, "message, *plus* several lines of output just above it.\n");
  fprintf (stderr, "For the best chance at having the bug fixed, also\n");
  fprintf (stderr, "include the syntax file that triggered it and a sample\n");
  fprintf (stderr, "of any data file used for input.\n");
  fprintf (stderr, "proximate cause:     %s\n", msg);
  fprintf (stderr, "version:             %s\n", stat_version);
  fprintf (stderr, "host_system:         %s\n", host_system);
  fprintf (stderr, "build_system:        %s\n", build_system);
  fprintf (stderr, "locale_dir:          %s\n", locale_dir);
  fprintf (stderr, "compiler version:    %s\n",
#ifdef __VERSION__
           __VERSION__
#else
           "Unknown"
#endif
           );
  fprintf (stderr, "******************************************************\n");
}

