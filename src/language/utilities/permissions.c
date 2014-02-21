/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2010, 2011 Free Software Foundation, Inc.

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

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "data/settings.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "libpspp/cast.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

enum PER {PER_RO, PER_RW};

int change_permissions(const char *file_name, enum PER per);


/* Parses the PERMISSIONS command. */
int
cmd_permissions (struct lexer *lexer, struct dataset *ds UNUSED)
{
  char  *fn = 0;

  lex_match (lexer, T_SLASH);

  if (lex_match_id (lexer, "FILE"))
    lex_match (lexer, T_EQUALS);

  if (!lex_force_string (lexer))
    return CMD_FAILURE;

  fn = ss_xstrdup (lex_tokss (lexer));
  lex_force_match (lexer, T_STRING);


  lex_match (lexer, T_SLASH);

  if ( ! lex_match_id (lexer, "PERMISSIONS"))
    goto error;

  lex_match (lexer, T_EQUALS);

  if ( lex_match_id (lexer, "READONLY"))
    {
      if ( ! change_permissions(fn, PER_RO ) )
	goto error;
    }
  else if ( lex_match_id (lexer, "WRITEABLE"))
    {
      if ( ! change_permissions(fn, PER_RW ) )
	goto error;
    }
  else
    {
      lex_error_expecting (lexer, "WRITEABLE", "READONLY", NULL_SENTINEL);
      goto error;
    }

  free(fn);

  return CMD_SUCCESS;

 error:

  free(fn);

  return CMD_FAILURE;
}



int
change_permissions (const char *file_name, enum PER per)
{
  char *locale_file_name;
  struct stat buf;
  mode_t mode;

  if (settings_get_safer_mode ())
    {
      msg (SE, _("This command not allowed when the %s option is set."), "SAFER");
      return 0;
    }


  locale_file_name = utf8_to_filename (file_name);
  if ( -1 == stat(locale_file_name, &buf) )
    {
      const int errnum = errno;
      msg (SE, _("Cannot stat %s: %s"), file_name, strerror(errnum));
      free (locale_file_name);
      return 0;
    }

  if ( per == PER_RW )
    mode = buf.st_mode | 0200;
  else
    mode = buf.st_mode & ~0222;

  if ( -1 == chmod(locale_file_name, mode))

    {
      const int errnum = errno;
      msg (SE, _("Cannot change mode of %s: %s"), file_name, strerror(errnum));
      free (locale_file_name);
      return 0;
    }

  free (locale_file_name);

  return 1;
}
