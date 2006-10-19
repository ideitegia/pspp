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

#include <stdio.h>

#include <data/procedure.h>
#include <data/dictionary.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_weight (void)
{
  if (lex_match_id ("OFF"))
    dict_set_weight (dataset_dict (current_dataset), NULL);
  else
    {
      struct variable *v;

      lex_match (T_BY);
      v = parse_variable ();
      if (!v)
	return CMD_CASCADING_FAILURE;
      if (v->type == ALPHA)
	{
	  msg (SE, _("The weighting variable must be numeric."));
	  return CMD_CASCADING_FAILURE;
	}
      if (dict_class_from_id (v->name) == DC_SCRATCH)
	{
	  msg (SE, _("The weighting variable may not be scratch."));
	  return CMD_CASCADING_FAILURE;
	}

      dict_set_weight (dataset_dict (current_dataset), v);
    }

  return lex_end_of_command ();
}
