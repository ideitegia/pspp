/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2007 Free Software Foundation, Inc.

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

#include <ctype.h>
#include <stdlib.h>

#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <libpspp/alloc.h>
#include <libpspp/message.h>
#include <libpspp/start-date.h>
#include <libpspp/version.h>
#include <output/output.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

static int get_title (struct lexer *, const char *cmd, char **title);

int
cmd_title (struct lexer *lexer, struct dataset *ds UNUSED)
{
  return get_title (lexer, "TITLE", &outp_title);
}

int
cmd_subtitle (struct lexer *lexer, struct dataset *ds UNUSED)
{
  return get_title (lexer, "SUBTITLE", &outp_subtitle);
}

static int
get_title (struct lexer *lexer, const char *cmd, char **title)
{
  int c;

  c = lex_look_ahead (lexer);
  if (c == '"' || c == '\'')
    {
      lex_get (lexer);
      if (!lex_force_string (lexer))
	return CMD_FAILURE;
      if (*title)
	free (*title);
      *title = ds_xstrdup (lex_tokstr (lexer));
      lex_get (lexer);
      if (lex_token (lexer) != '.')
	{
	  msg (SE, _("%s: `.' expected after string."), cmd);
	  return CMD_FAILURE;
	}
    }
  else
    {
      char *cp;

      if (*title)
	free (*title);
      *title = xstrdup (lex_rest_of_line (lexer));
      lex_discard_line (lexer);
      for (cp = *title; *cp; cp++)
	*cp = toupper ((unsigned char) (*cp));
    }
  return CMD_SUCCESS;
}

/* Performs the FILE LABEL command. */
int
cmd_file_label (struct lexer *lexer, struct dataset *ds)
{
  const char *label;

  label = lex_rest_of_line (lexer);
  lex_discard_line (lexer);
  while (isspace ((unsigned char) *label))
    label++;

  dict_set_label (dataset_dict (ds), label);

  return CMD_SUCCESS;
}

/* Add entry date line to DICT's documents. */
static void
add_document_trailer (struct dictionary *dict)
{
  char buf[64];

  sprintf (buf, _("   (Entered %s)"), get_start_date ());
  dict_add_document_line (dict, buf);
}

/* Performs the DOCUMENT command. */
int
cmd_document (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct string line = DS_EMPTY_INITIALIZER;
  bool end_dot;

  do
    {
      end_dot = lex_end_dot (lexer);
      ds_assign_string (&line, lex_entire_line_ds (lexer));
      if (end_dot)
        ds_put_char (&line, '.');
      dict_add_document_line (dict, ds_cstr (&line));

      lex_discard_line (lexer);
      lex_get_line (lexer);
    }
  while (!end_dot);

  add_document_trailer (dict);

  return CMD_SUCCESS;
}

/* Performs the DROP DOCUMENTS command. */
int
cmd_drop_documents (struct lexer *lexer, struct dataset *ds)
{
  dict_clear_documents (dataset_dict (ds));

  return lex_end_of_command (lexer);
}


/* Performs the ADD DOCUMENTS command. */
int
cmd_add_documents (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);

  if ( ! lex_force_string (lexer) )
    return CMD_FAILURE;

  while ( lex_is_string (lexer))
    {
      dict_add_document_line (dict, ds_cstr (lex_tokstr (lexer)));
      lex_get (lexer);
    }

  add_document_trailer (dict);

  return lex_end_of_command (lexer) ;
}
