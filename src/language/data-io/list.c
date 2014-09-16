/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009-2011, 2013, 2014 Free Software Foundation, Inc.

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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/data-out.h"
#include "data/format.h"
#include "data/subcase.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/dictionary/split-file.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/compiler.h"
#include "libpspp/ll.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "output/tab.h"
#include "output/table-item.h"

#include "gl/intprops.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xmalloca.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

enum numbering
  {
    format_unnumbered,
    format_numbered
  };


struct lst_cmd
{
  long first;
  long last;
  long step;
  const struct variable **v_variables;
  size_t n_variables;
  enum numbering numbering;
};


static int
list_execute (const struct lst_cmd *lcmd, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);

  bool ok;
  int i;
  struct casegrouper *grouper;
  struct casereader *group;
  struct subcase sc;

  subcase_init_empty (&sc);
  for (i = 0; i < lcmd->n_variables; i++)
    subcase_add_var (&sc, lcmd->v_variables[i], SC_ASCEND);


  grouper = casegrouper_create_splits (proc_open (ds), dict);
  while (casegrouper_get_next_group (grouper, &group))
    {
      struct ccase *ccase;
      struct table *t;

      ccase = casereader_peek (group, 0);
      if (ccase != NULL)
        {
          output_split_file_values (ds, ccase);
          case_unref (ccase);
        }

      group = casereader_project (group, &sc);
      if (lcmd->numbering == format_numbered)
        group = casereader_create_arithmetic_sequence (group, 1, 1);
      group = casereader_select (group, lcmd->first - 1,
                                 (lcmd->last != LONG_MAX ? lcmd->last
                                  : CASENUMBER_MAX), lcmd->step);

      if (lcmd->numbering == format_numbered)
        {
          struct fmt_spec fmt;
          size_t col;
          int width;

          width = lcmd->last == LONG_MAX ? 5 : intlog10 (lcmd->last);
          fmt = fmt_for_output (FMT_F, width, 0);
          col = caseproto_get_n_widths (casereader_get_proto (group)) - 1;

          t = table_from_casereader (group, col, _("Case Number"), &fmt);
        }
      else
        t = NULL;

      for (i = 0; i < lcmd->n_variables; i++)
        {
          const struct variable *var = lcmd->v_variables[i];
          struct table *c;

          c = table_from_casereader (group, i, var_get_name (var),
                                     var_get_print_format (var));
          t = table_hpaste (t, c);
        }

      casereader_destroy (group);

      table_item_submit (table_item_create (t, "Data List", NULL));
    }
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  subcase_destroy (&sc);
  free (lcmd->v_variables);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}


/* Parses and executes the LIST procedure. */
int
cmd_list (struct lexer *lexer, struct dataset *ds)
{
  struct lst_cmd cmd;
  const struct dictionary *dict = dataset_dict (ds);

  /* Fill in defaults. */
  cmd.step = 1;
  cmd.first = 1;
  cmd.last = LONG_MAX;
  cmd.n_variables = 0;
  cmd.v_variables = NULL;
  cmd.numbering = format_unnumbered;


  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);
      if (lex_match_id (lexer, "VARIABLES") )
        {
          lex_match (lexer, T_EQUALS);
          if (! parse_variables_const (lexer, dict, &cmd.v_variables, &cmd.n_variables, 0 ))
            {
              msg (SE, _("No variables specified."));
              return CMD_FAILURE;
            }
        }
      else if (lex_match_id (lexer, "FORMAT") )
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "NUMBERED") )
            {
              cmd.numbering = format_numbered;
            }
          else if (lex_match_id (lexer, "UNNUMBERED") )
            {
              cmd.numbering = format_unnumbered;
            }
          else
            {
              lex_error (lexer, NULL);
              goto error;
            }
        }
      /* example: LIST /CASES=FROM 1 TO 25 BY 5. */
      else if (lex_match_id (lexer, "CASES"))
        {
          lex_match (lexer, T_EQUALS);
          lex_force_match_id (lexer, "FROM");

          if (lex_force_int (lexer))
            {
	      cmd.first = lex_integer (lexer);
              lex_get (lexer);
            }

          lex_force_match (lexer, T_TO);

          if (lex_force_int (lexer))
            {
              cmd.last = lex_integer (lexer);
              lex_get (lexer);
            }

          lex_force_match (lexer, T_BY);

          if (lex_force_int (lexer))
            {
              cmd.step = lex_integer (lexer);
              lex_get (lexer);
            }
        }
      else if (! parse_variables_const (lexer, dict, &cmd.v_variables, &cmd.n_variables, 0 ))
        {
          return CMD_FAILURE;
        }
    }
        

  /* Verify arguments. */
  if (cmd.first > cmd.last)
    {
      int t;
      msg (SW, _("The first case (%ld) specified precedes the last case (%ld) "
                 "specified.  The values will be swapped."), cmd.first, cmd.last);
      t = cmd.first;
      cmd.first = cmd.last;
      cmd.last = t;
    }

  if (cmd.first < 1)
    {
      msg (SW, _("The first case (%ld) to list is less than 1.  The value is "
                 "being reset to 1."), cmd.first);
      cmd.first = 1;
    }

  if (cmd.last < 1)
    {
      msg (SW, _("The last case (%ld) to list is less than 1.  The value is "
                 "being reset to 1."), cmd.last);
      cmd.last = 1;
    }

  if (cmd.step < 1)
    {
      msg (SW, _("The step value %ld is less than 1.  The value is being "
                 "reset to 1."), cmd.step);
      cmd.step = 1;
    }

  /* If no variables were explicitly provided, then default to ALL */
  if (cmd.n_variables == 0)
    dict_get_vars (dict, &cmd.v_variables, &cmd.n_variables,
                   DC_SYSTEM | DC_SCRATCH);

  return list_execute (&cmd, ds);

 error:
  free (cmd.v_variables);
  return CMD_FAILURE;
}

