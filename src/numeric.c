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
#include "error.h"
#include <stdlib.h>
#include "command.h"
#include "error.h"
#include "lexer.h"
#include "str.h"
#include "var.h"

#include "debug-print.h"

/* Parses the NUMERIC command. */
int
cmd_numeric (void)
{
  int i;

  /* Names of variables to create. */
  char **v;
  int nv;

  /* Format spec for variables to create.  f.type==-1 if default is to
     be used. */
  struct fmt_spec f;

  do
    {
      if (!parse_DATA_LIST_vars (&v, &nv, PV_NONE))
	return CMD_PART_SUCCESS_MAYBE;

      /* Get the optional format specification. */
      if (lex_match ('('))
	{
	  if (!parse_format_specifier (&f, 0))
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
	  struct variable *new_var = dict_create_var (default_dict, v[i], 0);
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
  return CMD_PART_SUCCESS_MAYBE;
}

/* Parses the STRING command. */
int
cmd_string (void)
{
  int i;

  /* Names of variables to create. */
  char **v;
  int nv;

  /* Format spec for variables to create. */
  struct fmt_spec f;

  /* Width of variables to create. */
  int width;

  do
    {
      if (!parse_DATA_LIST_vars (&v, &nv, PV_NONE))
	return CMD_PART_SUCCESS_MAYBE;

      if (!lex_force_match ('(')
	  || !parse_format_specifier (&f, 0))
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
	  assert (0);
          abort ();
	}

      /* Create each variable. */
      for (i = 0; i < nv; i++)
	{
	  struct variable *new_var = dict_create_var (default_dict, v[i],
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
  return CMD_PART_SUCCESS_MAYBE;
}

/* Parses the LEAVE command. */
int
cmd_leave (void)
{
  struct variable **v;
  int nv;

  int i;

  if (!parse_variables (default_dict, &v, &nv, PV_NONE))
    return CMD_FAILURE;
  for (i = 0; i < nv; i++)
    {
      if (!v[i]->reinit)
	continue;
      v[i]->reinit = 0;
      v[i]->init = 1;
    }
  free (v);

  return lex_end_of_command ();
}
