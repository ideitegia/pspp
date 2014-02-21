/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009-2012 Free Software Foundation, Inc.

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

#include "data/case.h"
#include "data/dataset.h"
#include "data/transformations.h"
#include "data/value.h"
#include "language/command.h"
#include "language/control/control-stack.h"
#include "language/expressions/public.h"
#include "language/lexer/lexer.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* DO IF, ELSE IF, and ELSE are translated as a single
   transformation that evaluates each condition and jumps to the
   start of the appropriate block of transformations.  Each block
   of transformations (except for the last) ends with a
   transformation that jumps past the remaining blocks.

   So, the following code:

       DO IF a.
       ...block 1...
       ELSE IF b.
       ...block 2...
       ELSE.
       ...block 3...
       END IF.

   is effectively translated like this:

       IF a GOTO 1, IF b GOTO 2, ELSE GOTO 3.
       1: ...block 1...
          GOTO 4
       2: ...block 2...
          GOTO 4
       3: ...block 3...
       4:

*/

/* A conditional clause. */
struct clause
  {
    struct expression *condition; /* Test expression; NULL for ELSE clause. */
    int target_index;           /* Transformation to jump to if true. */
  };

/* DO IF transformation. */
struct do_if_trns
  {
    struct dataset *ds;         /* The dataset */
    struct clause *clauses;     /* Clauses. */
    size_t clause_cnt;          /* Number of clauses. */
    int past_END_IF_index;      /* Transformation just past last clause. */
  };

static const struct ctl_class do_if_class;

static int parse_clause (struct lexer *, struct do_if_trns *, struct dataset *ds);
static void add_clause (struct do_if_trns *, struct expression *condition);
static void add_else (struct do_if_trns *);

static bool has_else (struct do_if_trns *);
static bool must_not_have_else (struct do_if_trns *);
static void close_do_if (void *do_if);

static trns_finalize_func do_if_finalize_func;
static trns_proc_func do_if_trns_proc, break_trns_proc;
static trns_free_func do_if_trns_free;

/* Parse DO IF. */
int
cmd_do_if (struct lexer *lexer, struct dataset *ds)
{
  struct do_if_trns *do_if = xmalloc (sizeof *do_if);
  do_if->clauses = NULL;
  do_if->clause_cnt = 0;
  do_if->ds = ds;

  ctl_stack_push (&do_if_class, do_if);
  add_transformation_with_finalizer (ds, do_if_finalize_func,
                                     do_if_trns_proc, do_if_trns_free, do_if);

  return parse_clause (lexer, do_if, ds);
}

/* Parse ELSE IF. */
int
cmd_else_if (struct lexer *lexer, struct dataset *ds)
{
  struct do_if_trns *do_if = ctl_stack_top (&do_if_class);
  if (do_if == NULL || !must_not_have_else (do_if))
    return CMD_CASCADING_FAILURE;
  return parse_clause (lexer, do_if, ds);
}

/* Parse ELSE. */
int
cmd_else (struct lexer *lexer UNUSED, struct dataset *ds)
{
  struct do_if_trns *do_if = ctl_stack_top (&do_if_class);
  assert (ds == do_if->ds);
  if (do_if == NULL || !must_not_have_else (do_if))
    return CMD_CASCADING_FAILURE;
  add_else (do_if);
  return CMD_SUCCESS;
}

/* Parse END IF. */
int
cmd_end_if (struct lexer *lexer UNUSED, struct dataset *ds)
{
  struct do_if_trns *do_if = ctl_stack_top (&do_if_class);

  if (do_if == NULL)
    return CMD_CASCADING_FAILURE;

  assert (ds == do_if->ds);
  ctl_stack_pop (do_if);

  return CMD_SUCCESS;
}

/* Closes out DO_IF, by adding a sentinel ELSE clause if
   necessary and setting past_END_IF_index. */
static void
close_do_if (void *do_if_)
{
  struct do_if_trns *do_if = do_if_;

  if (!has_else (do_if))
    add_else (do_if);
  do_if->past_END_IF_index = next_transformation (do_if->ds);
}

