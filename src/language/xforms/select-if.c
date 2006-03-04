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
#include "alloc.h"
#include "command.h"
#include "dictionary.h"
#include "message.h"
#include "expressions/public.h"
#include "lexer.h"
#include "str.h"
#include "variable.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* SELECT IF transformation. */
struct select_if_trns
  {
    struct expression *e;	/* Test expression. */
  };

static trns_proc_func select_if_proc;
static trns_free_func select_if_free;

/* Parses the SELECT IF transformation. */
int
cmd_select_if (void)
{
  struct expression *e;
  struct select_if_trns *t;

  e = expr_parse (default_dict, EXPR_BOOLEAN);
  if (!e)
    return CMD_CASCADING_FAILURE;

  if (token != '.')
    {
      expr_free (e);
      lex_error (_("expecting end of command"));
      return CMD_CASCADING_FAILURE;
    }

  t = xmalloc (sizeof *t);
  t->e = e;
  add_transformation (select_if_proc, select_if_free, t);

  return CMD_SUCCESS;
}

/* Performs the SELECT IF transformation T on case C. */
static int
select_if_proc (void *t_, struct ccase *c,
                int case_num)
{
  struct select_if_trns *t = t_;
  return (expr_evaluate_num (t->e, c, case_num) == 1.0
          ? TRNS_CONTINUE : TRNS_DROP_CASE);
}

/* Frees SELECT IF transformation T. */
static bool
select_if_free (void *t_)
{
  struct select_if_trns *t = t_;
  expr_free (t->e);
  free (t);
  return true;
}

/* Parses the FILTER command. */
int
cmd_filter (void)
{
  if (lex_match_id ("OFF"))
    dict_set_filter (default_dict, NULL);
  else
    {
      struct variable *v;

      lex_match (T_BY);
      v = parse_variable ();
      if (!v)
	return CMD_CASCADING_FAILURE;

      if (v->type == ALPHA)
	{
	  msg (SE, _("The filter variable must be numeric."));
	  return CMD_CASCADING_FAILURE;
	}

      if (dict_class_from_id (v->name) == DC_SCRATCH)
	{
	  msg (SE, _("The filter variable may not be scratch."));
	  return CMD_CASCADING_FAILURE;
	}

      dict_set_filter (default_dict, v);
    }

  return CMD_SUCCESS;
}

/* Expression on PROCESS IF. */
struct expression *process_if_expr;

/* Parses the PROCESS IF command. */
int
cmd_process_if (void)
{
  struct expression *e;

  e = expr_parse (default_dict, EXPR_BOOLEAN);
  if (!e)
    return CMD_FAILURE;

  if (token != '.')
    {
      expr_free (e);
      lex_error (_("expecting end of command"));
      return CMD_FAILURE;
    }

  if (process_if_expr)
    {
      msg (MW, _("Only last instance of this command is in effect."));
      expr_free (process_if_expr);
    }
  process_if_expr = e;

  return CMD_SUCCESS;
}
