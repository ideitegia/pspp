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
#include <language/line-buffer.h>
#include <language/lexer/lexer.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_include (void)
{
  /* Skip optional FILE=. */
  if (lex_match_id ("FILE"))
    lex_match ('=');

  /* File name can be identifier or string. */
  if (token != T_ID && token != T_STRING) 
    {
      lex_error (_("expecting file name")); 
      return CMD_CASCADING_FAILURE;
    }
  getl_include_syntax_file (ds_cstr (&tokstr));

  lex_get ();
  return lex_end_of_command ();
}
