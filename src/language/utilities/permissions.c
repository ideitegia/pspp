/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
   Author: John Darrington

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
#include "message.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "settings.h"
#include "command.h"
#include "message.h"
#include "lexer.h"
#include "misc.h"
#include "stat-macros.h"
#include "str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

enum PER {PER_RO, PER_RW};

int change_permissions(const char *filename, enum PER per);


/* Parses the PERMISSIONS command. */
int
cmd_permissions (void)
{
  char  *fn = 0;

  lex_match ('/');

  if (lex_match_id ("FILE"))
    lex_match ('=');

  fn = strdup(ds_c_str(&tokstr));
  lex_force_match(T_STRING);


  lex_match ('/');
  
  if ( ! lex_match_id ("PERMISSIONS"))
    goto error;

  lex_match('=');

  if ( lex_match_id("READONLY"))
    {
      if ( ! change_permissions(fn, PER_RO ) ) 
	goto error;
    }
  else if ( lex_match_id("WRITEABLE"))
    {
      if ( ! change_permissions(fn, PER_RW ) ) 
	goto error;
    }
  else
    {
      msg(ME, _("Expecting %s or %s."), "WRITEABLE", "READONLY");
      goto error;
    }

  free(fn);

  return CMD_SUCCESS;

 error:

  free(fn);

  return CMD_FAILURE;
}



int
change_permissions(const char *filename, enum PER per)
{
  struct stat buf;
  mode_t mode;

  if (get_safer_mode ())
    {
      msg (SE, _("This command not allowed when the SAFER option is set."));
      return CMD_FAILURE;
    }


  if ( -1 == stat(filename, &buf) ) 
    {
      const int errnum = errno;
      msg(ME,_("Cannot stat %s: %s"), filename, strerror(errnum));
      return 0;
    }

  if ( per == PER_RW )
    mode = buf.st_mode | S_IWUSR ;
  else
    mode = buf.st_mode & ~( S_IWOTH | S_IWUSR | S_IWGRP );

  if ( -1 == chmod(filename, mode))

    {
      const int errnum = errno;
      msg(ME,_("Cannot change mode of %s: %s"), filename, strerror(errnum));
      return 0;
    }

  return 1;
}
