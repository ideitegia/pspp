/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2010, 2011 Free Software Foundation, Inc.

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

#include <stdlib.h>

#include "data/attributes.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static enum cmd_result parse_attributes (struct lexer *,
                                         const char *dict_encoding,
                                         struct attrset **, size_t n);

/* Parses the DATAFILE ATTRIBUTE command. */
int
cmd_datafile_attribute (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct attrset *set = dict_get_attributes (dict);
  return parse_attributes (lexer, dict_get_encoding (dict), &set, 1);
}

/* Parses the VARIABLE ATTRIBUTE command. */
int
cmd_variable_attribute (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  const char *dict_encoding = dict_get_encoding (dict);

  do 
    {
      struct variable **vars;
      struct attrset **sets;
      size_t n_vars, i;
      bool ok;

      if (!lex_force_match_id (lexer, "VARIABLES")
          || !lex_force_match (lexer, T_EQUALS)
          || !parse_variables (lexer, dict, &vars, &n_vars, PV_NONE))
        return CMD_FAILURE;

      sets = xmalloc (n_vars * sizeof *sets);
      for (i = 0; i < n_vars; i++)
        sets[i] = var_get_attributes (vars[i]);

      ok = parse_attributes (lexer, dict_encoding, sets, n_vars);
      free (vars);
      free (sets);
      if (!ok)
        return CMD_FAILURE;
    }
  while (lex_match (lexer, T_SLASH));

  return CMD_SUCCESS;
}

/* Parses an attribute name and verifies that it is valid in DICT_ENCODING,
   optionally followed by an index inside square brackets.  Returns the
   attribute name or NULL if there was a parse error.  Stores the index into
   *INDEX. */
static char *
parse_attribute_name (struct lexer *lexer, const char *dict_encoding,
                      size_t *index)
{
  char *name;

  if (!lex_force_id (lexer)
      || !id_is_valid (lex_tokcstr (lexer), dict_encoding, true))
    return NULL;
  name = xstrdup (lex_tokcstr (lexer));
  lex_get (lexer);

  if (lex_match (lexer, T_LBRACK))
    {
      if (!lex_force_int (lexer))
        goto error;
      if (lex_integer (lexer) < 1 || lex_integer (lexer) > 65535)
        {
          msg (SE, _("Attribute array index must be between 1 and 65535."));
          goto error;
        }
      *index = lex_integer (lexer);
      lex_get (lexer);
      if (!lex_force_match (lexer, T_RBRACK))
        goto error;
    }
  else
    *index = 0;
  return name;

error:
  free (name);
  return NULL;
}

static bool
add_attribute (struct lexer *lexer, const char *dict_encoding,
               struct attrset **sets, size_t n) 
{
  const char *value;
  size_t index, i;
  char *name;

  name = parse_attribute_name (lexer, dict_encoding, &index);
  if (name == NULL)
    return false;
  if (!lex_force_match (lexer, T_LPAREN) || !lex_force_string (lexer))
    {
      free (name);
      return false;
    }
  value = lex_tokcstr (lexer);

  for (i = 0; i < n; i++)
    {
      struct attribute *attr = attrset_lookup (sets[i], name);
      if (attr == NULL) 
        {
          attr = attribute_create (name);
          attrset_add (sets[i], attr); 
        }
      attribute_set_value (attr, index ? index - 1 : 0, value);
    }

  lex_get (lexer);
  free (name);
  return lex_force_match (lexer, T_RPAREN);
}

static bool
delete_attribute (struct lexer *lexer, const char *dict_encoding,
                  struct attrset **sets, size_t n) 
{
  size_t index, i;
  char *name;

  name = parse_attribute_name (lexer, dict_encoding, &index);
  if (name == NULL)
    return false;

  for (i = 0; i < n; i++) 
    {
      struct attrset *set = sets[i];
      if (index == 0)
        attrset_delete (set, name);
      else
        {
          struct attribute *attr = attrset_lookup (set, name);
          if (attr != NULL) 
            {
              attribute_del_value (attr, index - 1);
              if (attribute_get_n_values (attr) == 0)
                attrset_delete (set, name); 
            }
        }
    }

  free (name);
  return true;
}

static enum cmd_result
parse_attributes (struct lexer *lexer, const char *dict_encoding,
                  struct attrset **sets, size_t n) 
{
  enum { UNKNOWN, ADD, DELETE } command = UNKNOWN;
  do 
    {
      if (lex_match_phrase (lexer, "ATTRIBUTE="))
        command = ADD;
      else if (lex_match_phrase (lexer, "DELETE="))
        command = DELETE;
      else if (command == UNKNOWN)
        {
          lex_error_expecting (lexer, "ATTRIBUTE=", "DELETE=", NULL_SENTINEL);
          return CMD_FAILURE;
        }

      if (!(command == ADD
            ? add_attribute (lexer, dict_encoding, sets, n)
            : delete_attribute (lexer, dict_encoding, sets, n)))
        return CMD_FAILURE;
    }
  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD);
  return CMD_SUCCESS;
}
