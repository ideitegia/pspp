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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <data/procedure.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/format-parser.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

enum
  {
    FORMATS_PRINT = 001,
    FORMATS_WRITE = 002
  };

static int internal_cmd_formats (int);

int
cmd_print_formats (void)
{
  return internal_cmd_formats (FORMATS_PRINT);
}

int
cmd_write_formats (void)
{
  return internal_cmd_formats (FORMATS_WRITE);
}

int
cmd_formats (void)
{
  return internal_cmd_formats (FORMATS_PRINT | FORMATS_WRITE);
}

int
internal_cmd_formats (int which)
{
  /* Variables. */
  struct variable **v;
  size_t cv;

  /* Format to set the variables to. */
  struct fmt_spec f;

  /* Numeric or string. */
  int type;

  /* Counter. */
  size_t i;

  for (;;)
    {
      if (token == '.')
	break;

      if (!parse_variables (dataset_dict (current_dataset), &v, &cv, PV_NUMERIC))
	return CMD_FAILURE;
      type = v[0]->type;

      if (!lex_match ('('))
	{
	  msg (SE, _("`(' expected after variable list."));
	  goto fail;
	}
      if (!parse_format_specifier (&f)
          || !check_output_specifier (&f, true)
          || !check_specifier_type (&f, NUMERIC, true))
	goto fail;

      if (!lex_match (')'))
	{
	  msg (SE, _("`)' expected after output format."));
	  goto fail;
	}

      for (i = 0; i < cv; i++)
	{
	  if (which & FORMATS_PRINT)
	    v[i]->print = f;
	  if (which & FORMATS_WRITE)
	    v[i]->write = f;
	}
      free (v);
      v = NULL;
    }
  return CMD_SUCCESS;

fail:
  free (v);
  return CMD_FAILURE;
}
