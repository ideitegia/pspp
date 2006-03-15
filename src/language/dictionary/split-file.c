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
#include <stdlib.h>
#include <libpspp/alloc.h>
#include <language/command.h>
#include <data/dictionary.h>
#include <libpspp/message.h>
#include <language/lexer/lexer.h>
#include <libpspp/str.h>
#include <data/variable.h>

int
cmd_split_file (void)
{
  if (lex_match_id ("OFF"))
    dict_set_split_vars (default_dict, NULL, 0);
  else
    {
      struct variable **v;
      size_t n;

      /* For now, ignore SEPARATE and LAYERED. */
      lex_match_id ("SEPARATE") || lex_match_id ("LAYERED");
      
      lex_match (T_BY);
      if (!parse_variables (default_dict, &v, &n, PV_NO_DUPLICATE))
	return CMD_CASCADING_FAILURE;

      dict_set_split_vars (default_dict, v, n);
      free (v);
    }

  return lex_end_of_command ();
}
