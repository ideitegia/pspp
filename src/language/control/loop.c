/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009-2011 Free Software Foundation, Inc.

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

#include "language/control/control-stack.h"

#include "data/case.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/settings.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/expressions/public.h"
#include "language/lexer/lexer.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* LOOP outputs a transformation that is executed only on the
   first pass through the loop.  On this trip, it initializes for
   the first pass by resetting the pass number, setting up the
   indexing clause, and testing the LOOP IF clause.  If the loop
   is not to be entered at all, it jumps forward just past the
   END LOOP transformation; otherwise, it continues to the
   transformation following LOOP.

   END LOOP outputs a transformation that executes at the end of
   each trip through the loop.  It checks the END LOOP IF clause,
   then updates the pass number, increments the indexing clause,
   and tests the LOOP IF clause.  If another pass through the
   loop is due, it jumps backward to just after the LOOP
   transformation; otherwise, it continues to the transformation
   following END LOOP. */

struct loop_trns
  {
    struct pool *pool;
    struct dataset *ds;

    /* Iteration limit. */
    int max_pass_count;         /* Maximum number of passes (-1=unlimited). */
    int pass;			/* Number of passes thru the loop so far. */

    /* a=a TO b [BY c]. */
    struct variable *index_var; /* Index variable. */
    struct expression *first_expr; /* Starting index. */
    struct expression *by_expr;	/* Index increment (default 1.0 if null). */
    struct expression *last_expr; /* Terminal index. */
    double cur, by, last;       /* Current value, increment, last value. */

    /* IF condition for LOOP or END LOOP. */
    struct expression *loop_condition;
    struct expression *end_loop_condition;

    /* Transformation indexes. */
    int past_LOOP_index;        /* Just past LOOP transformation. */
    int past_END_LOOP_index;    /* Just past END LOOP transformation. */
  };

static const struct ctl_class loop_class;

static trns_finalize_func loop_trns_finalize;
static trns_proc_func loop_trns_proc, end_loop_trns_proc, break_trns_proc;
static trns_free_func loop_trns_free;

static struct loop_trns *create_loop_trns (struct dataset *);
static bool parse_if_clause (struct lexer *,
                             struct loop_trns *, struct expression **);
static bool parse_index_clause (struct dataset *, struct lexer *,
                                struct loop_trns *, bool *created_index_var);
static void close_loop (void *);

/* LOOP. */

/* Parses LOOP. */
int
cmd_loop (struct lexer *lexer, struct dataset *ds)
{
  struct loop_trns *loop;
  bool created_index_var = false;
  bool ok = true;

  loop = create_loop_trns (ds);
  while (lex_token (lexer) != T_ENDCMD && ok)
    {
      if (lex_match_id (lexer, "IF"))
        ok = parse_if_clause (lexer, loop, &loop->loop_condition);
      else
        ok = parse_index_clause (ds, lexer, loop, &created_index_var);
    }

  /* Clean up if necessary. */
  if (!ok)
    {
      loop->max_pass_count = 0;
      if (loop->index_var != NULL && created_index_var)
        {
          dict_delete_var (dataset_dict (ds), loop->index_var);
          loop->index_var = NULL;
        }
    }

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}

/* Parses END LOOP. */
int
cmd_end_loop (struct lexer *lexer, struct dataset *ds)
{
  struct loop_trns *loop;
  bool ok = true;

  loop = ctl_stack_top (&loop_class);
  if (loop == NULL)
    return CMD_CASCADING_FAILURE;

  assert (loop->ds == ds);

  /* Parse syntax. */
  if (lex_match_id (lexer, "IF"))
    ok = parse_if_clause (lexer, loop, &loop->end_loop_condition);
  if (ok)
    ok = lex_end_of_command (lexer) == CMD_SUCCESS;

  if (!ok)
    loop->max_pass_count = 0;

  ctl_stack_pop (loop);

  return ok ? CMD_SUCCESS : CMD_FAILURE;
}

