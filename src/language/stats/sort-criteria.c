/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/stats/sort-criteria.h"

#include <stdlib.h>

#include "data/dictionary.h"
#include "data/subcase.h"
#include "data/variable.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Parses a list of sort fields and appends them to ORDERING,
   which the caller must already have initialized.
   Returns true if successful, false on error.
   If SAW_DIRECTION is nonnull, sets *SAW_DIRECTION to true if at
   least one parenthesized sort direction was specified, false
   otherwise. */
bool
parse_sort_criteria (struct lexer *lexer, const struct dictionary *dict,
                     struct subcase *ordering,
                     const struct variable ***vars, bool *saw_direction)
{
  const struct variable **local_vars = NULL;
  size_t var_cnt = 0;

  if (vars == NULL) 
    vars = &local_vars;
  *vars = NULL;

  if (saw_direction != NULL)
    *saw_direction = false;

  do
    {
      size_t prev_var_cnt = var_cnt;
      enum subcase_direction direction;
      size_t i;

      /* Variables. */
      if (!parse_variables_const (lexer, dict, vars, &var_cnt,
                                  PV_APPEND | PV_NO_SCRATCH))
        goto error;

      /* Sort direction. */
      if (lex_match (lexer, T_LPAREN))
	{
	  if (lex_match_id (lexer, "D") || lex_match_id (lexer, "DOWN"))
	    direction = SC_DESCEND;
	  else if (lex_match_id (lexer, "A") || lex_match_id (lexer, "UP"))
            direction = SC_ASCEND;
          else
	    {
              lex_error_expecting (lexer, "A", "D", NULL_SENTINEL);
              goto error;
	    }
	  if (!lex_force_match (lexer, T_RPAREN))
            goto error;
          if (saw_direction != NULL)
            *saw_direction = true;
	}
      else
        direction = SC_ASCEND;

      for (i = prev_var_cnt; i < var_cnt; i++) 
        {
          const struct variable *var = (*vars)[i];
          if (!subcase_add_var (ordering, var, direction))
            msg (SW, _("Variable %s specified twice in sort criteria."),
                 var_get_name (var)); 
        }
    }
  while (lex_token (lexer) == T_ID
         && dict_lookup_var (dict, lex_tokcstr (lexer)) != NULL);

  free (local_vars);
  return true;

error:
  free (local_vars);
  if (vars)
    *vars = NULL;
  return false;
}
