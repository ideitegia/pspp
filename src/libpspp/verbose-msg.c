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

#include <libpspp/verbose-msg.h>

#include <stdarg.h>
#include <stdio.h>

#include "progname.h"

/* Level of verbosity.
   Higher values cause more output. */
static int verbosity;

/* Increases the verbosity level. */
void
verbose_increment_level (void)
{
  verbosity++;
}

/* Writes MESSAGE formatted with printf, to stderr, if the
   verbosity level is at least LEVEL. */
void
verbose_msg (int level, const char *format, ...)
{
  if (level <= verbosity)
    {
      va_list args;

      va_start (args, format);
      fprintf (stderr, "%s: ", program_name);
      vfprintf (stderr, format, args);
      putc ('\n', stderr);
      va_end (args);
    }
}

