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
#include <sys/types.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>
#include <libpspp/alloc.h>
#include <language/command.h>
#include <libpspp/message.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <data/settings.h>
#include <data/variable.h>
#include "sort-criteria.h"
#include <math/sort.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

static bool  is_terminator(int tok, const int *terminators);


/* Parses a list of sort keys and returns a struct sort_criteria
   based on it.  Returns a null pointer on error.
   If SAW_DIRECTION is nonnull, sets *SAW_DIRECTION to true if at
   least one parenthesized sort direction was specified, false
   otherwise. 
   If TERMINATORS is non-null, then it must be a pointer to a 
   null terminated list of tokens, in addition to the defaults,
   which are to be considered terminators of the clause being parsed.
   The default terminators are '/' and '.'
   
*/
struct sort_criteria *
sort_parse_criteria (struct lexer *lexer, const struct dictionary *dict,
                     struct variable ***vars, size_t *var_cnt,
                     bool *saw_direction,
		     const int *terminators
		     )
{
  struct sort_criteria *criteria;
  struct variable **local_vars = NULL;
  size_t local_var_cnt;

  assert ((vars == NULL) == (var_cnt == NULL));
  if (vars == NULL) 
    {
      vars = &local_vars;
      var_cnt = &local_var_cnt;
    }

  criteria = xmalloc (sizeof *criteria);
  criteria->crits = NULL;
  criteria->crit_cnt = 0;

  *vars = NULL;
  *var_cnt = 0;
  if (saw_direction != NULL)
    *saw_direction = false;

  do
    {
      size_t prev_var_cnt = *var_cnt;
      enum sort_direction direction;

      /* Variables. */
      if (!parse_variables (lexer, dict, vars, var_cnt,
			    PV_NO_DUPLICATE | PV_APPEND | PV_NO_SCRATCH))
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

      criteria->crits = xnrealloc (criteria->crits,
                                   *var_cnt, sizeof *criteria->crits);
      criteria->crit_cnt = *var_cnt;
      for (; prev_var_cnt < criteria->crit_cnt; prev_var_cnt++) 
        {
          struct sort_criterion *c = &criteria->crits[prev_var_cnt];
          c->fv = (*vars)[prev_var_cnt]->fv;
          c->width = (*vars)[prev_var_cnt]->width;
          c->dir = direction;
        }
    }
  while (lex_token (lexer) != '.' && lex_token (lexer) != '/' && !is_terminator(lex_token (lexer), terminators));

  free (local_vars);
  return criteria;

 error:
  free (local_vars);
  sort_destroy_criteria (criteria);
  return NULL;
}

/* Return TRUE if TOK is a member of the list of TERMINATORS.
   FALSE otherwise */
static bool 
is_terminator(int tok, const int *terminators)
{
  if (terminators == NULL ) 
    return false;

  while ( *terminators) 
    {
      if (tok == *terminators++)
	return true;
    }

  return false;
}



/* Destroys a SORT CASES program. */
void
sort_destroy_criteria (struct sort_criteria *criteria) 
{
  if (criteria != NULL) 
    {
      free (criteria->crits);
      free (criteria);
    }
}



