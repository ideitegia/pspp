/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2007, 2010, 2011, 2013 Free Software Foundation, Inc.

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

#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "language/command.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Performs DELETE VARIABLES command. */
int
cmd_delete_variables (struct lexer *lexer, struct dataset *ds)
{
  struct variable **vars;
  size_t var_cnt;
  bool ok;

  if (proc_make_temporary_transformations_permanent (ds))
    msg (SE, _("%s may not be used after %s.  "
               "Temporary transformations will be made permanent."), 
	 "DELETE VARIABLES", "TEMPORARY");

  if (!parse_variables (lexer, dataset_dict (ds), &vars, &var_cnt, PV_NONE))
    goto error;
  if (var_cnt == dict_get_var_cnt (dataset_dict (ds)))
    {
      msg (SE, _("%s may not be used to delete all variables "
                 "from the active dataset dictionary.  "
                 "Use %s instead."), "DELETE VARIABLES", "NEW FILE");
      goto error;
    }

  ok = casereader_destroy (proc_open_filtering (ds, false));
  ok = proc_commit (ds) && ok;
  if (!ok)
    goto error;
  dict_delete_vars (dataset_dict (ds), vars, var_cnt);

  free (vars);

  return CMD_SUCCESS;

 error:
  free (vars);
  return CMD_CASCADING_FAILURE;
}