/* Parses BREAK. */
int
cmd_break (struct lexer *lexer UNUSED, struct dataset *ds)
{
  struct ctl_stmt *loop = ctl_stack_search (&loop_class);
  if (loop == NULL)
    return CMD_CASCADING_FAILURE;

  add_transformation (ds, break_trns_proc, NULL, loop);

  return CMD_SUCCESS;
}

/* Closes a LOOP construct by emitting the END LOOP
   transformation and finalizing its members appropriately. */
static void
close_loop (void *loop_)
{
  struct loop_trns *loop = loop_;

  add_transformation (loop->ds, end_loop_trns_proc, NULL, loop);
  loop->past_END_LOOP_index = next_transformation (loop->ds);

  /* If there's nothing else limiting the number of loops, use
     MXLOOPS as a limit. */
  if (loop->max_pass_count == -1
      && loop->index_var == NULL
      && loop->loop_condition == NULL
      && loop->end_loop_condition == NULL)
    loop->max_pass_count = settings_get_mxloops ();
}

/* Parses an IF clause for LOOP or END LOOP and stores the
   resulting expression to *CONDITION.
   Returns true if successful, false on failure. */
static bool
parse_if_clause (struct lexer *lexer,
		 struct loop_trns *loop, struct expression **condition)
{
  if (*condition != NULL)
    {
      lex_sbc_only_once ("IF");
      return false;
    }

  *condition = expr_parse_pool (lexer, loop->pool, loop->ds, EXPR_BOOLEAN);
  return *condition != NULL;
}

/* Parses an indexing clause into LOOP.
   Stores true in *CREATED_INDEX_VAR if the index clause created
   a new variable, false otherwise.
   Returns true if successful, false on failure. */
static bool
parse_index_clause (struct dataset *ds, struct lexer *lexer,
                    struct loop_trns *loop, bool *created_index_var)
{
  if (loop->index_var != NULL)
    {
      msg (SE, _("Only one index clause may be specified."));
      return false;
    }

  if (lex_token (lexer) != T_ID)
    {
      lex_error (lexer, NULL);
      return false;
    }

  loop->index_var = dict_lookup_var (dataset_dict (ds), lex_tokcstr (lexer));
  if (loop->index_var != NULL)
    *created_index_var = false;
  else
    {
      loop->index_var = dict_create_var_assert (dataset_dict (ds),
                                                lex_tokcstr (lexer), 0);
      *created_index_var = true;
    }
  lex_get (lexer);

  if (!lex_force_match (lexer, T_EQUALS))
    return false;

  loop->first_expr = expr_parse_pool (lexer, loop->pool,
				      loop->ds, EXPR_NUMBER);
  if (loop->first_expr == NULL)
    return false;

  for (;;)
    {
      struct expression **e;
      if (lex_match (lexer, T_TO))
        e = &loop->last_expr;
      else if (lex_match (lexer, T_BY))
        e = &loop->by_expr;
      else
        break;

      if (*e != NULL)
        {
          lex_sbc_only_once (e == &loop->last_expr ? "TO" : "BY");
          return false;
        }
      *e = expr_parse_pool (lexer, loop->pool, loop->ds, EXPR_NUMBER);
      if (*e == NULL)
        return false;
    }
  if (loop->last_expr == NULL)
    {
      lex_sbc_missing ("TO");
      return false;
    }
  if (loop->by_expr == NULL)
    loop->by = 1.0;

  return true;
}

/* Creates, initializes, and returns a new loop_trns. */
static struct loop_trns *
create_loop_trns (struct dataset *ds)
{
  struct loop_trns *loop = pool_create_container (struct loop_trns, pool);
  loop->max_pass_count = -1;
  loop->pass = 0;
  loop->index_var = NULL;
  loop->first_expr = loop->by_expr = loop->last_expr = NULL;
  loop->loop_condition = loop->end_loop_condition = NULL;
  loop->ds = ds;

  add_transformation_with_finalizer (ds, loop_trns_finalize,
                                     loop_trns_proc, loop_trns_free, loop);
  loop->past_LOOP_index = next_transformation (ds);

  ctl_stack_push (&loop_class, loop);

  return loop;
}

