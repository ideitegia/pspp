/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/command.h"

#include <errno.h>
#include <unistd.h>

#include "language/lexer/lexer.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Parses the CD command. */
int
cmd_cd (struct lexer *lexer, struct dataset *ds UNUSED)
{
  char  *path = 0;

  if ( ! lex_force_string (lexer))
    goto error;

  path = utf8_to_filename (lex_tokcstr (lexer));

  if ( -1 == chdir (path) )
    {
      int err = errno;
      msg (SE, _("Cannot change directory to %s:  %s "), path,
	   strerror (err));
      goto error;
    }

  free (path);
  lex_get (lexer);

  return CMD_SUCCESS;

 error:

  free(path);

  return CMD_FAILURE;
}

