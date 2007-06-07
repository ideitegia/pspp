/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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

#include <language/stats/sort-criteria.h>

#include <stdlib.h>

#include <data/case-ordering.h>
#include <data/dictionary.h>
#include <data/variable.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/message.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Parses a list of sort keys and returns a struct sort_criteria
   based on it.  Returns a null pointer on error.
   If SAW_DIRECTION is nonnull, sets *SAW_DIRECTION to true if at
   least one parenthesized sort direction was specified, false
   otherwise. */
struct case_ordering *
parse_case_ordering (struct lexer *lexer, const struct dictionary *dict,
                     bool *saw_direction)
{
  struct case_ordering *ordering = case_ordering_create (dict);
  struct variable **vars = NULL;
  size_t var_cnt = 0;
  
 if (saw_direction != NULL)
    *saw_direction = false;

  do
    {
      enum sort_direction direction;
      size_t i;

      /* Variables. */
      free (vars);
      vars = NULL;
      if (!parse_variables_const (lexer, dict, &vars, &var_cnt, PV_NO_SCRATCH))
        goto error;

      /* Sort direction. */
      if (lex_match (lexer, '('))
	{
	  if (lex_match_id (lexer, "D") || lex_match_id (lexer, "DOWN"))
	    direction = SRT_DESCEND;
	  else if (lex_match_id (lexer, "A") || lex_match_id (lexer, "UP"))
            direction = SRT_ASCEND;
          else
	    {
	      msg (SE, _("`A' or `D' expected inside parentheses."));
              goto error;
	    }
	  if (!lex_match (lexer, ')'))
	    {
	      msg (SE, _("`)' expected."));
              goto error;
	    }
          if (saw_direction != NULL)
            *saw_direction = true;
	}
      else
        direction = SRT_ASCEND;

      for (i = 0; i < var_cnt; i++)
        if (!case_ordering_add_var (ordering, vars[i], direction))
          msg (SW, _("Variable %s specified twice in sort criteria."),
               var_get_name (vars[i]));
    }
  while (lex_token (lexer) == T_ID
         && dict_lookup_var (dict, lex_tokid (lexer)) != NULL);

  free (vars);
  return ordering;

 error:
  free (vars);
  case_ordering_destroy (ordering);
  return NULL;
}