/* Adds an ELSE clause to DO_IF pointing to the next
   transformation. */
static void
add_else (struct do_if_trns *do_if)
{
  assert (!has_else (do_if));
  add_clause (do_if, NULL);
}

/* Returns true if DO_IF does not yet have an ELSE clause.
   Reports an error and returns false if it does already. */
static bool
must_not_have_else (struct do_if_trns *do_if)
{
  if (has_else (do_if))
    {
      msg (SE, _("This command may not follow %s in %s ... %s."), "ELSE", "DO IF", "END IF");
      return false;
    }
  else
    return true;
}

/* Returns true if DO_IF already has an ELSE clause,
   false otherwise. */
static bool
has_else (struct do_if_trns *do_if)
{
  return (do_if->clause_cnt != 0
          && do_if->clauses[do_if->clause_cnt - 1].condition == NULL);
}

/* Parses a DO IF or ELSE IF expression and appends the
   corresponding clause to DO_IF.  Checks for end of command and
   returns a command return code. */
static int
parse_clause (struct lexer *lexer, struct do_if_trns *do_if, struct dataset *ds)
{
  struct expression *condition;

  condition = expr_parse (lexer, ds, EXPR_BOOLEAN);
  if (condition == NULL)
    return CMD_CASCADING_FAILURE;

  add_clause (do_if, condition);

  return CMD_SUCCESS;
}

/* Adds a clause to DO_IF that tests for the given CONDITION and,
   if true, jumps to the set of transformations produced by
   following commands. */
static void
add_clause (struct do_if_trns *do_if, struct expression *condition)
{
  struct clause *clause;

  if (do_if->clause_cnt > 0)
    add_transformation (do_if->ds, break_trns_proc, NULL, do_if);

  do_if->clauses = xnrealloc (do_if->clauses,
                              do_if->clause_cnt + 1, sizeof *do_if->clauses);
  clause = &do_if->clauses[do_if->clause_cnt++];
  clause->condition = condition;
  clause->target_index = next_transformation (do_if->ds);
}

/* Finalizes DO IF by clearing the control stack, thus ensuring
   that all open DO IFs are closed. */
static void
do_if_finalize_func (void *do_if_ UNUSED)
{
  /* This will be called multiple times if multiple DO IFs were
     executed, which is slightly unclean, but at least it's
     idempotent. */
  ctl_stack_clear ();
}

/* DO IF transformation procedure.
   Checks each clause and jumps to the appropriate
   transformation. */
static int
do_if_trns_proc (void *do_if_, struct ccase **c, casenumber case_num UNUSED)
{
  struct do_if_trns *do_if = do_if_;
  struct clause *clause;

  for (clause = do_if->clauses; clause < do_if->clauses + do_if->clause_cnt;
       clause++)
    {
      if (clause->condition != NULL)
        {
          double boolean = expr_evaluate_num (clause->condition, *c, case_num);
          if (boolean == 1.0)
            return clause->target_index;
          else if (boolean == SYSMIS)
            return do_if->past_END_IF_index;
        }
      else
        return clause->target_index;
    }
  return do_if->past_END_IF_index;
}

/* Frees a DO IF transformation. */
static bool
do_if_trns_free (void *do_if_)
{
  struct do_if_trns *do_if = do_if_;
  struct clause *clause;

  for (clause = do_if->clauses; clause < do_if->clauses + do_if->clause_cnt;
       clause++)
    expr_free (clause->condition);
  free (do_if->clauses);
  free (do_if);
  return true;
}

/* Breaks out of a DO IF construct. */
static int
break_trns_proc (void *do_if_, struct ccase **c UNUSED,
                 casenumber case_num UNUSED)
{
  struct do_if_trns *do_if = do_if_;

  return do_if->past_END_IF_index;
}

/* DO IF control structure class definition. */
static const struct ctl_class do_if_class =
  {
    "DO IF",
    "END IF",
    close_do_if,
  };
