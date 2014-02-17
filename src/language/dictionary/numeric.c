/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2011, 2014 Free Software Foundation, Inc.

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

#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/variable.h"
#include "data/format.h"
#include "language/command.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Parses the NUMERIC command. */
int
cmd_numeric (struct lexer *lexer, struct dataset *ds)
{
  size_t i;

  /* Names of variables to create. */
  char **v;
  size_t nv;

  do
    {
      /* Format spec for variables to create. */
      struct fmt_spec f;

      if (!parse_DATA_LIST_vars (lexer, dataset_dict (ds),
                                 &v, &nv, PV_NO_DUPLICATE))
	return CMD_FAILURE;

      /* Get the optional format specification. */
      if (lex_match (lexer, T_LPAREN))
	{
	  if (!parse_format_specifier (lexer, &f))
	    goto fail;

	  if ( ! fmt_check_output (&f))
	    goto fail;

	  if (fmt_is_string (f.type))
	    {
              char str[FMT_STRING_LEN_MAX + 1];
	      msg (SE, _("Format type %s may not be used with a numeric "
                         "variable."), fmt_to_string (&f, str));
	      goto fail;
	    }

	  if (!lex_match (lexer, T_RPAREN))
	    {
              lex_error_expecting (lexer, "`)'", NULL_SENTINEL);
	      goto fail;
	    }
	}
      else
	f = var_default_formats (0);

      /* Create each variable. */
      for (i = 0; i < nv; i++)
	{
	  struct variable *new_var = dict_create_var (dataset_dict (ds), v[i], 0);
	  if (!new_var)
	    msg (SE, _("There is already a variable named %s."), v[i]);
	  else
            var_set_both_formats (new_var, &f);
	}

      /* Clean up. */
      for (i = 0; i < nv; i++)
	free (v[i]);
      free (v);
    }
  while (lex_match (lexer, T_SLASH));

  return CMD_SUCCESS;

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
cmd_string (struct lexer *lexer, struct dataset *ds)
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
      if (!parse_DATA_LIST_vars (lexer, dataset_dict (ds),
                                 &v, &nv, PV_NO_DUPLICATE))
	return CMD_FAILURE;

      if (!lex_force_match (lexer, T_LPAREN)
          || !parse_format_specifier (lexer, &f)
          || !lex_force_match (lexer, T_RPAREN))
	goto fail;
      if (!fmt_is_string (f.type))
	{
          char str[FMT_STRING_LEN_MAX + 1];
	  msg (SE, _("Format type %s may not be used with a string "
                     "variable."), fmt_to_string (&f, str));
	  goto fail;
	}
      if (!fmt_check_output (&f))
        goto fail;

      width = fmt_var_width (&f);

      /* Create each variable. */
      for (i = 0; i < nv; i++)
	{
	  struct variable *new_var = dict_create_var (dataset_dict (ds), v[i],
                                                      width);
	  if (!new_var)
	    msg (SE, _("There is already a variable named %s."), v[i]);
	  else
            var_set_both_formats (new_var, &f);
	}

      /* Clean up. */
      for (i = 0; i < nv; i++)
	free (v[i]);
      free (v);
    }
  while (lex_match (lexer, T_SLASH));

  return CMD_SUCCESS;

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
cmd_leave (struct lexer *lexer, struct dataset *ds)
{
  struct variable **v;
  size_t nv;

  size_t i;

  if (!parse_variables (lexer, dataset_dict (ds), &v, &nv, PV_NONE))
    return CMD_CASCADING_FAILURE;
  for (i = 0; i < nv; i++)
    var_set_leave (v[i], true);
  free (v);

  return CMD_SUCCESS;
}
