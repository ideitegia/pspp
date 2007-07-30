/* PSPP - computes sample statistics.
   Copyright (C) 2007 Free Software Foundation, Inc.

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
#include <language/command.h>
#include <libpspp/message.h>
#include <errno.h>
#include <language/lexer/lexer.h>
#include <unistd.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Parses the CD command. */
int
cmd_cd (struct lexer *lexer, struct dataset *ds UNUSED)
{
  char  *path = 0;

  if ( ! lex_force_string (lexer))
    goto error;

  path = ds_xstrdup (lex_tokstr (lexer));

  if ( -1 == chdir (path) )
    {
      int err = errno;
      msg (SE, _("Cannot change directory to %s:  %s "), path,
	   strerror (errno));
      goto error;
    }

  free (path);

  return CMD_SUCCESS;

 error:

  free(path);

  return CMD_FAILURE;
}

