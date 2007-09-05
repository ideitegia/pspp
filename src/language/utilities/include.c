/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007 Free Software Foundation, Inc.

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
#include <ctype.h>
#include <stdlib.h>
#include <libpspp/alloc.h>
#include <language/command.h>
#include <libpspp/message.h>
#include <libpspp/getl.h>
#include <language/syntax-file.h>
#include <language/lexer/lexer.h>
#include <libpspp/str.h>
#include <data/file-name.h>
#include <dirname.h>
#include <canonicalize.h>


#include "gettext.h"
#define _(msgid) gettext (msgid)

static int parse_insert (struct lexer *lexer, char **filename);


int
cmd_include (struct lexer *lexer, struct dataset *ds UNUSED)
{
  char *filename = NULL;
  int status = parse_insert (lexer, &filename);

  if ( CMD_SUCCESS != status)
    return status;

  lex_get (lexer);

  status = lex_end_of_command (lexer);

  if ( status == CMD_SUCCESS)
    {
      struct source_stream *ss = lex_get_source_stream (lexer);

      assert (filename);
      getl_include_source (ss, create_syntax_file_source (filename),
			   GETL_BATCH, ERRMODE_STOP);
      free (filename);
    }

  return status;
}


int
cmd_insert (struct lexer *lexer, struct dataset *ds UNUSED)
{
  enum syntax_mode syntax_mode = GETL_INTERACTIVE;
  enum error_mode error_mode = ERRMODE_CONTINUE;
  char *filename = NULL;
  int status = parse_insert (lexer, &filename);
  bool cd = false;

  if ( CMD_SUCCESS != status)
    return status;

  lex_get (lexer);

  while ( '.' != lex_token (lexer))
    {
      if (lex_match_id (lexer, "SYNTAX"))
	{
	  lex_match (lexer, '=');
	  if ( lex_match_id (lexer, "INTERACTIVE") )
	    syntax_mode = GETL_INTERACTIVE;
	  else if ( lex_match_id (lexer, "BATCH"))
	    syntax_mode = GETL_BATCH;
	  else
	    {
	      lex_error(lexer,
			_("Expecting BATCH or INTERACTIVE after SYNTAX."));
	      return CMD_FAILURE;
	    }
	}
      else if (lex_match_id (lexer, "CD"))
	{
	  lex_match (lexer, '=');
	  if ( lex_match_id (lexer, "YES") )
	    {
	      cd = true;
	    }
	  else if ( lex_match_id (lexer, "NO"))
	    {
	      cd = false;
	    }
	  else
	    {
	      lex_error (lexer, _("Expecting YES or NO after CD."));
	      return CMD_FAILURE;
	    }
	}
      else if (lex_match_id (lexer, "ERROR"))
	{
	  lex_match (lexer, '=');
	  if ( lex_match_id (lexer, "CONTINUE") )
	    {
	      error_mode = ERRMODE_CONTINUE;
	    }
	  else if ( lex_match_id (lexer, "STOP"))
	    {
	      error_mode = ERRMODE_STOP;
	    }
	  else
	    {
	      lex_error (lexer, _("Expecting CONTINUE or STOP after ERROR."));
	      return CMD_FAILURE;
	    }
	}

      else
	{
	  lex_error (lexer, _("Unexpected token: `%s'."),
		     lex_token_representation (lexer));

	  return CMD_FAILURE;
	}
    }

  status = lex_end_of_command (lexer);

  if ( status == CMD_SUCCESS)
    {
      struct source_stream *ss = lex_get_source_stream (lexer);

      assert (filename);
      getl_include_source (ss, create_syntax_file_source (filename),
			   syntax_mode,
			   error_mode);

      if ( cd )
	{
	  char *directory = dir_name (filename);
	  chdir (directory);
	  free (directory);
	}

      free (filename);
    }

  return status;
}


static int
parse_insert (struct lexer *lexer, char **filename)
{
  char *target_fn;
  char *relative_filename;

  /* Skip optional FILE=. */
  if (lex_match_id (lexer, "FILE"))
    lex_match (lexer, '=');

  /* File name can be identifier or string. */
  if (lex_token (lexer) != T_ID && lex_token (lexer) != T_STRING)
    {
      lex_error (lexer, _("expecting file name"));
      return CMD_FAILURE;
    }

  target_fn = ds_cstr (lex_tokstr (lexer));

  relative_filename =
    fn_search_path (target_fn,
		    getl_include_path (lex_get_source_stream (lexer)));

  if ( ! relative_filename)
    {
      msg (SE, _("Can't find `%s' in include file search path."),
	 target_fn);
      return CMD_FAILURE;
    }

  *filename = canonicalize_file_name (relative_filename);
  free (relative_filename);

  return CMD_SUCCESS;
}
