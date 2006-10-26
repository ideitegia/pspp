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

#include <stdlib.h>

#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/format-parser.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Parses the NUMERIC command. */
int
cmd_numeric (struct dataset *ds)
{
  size_t i;

  /* Names of variables to create. */
  char **v;
  size_t nv;

  /* Format spec for variables to create.  f.type==-1 if default is to
     be used. */
  struct fmt_spec f;

  do
    {
      if (!parse_DATA_LIST_vars (&v, &nv, PV_NONE))
	return CMD_FAILURE;

      /* Get the optional format specification. */
      if (lex_match ('('))
	{
	  if (!parse_format_specifier (&f))
	    goto fail;
	  if (formats[f.type].cat & FCAT_STRING)
	    {
	      msg (SE, _("Format type %s may not be used with a numeric "
		   "variable."), fmt_to_string (&f));
	      goto fail;
	    }

	  if (!lex_match (')'))
	    {
	      msg (SE, _("`)' expected after output format."));
	      goto fail;
	    }
	}
      else
	f.type = -1;

      /* Create each variable. */
      for (i = 0; i < nv; i++)
	{
	  struct variable *new_var = dict_create_var (dataset_dict (ds), v[i], 0);
	  if (!new_var)
	    msg (SE, _("There is already a variable named %s."), v[i]);
	  else
	    {
	      if (f.type != -1)
		new_var->print = new_var->write = f;
	    }
	}

      /* Clean up. */
      for (i = 0; i < nv; i++)
	free (v[i]);
      free (v);
    }
  while (lex_match ('/'));

  return lex_end_of_command ();

  /* If we have an error at a point where cleanup is required,
     flow-of-control comes here. */
fail:
  for (i = 0; i < nv; i++)
    free (v[i]);
  free (v);
  return CMD_FAILURE;
}

/* Parses the STRING command. */
int
cmd_string (struct dataset *ds)
{
  size_t i;

  /* Names of variables to create. */
  char **v;
  size_t nv;

  /* Format spec for variables to create. */
  struct fmt_spec f;

  /* Width of variables to create. */
  int width;

  do
    {
      if (!parse_DATA_LIST_vars (&v, &nv, PV_NONE))
	return CMD_FAILURE;

      if (!lex_force_match ('(') || !parse_format_specifier (&f))
	goto fail;
      if (!(formats[f.type].cat & FCAT_STRING))
	{
	  msg (SE, _("Format type %s may not be used with a string "
	       "variable."), fmt_to_string (&f));
	  goto fail;
	}

      if (!lex_match (')'))
	{
	  msg (SE, _("`)' expected after output format."));
	  goto fail;
	}

      switch (f.type)
	{
	case FMT_A:
	  width = f.w;
	  break;
	case FMT_AHEX:
	  width = f.w / 2;
	  break;
	default:
          NOT_REACHED ();
	}

      /* Create each variable. */
      for (i = 0; i < nv; i++)
	{
	  struct variable *new_var = dict_create_var (dataset_dict (ds), v[i],
                                                      width);
	  if (!new_var)
	    msg (SE, _("There is already a variable named %s."), v[i]);
	  else
            new_var->print = new_var->write = f;
	}

      /* Clean up. */
      for (i = 0; i < nv; i++)
	free (v[i]);
      free (v);
    }
  while (lex_match ('/'));

  return lex_end_of_command ();

  /* If we have an error at a point where cleanup is required,
     flow-of-control comes here. */
fail:
  for (i = 0; i < nv; i++)
    free (v[i]);
  free (v);
  return CMD_FAILURE;
}

/* Parses the LEAVE command. */
int
cmd_leave (struct dataset *ds)
{
  struct variable **v;
  size_t nv;

  size_t i;

  if (!parse_variables (dataset_dict (ds), &v, &nv, PV_NONE))
    return CMD_CASCADING_FAILURE;
  for (i = 0; i < nv; i++)
    v[i]->leave = true;
  free (v);

  return lex_end_of_command ();
}
