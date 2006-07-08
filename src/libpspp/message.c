/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libpspp/alloc.h>
#include <libpspp/version.h>

#include "progname.h"
#include "xvasprintf.h"

/* Current command name as set by msg_set_command_name(). */
static char *command_name;

/* Message handler as set by msg_init(). */
static void (*msg_handler)  (const struct msg *);
static void (*msg_location) (struct msg_locator *);


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

void
msg_init ( void (*handler) (const struct msg *), 
	   void (*location) (struct msg_locator *) ) 
{
  msg_handler = handler;
  msg_location = location;
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
  msg_location (&m->where);
  msg_handler (m);
  free (m->text);
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
