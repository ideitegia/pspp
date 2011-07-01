/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011 Free Software Foundation, Inc.

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

#include <stdio.h>

#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_weight (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  if (lex_match_id (lexer, "OFF"))
    dict_set_weight (dataset_dict (ds), NULL);
  else
    {
      struct variable *v;

      lex_match (lexer, T_BY);
      v = parse_variable (lexer, dict);
      if (!v)
	return CMD_CASCADING_FAILURE;
      if (var_is_alpha (v))
	{
	  msg (SE, _("The weighting variable must be numeric."));
	  return CMD_CASCADING_FAILURE;
	}
      if (dict_class_from_id (var_get_name (v)) == DC_SCRATCH)
	{
	  msg (SE, _("The weighting variable may not be scratch."));
	  return CMD_CASCADING_FAILURE;
	}

      dict_set_weight (dict, v);
    }

  return CMD_SUCCESS;
}
