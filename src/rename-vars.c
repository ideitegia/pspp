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
#include <stdlib.h>
#include <assert.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "hash.h"
#include "lexer.h"
#include "str.h"
#include "var.h"

/* The code for this function is very similar to the code for the
   RENAME subcommand of MODIFY VARS. */
int
cmd_rename_variables (void)
{
  struct variable **rename_vars = NULL;
  char **rename_new_names = NULL;
  int rename_cnt = 0;
  char *err_name;

  int status = CMD_FAILURE;

  int i;

  if (temporary != 0)
    {
      msg (SE, _("RENAME VARS may not be used after TEMPORARY.  "
                 "Temporary transformations will be made permanent."));
      cancel_temporary (); 
    }

  lex_match_id ("RENAME");
  lex_match_id ("VARIABLES");

  do
    {
      int prev_nv_1 = rename_cnt;
      int prev_nv_2 = rename_cnt;

      if (!lex_match ('('))
	{
	  msg (SE, _("`(' expected."));
	  goto lossage;
	}
      if (!parse_variables (default_dict, &rename_vars, &rename_cnt,
			    PV_APPEND | PV_NO_DUPLICATE))
	goto lossage;
      if (!lex_match ('='))
	{
	  msg (SE, _("`=' expected between lists of new and old variable names."));
	  goto lossage;
	}
      if (!parse_DATA_LIST_vars (&rename_new_names, &prev_nv_1, PV_APPEND))
	goto lossage;
      if (prev_nv_1 != rename_cnt)
	{
	  msg (SE, _("Differing number of variables in old name list "
	       "(%d) and in new name list (%d)."),
	       rename_cnt - prev_nv_2, prev_nv_1 - prev_nv_2);
	  for (i = 0; i < prev_nv_1; i++)
	    free (rename_new_names[i]);
	  free (rename_new_names);
	  rename_new_names = NULL;
	  goto lossage;
	}
      if (!lex_match (')'))
	{
	  msg (SE, _("`)' expected after variable names."));
	  goto lossage;
	}
    }
  while (token != '.');

  if (!dict_rename_vars (default_dict,
                         rename_vars, rename_new_names, rename_cnt,
                         &err_name)) 
    {
      msg (SE, _("Renaming would duplicate variable name %s."), err_name);
      goto lossage;
    }

  status = CMD_SUCCESS;

 lossage:
  free (rename_vars);
  if (rename_new_names != NULL) 
    {
      for (i = 0; i < rename_cnt; i++)
        free (rename_new_names[i]);
      free (rename_new_names); 
    }
  return status;
}
