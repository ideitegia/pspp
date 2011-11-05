/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2008, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/data-io/trim.h"

#include <stdlib.h>

#include "data/dictionary.h"
#include "data/variable.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Commands that read and write system files share a great deal
   of common syntactic structure for rearranging and dropping
   variables.  This function parses this syntax and modifies DICT
   appropriately.  Returns true on success, false on failure. */
bool
parse_dict_trim (struct lexer *lexer, struct dictionary *dict)
{
  if (lex_match_id (lexer, "MAP"))
    {
      /* FIXME. */
      return true;
    }
  else if (lex_match_id (lexer, "DROP"))
    return parse_dict_drop (lexer, dict);
  else if (lex_match_id (lexer, "KEEP"))
    return parse_dict_keep (lexer, dict);
  else if (lex_match_id (lexer, "RENAME"))
    return parse_dict_rename (lexer, dict);
  else
    {
      lex_error (lexer, _("expecting a valid subcommand"));
      return false;
    }
}

/* Parses and performs the RENAME subcommand of GET, SAVE, and
   related commands. */
bool
parse_dict_rename (struct lexer *lexer, struct dictionary *dict)
{
  size_t i;

  int success = 0;

  struct variable **v;
  char **new_names;
  size_t nv, nn;
  char *err_name;

  int group;

  lex_match (lexer, T_EQUALS);
  if (lex_token (lexer) != T_LPAREN)
    {
      struct variable *v;

      v = parse_variable (lexer, dict);
      if (v == NULL)
	return 0;
      if (!lex_force_match (lexer, T_EQUALS)
	  || !lex_force_id (lexer)
          || !dict_id_is_valid (dict, lex_tokcstr (lexer), true))
	return 0;
      if (dict_lookup_var (dict, lex_tokcstr (lexer)) != NULL)
	{
	  msg (SE, _("Cannot rename %s as %s because there already exists "
		     "a variable named %s.  To rename variables with "
		     "overlapping names, use a single RENAME subcommand "
		     "such as `/RENAME (A=B)(B=C)(C=A)', or equivalently, "
		     "`/RENAME (A B C=B C A)'."),
               var_get_name (v), lex_tokcstr (lexer), lex_tokcstr (lexer));
	  return 0;
	}

      dict_rename_var (dict, v, lex_tokcstr (lexer));
      lex_get (lexer);
      return 1;
    }

  nv = nn = 0;
  v = NULL;
  new_names = 0;
  group = 1;
  while (lex_match (lexer, T_LPAREN))
    {
      size_t old_nv = nv;

      if (!parse_variables (lexer, dict, &v, &nv, PV_NO_DUPLICATE | PV_APPEND))
	goto done;
      if (!lex_match (lexer, T_EQUALS))
	{
          lex_error_expecting (lexer, "`='", NULL_SENTINEL);
	  goto done;
	}
      if (!parse_DATA_LIST_vars (lexer, dict, &new_names, &nn,
                                 PV_APPEND | PV_NO_SCRATCH | PV_NO_DUPLICATE))
	goto done;
      if (nn != nv)
	{
	  msg (SE, _("Number of variables on left side of `=' (%zu) does not "
                     "match number of variables on right side (%zu), in "
                     "parenthesized group %d of RENAME subcommand."),
	       nv - old_nv, nn - old_nv, group);
	  goto done;
	}
      if (!lex_force_match (lexer, T_RPAREN))
	goto done;
      group++;
    }

  if (!dict_rename_vars (dict, v, new_names, nv, &err_name))
    {
      msg (SE, _("Requested renaming duplicates variable name %s."), err_name);
      goto done;
    }
  success = 1;

 done:
  for (i = 0; i < nn; i++)
    free (new_names[i]);
  free (new_names);
  free (v);

  return success;
}

/* Parses and performs the DROP subcommand of GET, SAVE, and
   related commands.
   Returns true if successful, false on failure.*/
bool
parse_dict_drop (struct lexer *lexer, struct dictionary *dict)
{
  struct variable **v;
  size_t nv;

  lex_match (lexer, T_EQUALS);
  if (!parse_variables (lexer, dict, &v, &nv, PV_NONE))
    return false;
  dict_delete_vars (dict, v, nv);
  free (v);

  if (dict_get_var_cnt (dict) == 0)
    {
      msg (SE, _("Cannot DROP all variables from dictionary."));
      return false;
    }
  return true;
}

/* Parses and performs the KEEP subcommand of GET, SAVE, and
   related commands.
   Returns true if successful, false on failure.*/
bool
parse_dict_keep (struct lexer *lexer, struct dictionary *dict)
{
  struct variable **v;
  size_t nv;
  size_t i;

  lex_match (lexer, T_EQUALS);
  if (!parse_variables (lexer, dict, &v, &nv, PV_NONE))
    return false;

  /* Move the specified variables to the beginning. */
  dict_reorder_vars (dict, v, nv);

  /* Delete the remaining variables. */
  v = xnrealloc (v, dict_get_var_cnt (dict) - nv, sizeof *v);
  for (i = nv; i < dict_get_var_cnt (dict); i++)
    v[i - nv] = dict_get_var (dict, i);
  dict_delete_vars (dict, v, dict_get_var_cnt (dict) - nv);
  free (v);

  return true;
}
