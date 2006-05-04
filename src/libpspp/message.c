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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpspp/alloc.h>
#include <libpspp/version.h>

#include "progname.h"
#include "xvasprintf.h"

/* Current command name as set by msg_set_command_name(). */
static char *command_name;

/* Message handler as set by msg_init(). */
static void (*msg_handler) (const struct msg *);

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

void
msg_init (void (*handler) (const struct msg *)) 
{
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

