/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2010, 2011 Free Software Foundation, Inc.

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

#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "libpspp/message.h"
#include "libpspp/start-date.h"
#include "libpspp/version.h"
#include "output/text-item.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static int parse_title (struct lexer *, enum text_item_type);
static void set_title (const char *title, enum text_item_type);

int
cmd_title (struct lexer *lexer, struct dataset *ds UNUSED)
{
  return parse_title (lexer, TEXT_ITEM_TITLE);
}

int
cmd_subtitle (struct lexer *lexer, struct dataset *ds UNUSED)
{
  return parse_title (lexer, TEXT_ITEM_SUBTITLE);
}

static int
parse_title (struct lexer *lexer, enum text_item_type type)
{
  if (!lex_force_string (lexer))
    return CMD_FAILURE;
  set_title (lex_tokcstr (lexer), type);
  lex_get (lexer);
  return CMD_SUCCESS;
}

static void
set_title (const char *title, enum text_item_type type)
{
  text_item_submit (text_item_create (type, title));
}

/* Performs the FILE LABEL command. */
int
cmd_file_label (struct lexer *lexer, struct dataset *ds)
{
  if (!lex_force_string (lexer))
    return CMD_FAILURE;

  dict_set_label (dataset_dict (ds), lex_tokcstr (lexer));
  lex_get (lexer);

  return CMD_SUCCESS;
}

/* Performs the DOCUMENT command. */
int
cmd_document (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  char *trailer;

  if (!lex_force_string (lexer))
    return CMD_FAILURE;

  while (lex_is_string (lexer))
    {
      dict_add_document_line (dict, lex_tokcstr (lexer), true);
      lex_get (lexer);
    }

  trailer = xasprintf (_("   (Entered %s)"), get_start_date ());
  dict_add_document_line (dict, trailer, true);
  free (trailer);

  return CMD_SUCCESS;
}

/* Performs the ADD DOCUMENTS command. */
int
cmd_add_documents (struct lexer *lexer, struct dataset *ds)
{
  return cmd_document (lexer, ds);
}

/* Performs the DROP DOCUMENTS command. */
int
cmd_drop_documents (struct lexer *lexer UNUSED, struct dataset *ds)
{
  dict_clear_documents (dataset_dict (ds));
  return CMD_SUCCESS;
}
