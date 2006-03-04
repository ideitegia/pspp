/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

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
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "message.h"
#include "lexer.h"
#include "str.h"
#include "variable.h"

#include "debug-print.h"

/* Set variables' alignment
   This is the alignment for GUI display only.
   It affects nothing but GUIs
*/
int
cmd_variable_alignment (void)
{
  do
    {
      struct variable **v;
      size_t nv;

      size_t i;
      enum alignment align;


      if (!parse_variables (default_dict, &v, &nv, PV_NONE))
        return CMD_PART_SUCCESS_MAYBE;

      if ( lex_force_match('(') ) 
	{
	  if ( lex_match_id("LEFT"))
	    align = ALIGN_LEFT;
	  else if ( lex_match_id("RIGHT"))
	    align = ALIGN_RIGHT;
	  else if ( lex_match_id("CENTER"))
	    align = ALIGN_CENTRE;
	  else 
            {
              free (v);
              return CMD_FAILURE; 
            }

	  lex_force_match(')');
	}
      else 
        {
          free (v);
          return CMD_FAILURE; 
        }

      for( i = 0 ; i < nv ; ++i ) 
	v[i]->alignment = align;


      while (token == '/')
	lex_get ();
      free (v);

    }
  while (token != '.');
  return CMD_SUCCESS;
}

/* Set variables' display width.
   This is the width for GUI display only.
   It affects nothing but GUIs
*/
int
cmd_variable_width (void)
{
  do
    {
      struct variable **v;
      size_t nv;
      size_t i;

      if (!parse_variables (default_dict, &v, &nv, PV_NONE))
        return CMD_PART_SUCCESS_MAYBE;

      if ( lex_force_match('(') ) 
	{
	  if ( lex_force_int()) 
	    lex_get();
	  else
	    return CMD_FAILURE;
	  lex_force_match(')');
	}

      for( i = 0 ; i < nv ; ++i ) 
	  v[i]->display_width = tokval;

      while (token == '/')
	lex_get ();
      free (v);

    }
  while (token != '.');
  return CMD_SUCCESS;
}

/* Set variables' measurement level */
int
cmd_variable_level (void)
{
  do
    {
      struct variable **v;
      size_t nv;
      enum measure level;
      size_t i;

      if (!parse_variables (default_dict, &v, &nv, PV_NONE))
        return CMD_PART_SUCCESS_MAYBE;

      if ( lex_force_match('(') ) 
	{
	  if ( lex_match_id("SCALE"))
	    level = MEASURE_SCALE;
	  else if ( lex_match_id("ORDINAL"))
	    level = MEASURE_ORDINAL;
	  else if ( lex_match_id("NOMINAL"))
	    level = MEASURE_NOMINAL;
	  else 
            {
              free (v);
              return CMD_FAILURE; 
            }

	  lex_force_match(')');
	}
      else
        {
          free (v);
          return CMD_FAILURE; 
        }
      
      for( i = 0 ; i < nv ; ++i ) 
	v[i]->measure = level ;


      while (token == '/')
	lex_get ();
      free (v);

    }
  while (token != '.');
  return CMD_SUCCESS;
}