/* Finalizes LOOP by clearing the control stack, thus ensuring
   that all open LOOPs are closed. */
static void
loop_trns_finalize (void *do_if_ UNUSED)
{
  /* This will be called multiple times if multiple LOOPs were
     executed, which is slightly unclean, but at least it's
     idempotent. */
  ctl_stack_clear ();
}

/* Sets up LOOP for the first pass. */
static int
loop_trns_proc (void *loop_, struct ccase **c, casenumber case_num)
{
  struct loop_trns *loop = loop_;

  if (loop->index_var != NULL)
    {
      /* Evaluate loop index expressions. */
      loop->cur = expr_evaluate_num (loop->first_expr, *c, case_num);
      if (loop->by_expr != NULL)
	loop->by = expr_evaluate_num (loop->by_expr, *c, case_num);
      loop->last = expr_evaluate_num (loop->last_expr, *c, case_num);

      /* Even if the loop is never entered, set the index
         variable to the initial value. */
      *c = case_unshare (*c);
      case_data_rw (*c, loop->index_var)->f = loop->cur;

      /* Throw out pathological cases. */
      if (!isfinite (loop->cur) || !isfinite (loop->by)
          || !isfinite (loop->last)
          || loop->by == 0.0
          || (loop->by > 0.0 && loop->cur > loop->last)
          || (loop->by < 0.0 && loop->cur < loop->last))
        goto zero_pass;
    }

  /* Initialize pass count. */
  loop->pass = 0;
  if (loop->max_pass_count >= 0 && loop->pass >= loop->max_pass_count)
    goto zero_pass;

  /* Check condition. */
  if (loop->loop_condition != NULL
      && expr_evaluate_num (loop->loop_condition, *c, case_num) != 1.0)
    goto zero_pass;

  return loop->past_LOOP_index;

 zero_pass:
  return loop->past_END_LOOP_index;
}

/* Frees LOOP. */
static bool
loop_trns_free (void *loop_)
{
  struct loop_trns *loop = loop_;

  pool_destroy (loop->pool);
  return true;
}

/* Finishes a pass through the loop and starts the next. */
static int
end_loop_trns_proc (void *loop_, struct ccase **c, casenumber case_num UNUSED)
{
  struct loop_trns *loop = loop_;

  if (loop->end_loop_condition != NULL
      && expr_evaluate_num (loop->end_loop_condition, *c, case_num) != 0.0)
    goto break_out;

  /* MXLOOPS limiter. */
  if (loop->max_pass_count >= 0 && ++loop->pass >= loop->max_pass_count)
    goto break_out;

  /* Indexing clause limiter: counting downward. */
  if (loop->index_var != NULL)
    {
      loop->cur += loop->by;
      if ((loop->by > 0.0 && loop->cur > loop->last)
          || (loop->by < 0.0 && loop->cur < loop->last))
        goto break_out;
      *c = case_unshare (*c);
      case_data_rw (*c, loop->index_var)->f = loop->cur;
    }

  if (loop->loop_condition != NULL
      && expr_evaluate_num (loop->loop_condition, *c, case_num) != 1.0)
    goto break_out;

  return loop->past_LOOP_index;

 break_out:
  return loop->past_END_LOOP_index;
}

/* Executes BREAK. */
static int
break_trns_proc (void *loop_, struct ccase **c UNUSED,
                 casenumber case_num UNUSED)
{
  struct loop_trns *loop = loop_;

  return loop->past_END_LOOP_index;
}

/* LOOP control structure class definition. */
static const struct ctl_class loop_class =
  {
    "LOOP",
    "END LOOP",
    close_loop,
  };
