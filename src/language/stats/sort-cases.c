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

#include <assert.h>
#include <stdlib.h>
#include <limits.h>

#include "sort-criteria.h"
#include <data/procedure.h>
#include <data/settings.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <libpspp/alloc.h>
#include <libpspp/message.h>
#include <math/sort.h>
#include <sys/types.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)


/* Performs the SORT CASES procedures. */
int
cmd_sort_cases (struct lexer *lexer, struct dataset *ds)
{
  struct sort_criteria *criteria;
  bool success = false;

  lex_match (lexer, T_BY);

  criteria = sort_parse_criteria (lexer, dataset_dict (ds), NULL, NULL, NULL, NULL);
  if (criteria == NULL)
    return CMD_CASCADING_FAILURE;

  if (get_testing_mode () && lex_match (lexer, '/')) 
    {
      if (!lex_force_match_id (lexer, "BUFFERS") || !lex_match (lexer, '=')
          || !lex_force_int (lexer))
        goto done;

      min_buffers = max_buffers = lex_integer (lexer);
      allow_internal_sort = false;
      if (max_buffers < 2) 
        {
          msg (SE, _("Buffer limit must be at least 2."));
          goto done;
        }

      lex_get (lexer);
    }

  success = sort_active_file_in_place (ds, criteria);

 done:
  min_buffers = 64;
  max_buffers = INT_MAX;
  allow_internal_sort = true;
  
  sort_destroy_criteria (criteria);
  return success ? lex_end_of_command (lexer) : CMD_CASCADING_FAILURE;
}

