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

#include <stdlib.h>

#include <data/dictionary.h>
#include <data/file-handle-def.h>
#include <data/procedure.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/data-io/file-handle.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/compiler.h>

#include "xalloc.h"

/* (headers) */

struct cor_set
  {
    struct cor_set *next;
    const struct variable **v1, **v2;
    size_t nv1, nv2;
  };

static struct cor_set *cor_list, *cor_last;

static struct file_handle *matrix_file;

static void free_correlations_state (void);
static int internal_cmd_correlations (struct lexer *lexer, struct dataset *ds);

int
cmd_correlations (struct lexer *lexer, struct dataset *ds)
{
  int result = internal_cmd_correlations (lexer, ds);
  free_correlations_state ();
  return result;
}

/* (specification)
   "CORRELATIONS" (cor_):
     *variables=custom;
     missing=miss:!pairwise/listwise,
	     inc:include/exclude;
     +print=tail:!twotail/onetail,
	    sig:!sig/nosig;
     +format=fmt:!matrix/serial;
     +matrix=custom;
     +statistics[st_]=descriptives,xprod,all.
*/
/* (declarations) */
/* (functions) */

int
internal_cmd_correlations (struct lexer *lexer, struct dataset *ds)
{
  struct cmd_correlations cmd;

  cor_list = cor_last = NULL;
  matrix_file = NULL;

  if (!parse_correlations (lexer, ds, &cmd, NULL))
    return CMD_FAILURE;

  free_correlations (&cmd);

  return CMD_SUCCESS;
}

static int
cor_custom_variables (struct lexer *lexer, struct dataset *ds, struct cmd_correlations *cmd UNUSED, void *aux UNUSED)
{
  const struct variable **v1, **v2;
  size_t nv1, nv2;
  struct cor_set *cor;

  /* Ensure that this is a VARIABLES subcommand. */
  if (!lex_match_id (lexer, "VARIABLES")
      && (lex_token (lexer) != T_ID || dict_lookup_var (dataset_dict (ds), lex_tokid (lexer)) != NULL)
      && lex_token (lexer) != T_ALL)
    return 2;
  lex_match (lexer, '=');

  if (!parse_variables_const (lexer, dataset_dict (ds), &v1, &nv1,
			PV_NO_DUPLICATE | PV_NUMERIC))
    return 0;

  if (lex_match (lexer, T_WITH))
    {
      if (!parse_variables_const (lexer, dataset_dict (ds), &v2, &nv2,
			    PV_NO_DUPLICATE | PV_NUMERIC))
	{
	  free (v1);
	  return 0;
	}
    }
  else
    {
      nv2 = nv1;
      v2 = v1;
    }

  cor = xmalloc (sizeof *cor);
  cor->next = NULL;
  cor->v1 = v1;
  cor->v2 = v2;
  cor->nv1 = nv1;
  cor->nv2 = nv2;
  if (cor_list)
    cor_last = cor_last->next = cor;
  else
    cor_list = cor_last = cor;

  return 1;
}

static int
cor_custom_matrix (struct lexer *lexer, struct dataset *ds UNUSED, struct cmd_correlations *cmd UNUSED, void *aux UNUSED)
{
  if (!lex_force_match (lexer, '('))
    return 0;

  if (lex_match (lexer, '*'))
    matrix_file = NULL;
  else
    {
      matrix_file = fh_parse (lexer, FH_REF_FILE);
      if (matrix_file == NULL)
        return 0;
    }

  if (!lex_force_match (lexer, ')'))
    return 0;

  return 1;
}

static void
free_correlations_state (void)
{
  struct cor_set *cor, *next;

  for (cor = cor_list; cor != NULL; cor = next)
    {
      next = cor->next;
      if (cor->v1 != cor->v2)
	free (cor->v2);
      free (cor->v1);
      free (cor);
    }
}

/*
  Local Variables:
  mode: c
  End:
*/
