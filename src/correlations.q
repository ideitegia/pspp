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
#include "alloc.h"
#include "file-handle.h"
#include "command.h"
#include "lexer.h"
#include "var.h"
/* (headers) */

#undef DEBUGGING
#define DEBUGGING 1
#include "debug-print.h"

struct cor_set
  {
    struct cor_set *next;
    struct variable **v1, **v2;
    int nv1, nv2;
  };

struct cor_set *cor_list, *cor_last;

struct file_handle *matrix_file;

static void free_correlations_state (void);
static int internal_cmd_correlations (void);

int
cmd_correlations (void)
{
  int result = internal_cmd_correlations ();
  free_correlations_state ();
  return result;
}

/* (specification)
   "CORRELATIONS" (cor_):
     *variables=custom;
     +missing=miss:!pairwise/listwise,
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
internal_cmd_correlations (void)
{
  struct cmd_correlations cmd;

  cor_list = cor_last = NULL;
  matrix_file = NULL;

  lex_match_id ("PEARSON");
  lex_match_id ("CORRELATIONS");

  if (!parse_correlations (&cmd))
    return CMD_FAILURE;
  free_correlations (&cmd);

  return CMD_SUCCESS;
}

static int
cor_custom_variables (struct cmd_correlations *cmd unused)
{
  struct variable **v1, **v2;
  int nv1, nv2;
  struct cor_set *cor;

  /* Ensure that this is a VARIABLES subcommand. */
  if (!lex_match_id ("VARIABLES") && (token != T_ID || !is_varname (tokid))
      && token != T_ALL)
    return 2;
  lex_match ('=');

  if (!parse_variables (&default_dict, &v1, &nv1,
			PV_NO_DUPLICATE | PV_NUMERIC))
    return 0;
  
  if (lex_match (T_WITH))
    {
      if (!parse_variables (&default_dict, &v2, &nv2,
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
cor_custom_matrix (struct cmd_correlations *cmd unused)
{
  if (!lex_force_match ('('))
    return 0;
  
  if (lex_match ('*'))
    matrix_file = inline_file;
  else
    matrix_file = fh_parse_file_handle ();

  if (!matrix_file)
    return 0;

  if (!lex_force_match (')'))
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
