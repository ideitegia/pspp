/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#include <assert.h>
#include <stdlib.h>
#include <limits.h>

#include "sort-criteria.h"
#include <data/procedure.h>
#include <data/settings.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <libpspp/message.h>
#include <data/case-ordering.h>
#include <math/sort.h>
#include <sys/types.h>

#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


/* Performs the SORT CASES procedures. */
int
cmd_sort_cases (struct lexer *lexer, struct dataset *ds)
{
  struct case_ordering *ordering;
  struct casereader *output;
  bool ok = false;

  lex_match (lexer, T_BY);

  proc_cancel_temporary_transformations (ds);
  ordering = parse_case_ordering (lexer, dataset_dict (ds), NULL);
  if (ordering == NULL)
    return CMD_CASCADING_FAILURE;

  if (get_testing_mode () && lex_match (lexer, '/'))
    {
      if (!lex_force_match_id (lexer, "BUFFERS") || !lex_match (lexer, '=')
          || !lex_force_int (lexer))
        goto done;

      min_buffers = max_buffers = lex_integer (lexer);
      if (max_buffers < 2)
        {
          msg (SE, _("Buffer limit must be at least 2."));
          goto done;
        }

      lex_get (lexer);
    }

  proc_discard_output (ds);
  output = sort_execute (proc_open (ds), ordering);
  ordering = NULL;
  ok = proc_commit (ds);
  ok = proc_set_active_file_data (ds, output) && ok;

 done:
  min_buffers = 64;
  max_buffers = INT_MAX;

  case_ordering_destroy (ordering);
  return ok ? lex_end_of_command (lexer) : CMD_CASCADING_FAILURE;
}

