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
#include <assert.h>
#include <stdio.h>
#include "command.h"
#include "error.h"
#include "lexer.h"
#include "str.h"
#include "var.h"

/* Notes:

   If the weighting variable is deleted somehow (for instance by
   end-of-scope of TEMPORARY), weighting must be canceled.

   Scratch vars may not be used for weighting. */

/* WEIGHT transformation. */
struct weight_trns
  {
    struct trns_header h;
    int src;			/* `value' index of weighting variable. */
    int dest;			/* `value' index of $WEIGHT. */
  };

int
cmd_weight (void)
{
  lex_match_id ("WEIGHT");

  if (lex_match_id ("OFF"))
    dict_set_weight (default_dict, NULL);
  else
    {
      struct variable *v;

      lex_match (T_BY);
      v = parse_variable ();
      if (!v)
	return CMD_FAILURE;
      if (v->type == ALPHA)
	{
	  msg (SE, _("The weighting variable must be numeric."));
	  return CMD_FAILURE;
	}
      if (v->name[0] == '#')
	{
	  msg (SE, _("The weighting variable may not be scratch."));
	  return CMD_FAILURE;
	}

      dict_set_weight (default_dict, v);
    }

  return lex_end_of_command ();
}
