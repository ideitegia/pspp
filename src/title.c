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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include <ctype.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "lexer.h"
#include "main.h"
#include "output.h"
#include "var.h"
#include "version.h"
#include "vfm.h"

#undef DEBUGGING
/*#define DEBUGGING 1 */
#include "debug-print.h"

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
  debug_printf ((_("%s before: %s\n"), cmd, *title ? *title : _("<none>")));
  if (c == '"' || c == '\'')
    {
      lex_get ();
      if (!lex_force_string ())
	return CMD_FAILURE;
      if (*title)
	free (*title);
      *title = xstrdup (ds_value (&tokstr));
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
      for (cp = *title; *cp; cp++)
	*cp = toupper ((unsigned char) (*cp));
      token = '.';
    }
  debug_printf ((_("%s after: %s\n"), cmd, *title));
  return CMD_SUCCESS;
}

/* Performs the FILE LABEL command. */
int
cmd_file_label (void)
{
  char *label;

  label = lex_rest_of_line (NULL);
  while (isspace ((unsigned char) *label))
    label++;

  free (default_dict.label);
  default_dict.label = xstrdup (label);
  if (strlen (default_dict.label) > 60)
    default_dict.label[60] = 0;
  token = '.';

  return CMD_SUCCESS;
}

/* Add LINE as a line of document information to default_dict,
   indented by INDENT spaces. */
static void
add_document_line (const char *line, int indent)
{
  char *doc;

  default_dict.n_documents++;
  default_dict.documents = xrealloc (default_dict.documents,
				     80 * default_dict.n_documents);
  doc = &default_dict.documents[80 * (default_dict.n_documents - 1)];
  memset (doc, ' ', indent);
  st_bare_pad_copy (&doc[indent], line, 80 - indent);
}

/* Performs the DOCUMENT command. */
int
cmd_document (void)
{
  /* Add a few header lines for reference. */
  {
    char buf[256];
    struct tm *tmp = localtime (&last_vfm_invocation);

    if (default_dict.n_documents)
      add_document_line ("", 0);

    sprintf (buf, _("Document entered %s %02d:%02d:%02d by %s (%s):"),
	     curdate, tmp->tm_hour, tmp->tm_min, tmp->tm_sec, version,
	     host_system);
    add_document_line (buf, 1);
  }

  for (;;)
    {
      int had_dot;
      char *line;

      line = lex_rest_of_line (&had_dot);
      while (isspace ((unsigned char) *line))
	line++;

      if (had_dot)
	{
	  char *cp = strchr (line, 0);
	  *cp++ = '.';
	  *cp = 0;
	}

      add_document_line (line, 3);

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
  lex_match_id ("DROP");
  lex_match_id ("DOCUMENTS");

  free (default_dict.documents);
  default_dict.documents = NULL;
  default_dict.n_documents = 0;

  return lex_end_of_command ();
}
