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

#include <stdio.h>
#include <stdlib.h>

#include <data/procedure.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/alloc.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_variable_labels (struct lexer *lexer, struct dataset *ds)
{
  do
    {
      struct variable **v;
      struct string label;
      size_t nv;

      size_t i;

      if (!parse_variables (lexer, dataset_dict (ds), &v, &nv, PV_NONE))
        return CMD_FAILURE;

      if (lex_token (lexer) != T_STRING)
	{
	  msg (SE, _("String expected for variable label."));
	  free (v);
	  return CMD_FAILURE;
	}

      ds_init_string (&label, lex_tokstr (lexer) );
      if (ds_length (&label) > 255)
	{
	  msg (SW, _("Truncating variable label to 255 characters."));
	  ds_truncate (&label, 255);
	}
      for (i = 0; i < nv; i++)
        var_set_label (v[i], ds_cstr (&label));
      ds_destroy (&label);

      lex_get (lexer);
      while (lex_token (lexer) == '/')
	lex_get (lexer);
      free (v);
    }
  while (lex_token (lexer) != '.');
  return CMD_SUCCESS;
}



const char *
var_to_string(const struct variable *var)
{
  const char *label;
  
  if ( !var ) 
    return 0;

  label = var_get_label (var);
  return label ? label : var_get_name (var);
}
