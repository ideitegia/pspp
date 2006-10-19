/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

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

static int get_title (const char *cmd, char **title);

int
cmd_title (void)
{
  return get_title ("TITLE", &outp_title);
}

int
cmd_subtitle (void)
{
  return get_title ("SUBTITLE", &outp_subtitle);
}

static int
get_title (const char *cmd, char **title)
{
  int c;

  c = lex_look_ahead ();
  if (c == '"' || c == '\'')
    {
      lex_get ();
      if (!lex_force_string ())
	return CMD_FAILURE;
      if (*title)
	free (*title);
      *title = ds_xstrdup (&tokstr);
      lex_get ();
      if (token != '.')
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
      *title = xstrdup (lex_rest_of_line (NULL));
      lex_discard_line ();
      for (cp = *title; *cp; cp++)
	*cp = toupper ((unsigned char) (*cp));
      token = '.';
    }
  return CMD_SUCCESS;
}

/* Performs the FILE LABEL command. */
int
cmd_file_label (void)
{
  const char *label;

  label = lex_rest_of_line (NULL);
  lex_discard_line ();
  while (isspace ((unsigned char) *label))
    label++;

  dict_set_label (dataset_dict (current_dataset), label);
  token = '.';

  return CMD_SUCCESS;
}

/* Add LINE as a line of document information to dataset_dict (current_dataset),
   indented by INDENT spaces. */
static void
add_document_line (const char *line, int indent)
{
  const char *old_documents;
  size_t old_len;
  char *new_documents;

  old_documents = dict_get_documents (dataset_dict (current_dataset));
  old_len = old_documents != NULL ? strlen (old_documents) : 0;
  new_documents = xmalloc (old_len + 81);

  memcpy (new_documents, old_documents, old_len);
  memset (new_documents + old_len, ' ', indent);
  buf_copy_str_rpad (new_documents + old_len + indent, 80 - indent, line);
  new_documents[old_len + 80] = '\0';

  dict_set_documents (dataset_dict (current_dataset), new_documents);

  free (new_documents);
}

/* Performs the DOCUMENT command. */
int
cmd_document (void)
{
  /* Add a few header lines for reference. */
  {
    char buf[256];

    if (dict_get_documents (dataset_dict (current_dataset)) != NULL)
      add_document_line ("", 0);

    sprintf (buf, _("Document entered %s by %s:"), get_start_date (), version);
    add_document_line (buf, 1);
  }

  for (;;)
    {
      int had_dot;
      const char *orig_line;
      char *copy_line;

      orig_line = lex_rest_of_line (&had_dot);
      lex_discard_line ();
      while (isspace ((unsigned char) *orig_line))
	orig_line++;

      copy_line = xmalloc (strlen (orig_line) + 2);
      strcpy (copy_line, orig_line);
      if (had_dot)
        strcat (copy_line, ".");

      add_document_line (copy_line, 3);
      free (copy_line);

      lex_get_line ();
      if (had_dot)
	break;
    }

  token = '.';
  return CMD_SUCCESS;
}

/* Performs the DROP DOCUMENTS command. */
int
cmd_drop_documents (void)
{
  dict_set_documents (dataset_dict (current_dataset), NULL);

  return lex_end_of_command ();
}
