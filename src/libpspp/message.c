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

#include "message.h"
#include "msg-locator.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libpspp/version.h>

#include "progname.h"
#include "xalloc.h"
#include "xvasprintf.h"

/* Current command name as set by msg_set_command_name(). */
static char *command_name;

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


/* Duplicate a message */
struct msg *
msg_dup(const struct msg *m)
{
  struct msg *new_msg = xmalloc (sizeof *m);

  *new_msg = *m;
  new_msg->text = strdup(m->text);

  return new_msg;
}

void
msg_destroy(struct msg *m)
{
  free(m->text);
  free(m);
}

/* Emits M as an error message.
   Frees allocated data in M. */
void
msg_emit (struct msg *m)
{
  if ( s_stream )
    get_msg_location (s_stream, &m->where);

  if (!messages_disabled)
     msg_handler (m);
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

/* Sets COMMAND_NAME as the command name included in some kinds
   of error messages. */
void
msg_set_command_name (const char *command_name_)
{
  free (command_name);
  command_name = command_name_ ? xstrdup (command_name_) : NULL;
}

/* Returns the current command name, or NULL if none. */
const char *
msg_get_command_name (void)
{
  return command_name;
}

void
request_bug_report_and_abort (const char *msg)
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
  fprintf (stderr, "default_config_path: %s\n", default_config_path);
  fprintf (stderr, "include_path:        %s\n", include_path);
  fprintf (stderr, "locale_dir:          %s\n", locale_dir);
  fprintf (stderr, "compiler version:    %s\n",
#ifdef __VERSION__
           __VERSION__
#else
           "Unknown"
#endif
           );
  fprintf (stderr, "******************************************************\n");

  _exit (EXIT_FAILURE);
}
