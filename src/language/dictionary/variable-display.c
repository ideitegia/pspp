/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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
#include <stdlib.h>

#include <data/procedure.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#include "minmax.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Set variables' alignment
   This is the alignment for GUI display only.
   It affects nothing but GUIs
*/
int
cmd_variable_alignment (struct lexer *lexer, struct dataset *ds)
{
  do
    {
      struct variable **v;
      size_t nv;

      size_t i;
      enum alignment align;

      if (!parse_variables (lexer, dataset_dict (ds), &v, &nv, PV_NONE))
        return CMD_FAILURE;

      if ( lex_force_match (lexer, '(') )
	{
	  if ( lex_match_id (lexer, "LEFT"))
	    align = ALIGN_LEFT;
	  else if ( lex_match_id (lexer, "RIGHT"))
	    align = ALIGN_RIGHT;
	  else if ( lex_match_id (lexer, "CENTER"))
	    align = ALIGN_CENTRE;
	  else
            {
              free (v);
              return CMD_FAILURE;
            }

	  lex_force_match (lexer, ')');
	}
      else
        {
          free (v);
          return CMD_FAILURE;
        }

      for( i = 0 ; i < nv ; ++i )
        var_set_alignment (v[i], align);

      while (lex_token (lexer) == '/')
	lex_get (lexer);
      free (v);

    }
  while (lex_token (lexer) != '.');
  return CMD_SUCCESS;
}

/* Set variables' display width.
   This is the width for GUI display only.
   It affects nothing but GUIs
*/
int
cmd_variable_width (struct lexer *lexer, struct dataset *ds)
{
  do
    {
      struct variable **v;
      long int width;
      size_t nv;
      size_t i;

      if (!parse_variables (lexer, dataset_dict (ds), &v, &nv, PV_NONE))
        return CMD_FAILURE;

      if (!lex_force_match (lexer, '(') || !lex_force_int (lexer))
        {
          free (v);
          return CMD_FAILURE;
        }
      width = lex_integer (lexer);
      lex_get (lexer);
      if (!lex_force_match (lexer, ')'))
        {
          free (v);
          return CMD_FAILURE;
        }

      if (width < 0)
        {
          msg (SE, _("Variable display width must be a positive integer."));
          free (v);
          return CMD_FAILURE;
        }
      width = MIN (width, 2 * MAX_STRING);

      for( i = 0 ; i < nv ; ++i )
        var_set_display_width (v[i], width);

      while (lex_token (lexer) == '/')
	lex_get (lexer);
      free (v);

    }
  while (lex_token (lexer) != '.');
  return CMD_SUCCESS;
}

/* Set variables' measurement level */
int
cmd_variable_level (struct lexer *lexer, struct dataset *ds)
{
  do
    {
      struct variable **v;
      size_t nv;
      enum measure level;
      size_t i;

      if (!parse_variables (lexer, dataset_dict (ds), &v, &nv, PV_NONE))
        return CMD_FAILURE;

      if ( lex_force_match (lexer, '(') )
	{
	  if ( lex_match_id (lexer, "SCALE"))
	    level = MEASURE_SCALE;
	  else if ( lex_match_id (lexer, "ORDINAL"))
	    level = MEASURE_ORDINAL;
	  else if ( lex_match_id (lexer, "NOMINAL"))
	    level = MEASURE_NOMINAL;
	  else
            {
              free (v);
              return CMD_FAILURE;
            }

	  lex_force_match (lexer, ')');
	}
      else
        {
          free (v);
          return CMD_FAILURE;
        }

      for( i = 0 ; i < nv ; ++i )
	var_set_measure (v[i], level);


      while (lex_token (lexer) == '/')
	lex_get (lexer);
      free (v);

    }
  while (lex_token (lexer) != '.');
  return CMD_SUCCESS;
}
