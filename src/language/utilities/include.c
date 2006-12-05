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
#include <ctype.h>
#include <stdlib.h>
#include <libpspp/alloc.h>
#include <language/command.h>
#include <libpspp/message.h>
#include <libpspp/getl.h>
#include <language/syntax-file.h>
#include <language/lexer/lexer.h>
#include <libpspp/str.h>
#include <data/file-name.h>


#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_include (struct lexer *lexer, struct dataset *ds UNUSED)
{
  struct source_stream *ss;
  char *found_fn;
  char *target_fn;

  /* Skip optional FILE=. */
  if (lex_match_id (lexer, "FILE"))
    lex_match (lexer, '=');

  /* File name can be identifier or string. */
  if (lex_token (lexer) != T_ID && lex_token (lexer) != T_STRING) 
    {
      lex_error (lexer, _("expecting file name")); 
      return CMD_CASCADING_FAILURE;
    }

  target_fn = ds_cstr (lex_tokstr (lexer));

  ss = lex_get_source_stream (lexer);
  found_fn = fn_search_path (target_fn, getl_include_path ( ss ), NULL);

  if (found_fn != NULL) 
    {
      getl_include_source (ss, create_syntax_file_source (found_fn));
      free (found_fn); 
    }
  else
    msg (SE, _("Can't find `%s' in include file search path."), 
	 target_fn);

  lex_get (lexer);
  return lex_end_of_command (lexer);
}
