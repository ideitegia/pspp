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
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/alloc.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* The code for this function is very similar to the code for the
   RENAME subcommand of MODIFY VARS. */
int
cmd_rename_variables (struct lexer *lexer, struct dataset *ds)
{
  struct variable **rename_vars = NULL;
  char **rename_new_names = NULL;
  size_t rename_cnt = 0;
  char *err_name;

  int status = CMD_CASCADING_FAILURE;

  if (proc_make_temporary_transformations_permanent (ds))
    msg (SE, _("RENAME VARS may not be used after TEMPORARY.  "
               "Temporary transformations will be made permanent."));

  do
    {
      size_t prev_nv_1 = rename_cnt;
      size_t prev_nv_2 = rename_cnt;

      if (!lex_match (lexer, '('))
	{
	  msg (SE, _("`(' expected."));
	  goto lossage;
	}
      if (!parse_variables (lexer, dataset_dict (ds), &rename_vars, &rename_cnt,
			    PV_APPEND | PV_NO_DUPLICATE))
	goto lossage;
      if (!lex_match (lexer, '='))
	{
	  msg (SE, _("`=' expected between lists of new and old variable names."));
	  goto lossage;
	}
      if (!parse_DATA_LIST_vars (lexer, &rename_new_names, &prev_nv_1, PV_APPEND))
	goto lossage;
      if (prev_nv_1 != rename_cnt)
	{
          size_t i;

	  msg (SE, _("Differing number of variables in old name list "
                     "(%u) and in new name list (%u)."),
	       (unsigned) rename_cnt - prev_nv_2,
               (unsigned) prev_nv_1 - prev_nv_2);
	  for (i = 0; i < prev_nv_1; i++)
	    free (rename_new_names[i]);
	  free (rename_new_names);
	  rename_new_names = NULL;
	  goto lossage;
	}
      if (!lex_match (lexer, ')'))
	{
	  msg (SE, _("`)' expected after variable names."));
	  goto lossage;
	}
    }
  while (lex_token (lexer) != '.');

  if (!dict_rename_vars (dataset_dict (ds),
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
      size_t i;
      for (i = 0; i < rename_cnt; i++)
        free (rename_new_names[i]);
      free (rename_new_names); 
    }
  return status;
}
