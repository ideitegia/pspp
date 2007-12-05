/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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


#include <libpspp/message.h>
#include <data/gnumeric-reader.h>

#include <language/command.h>
#include <language/lexer/lexer.h>
#include <stdlib.h>
#include <data/procedure.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

static int parse_get_gnm (struct lexer *lexer, struct dataset *);

int
cmd_get_data (struct lexer *lexer, struct dataset *ds)
{
  lex_force_match (lexer, '/');

  if (!lex_force_match_id (lexer, "TYPE"))
    return CMD_FAILURE;

  lex_force_match (lexer, '=');

  if (lex_match_id (lexer, "GNM"))
    return parse_get_gnm (lexer, ds);

  msg (SE, _("Unsupported TYPE %s"), lex_tokid (lexer));
  return CMD_FAILURE;
}


static int
parse_get_gnm (struct lexer *lexer, struct dataset *ds)
{
  struct gnumeric_read_info gri  = {NULL, NULL, NULL, 1, true, -1};

  lex_force_match (lexer, '/');

  if (!lex_force_match_id (lexer, "FILE"))
    goto error;

  lex_force_match (lexer, '=');

  if (!lex_force_string (lexer))
    goto error;

  gri.file_name = strdup (ds_cstr (lex_tokstr (lexer)));

  lex_get (lexer);

  while (lex_match (lexer, '/') )
    {
      if ( lex_match_id (lexer, "ASSUMEDSTRWIDTH"))
	{
	  lex_match (lexer, '=');
	  gri.asw = lex_integer (lexer);
	}
      else if (lex_match_id (lexer, "SHEET"))
	{
	  lex_match (lexer, '=');
	  if (lex_match_id (lexer, "NAME"))
	    {
	      if ( ! lex_force_string (lexer) )
		goto error;

	      gri.sheet_name = strdup (ds_cstr (lex_tokstr (lexer)));
	      gri.sheet_index = -1;
	    }
	  else if (lex_match_id (lexer, "INDEX"))
	    {
	      gri.sheet_index = lex_integer (lexer);
	    }
	  else
	    goto error;
	}
      else if (lex_match_id (lexer, "CELLRANGE"))
	{
	  lex_match (lexer, '=');

	  if (lex_match_id (lexer, "FULL"))
	    {
	      gri.cell_range = NULL;
	      lex_put_back (lexer, T_ID);
	    }
	  else if (lex_match_id (lexer, "RANGE"))
	    {
	      if ( ! lex_force_string (lexer) )
		goto error;

	      gri.cell_range = strdup (ds_cstr (lex_tokstr (lexer)));
	    }
	  else
	    goto error;
	}
      else if (lex_match_id (lexer, "READNAMES"))
	{
	  lex_match (lexer, '=');

	  if ( lex_match_id (lexer, "ON"))
	    {
	      gri.read_names = true;
	    }
	  else if (lex_match_id (lexer, "OFF"))
	    {
	      gri.read_names = false;
	    }
	  else
	    goto error;
	  lex_put_back (lexer, T_ID);
	}
      else
	{
	  printf ("Unknown data file type \"\%s\"\n", lex_tokid (lexer));
	  goto error;
	}
      lex_get (lexer);
    }

  {
    struct dictionary *dict = NULL;
    struct casereader *reader = gnumeric_open_reader (&gri, &dict);

    if ( reader )
      proc_set_active_file (ds, reader, dict);
  }

  free (gri.file_name);
  free (gri.sheet_name);
  free (gri.cell_range);
  return CMD_SUCCESS;

 error:

  free (gri.file_name);
  free (gri.sheet_name);
  free (gri.cell_range);
  return CMD_FAILURE;
}
