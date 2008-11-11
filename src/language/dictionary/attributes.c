/* PSPP - a program for statistical analysis.
   Copyright (C) 2008 Free Software Foundation, Inc.

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

#include <data/attributes.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/message.h>

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static enum cmd_result parse_attributes (struct lexer *, struct attrset **,
                                         size_t n);

/* Parses the DATAFILE ATTRIBUTE command. */
int
cmd_datafile_attribute (struct lexer *lexer, struct dataset *ds)
{
  struct attrset *set = dict_get_attributes (dataset_dict (ds));
  return parse_attributes (lexer, &set, 1);
}

/* Parses the VARIABLE ATTRIBUTE command. */
int
cmd_variable_attribute (struct lexer *lexer, struct dataset *ds)
{
  do 
    {
      struct variable **vars;
      struct attrset **sets;
      size_t n_vars, i;
      bool ok;

      if (!lex_force_match_id (lexer, "VARIABLES")
          || !lex_force_match (lexer, '=')
          || !parse_variables (lexer, dataset_dict (ds), &vars, &n_vars,
                               PV_NONE))
        return CMD_FAILURE;

      sets = xmalloc (n_vars * sizeof *sets);
      for (i = 0; i < n_vars; i++)
        sets[i] = var_get_attributes (vars[i]);

      ok = parse_attributes (lexer, sets, n_vars);
      free (vars);
      free (sets);
      if (!ok)
        return CMD_FAILURE;
    }
  while (lex_match (lexer, '/'));

  return lex_end_of_command (lexer);
}

static bool
match_subcommand (struct lexer *lexer, const char *keyword) 
{
  if (lex_token (lexer) == T_ID
      && lex_id_match (ss_cstr (lex_tokid (lexer)), ss_cstr (keyword))
      && lex_look_ahead (lexer) == '=') 
    {
      lex_get (lexer);          /* Skip keyword. */
      lex_get (lexer);          /* Skip '='. */
      return true;
    }
  else
    return false;
}

static bool
parse_attribute_name (struct lexer *lexer, char name[VAR_NAME_LEN + 1],
                      size_t *index) 
{
  if (!lex_force_id (lexer))
    return false;
  strcpy (name, lex_tokid (lexer));
  lex_get (lexer);

  if (lex_match (lexer, '[')) 
    {
      if (!lex_force_int (lexer))
        return false;
      if (lex_integer (lexer) < 1 || lex_integer (lexer) > 65535)
        {
          msg (SE, _("Attribute array index must be between 1 and 65535."));
          return false;
        }
      *index = lex_integer (lexer);
      lex_get (lexer);
      if (!lex_force_match (lexer, ']'))
        return false;
    }
  else
    *index = 0;
  return true;
}

static bool
add_attribute (struct lexer *lexer, struct attrset **sets, size_t n) 
{
  char name[VAR_NAME_LEN + 1];
  size_t index, i;
  char *value;

  if (!parse_attribute_name (lexer, name, &index)
      || !lex_force_match (lexer, '(')
      || !lex_force_string (lexer))
    return false;
  value = ds_cstr (lex_tokstr (lexer));

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
  return lex_force_match (lexer, ')');
}

static bool
delete_attribute (struct lexer *lexer, struct attrset **sets, size_t n) 
{
  char name[VAR_NAME_LEN + 1];
  size_t index, i;

  if (!parse_attribute_name (lexer, name, &index))
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
  return true;
}

static enum cmd_result
parse_attributes (struct lexer *lexer, struct attrset **sets, size_t n) 
{
  enum { UNKNOWN, ADD, DELETE } command = UNKNOWN;
  do 
    {
      if (match_subcommand (lexer, "ATTRIBUTE"))
        command = ADD;
      else if (match_subcommand (lexer, "DELETE"))
        command = DELETE;
      else if (command == UNKNOWN)
        {
          lex_error (lexer, _("expecting ATTRIBUTE= or DELETE="));
          return CMD_FAILURE;
        }

      if (!(command == ADD
            ? add_attribute (lexer, sets, n)
            : delete_attribute (lexer, sets, n)))
        return CMD_FAILURE;
    }
  while (lex_token (lexer) != '/' && lex_token (lexer) != '.');
  return CMD_SUCCESS;
}
