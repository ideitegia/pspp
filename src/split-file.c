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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "lexer.h"
#include "str.h"
#include "var.h"

int
cmd_split_file (void)
{
  if (lex_match_id ("OFF"))
    dict_set_split_vars (default_dict, NULL, 0);
  else
    {
      struct variable **v;
      int n;

      lex_match (T_BY);
      if (!parse_variables (default_dict, &v, &n, PV_NO_DUPLICATE))
	return CMD_FAILURE;

      dict_set_split_vars (default_dict, v, n);
      free (v);
    }

  return lex_end_of_command ();
}
