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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include "command.h"
#include "error.h"
#include "lexer.h"
#include "misc.h"
#include "str.h"
#include "var.h"

#include "debug-print.h"

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
  int cv;

  /* Format to set the variables to. */
  struct fmt_spec f;

  /* Numeric or string. */
  int type;

  /* Counter. */
  int i;

  for (;;)
    {
      if (token == '.')
	break;

      if (!parse_variables (default_dict, &v, &cv, PV_SAME_TYPE))
	return CMD_PART_SUCCESS_MAYBE;
      type = v[0]->type;

      if (!lex_match ('('))
	{
	  msg (SE, _("`(' expected after variable list"));
	  goto fail;
	}
      if (!parse_format_specifier (&f, 0) || !check_output_specifier (&f))
	goto fail;

      /* Catch type mismatch errors. */
      if ((type == ALPHA) ^ (0 != (formats[f.type].cat & FCAT_STRING)))
	{
	  msg (SE, _("Format %s may not be assigned to a %s variable."),
	       fmt_to_string (&f), type == NUMERIC ? _("numeric") : _("string"));
	  goto fail;
	}

      /* This is an additional check for string variables.  We can't
         let the user specify an A8 format for a string variable with
         width 4. */
      if (type == ALPHA)
	{
	  /* Shortest string so far. */
	  int min_len = INT_MAX;

	  for (i = 0; i < cv; i++)
	    min_len = min (min_len, v[i]->width);
	  if (!check_string_specifier (&f, min_len))
	    goto fail;
	}

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
  return CMD_PART_SUCCESS_MAYBE;
}
