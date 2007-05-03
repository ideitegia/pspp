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
      *title = xstrdup (lex_rest_of_line (lexer, NULL));
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

  label = lex_rest_of_line (lexer, NULL);
  lex_discard_line (lexer);
  while (isspace ((unsigned char) *label))
    label++;

  dict_set_label (dataset_dict (ds), label);

  return CMD_SUCCESS;
}

/* Add LINE as a line of document information to dictionary
   indented by INDENT spaces. */
static void
add_document_line (struct dictionary *dict, const char *line, int indent)
{
  const char *old_documents;
  size_t old_len;
  char *new_documents;

  old_documents = dict_get_documents (dict);
  old_len = old_documents != NULL ? strlen (old_documents) : 0;
  new_documents = xmalloc (old_len + 81);

  memcpy (new_documents, old_documents, old_len);
  memset (new_documents + old_len, ' ', indent);
  buf_copy_str_rpad (new_documents + old_len + indent, 80 - indent, line);
  new_documents[old_len + 80] = '\0';

  dict_set_documents (dict, new_documents);

  free (new_documents);
}

/* Performs the DOCUMENT command. */
int
cmd_document (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  /* Add a few header lines for reference. */
  {
    char buf[256];

    if (dict && dict_get_documents (dict))
      add_document_line (dict, "", 0);

    sprintf (buf, _("Document entered %s by %s:"), get_start_date (), version);
    add_document_line (dict, buf, 1);
  }

  for (;;)
    {
      int had_dot;
      const char *orig_line;
      char *copy_line;

      orig_line = lex_rest_of_line (lexer, &had_dot);
      lex_discard_line (lexer);
      while (isspace ((unsigned char) *orig_line))
	orig_line++;

      copy_line = xmalloc (strlen (orig_line) + 2);
      strcpy (copy_line, orig_line);
      if (had_dot)
        strcat (copy_line, ".");

      add_document_line (dict, copy_line, 3);
      free (copy_line);

      lex_get_line (lexer);
      if (had_dot)
	break;
    }

  return CMD_SUCCESS;
}

/* Performs the DROP DOCUMENTS command. */
int
cmd_drop_documents (struct lexer *lexer, struct dataset *ds)
{
  dict_set_documents (dataset_dict (ds), NULL);

  return lex_end_of_command (lexer);
}


/* Performs the ADD DOCUMENTS command. */
int
cmd_add_documents (struct lexer *lexer, struct dataset *ds)
{
  int i;
  int n_lines = 0;
  char buf[256];
  struct string *lines = NULL;

  sprintf (buf, _("(Entered %s)"), get_start_date ());

  if ( ! lex_force_string (lexer) )
    return CMD_FAILURE;

  while ( lex_is_string (lexer))
    {
      const struct string *s = lex_tokstr (lexer);
      if ( ds_length (s) > 80)
	{
	  /* Note to translators: "bytes" is correct, not characters */
	  msg (SE, _("Document lines may not be more than 80 bytes long."));
	  goto failure;

	}
      lines = xrealloc (lines, (n_lines + 1) * sizeof (*lines));
      ds_init_string (&lines[n_lines++], s);

      lex_get (lexer);
    }

  for ( i = 0 ; i < n_lines ; ++i)
    {
      add_document_line (dataset_dict (ds), ds_cstr (&lines[i]), 0);
      ds_destroy (&lines[i]);
    }

  free (lines);

  add_document_line (dataset_dict (ds), buf, 3);

  return lex_end_of_command (lexer) ;

 failure:
  for ( i = 0 ; i < n_lines ; ++i)
    ds_destroy (&lines[i]);

  free (lines);

  return CMD_FAILURE;
}
