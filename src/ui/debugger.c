/* PSPP - computes sample statistics.
   Copyright (C) 2006 Free Software Foundation, Inc.

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

#include "debugger.h"

#if HAVE_SYS_TYPES_H && HAVE_SYS_WAIT_H
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

/* Fork, start gdb and connect to the parent process.
   If that happens successfully, then this function does not return,
   but exits with EXIT_FAILURE. Otherwise it returns.
 */
void
connect_debugger (void)
{
  char pidstr[20];
  pid_t pid;

  snprintf (pidstr, 20, "%d", getpid ());
  pid = fork ();
  if ( pid  == -1 )
    {
      perror ("Cannot fork");
      return ;
    }
  if ( pid == 0 )
    {
      /* child */
      execlp ("gdb", "gdb", "-p", pidstr, NULL);
      perror ("Cannot exec debugger");
      exit (EXIT_FAILURE);
    }
  else
    {
      int status;
      wait (&status);
      if ( EXIT_SUCCESS != WEXITSTATUS (status) )
	return ;
    }

  exit (EXIT_FAILURE);
}

#else /* !(HAVE_SYS_TYPES_H && HAVE_SYS_WAIT_H) */
/* Don't know how to connect to gdb.
   Just return.
 */
void
connect_debugger (void)
{
}
#endif /* !(HAVE_SYS_TYPES_H && HAVE_SYS_WAIT_H) */
