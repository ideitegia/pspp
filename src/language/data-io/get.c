/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include "data/any-reader.h"
#include "data/case-map.h"
#include "data/case.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/por-file-writer.h"
#include "language/command.h"
#include "language/data-io/file-handle.h"
#include "language/data-io/trim.h"
#include "language/lexer/lexer.h"
#include "libpspp/compiler.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Reading system and portable files. */

/* Type of command. */
enum reader_command
  {
    GET_CMD,
    IMPORT_CMD
  };

static int parse_read_command (struct lexer *, struct dataset *,
                               enum reader_command);

/* GET. */
int
cmd_get (struct lexer *lexer, struct dataset *ds)
{
  return parse_read_command (lexer, ds, GET_CMD);
}

/* IMPORT. */
int
cmd_import (struct lexer *lexer, struct dataset *ds)
{
  return parse_read_command (lexer, ds, IMPORT_CMD);
}

/* Parses a GET or IMPORT command. */
static int
parse_read_command (struct lexer *lexer, struct dataset *ds,
                    enum reader_command command)
{
  struct casereader *reader = NULL;
  struct file_handle *fh = NULL;
  struct dictionary *dict = NULL;
  struct case_map *map = NULL;
  struct case_map_stage *stage = NULL;
  char *encoding = NULL;

  for (;;)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "FILE") || lex_is_string (lexer))
	{
	  lex_match (lexer, T_EQUALS);

          fh_unref (fh);
	  fh = fh_parse (lexer, FH_REF_FILE, NULL);
	  if (fh == NULL)
            goto error;
	}
      else if (command == GET_CMD && lex_match_id (lexer, "ENCODING"))
        {
	  lex_match (lexer, T_EQUALS);

          if (!lex_force_string (lexer))
            goto error;

          free (encoding);
          encoding = ss_xstrdup (lex_tokss (lexer));

          lex_get (lexer);
        }
      else if (command == IMPORT_CMD && lex_match_id (lexer, "TYPE"))
	{
	  lex_match (lexer, T_EQUALS);

	  if (!lex_match_id (lexer, "COMM")
              && !lex_match_id (lexer, "TAPE"))
	    {
	      lex_error_expecting (lexer, "COMM", "TAPE", NULL_SENTINEL);
              goto error;
	    }
	}
      else
        break;
    }

  if (fh == NULL)
    {
      lex_sbc_missing ("FILE");
      goto error;
    }

  reader = any_reader_open_and_decode (fh, encoding, &dict, NULL);
  if (reader == NULL)
    goto error;

  stage = case_map_stage_create (dict);

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);
      if (!parse_dict_trim (lexer, dict))
        goto error;
    }
  dict_compact_values (dict);

  map = case_map_stage_get_case_map (stage);
  case_map_stage_destroy (stage);
  if (map != NULL)
    reader = case_map_create_input_translator (map, reader);

  dataset_set_dict (ds, dict);
  dataset_set_source (ds, reader);

  fh_unref (fh);
  free (encoding);
  return CMD_SUCCESS;

 error:
  case_map_stage_destroy (stage);
  fh_unref (fh);
  casereader_destroy (reader);
  if (dict != NULL)
    dict_destroy (dict);
  free (encoding);
  return CMD_CASCADING_FAILURE;
}
