/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011 Free Software Foundation, Inc.

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
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

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
    msg (SE, _("%s may not be used after %s.  "
               "Temporary transformations will be made permanent."), "RENAME VARS", "TEMPORARY");

  do
    {
      size_t prev_nv_1 = rename_cnt;
      size_t prev_nv_2 = rename_cnt;

      if (!lex_force_match (lexer, T_LPAREN))
        goto lossage;
      if (!parse_variables (lexer, dataset_dict (ds), &rename_vars, &rename_cnt,
			    PV_APPEND | PV_NO_DUPLICATE))
	goto lossage;
      if (!lex_force_match (lexer, T_EQUALS))
        goto lossage;
      if (!parse_DATA_LIST_vars (lexer, dataset_dict (ds),
                                 &rename_new_names, &prev_nv_1,
                                 PV_APPEND | PV_NO_DUPLICATE))
	goto lossage;
      if (prev_nv_1 != rename_cnt)
	{
          size_t i;

	  msg (SE, _("Differing number of variables in old name list "
                     "(%zu) and in new name list (%zu)."),
	       rename_cnt - prev_nv_2, prev_nv_1 - prev_nv_2);
	  for (i = 0; i < prev_nv_1; i++)
	    free (rename_new_names[i]);
	  free (rename_new_names);
	  rename_new_names = NULL;
	  goto lossage;
	}
      if (!lex_force_match (lexer, T_RPAREN))
        goto lossage;
    }
  while (lex_token (lexer) != T_ENDCMD);

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
