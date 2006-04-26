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
#include <libpspp/alloc.h>
#include <language/command.h>
#include <libpspp/message.h>
#include <language/lexer/lexer.h>
#include <libpspp/str.h>
#include <data/variable.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_variable_labels (void)
{
  do
    {
      struct variable **v;
      size_t nv;

      size_t i;

      if (!parse_variables (default_dict, &v, &nv, PV_NONE))
        return CMD_FAILURE;

      if (token != T_STRING)
	{
	  msg (SE, _("String expected for variable label."));
	  free (v);
	  return CMD_FAILURE;
	}
      if (ds_length (&tokstr) > 255)
	{
	  msg (SW, _("Truncating variable label to 255 characters."));
	  ds_truncate (&tokstr, 255);
	}
      for (i = 0; i < nv; i++)
	{
	  if (v[i]->label)
	    free (v[i]->label);
	  v[i]->label = xstrdup (ds_c_str (&tokstr));
	}

      lex_get ();
      while (token == '/')
	lex_get ();
      free (v);
    }
  while (token != '.');
  return CMD_SUCCESS;
}



const char *
var_to_string(const struct variable *var)
{
  if ( !var ) 
    return 0;

  return ( var->label ? var->label : var->name);
}
